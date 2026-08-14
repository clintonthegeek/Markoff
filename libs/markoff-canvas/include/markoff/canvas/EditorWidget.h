// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.
#pragma once

#include <memory>

#include <QPointer>

#include <markoff/core/FindController.h>
#include <markoff/core/MarkdownView.h>

namespace Markoff {
class Session;
}  // namespace Markoff

namespace Markoff::Canvas {

class View;

/// `Markoff::MarkdownView` contract-v2 wrapper composing the projection
/// `View` (spec §4.1 "EditorWidget wrapper"). `View` stays public for tests
/// and the manual demo harness; this class is the adoption seam — a
/// consumer that swaps leaves by `MarkdownView *` (Corbomite's
/// `NoteEditorWidget`) constructs this, not `View` directly.
///
/// Lifecycle (mirrors `Markoff::Live::EditorWidget`, spec §4.1):
///   - `setDocument(doc)` wires the composed `View` and auto-creates a
///     `Session` for the document; the session is destroyed in
///     `~EditorWidget` (or on document teardown, or on `setDocument`
///     replacing the document). `setDocument(nullptr)` detaches without
///     touching document content.
///
/// Attach-window contract (P3.1): `setDocument()`'s only caret-touching
/// step is the composed `View::setDocument()`'s internal reset to the
/// document's first block/byte 0, and it completes synchronously before
/// `setDocument()` returns. Canvas has no QML-style async model
/// population (C2 forbids deferral in this leaf entirely), so a caret
/// write issued by the consumer immediately after `setDocument()` returns
/// — in the same call stack, e.g. `ed->setDocument(doc);
/// ed->setCursorPosition(pos);` — is never raced or clobbered by anything
/// this class does later. Asserted by
/// `ViewContract::checkAttachWindowCaretWriteSurvives` (enrolled in
/// `tst_canvas_view_contract`).
///
/// Cursor contract: `cursorPosition()`/`setCursorPosition()` map the
/// composed `View`'s `{BlockId, byteOffset}` caret to/from the base's
/// flat-visual-line `CursorPos` model (each document block contributes
/// 1 + internal-'\n' lines; column is 1-based UTF-16 within the line) —
/// the same model live/source/styled report. O(blocks) per call,
/// deliberately uncached (a cache would be a second cursor store,
/// INVARIANTS #3). Out-of-range positions clamp to the last block/byte
/// end — never a no-op.
///
/// Signals (P3.2): `cursorPositionChanged` is wired to `View::caretChanged`
/// — the composed view's single chokepoint every caret-changing code path
/// (real events AND document-driven clamps) already funnels through
/// (`View::ensureCaretVisible`). `View::caretChanged` itself is
/// unconditional; the change-gating that matters at the contract's
/// `CursorPos` granularity happens here, comparing against the last
/// emitted position — a plain diff check, not a second caret store
/// (INVARIANTS #3 is about authority, not about remembering what you last
/// told a signal listener). `scrollPositionChanged` is wired to the
/// composed `View`'s `verticalScrollBar()->valueChanged` — see
/// `scrollPositionVisualLine()`.
class EditorWidget : public Markoff::MarkdownView {
    Q_OBJECT
public:
    explicit EditorWidget(QWidget *parent = nullptr);
    ~EditorWidget() override;

    // MarkdownView contract.
    void setDocument(Markoff::MarkoffDocument *doc) override;

    /// Flat-line CursorPos over the composed View's `{BlockId, byteOffset}`
    /// caret — see class doc.
    Markoff::CursorPos cursorPosition() const override;
    /// Reverse-maps to `{BlockId, byteOffset}` and routes through
    /// `View::setCaretPosition`. Out-of-range positions clamp to the last
    /// block/byte end (never a no-op).
    void setCursorPosition(Markoff::CursorPos pos) override;

