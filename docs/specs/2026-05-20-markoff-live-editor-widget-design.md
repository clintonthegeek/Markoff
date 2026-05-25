# Markoff::Live::EditorWidget — retroactive design note

**Date:** 2026-05-20 (post-facto; widget landed earlier this session in `bc8216d`).
**Status:** Implementation shipped. This note records the design calls so the next agent doesn't re-litigate them.
**Branch:** `exploration/new-foundation`.

---

## Why this is retroactive

The widget was built mid-session in response to live Corbomite port pressure (NoteEditorWidget needed a QWidget-shaped live editor host). Option B (build it properly) was chosen over Option A (partial-stub the live-leaf branch) conversationally; no design doc was drafted first because the port-first pivot earlier the same day had explicitly withdrawn the speculative `markoff-core` freeze. This note reconstructs the design after the fact for the historical record.

The withdrawn `markoff-live` freeze amendment **D12** (`docs/specs/2026-05-19-markoff-live-freeze-shape-design.md` amendments log) pinned the same shape. That amendment is still marked "withdrawn same-day"; this doc supersedes it for the EditorWidget piece specifically. The companion D11 (`Capabilities::Editable` for read-only mode) remains withdrawn — reintroduce only when a real consumer pulls on it.

## Problem

Corbomite is a QWidget app. The new live leaf is a QML view driven by `LiveListModelBinding`. Hosting QML inside a QWidget normally requires the consumer to:

1. Construct a `QQuickWidget`.
2. Construct a `LiveListModelBinding` with the right `Capabilities`.
3. Inject the binding into the QML root context.
4. Resolve the correct QML resource URL (the `qt_add_qml_module` source-side `/qml/` prefix is preserved — a footgun caught in `d4b117a`).
5. Wire `setDocument` to create + destroy a `Session` per document lifetime.
6. Re-emit `d2DocumentChanged` on initial population because `loadFromMarkdown` typically ran before the widget was constructed (the host populates the document and then hands it over).

Six steps with three non-obvious footguns. The consumer story isn't "embed the QML view" — it's "absorb the QML/QWidget impedance boundary." That's a library concern, not a host concern.

## Decision

Ship `Markoff::Live::EditorWidget` — a `QWidget` subclass of `Markoff::MarkdownView` (the polymorphic tri-view contract) that owns the `QQuickWidget` + `LiveListModelBinding` + `Session` lifecycle internally.

- **Inheritance:** `public Markoff::MarkdownView` — consumers hold `MarkdownView *activeLeaf()` and dispatch through the contract, identical to `markoff-source::Editor`.
- **Construction:** `EditorWidget(Capabilities caps, QWidget *parent)`. The binding is created immediately; the QQuickWidget loads `EditorContent.qml` synchronously the first time the widget is shown.
- **Document lifecycle:** `setDocument(doc)` tears down the old session (if any), wires the binding, creates a new `Session` for this widget, and calls `doc->flushPendingD2Changed()` to force initial model population.
- **Find lifecycle:** `attachFindController(FindController *)` / `detachFindController()` forward to the binding. The controller is consumer-owned — `EditorWidget` does NOT take ownership.
- **Capabilities:** Defaults to `AllCapabilities`. The future `Capabilities::Editable` flag (D11, currently withdrawn) is the lever for read-only Live; not wired yet.

## Known degradations (port-state)

Tracked here so they don't surprise the next consumer:

1. **`cursorPosition()` / `setCursorPosition(CursorPos)`** return / accept `{0,0}`. The legacy line/column model doesn't map directly to `TextAnchor`/`BlockAnchor`. Re-implement when EphemeralState round-trip pulls on it.
2. **`setReadOnly(true)`** stores the bool but does not actually disable editing. Requires the D11 `Capabilities::Editable` work — reintroduce when HoverPopover or Reading-mode-via-Live pulls on it.

These are intentional gaps documented in the header. They don't block Corbomite's Live-mode render path (the port-first milestone reached 2026-05-20).

## Companion: `EditorContent.qml`

A minimal QML wrapper (in `libs/markoff-live/qml/EditorContent.qml`) hosting `LiveView`. The resource URL the C++ side uses is `qrc:/qt/qml/org/markoff/live/qml/EditorContent.qml` — Qt preserves the source-side `/qml/` prefix when `qt_add_qml_module` builds the resource tree. Direct `qrc:/qt/qml/org/markoff/live/EditorContent.qml` (without the `/qml/`) fails silently with an empty render. That fix landed in `d4b117a`.

## Consumers

- **CorbomiteApp** (`port/foundation-exploration` branch): `NoteEditorWidget` constructs and parents an `EditorWidget` per live leaf, calls `setDocument` on document open.
- **markoff-testapp**: still uses `LiveListModelBinding` + `QQuickView` directly. The test app is QML-native; it doesn't benefit from the wrapper. No plan to migrate.

## What this widget is NOT

- **Not the entry point to `markoff-live`.** Direct QML hosts still use `LiveListModelBinding` against a `QQuickView` or `QQuickWindow`. `EditorWidget` is the QWidget-host convenience.
- **Not a polymorphic carrier for reading mode.** The retired `Markoff::Reading::ReadingView` is not coming back via this widget; the future Reading restoration (or read-only Live via Capabilities) will be its own type.
- **Not feature-equivalent to the retired `Markoff::Editor`.** The old class exposed ~20 signals (linkHovered, wordCountChanged, completionDismissHint, etc.) — most of those are stubbed on the Corbomite side with `TODO(port-foundation-exploration)` markers. Each comes back as its corresponding port feature pulls.

## Test coverage

None of its own. The widget is a thin lifecycle wrapper; the underlying `LiveListModelBinding` + `LiveView` are covered by `tst_live_render_*` (200+ slots). A widget-level smoke test could be added later if a regression class emerges; today the cost-benefit doesn't justify the QWidget+QML test scaffold.

## Open items

1. **Initial population fix is a workaround.** `EditorWidget::setDocument` calls `doc->flushPendingD2Changed()` because the host already ran `loadFromMarkdown` before handing the doc to the widget. The real fix is one of (a) `setDocument` always replays the current state, (b) the binding subscribes to a "current state" pull on attach. Resolution comes with the `MarkoffDocument::resetContent` D2-build fix (port-first recap follow-up #4).

2. **Find UI wiring is consumer-owned.** The next port pass builds a Corbomite `FindBar` QWidget and constructs `Markoff::FindController` per document. The widget's `attachFindController` accepts it; the binding routes it through. No Markoff-side micro-spec needed unless the controller surface itself proves insufficient.

3. **Multi-tab document sharing surfaced a quality bug.** Today multiple `EditorWidgets` pointing at the same `MarkoffDocument` (Corbomite's NoteEditorWidget per-tab model) cause edits to echo. The root cause is shared-document + per-widget sessions firing `d2DocumentChanged` on every binding. The fix likely lives in Corbomite (one binding per doc shared across views, or one doc per leaf with state replication) but may surface a Markoff-side `Session`/binding contract question. Brainstorm before coding.

## References

- Header: `libs/markoff-live/include/markoff/live/EditorWidget.h`
- Implementation: `libs/markoff-live/src/EditorWidget.cpp`
- Companion QML: `libs/markoff-live/qml/EditorContent.qml`
- Live freeze amendments log (D11/D12 withdrawal): `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md`
- Port-first session recap: `docs/handoff/2026-05-20-port-first-session-recap.md`
- Resource-path fix: commit `d4b117a`
- Initial-population fix: commit `d5d210e`
- EditorWidget landing: commit `bc8216d`
