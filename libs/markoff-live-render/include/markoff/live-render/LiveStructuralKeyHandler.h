// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff-foundation/BlockAnchor.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <qqmlintegration.h>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::LiveRender {

class LiveBlockModel;
class LiveCursorState;
class BlockKindRegistry;
class UndoCoalescer;
class LiveHoleLayer;
class LiveProxyBlockModel;

/// Single dispatcher for structural key events (Enter, Backspace-edge,
/// Delete-edge, Shift-Enter; future Tab/Shift-Tab in R7). Looks up the
/// descriptor for the focused block's kind, checks if the key is in
/// `consumedStructuralKeys`, then dispatches to a kind-keyed handler
/// function registered at library init. Spec §5.4.
///
/// Returns `true` if the key was consumed (caller sets event.accepted);
/// `false` otherwise (caller falls back to TextEdit's native handling
/// — that's how code-block's Enter inserts a literal newline).
class MARKOFF_LIVE_RENDER_EXPORT LiveStructuralKeyHandler : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveStructuralKeyHandler is provided by LiveListModelBinding")

public:
    /// Per-handler context. Constructed by `tryHandle`; consumed by the
    /// kind-keyed handler function.
    struct Ctx {
        Markoff::MarkoffDocument *document;
        LiveBlockModel           *model;
        LiveCursorState          *cursorState;
        UndoCoalescer            *undoCoalescer;
        LiveHoleLayer            *holeLayer;
        LiveProxyBlockModel      *proxyModel;

        int                       blockIndex;       ///< inner-model row (for byte arithmetic)
        int                       proxyBlockIndex;  ///< proxy-model row (for cursor delivery)
        Markoff::BlockAnchor      blockAnchor;
        quint32                   currentBlockStart;
        quint32                   currentBlockEnd;
        int                       qtPos;
        int                       modifiers;       ///< Qt::KeyboardModifiers
        QString                   blockText;
    };

    enum class HandleResult { Handled, NotHandled };

    using HandlerFn = std::function<HandleResult(const Ctx &)>;

    LiveStructuralKeyHandler(Markoff::MarkoffDocument *document,
                             LiveBlockModel           *model,
                             LiveCursorState          *cursorState,
                             const BlockKindRegistry  *registry,
                             UndoCoalescer            *undoCoalescer,
                             LiveHoleLayer            *holeLayer,
                             LiveProxyBlockModel      *proxyModel,
                             QObject                  *parent = nullptr);

    /// Register a kind-specific handler for `key` (a `Qt::Key_*` value).
    /// Replaces any prior registration for the same (kind, key).
    void registerHandler(const QString &kind, int key, HandlerFn fn);

    /// QML-invokable dispatch entry. See class docstring.
    Q_INVOKABLE bool tryHandle(int key,
                               int modifiers,
                               int blockIndex,
                               int qtPos,
                               bool selectionEmpty,
                               const QString &blockText);

private:
    void registerBuiltins();

    /// Dispatch all structural keys for a hole row: Enter
    /// (commit+new-hole / split / empty-noop), Esc/Backspace/Delete
    /// abandon paths. Called from tryHandle when
    /// proxyRowIsHole(blockIndex) is true.
    HandleResult handleHoleRow(quint64 holeId, int key, int modifiers,
                               int qtPos);

    /// After abandoning a hole at `holeProxyRow`, route focus to the
    /// nearest live (non-hole) neighbor. `preferNext == false` →
    /// previous neighbor (Esc / Backspace); `preferNext == true` →
    /// next neighbor (Delete). Falls back to the other direction; sets
    /// NoCursor only if the document has no inner rows at all.
    void routeFocusAfterAbandon(int holeProxyRow, bool preferNext);

    QPointer<Markoff::MarkoffDocument> m_document;
    LiveBlockModel                    *m_model;
    LiveCursorState                   *m_cursorState;
    const BlockKindRegistry           *m_registry;
    UndoCoalescer                     *m_undoCoalescer;
    LiveHoleLayer                     *m_holeLayer;
    LiveProxyBlockModel               *m_proxyModel;

    // Outer key: kind ("paragraph", etc.). Inner key: Qt::Key_*.
    QHash<QString, QHash<int, HandlerFn>> m_handlers;
};

}  // namespace Markoff::LiveRender