    /// Fraction of the composed `View`'s vertical scrollbar range (same
    /// convention as `Source::Editor`/`Styled::Editor` — despite the name,
    /// this is `value() / maximum()`, not a literal line number). The
    /// scrollbar's range is in pixels over `View::documentHeight()`, whose
    /// unrealized entries carry an estimated height (`BlockLayoutCache`);
    /// no separate correction step is needed here because
    /// `View::updateScrollRange()` already re-derives the range (and
    /// re-pins a bottom-parked view) every time realization corrects an
    /// estimate — this accessor just reads whatever the scrollbar's
    /// current range says, after that correction has already happened.
    /// 0.0 if there is nothing to scroll (document fits the viewport) or
    /// no `View` yet.
    float scrollPositionVisualLine() const override;
    /// Clamps `pos` to `[0, 1]` — never a no-op. If the document currently
    /// fits the viewport (scrollbar range is empty), `setValue` would be a
    /// silent no-op and the scrollbar's own `valueChanged` would not fire;
    /// `scrollPositionChanged` is emitted explicitly in that case so the
    /// contract's "the write is always observable" minimum still holds.
    void setScrollPositionVisualLine(float pos) override;

    bool hasCursor() const override { return true; }

    /// Base store (`isReadOnly()`/`hasEditing()` read it) + push to the
    /// composed `View`'s own `setReadOnly` — the single authority its six
    /// mutation-ingress gates read (P3.3, spec §4.2; mirrors
    /// `Markoff::Live::EditorWidget::setReadOnly`'s pattern).
    void setReadOnly(bool ro) override;

    bool hasEditing() const override { return !isReadOnly(); }

    /// Composed `View::caretRect()`, which already reports in View's own
    /// widget-local coordinates — a pass-through, since View fills this
    /// widget's entire client area (spec §4.1 composition; no translation
    /// needed beyond View's own viewport->View mapping).
    QRect caretRect() const override;

    /// The composed rendering/input engine. Non-owning; valid for the
    /// widget's lifetime. Leaf-specific escape hatch for tests/demo, same
    /// role as `Live::EditorWidget::binding()`.
    View *view() const noexcept;

    /// Find (contract-v2 P3.4, spec §3): subscribes to `fc`'s
    /// `matchesChanged`/`currentMatchChanged` and repaints all-match +
    /// current-match highlights on the composed `View` (draw-time
    /// `FormatRange`s — see `View::setFindHighlights`), and to
    /// `navigationRequested` to scroll the match into view + place a
    /// non-focusing caret at it (both via `View::setCaretPosition`, the
    /// same chokepoint `setCursorPosition` routes through — its
    /// `ensureCaretVisible` call already does the scroll-into-view, so no
    /// separate scroll step is needed here). `fc` is consumer-owned; not
    /// linked, only observed. Calling this while already attached to a
    /// different controller detaches from the old one first.
    void attachFindController(Markoff::FindController *fc) override;
    /// Disconnects from the attached controller (if any) and clears all
    /// find-related paint state on the composed `View` (no leftover
    /// highlights).
    void detachFindController() override;

private:
    /// `View::caretChanged` handler: recomputes `cursorPosition()` and
    /// emits `cursorPositionChanged` iff it actually differs from
    /// `m_lastCursorPos` (change-gated per class doc).
    void onViewCaretChanged();

    /// Rebuilds the composed `View`'s find highlights wholesale from
    /// `m_findController->matches()` + `currentMatchIndex()`. Shared by
    /// both `matchesChanged` and `currentMatchChanged` — whole-list
    /// rebuild is cheap relative to a keystroke and keeps a single source
    /// of truth (the controller) rather than diffing incrementally.
    void rebuildFindHighlights();
    void onFindNavigationRequested(Markoff::FindController::Match match);

    View *m_view = nullptr;
    Markoff::Session *m_session = nullptr;
    QMetaObject::Connection m_docDestroyedCon;
    /// Last `CursorPos` this widget emitted `cursorPositionChanged` for —
    /// the change-gate `onViewCaretChanged` diffs against. Not a cursor
    /// authority (the composed `View`'s caret is); see class doc.
    Markoff::CursorPos m_lastCursorPos{1, 1};
    /// Consumer-owned; observed only (see `attachFindController` doc).
    QPointer<Markoff::FindController> m_findController;
};

}  // namespace Markoff::Canvas
