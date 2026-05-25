// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/core/BlockAnchor.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <qqmlintegration.h>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::Live {

class LiveBlockModel;
class LiveCursorState;
class BlockKindRegistry;

/// Single dispatcher for structural key events (Enter, Backspace-edge,
/// Delete-edge, Shift-Enter; future Tab/Shift-Tab in R7). Looks up the
/// descriptor for the focused block's kind, checks if the key is in
/// `consumedStructuralKeys`, then dispatches to a kind-keyed handler
/// function registered at library init. Spec §5.4.
///
/// Returns `true` if the key was consumed (caller sets event.accepted);
/// `false` otherwise (caller falls back to TextEdit's native handling
/// — that's how code-block's Enter inserts a literal newline).
///
/// D2 migration: all handlers use Cmd::* functions (enterAtEnd,
/// backspaceMerge, deleteMerge, insertSoftBreak) rather than building
/// flat-buffer MarkoffEdits.
class MARKOFF_LIVE_EXPORT LiveStructuralKeyHandler : public QObject {
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

        int                       blockIndex;       ///< model row
        Markoff::BlockAnchor      blockAnchor;      ///< == BlockId in D2
        int                       qtPos;
        int                       modifiers;        ///< Qt::KeyboardModifiers
        QString                   blockText;
    };

    enum class HandleResult { Handled, NotHandled };

    using HandlerFn = std::function<HandleResult(const Ctx &)>;

    LiveStructuralKeyHandler(Markoff::MarkoffDocument *document,
                             LiveBlockModel           *model,
                             LiveCursorState          *cursorState,
                             const BlockKindRegistry  *registry,
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

    /// Update the info-string (language tag) of a CodeBlock.
    Q_INVOKABLE void changeCodeLanguage(Markoff::BlockAnchor anchor,
                                        const QString &lang);

    /// Update the alt-text attribute of an Image block.
    Q_INVOKABLE void changeImageAlt(Markoff::BlockAnchor anchor, const QString &alt);

private:
    void registerBuiltins();

    QPointer<Markoff::MarkoffDocument> m_document;
    LiveBlockModel                    *m_model;
    LiveCursorState                   *m_cursorState;
    const BlockKindRegistry           *m_registry;

    // Outer key: kind ("paragraph", etc.). Inner key: Qt::Key_*.
    QHash<QString, QHash<int, HandlerFn>> m_handlers;
};

}  // namespace Markoff::Live
