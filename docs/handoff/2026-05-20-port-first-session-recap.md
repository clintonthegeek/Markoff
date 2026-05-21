# 2026-05-20 — Port-first session recap

**Branch:** `exploration/new-foundation` (Markoff) + `port/foundation-exploration` (Corbomite, new this session)

**Status at session end:** Corbomite renders foundation-exploration content in the Live editor. Two real Markoff fixes + one Corbomite-side fix unblocked this. Multiple known degradations remain — they're the natural next port-first features.

## The pivot

Session started with a 19-decision `markoff-core` freeze spec being drafted (`docs/specs/2026-05-20-markoff-core-freeze-shape-design.md`). User pushed back: speccing a freeze before any Corbomite reintegration begins is the spec-review-between-two-agents antipattern that the 2026-04-20 Phase C handoff explicitly warned against. The freeze spec was withdrawn (preserved in the file as draft-reference-not-action-plan) and we pivoted to **port-first**: begin Corbomite reintegration against the foundation-exploration branch HEAD, let API gaps surface, drive each one with a one-decision micro-spec + one-commit Markoff fix.

This is the same rhythm that produced the find-session-scope work earlier — that one fell out of a real bug (typing in the find bar hijacked focus), and the resulting `Markoff::FindController` design is much sharper than the speculative version that would have come out of a freeze pass.

## Branch state across both repos

| Repo | Branch | State |
|------|--------|-------|
| Markoff (`/home/clinton/dev/Markoff`) | `master` | v0.6.x line. Frozen for now. Foundation-exploration will eventually merge here. |
| Markoff | `exploration/new-foundation` | **Active development.** D-arc + E-arc rebuild. ~1000 commits ahead of master. Tag held: `v0.7.0-e3a` (dogfood pending). |
| Corbomite (`/home/clinton/dev/Corbomite`) | `master` | Pinned to Markoff `master` tip (`2b7b3e7`). Stable; no port work touches it. |
| Corbomite | `port/foundation-exploration` | **Active port branch.** Pinned to Markoff `exploration/new-foundation` HEAD. Builds + launches + renders docs (with known degradations). |

The Markoff side is being driven from `.worktrees/foundation-exploration/` (the existing worktree). The Corbomite port is on the main `/home/clinton/dev/Corbomite/` checkout (no worktree — different branch on the main tree).

## Eventual merge plan back to masters

Decided 2026-05-20:

1. **Complete Corbomite port on `port/foundation-exploration`** — port features one at a time as evidence-driven micro-specs against Markoff `exploration/new-foundation`. Find UI is #1.
2. **When Corbomite port is functionally complete** (find, source-widget swap, embeds/tags/callouts, table editor minimum, theme port), draft a small evidence-driven `markoff-core` freeze spec based on actual port pressure (not the speculative draft).
3. **Tag Markoff** at that point — probably `v0.7.0-freeze`.
4. **Merge Markoff `exploration/new-foundation` → Markoff `master`.** Large merge: retires the old leaves wholesale; new layout becomes canonical.
5. **Merge Corbomite `port/foundation-exploration` → Corbomite `master`.** Submodule pin tracks new Markoff master tip.

**The order matters.** Markoff merges first so Corbomite master's submodule pin always points at a valid Markoff commit. Reversing the order would leave Corbomite master with a Markoff pin nobody else can resolve.

## What happened this session (chronological)

### Phase 1 — Withdrawn freeze (3 docs commits, no functional change)

- `6ed2bc8` Markoff: doc updates marking freeze spec as draft-reference, audit as "questions open," CLAUDE.md banner reflecting port-first.

### Phase 2 — Submodule pin bump + initial stubbing

