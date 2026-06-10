// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include <memory>

#include <markoff/core/MarkdownView.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/MarkoffLiveExport.h>

namespace Markoff { class FindController; }

namespace Markoff::Live {

/// QWidget host for the QML LiveView. Sits at the QWidget-vs-QML
/// impedance boundary so a host like Corbomite — a QWidget app — can
/// host the QML-based live editor without managing QQuickWidget +
/// LiveListModelBinding wiring by hand.
///
/// Subclasses `Markoff::MarkdownView` so the consumer's polymorphic
/// `MarkdownView *activeLeaf()` swap-dispatch works the same way
/// markoff-source's `Editor` does.
///
/// Restored 2026-05-20 driven by Corbomite port pull. The withdrawn
/// markoff-live freeze D12 amendment pinned this shape; this is the
/// implementation.
///
/// Lifecycle:
///   - Constructed with a `Capabilities` mask. The binding is created
///     immediately; the QQuickWidget loads `EditorContent.qml`
///     synchronously (loaded the first time the widget is shown).
///   - `setDocument(doc)` wires the binding + auto-creates a `Session`
///     for the binding to use. The session is destroyed in `~EditorWidget`.
///
/// Cursor contract (spec 2026-06-09-markdownview-contract-v2 §3/§4.1):
///   - `cursorPosition()` reads `LiveCursorState::currentTextCaret()` (the
///     canonical store — no widget-side cursor state) and maps
///     (block row, qtPos) to the flat-line CursorPos model: each model
///     block contributes 1 + internal-'\n' lines; column is 1-based UTF-16
///     within the line. Non-TextCaret variants report the block's first
///     line, column 1; no cursor reports {1,1}.
///   - `setCursorPosition()` reverses the mapping and routes through the
///     chokepoint `LiveCursorState::requestTextCaretAtRow` (L3 decision,
///     docs/specs/2026-05-22-cursor-authority-decision.md). Out-of-range
///     positions clamp to the last block / line end — never a no-op.
///   - `cursorPositionChanged(line, column)` is emitted on every
///     `LiveCursorState::cursorChanged`, mapped the same way.
///
/// Read-only contract (spec 2026-06-09-markdownview-contract-v2 §4.2):
///   - `setReadOnly(ro)` stores via the base, then pushes to the binding's
///     `readOnly` flag — the single authority every mutation-ingress gate
///     reads (LiveEditBinding, LiveStructuralKeyHandler,
///     LiveClipboardController, TableEditBinding, LiveActionController).
///     Navigation, selection, copy, link activation and find keep working.
class MARKOFF_LIVE_EXPORT EditorWidget : public Markoff::MarkdownView {
    Q_OBJECT
public:
    explicit EditorWidget(LiveListModelBinding::Capabilities caps =
                              LiveListModelBinding::AllCapabilities,
                          QWidget *parent = nullptr);
    ~EditorWidget() override;

    // MarkdownView contract.
    void setDocument(Markoff::MarkoffDocument *doc) override;

    /// Flat-line CursorPos over LiveCursorState — see class doc. O(blocks)
    /// per read; deliberately uncached (a cache would be a second cursor
    /// store, INVARIANTS #3).
    Markoff::CursorPos cursorPosition() const override;
    /// Routes through LiveCursorState::requestTextCaretAtRow; clamps
    /// out-of-range positions to the last block / line end.
    void setCursorPosition(Markoff::CursorPos pos) override;

    /// Read the QML ListView (Flickable) contentY / contentHeight ratio.
    /// Returns 0.0 when the content fits in the viewport or when the
    /// QML root has not yet loaded. While an attach-window write is
    /// pending (see setter), returns the pending fraction so a
    /// capture-after-restore round-trips. O(1) QML property read.
    float scrollPositionVisualLine() const override;
    /// Set the ListView's contentY to pos × contentHeight and emit
    /// scrollPositionChanged (spec §9). Clamps pos to [0, 1].
    /// Attach-window contract: a write issued before the QML scene has
    /// scrollable content (e.g. in the same call stack as setDocument)
    /// is latched and applied once contentHeight materializes — never
    /// silently dropped.
    void setScrollPositionVisualLine(float pos) override;

    /// Base store + push to the binding's `readOnly` flag — the single
    /// authority for the mutation-ingress gates (spec §4.2).
    void setReadOnly(bool ro) override;

    /// Base store + signal, then forward a widget-owned copy's address to
    /// the binding (spec §4.3). The binding's theme() is never null —
    /// it rotates two internal copy-buffers — so the widget keeps a copy
    /// alive and passes its address; there is no pointer-equality
    /// short-circuit in the binding, so every call notifies QML.
    void setTheme(const Markoff::Theme &t) override;

    /// Base clamp + store + signal, then forward the base's canonical
    /// value to the binding (spec §4.3).
    void setFontScale(qreal s) override;

    // --- Format verbs — delegate to LiveActionController's QActions ---
    // (spec §4.4). QAction::trigger() respects the action's enabled state,
    // so read-only gating, undo/redo, and selection requirements all ride
    // along in one authority.
    void toggleBold() override;
    void toggleItalic() override;
    void toggleStrikethrough() override;
    void toggleInlineCode() override;
    void insertLink() override;
    /// Levels 0..6: trigger heading<level>Action(). Out-of-range: no-op.
    void setHeadingLevel(int level) override;

    bool hasCursor()  const override { return true; }
    bool hasEditing() const override { return !isReadOnly(); }

    // Live-specific accessors.
    LiveListModelBinding *binding() const noexcept;

    /// Forward to the binding's attach hook. The controller is consumer-owned;
    /// EditorWidget does NOT take ownership.
    void attachFindController(Markoff::FindController *fc) override;
    void detachFindController() override;

private:
    /// Recompute the EditorContext from the current cursor (via LiveCursorState)
    /// and emit contextChanged if the context has changed (change-gated, spec §7).
    /// Called on every LiveCursorState::cursorChanged and on model dataChanged.
    void recomputeContext();

    /// Connected to the QML ListView's contentYChanged NOTIFY signal via the
    /// runtime SIGNAL() string form. Emits scrollPositionChanged (spec §9).
    Q_SLOT void onContentYChanged();

    /// Connected to the QML ListView's contentHeightChanged NOTIFY signal
    /// (same SIGNAL() string pattern). Applies a pending attach-window
    /// scroll write once the scene has scrollable content.
    Q_SLOT void onContentHeightChanged();

    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff::Live
