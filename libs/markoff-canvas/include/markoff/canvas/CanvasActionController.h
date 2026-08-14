// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.
#pragma once

#include <QAction>
#include <QActionGroup>
#include <QObject>

#include <markoff/canvas/TableTypes.h>

namespace Markoff {
class MarkoffDocument;
}  // namespace Markoff

namespace Markoff::Canvas {

class View;

/// QActions for the canvas leaf's format verbs (contract-v2 P4.3), the
/// canvas-side mirror of `Markoff::Live::LiveActionController`'s shape —
/// Corbomite binds its KF6 shortcuts/KActions to these pointers. Not a
/// second authority: every trigger just calls the corresponding `View`
/// method (`toggleBold`, `insertLink`, `setHeadingLevel`, …), which is
/// itself a thin driver over core `FormatOps`'s new per-block overloads.
///
/// Scope (spec §5.2 parity floor, this task's own wording): QActions cover
/// the `ActionId` set actually wireable right now — bold/italic/strike/
/// inline-code/link/heading0-6, plus (P5.2) the full table row/col/
/// alignment set. `ActionId::Blockquote`/`BulletedList`/`NumberedList`/
/// `TaskList`/`IndentMore`/`IndentLess` still have no corresponding `View`
/// verb (no toggle-list/indent mechanism exists in this leaf outside
/// typed-marker kind inference and real Tab key events) — building
/// QActions for them now would be dead triggers, the same "don't invent
/// the verb, stop and log" call the plan already made for table ops before
/// P5.2 landed them. `ActionId::Image`/`HorizontalRule` are likewise not
/// covered (no verb yet). Whoever lands those verbs adds their QActions
/// here in the same task, not as an afterthought.
///
/// Enabled-state (`updateEnabledStates`): every non-table action here is
/// enabled iff a document is attached AND the composed `View` is not
/// read-only — selection is deliberately NOT a gating factor (unlike
/// `LiveActionController`'s bold/italic/strike/code, which require a
/// selection): canvas's per-block `FormatOps` overloads are caret-capable
/// (a caret-only toggle inserts a paired-marker template and parks the
/// caret inside it), so requiring a selection first would disable a verb
/// that actually works without one. There is no `d2`-level undo/redo
/// depth query today (same gap `LiveActionController` notes on its own
/// undo/redo actions) — this controller carries no undo/redo actions of
/// its own for that reason; `EditorWidget`'s base contract exposes
/// `undo()`/`redo()` directly.
///
/// Table actions (P5.2) are a second enabled-state tier: `insertTable` is
/// enabled iff `hasDoc && !readOnly` like the format verbs (it works from
/// outside a table — that's the whole point of the action); every OTHER
/// table action additionally requires `View::caretTableContext()` to have
/// a value (the caret must actually be sitting in a table cell), since
/// they all act on that cell. `deleteRow` further requires `row > 0`
/// (the header isn't deletable) and `deleteColumn` requires `colCount > 1`
/// (the table's last column isn't deletable) — both read straight off the
/// same `TableCellContext`, not a second query. The four alignment actions
/// are mutually exclusive and CHECKABLE (a `QActionGroup`, radio-button
/// style): `updateEnabledStates` also syncs whichever one is checked to
/// the caret column's current `TableAlign`, so a consumer's toolbar
/// reflects the column the caret is actually in without polling.
class CanvasActionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAction *boldAction          READ boldAction          CONSTANT)
    Q_PROPERTY(QAction *italicAction        READ italicAction        CONSTANT)
    Q_PROPERTY(QAction *strikeAction        READ strikeAction        CONSTANT)
    Q_PROPERTY(QAction *inlineCodeAction    READ inlineCodeAction    CONSTANT)
    Q_PROPERTY(QAction *linkAction          READ linkAction          CONSTANT)
    Q_PROPERTY(QAction *heading0Action      READ heading0Action      CONSTANT)
    Q_PROPERTY(QAction *heading1Action      READ heading1Action      CONSTANT)
    Q_PROPERTY(QAction *heading2Action      READ heading2Action      CONSTANT)
    Q_PROPERTY(QAction *heading3Action      READ heading3Action      CONSTANT)
    Q_PROPERTY(QAction *heading4Action      READ heading4Action      CONSTANT)
    Q_PROPERTY(QAction *heading5Action      READ heading5Action      CONSTANT)
    Q_PROPERTY(QAction *heading6Action      READ heading6Action      CONSTANT)
    Q_PROPERTY(QAction *insertTableAction        READ insertTableAction        CONSTANT)
    Q_PROPERTY(QAction *insertRowAboveAction     READ insertRowAboveAction     CONSTANT)
    Q_PROPERTY(QAction *insertRowBelowAction     READ insertRowBelowAction     CONSTANT)
    Q_PROPERTY(QAction *deleteRowAction          READ deleteRowAction          CONSTANT)
    Q_PROPERTY(QAction *insertColumnLeftAction   READ insertColumnLeftAction   CONSTANT)
    Q_PROPERTY(QAction *insertColumnRightAction  READ insertColumnRightAction  CONSTANT)
    Q_PROPERTY(QAction *deleteColumnAction       READ deleteColumnAction       CONSTANT)
    Q_PROPERTY(QAction *alignColumnNoneAction    READ alignColumnNoneAction    CONSTANT)
    Q_PROPERTY(QAction *alignColumnLeftAction    READ alignColumnLeftAction    CONSTANT)
    Q_PROPERTY(QAction *alignColumnCenterAction  READ alignColumnCenterAction  CONSTANT)
    Q_PROPERTY(QAction *alignColumnRightAction   READ alignColumnRightAction   CONSTANT)

