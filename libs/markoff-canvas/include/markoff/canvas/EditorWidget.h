// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.
#pragma once

#include <memory>

#include <markoff/core/MarkdownView.h>

namespace Markoff {
class FindController;
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
/// end — never a no-op. `cursorPositionChanged`/`scrollPositionChanged`
/// emission and `scrollPositionVisualLine()` land in P3.2.
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

    bool hasCursor() const override { return true; }

    /// The composed rendering/input engine. Non-owning; valid for the
    /// widget's lifetime. Leaf-specific escape hatch for tests/demo, same
    /// role as `Live::EditorWidget::binding()`.
    View *view() const noexcept;

private:
    View *m_view = nullptr;
    Markoff::Session *m_session = nullptr;
    QMetaObject::Connection m_docDestroyedCon;
};

}  // namespace Markoff::Canvas