- `f4ad88a4` Corbomite: bump `libs/markoff-family` submodule from `2b7b3e7` (Markoff master tip) to `30c4f57` (Markoff foundation-exploration tip — find work head). Created `port/foundation-exploration` branch.
- `af45aa5` Markoff: add `MARKOFF_BUILD_APPS` CMake option (default-ON standalone, default-OFF subdirectory) so Corbomite's parent build doesn't try to link `Qt6::QuickWidgets` for `markoff-collab-testapp` it doesn't need.
- `3f19703f` Corbomite: initial CMake target renames + include-path renames + Markoff::Reading retirement + several Corbomite-side headers stubbed (MarkdownRenderChild, MermaidRenderer, EmbedRegistrar, MarkoffAdapters, MarkdownRenderer, SourceEditor's qutepart shim).

### Phase 3 — EmbedRegistry restoration (driven by Corbomite port pull)

- `47f62c4` Markoff: restore `Markoff::EmbedRegistry` + `EmbedRequest` + `EmbedDepthGuard` + `MarkdownRenderChild` + `Vault::ResourceProvider` under new `markoff/core/` + `markoff/core/vault/` path conventions. First port-driven Markoff restoration.
- `e8986f8` Markoff: add `hasExtension` + `unregisterExtension` to EmbedRegistry abstract (Corbomite's `EmbedRegistrar` lifecycle pattern needs both).
- `00d8c455` Corbomite: more stubs — CodeBlockRegistrar migrated to new shared_ptr-based API; theme files wholly disabled; tests gated by `CORBOMITE_PORT_BUILD_TESTS=OFF`; `CMAKE_POSITION_INDEPENDENT_CODE=ON` for static-Markoff-libs-in-shared-Corbomite-libs.

### Phase 4 — EditorWidget design + build

User confirmed: Option B (build `Markoff::Live::EditorWidget` properly rather than partial-stub the live-leaf branch).

- `bc8216d` Markoff: `Markoff::Live::EditorWidget` lands. QQuickWidget subclass that subclasses `Markoff::MarkdownView`, owns a `LiveListModelBinding` (with caller-supplied Capabilities), loads `EditorContent.qml` (a minimal LiveView wrapper), auto-creates a Session on setDocument, forwards attachFindController. Adds `Qt6::QuickWidgets` to markoff-live's find_package; consumers must too.
- `76bcf7b3` Corbomite: NoteEditorWidget hosts Live via `Markoff::Live::EditorWidget`. Stubs nearly every old Markoff::Editor signal/method (textChanged, cursorPositionChanged(int,int), wordCountChanged, linkClicked, linkHovered, completionDismissHint, toPlainText, cursorLine/cursorColumn, viewport, cursorScreenRect, goToLine, goToLineAndColumn, setReadOnly behavior, setMermaidRenderer, etc.) with TODO(port-foundation-exploration) markers. MainWindow.cpp's whole editor-action registration block disabled (most Markoff::ActionId values renamed/retired in the new core). Corbomite + CorbomiteApp targets both build clean at this point.

### Phase 5 — Runtime debug + the actual render

User reports app launches but editor area is empty. Three iterations of debug instrumentation and fixes:

- `d4b117a` Markoff: fix EditorContent.qml resource path. `setSource()` was looking at `qrc:/qt/qml/org/markoff/live/EditorContent.qml`; qt_add_qml_module preserves the source-side `/qml/` directory prefix so the actual path is `qrc:/qt/qml/org/markoff/live/qml/EditorContent.qml`.
- Corbomite `src/CMakeLists.txt`: also link `markoff_liveplugin` + `markoff_liveplugin_init` (static QML module requires the auto-generated plugin lib + initializer for the consumer-side `import org.markoff.live 1.0` to resolve).
- `d5d210e` Markoff: `EditorWidget::setDocument` calls `doc->flushPendingD2Changed()` after `binding->setSession`. The setDocument code only connects to `documentLoaded` / `d2DocumentChanged` signals — and the host (Corbomite Vault) populates the doc via `loadFromMarkdown` BEFORE handing the doc to the widget. So those signals already fired; the binding was never told to populate. flushPendingD2Changed forces a `d2DocumentChanged` emit.
- `875f852c` Corbomite: `Vault::openDocument` switches from `MarkoffDocument::resetContent(bytes, FirstOpen)` to `MarkoffDocument::loadFromMarkdown(bytes)`. The discovery: `resetContent` only populates the legacy `d->buffer`; D2 per-block CRDT state (what `iterateBlocks()` returns, what `LiveBlockModel` rebuilds from) is built only by `loadFromMarkdown`. So `resetContent` left the document with `visibleLength=480` but `iterateBlocks()` empty → model had zero rows → nothing visible.

After the loadFromMarkdown fix landed: editor renders content. Milestone hit.

### Phase 6 — Cleanup + this handoff

- `2291c99` Markoff: remove debug logging from EditorWidget.
- (this commit) Markoff: handoff doc + CLAUDE.md banner update + e-arc-status entry.
- (paired commit) Corbomite: status doc + CLAUDE.md banner update.

## Markoff repo state at end of session

**Branch HEAD:** `2291c99` (after this doc lands, the new HEAD).

**Test baseline:** 215/218 fast tests pass. Three pre-existing failures unchanged from 2026-05-18.

**Files added this session (all under foundation-exploration tree):**

- `docs/specs/2026-05-20-markoff-core-freeze-shape-design.md` (the withdrawn freeze; preserved as draft reference)
- `docs/handoff/2026-05-20-port-first-session-recap.md` (this doc)
- `libs/markoff-core/include/markoff/core/EmbedRegistry.h`
- `libs/markoff-core/include/markoff/core/EmbedDepthGuard.h`
- `libs/markoff-core/include/markoff/core/MarkdownRenderChild.h`
- `libs/markoff-core/include/markoff/core/vault/ResourceProvider.h`
- `libs/markoff-core/src/MarkdownRenderChild.cpp`
- `libs/markoff-live/include/markoff/live/EditorWidget.h`
- `libs/markoff-live/src/EditorWidget.cpp`
- `libs/markoff-live/qml/EditorContent.qml`

**Files modified this session:**

- `CLAUDE.md` (banner)
- `CMakeLists.txt` (MARKOFF_BUILD_APPS option)
- `docs/2026-05-18-public-api-surface-audit.md` (status reverted to "awaiting port evidence")
- `docs/e-arc/e-arc-status.md` (recent-changes log + active-phase header)
- `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md` (amendment log)
- `libs/markoff-core/CMakeLists.txt` (new sources)
- `libs/markoff-live/CMakeLists.txt` (EditorWidget sources + Qt6::QuickWidgets)

## Corbomite repo state at end of session

**Branch:** `port/foundation-exploration` (forked from `master`). Submodule pinned to Markoff `exploration/new-foundation` HEAD via `libs/markoff-family/`.

**Build state:** `Corbomite` and `CorbomiteApp` targets both build clean. Test executables under `tests/` are gated by `CORBOMITE_PORT_BUILD_TESTS=OFF` (most reference retired Markoff types — re-enable in chunks as feature ports unblock them).

**Runtime state:** launches; opens vault; opens docs; renders content in Live mode.

**Known degraded behaviors:**

1. **Doc-sharing doubling.** Editing causes content to repeat at end of doc. Root cause: multiple `LiveListModelBindings` share one `MarkoffDocument` across NoteEditorWidget tabs. Each EditorWidget creates its own Session; edits fire `d2DocumentChanged` on the shared doc; all bindings respond. Needs a dedicated micro-spec — see Corbomite's `docs/port-foundation-exploration.md` for follow-up planning.
2. **Source mode empty.** Switching to Source mode shows an empty source widget. `SourceTextDocumentBinding` needs its own population trigger equivalent to EditorWidget's `flushPendingD2Changed`. Likely a Markoff-side fix.
3. **Most toolbar actions stubbed.** `Markoff::ActionId` enum was restructured for foundation-exploration (no `FindNext` / `FindPrevious` / `Replace` / `ToggleBold` / `IncreaseHeading` / etc.); Corbomite's editor-action registration block is wholly disabled. Each action comes back as its corresponding port feature lands.
4. **Sidebars gone.** Unrelated to the editor port (Corbomite-side issue, possibly plugin loading or sidebar-construction in MainWindow). Not investigated.
5. **Reading mode = no-op.** `Markoff::Reading::ReadingView` retired with the old leaves. `setViewMode(Reading)` falls back to LivePreview. Restoration awaits either reading-leaf restoration or read-only Live (`Capabilities::Editable`).
6. **MermaidRenderer is no-op.** Retired with old leaves (E5 work).
7. **Embeds non-functional.** EmbedRegistry abstract exists; no concrete factories registered (E3 work).
8. **HoverPopover renders nothing.** Used `Markoff::Reading::ReadingView`; retired. Stubbed.

## Open Markoff-side issues surfaced this session

These came up during the port but weren't blockers; should be addressed soon:

1. **`MarkoffDocument::resetContent` doesn't build D2 blocks.** The legacy path only updates `d->buffer`. `loadFromMarkdown` is the only path that materializes D2 blocks. Either `resetContent` should also build D2 (likely the right fix — Origin enum then drives only undo-stack handling) or it should be documented as legacy-buffer-only and consumers always use `loadFromMarkdown`.

2. **Two-buffer Theme rotation may need a different default.** `LiveListModelBinding` initializes both buffers to `Markoff::Theme::defaultLight()` — works for the test app, but consumers may want OS-driven theming via something like the disabled `Corbomite::Core::SystemThemeBuilder`. Theme port (when it lands) needs to figure this out.

3. **`Theme` QML element name uppercase warning.** `qt.qml.typeregistration: Invalid QML element name "Theme"; value type names should begin with a lowercase letter.` Informational, doesn't block. Could be fixed by renaming `QML_NAMED_ELEMENT(Theme)` → `QML_NAMED_ELEMENT(theme)` in `ThemeForeign.h` if QML callsites can adapt.

## Next session — priority order

Per Corbomite's `docs/port-foundation-exploration.md`:

1. **Find UI port (Corbomite task #5)** — the original port-first target. Build a Corbomite-owned `FindBar` QWidget; instantiate `Markoff::FindController` per document; attach/detach on leaf swap; wire Ctrl+F + FindNext + FindPrevious. Should produce zero or one Markoff-side micro-spec.
2. **Doc-sharing doubling (Corbomite task #8)** — quality bug. Likely Corbomite-side restructuring (one binding per doc with views sharing it, or one doc per leaf with state replication). Brainstorm needed.
3. **Source mode empty (Corbomite task #9)** — `SourceTextDocumentBinding` population. Probably Markoff-side fix.
4. **`MarkoffDocument::resetContent` builds D2** — Markoff-side cleanup of the issue we worked around. Two-line spec.

## How to resume

Fresh agent landing on either repo: read this doc, then the relevant repo's `CLAUDE.md` banner. Both have been updated to point here.

For Markoff work: `cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration/`. The branch is `exploration/new-foundation`.

For Corbomite work: `cd /home/clinton/dev/Corbomite/`, `git checkout port/foundation-exploration`. Note this is the main checkout, not a worktree.

Cross-repo work (typical for port-first): edit in both, push Markoff first, then re-bump the submodule pin in Corbomite (`cd /home/clinton/dev/Corbomite/libs/markoff-family && git fetch && git checkout <new-commit>`), commit submodule bump in Corbomite, then continue.
