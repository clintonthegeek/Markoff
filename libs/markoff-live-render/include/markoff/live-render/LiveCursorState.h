// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/Cursor.h>

#include <QObject>
#include <QString>
#include <optional>
#include <qqmlintegration.h>

namespace Markoff::LiveRender {

class BlockKindRegistry;
class LiveBlockModel;

/// Owns the single canonical cursor value for the live view. Validates
/// `request()` calls against the target block's `BlockKindDescriptor`
/// (so BlockSelected is refused on a paragraph, etc.). Emits
/// `cursorChanged()` only when the cursor actually changes. Spec §5.3.
///
/// `cursorKind` Q_PROPERTY exposes the active variant as a string for QML
/// bindings that need to react to focus type (e.g. focus ring vs. caret).
/// Values: "none", "TextCaret", "BlockSelected", "BlockInternalEdit".
///
/// `requestTextCaretAtRow` is the deterministic-pending variant used by
/// the structural-key handler in R5: when a structural edit creates a new
/// row, the row doesn't exist in the model until the parse-back arrives.
/// The pending request is held; `rowsInserted` resolves it. Spec §5.3
/// step 6.
class MARKOFF_LIVE_RENDER_EXPORT LiveCursorState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveCursorState is provided by LiveListModelBinding")

    Q_PROPERTY(QString cursorKind READ cursorKind NOTIFY cursorChanged)
    /// The holeId if the active cursor is a TextCaret targeting a hole, else 0.
    /// Exposed so QML LiveView can route keyboard focus into the hole's delegate
    /// TextEdit when the hole is created via a structural key (e.g. Enter at EOB).
    Q_PROPERTY(quint64 focusedHoleId READ focusedHoleId NOTIFY cursorChanged)

public:
    explicit LiveCursorState(const BlockKindRegistry *registry,
                             const LiveBlockModel    *model,
                             QObject                 *parent = nullptr);

    Cursor cursor() const { return m_cursor; }
    QString cursorKind() const;
    quint64 focusedHoleId() const;

    void request(const Cursor &newCursor);
    void clear();

    int rowForBlock(const Markoff::BlockAnchor &block) const;

    /// Request a TextCaret at `qtPos` of the row at `expectedRow` once it
    /// exists. If the row already exists, equivalent to constructing a
    /// TextCaret from `model->recordAt(expectedRow).blockAnchor` and
    /// calling `request()`. If the row does not yet exist (because a
    /// structural edit was applied and the parse-back hasn't created it),
    /// record the request and watch `model->rowsInserted` for resolution.
    /// Pending requests linger up to two parse cycles before being
    /// dropped (see spec §8.4). Spec §5.3 step 6.
    ///
    /// If a request is already pending, it is replaced (latest-request-wins).
    Q_INVOKABLE void requestTextCaretAtRow(int expectedRow, int qtPos);

    /// Called by LiveListModelBinding from onParseUpdated. Increments the
    /// pending request's parse-cycle counter; drops on the SECOND call
    /// after the request was recorded (i.e. drops at parseCyclesSeen >= 2).
    /// `parseSeq` is unused as a value (we only care about the count);
    /// keep the signature so the call-site is self-documenting.
    void noteParseArrived(quint64 parseSeq);

Q_SIGNALS:
    void cursorChanged();

private:
    bool validateVariant(const Cursor &c) const;
    void onRowsInserted(const QModelIndex &parent, int first, int last);
    void resolvePendingForRow(int row);

    Cursor                   m_cursor;
    const BlockKindRegistry *m_registry;
    const LiveBlockModel    *m_model;

    struct PendingRow {
        int row;
        int qtPos;
        int parseCyclesSeen = 0;  // bumped on each noteParseArrived
    };
    std::optional<PendingRow> m_pendingRow;
};

}  // namespace Markoff::LiveRender
