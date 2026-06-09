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
/// Known degradations (port-state):
///   - `cursorPosition()` / `setCursorPosition(CursorPos)` return / accept
///     `{0,0}` — the legacy line/column model doesn't map directly to the
///     new TextAnchor/BlockAnchor model. Re-implement when the
///     EphemeralState round-trip pulls on it.
///   - `setReadOnly(true)` stores the bool but doesn't actually disable
///     editing; that requires `Capabilities::Editable` (E1, currently a
///     withdrawn live-freeze D11 amendment — reintroduce when HoverPopover
///     or Reading-mode-via-Live pulls on it).
class MARKOFF_LIVE_EXPORT EditorWidget : public Markoff::MarkdownView {
    Q_OBJECT
public:
    explicit EditorWidget(LiveListModelBinding::Capabilities caps =
                              LiveListModelBinding::AllCapabilities,
                          QWidget *parent = nullptr);
    ~EditorWidget() override;

    // MarkdownView contract.
    void setDocument(Markoff::MarkoffDocument *doc) override;

    bool hasCursor()  const override { return true; }
    bool hasEditing() const override { return !isReadOnly(); }

    // Live-specific accessors.
    LiveListModelBinding *binding() const noexcept;

    /// Forward to the binding's attach hook. The controller is consumer-owned;
    /// EditorWidget does NOT take ownership.
    void attachFindController(Markoff::FindController *fc) override;
    void detachFindController() override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff::Live