public:
    explicit CanvasActionController(QObject *parent = nullptr);

    /// Non-owning; nullptr detaches (all actions disable). Reconnects the
    /// `caretChanged`/read-only-relevant signals this controller's
    /// enabled-state tracks.
    void setView(View *view);
    /// Non-owning. Reconnects `d2DocumentChanged` so a document swap or a
    /// remote/undo-driven change re-evaluates enabled-state too.
    void setDocument(Markoff::MarkoffDocument *doc);

    QAction *boldAction()          const { return m_bold; }
    QAction *italicAction()        const { return m_italic; }
    QAction *strikeAction()        const { return m_strike; }
    QAction *inlineCodeAction()    const { return m_inlineCode; }
    QAction *linkAction()          const { return m_link; }
    QAction *heading0Action()      const { return m_heading[0]; }
    QAction *heading1Action()      const { return m_heading[1]; }
    QAction *heading2Action()      const { return m_heading[2]; }
    QAction *heading3Action()      const { return m_heading[3]; }
    QAction *heading4Action()      const { return m_heading[4]; }
    QAction *heading5Action()      const { return m_heading[5]; }
    QAction *heading6Action()      const { return m_heading[6]; }

    QAction *insertTableAction()       const { return m_insertTable; }
    QAction *insertRowAboveAction()    const { return m_insertRowAbove; }
    QAction *insertRowBelowAction()    const { return m_insertRowBelow; }
    QAction *deleteRowAction()         const { return m_deleteRow; }
    QAction *insertColumnLeftAction()  const { return m_insertColumnLeft; }
    QAction *insertColumnRightAction() const { return m_insertColumnRight; }
    QAction *deleteColumnAction()      const { return m_deleteColumn; }
    QAction *alignColumnNoneAction()   const { return m_alignColumn[int(TableAlign::None)]; }
    QAction *alignColumnLeftAction()   const { return m_alignColumn[int(TableAlign::Left)]; }
    QAction *alignColumnCenterAction() const { return m_alignColumn[int(TableAlign::Center)]; }
    QAction *alignColumnRightAction()  const { return m_alignColumn[int(TableAlign::Right)]; }

public Q_SLOTS:
    /// Re-evaluates every action's enabled state from `hasDoc && !readOnly`
    /// (class doc), plus the table-specific tier described above. Call
    /// after attaching/detaching the view or document, after
    /// `View::setReadOnly`, and on the view's `caretChanged`/the
    /// document's `d2DocumentChanged` (both wired automatically by
    /// `setView`/`setDocument`) — public so a consumer that flips
    /// read-only through a path this controller doesn't observe can force
    /// a re-check.
    void updateEnabledStates();

private:
    void setupActions();
    void setupTableActions();

    View                    *m_view     = nullptr;
    Markoff::MarkoffDocument *m_document = nullptr;

    QAction *m_bold       = nullptr;
    QAction *m_italic     = nullptr;
    QAction *m_strike     = nullptr;
    QAction *m_inlineCode = nullptr;
    QAction *m_link       = nullptr;
    QAction *m_heading[7] = {nullptr};  // index = level (0 = paragraph, 1..6 = heading)

    QAction *m_insertTable       = nullptr;
    QAction *m_insertRowAbove    = nullptr;
    QAction *m_insertRowBelow    = nullptr;
    QAction *m_deleteRow         = nullptr;
    QAction *m_insertColumnLeft  = nullptr;
    QAction *m_insertColumnRight = nullptr;
    QAction *m_deleteColumn      = nullptr;
    /// Indexed by `TableAlign` (None/Left/Center/Right) — a checkable,
    /// mutually exclusive quartet (`m_alignGroup`).
    QAction *m_alignColumn[4] = {nullptr};
    QActionGroup *m_alignGroup = nullptr;
};

}  // namespace Markoff::Canvas
