# Live Render — walking skeleton design

**Date:** 2026-04-29
**Branch:** `exploration/new-foundation`
**Phase:** Phase-2 of `markoff-view-qml` (sibling of the Source-mode editor shipped in Phase-1)
**Status:** approved (pending user spec review)

## 0. TL;DR

A new QML component, `LiveView.qml`, renders a markdown document as a **`ListView` of per-AST-block delegates** — paragraphs, headings, horizontal rules, images, code blocks for v0. It lives alongside `SourceEditor.qml` inside `markoff-view-qml`, slots into the existing `// PHASE-2 SEAM` in `MarkoffEditor.qml`, and consumes the same `EditorBackend` (so Source and Live share one `MarkoffDocument`, one `Session`, one undo stack, one `ParsePool`).

The architecture replaces the Qt-Widgets-era `markoff-live` editor whose six accumulated mess-causes (forked `TextControl`, `setFocusProxy`+`sendEvent` recursion, inline `Q_OBJECT` text-objects, AST-rebuild escape hatch, scene-out-of-sync re-serialize, offset-map cursor drift) are eliminated by virtue of switching to QML and block-level composition.

The one architectural risk that remains — *cross-block selection requires a custom layer* — was de-risked by the `cross-block-selection` spike (`docs/specs/2026-04-29-cross-block-selection-spike-findings.md`). The spike code stays committed at `.spike/cross-block-selection/`; the production `LiveSelectionModel` is structurally identical.

**v0 scope:** read-only render of paragraph + heading + horizontal-rule + image + code-block delegates, with cross-block selection + Ctrl+C copy + a right-click context menu wired through the KDAB Widget bridge. Editing, math, mermaid, tables, lists, frontmatter, callouts, links/wikilinks, autocomplete, etc. are explicitly deferred to subsequent specs.

---

## 1. Architecture overview

**Shape.** A new QML component `LiveView.qml` lives in `markoff-view-qml` alongside `SourceEditor.qml` and plugs into the existing `// PHASE-2 SEAM` in `MarkoffEditor.qml`. It consumes the same `EditorBackend` (so Source and Live share `MarkoffDocument`, `Session`, undo stack, parse pool — toggling between them is just swapping which child is visible).

**Internals.** `LiveView.qml` is a `ListView` with a `DelegateChooser` keyed on block-kind. The model is a C++ `QAbstractListModel` (`LiveBlockModel`) whose rows are AST blocks. A C++ `LiveListModelBinding` subscribes to `EditorBackend::parseUpdatedAt(parsed, atVersion)` and on each new parse, diffs the new block sequence against the current model and emits `beginInsertRows` / `beginRemoveRows` / `beginMoveRows` / `dataChanged` so ListView preserves delegates whose AST block still exists. Identity is **emergent from the diff** — Myers-style longest-common-subsequence over `(kind, content-hash)` keys does the actual matching, sidestepping the "synthesized hash key changes when previous sibling changes" trap.

**Selection.** `LiveSelectionModel` (C++) holds `(anchorBlock, anchorOffset, activeBlock, activeOffset)` and is the single source of truth for highlighted text. A top-level `MouseArea` over the `ListView` owns all left-button input. Each delegate's `TextEdit` has `selectByMouse: false` and applies its slice of the global selection via per-delegate `Connections` reacting to `selModel.selectionChanged`. The `hit(mouseX, mouseY)` function in QML translates mouse coordinates into `(blockIndex, offset)` pairs, handling all the edge cases enumerated in the spike findings (mouse leaving window, in-gap column tracking, viewport margins, item bounds, etc.).

**Read-only contract for v0.** Delegates render block content. They use `TextEdit` with `readOnly: true` (not `Text`) so the eventual editing path is a flag flip plus the focus/selection layer. No `setFocusProxy`, no manual key dispatch — QML's natural focus chain handles per-delegate keyboard input out of the box. Selection within a delegate works natively; cross-delegate selection is delivered by `LiveSelectionModel`.

**Widget-window bridge.** Context menus, autocomplete popups, hover popovers, and modal dialogs are designed as Qt Widget windows (per the KDAB pattern: a `QML_ELEMENT`-registered `QObject` handler owning a `std::unique_ptr<QWidget>` instance; QML invokes show/hide via the handler; widget shows as a top-level OS window). This buys native OS styling, real platform menus on KDE/Plasma, and avoids QML-window scene-graph constraints. **v0 commitment:** the test app + library link `Qt6::Widgets`, `main.cpp` uses `QApplication`, and v0 ships *one* usage to validate end-to-end — a right-click `QMenu` on `LiveView` with "Copy" + "Select All" actions. The bridge is the documented default for *all* future menus/popups/dialogs in this library; QML `Menu`/`Popup` is the exception, used only for trivial in-flow widgets where native windowing isn't needed.

**Wayland note.** Per the KDAB article, widget-window positioning is restricted on Wayland (no animated window-move). For our use cases (modal context menus, popups at fixed cursor positions) this isn't a constraint. Modality must use `Qt::ApplicationModal` (not `Qt::WindowModal`) since widget windows can't parent to QQuickWindow.

---

## 2. File structure & components

### C++ side, `markoff-view-qml`

| File | Responsibility |
|---|---|
| `src/LiveBlockModel.{h,cpp}` | `QAbstractListModel` over AST blocks. Roles: `kind` (string), `text` (string), plus kind-specific (`headingLevel`, `imageSrc`/`imageAlt`/`imageTitle`, `codeLanguage`, `codeText`). The `kind` role is **string-keyed**, not a closed C++ enum, so plugin-registered kinds (future) don't require recompiling. |
| `src/LiveListModelBinding.{h,cpp}` | `QObject` exposed to QML. Subscribes to `EditorBackend::parseUpdatedAt(parsed, atVersion)`, owns `LiveBlockModel`, drives the diff machinery. Also clears `LiveSelectionModel` if any block touched by selection is removed by the diff. Properties: `editorBackend`, read-only `model`, read-only `selectionModel`. |
| `src/AstBlockDiff.{h,cpp}` | Pure C++ Myers/LCS diff over block sequences (no Qt dependencies beyond `QList`/`QString`). Input: two `QList<BlockKey>`; a `BlockKey` is `(kind, content-hash)`. Output: an edit script (`{Equal, Insert, Delete, Move}` ops with row indices). Separated for unit testing. |
| `src/LiveSelectionModel.{h,cpp}` | Cross-block selection. `(anchorBlock, anchorOffset, activeBlock, activeOffset)` state. `Q_INVOKABLE` `begin/extend/clear`, `rangeForBlock`, `collectSelectedText`, `copySelectionToClipboard`. Structurally identical to the spike at `.spike/cross-block-selection/SelectionModel.{h,cpp}`. |
| `src/LiveContextMenuHandler.{h,cpp}` | KDAB-pattern Widget bridge. Owns `std::unique_ptr<QMenu>`. `Q_INVOKABLE` `popup(QPoint globalPos)`. v0 actions: Copy, Select All. |

### QML side

| File | Responsibility |
|---|---|
| `qml/LiveView.qml` | Top-level component. Holds `ListView` + `DelegateChooser` (from `Qt.labs.qmlmodels`) + a top-level `MouseArea` driving `LiveSelectionModel`. Properties: `editorBackend` (in). The `hit(mouseX, mouseY)` JS function from the spike's findings is the load-bearing piece (final form documented in `docs/specs/2026-04-29-cross-block-selection-spike-findings.md` §3). Right-click handler invokes `LiveContextMenuHandler.popup`. |
| `qml/delegates/ParagraphDelegate.qml` | `TextEdit` with `textFormat: TextEdit.MarkdownText`, `readOnly: true`, `selectByMouse: false`. Renders inline formatting (bold/italic/inline-code/link) via Qt's MD parser. Per-delegate `Connections` to `LiveSelectionModel` apply the selection slice with `INT32_MAX → textEdit.length` clamping. |
| `qml/delegates/HeadingDelegate.qml` | Same shape as paragraph delegate, level-driven font sizing per `Theme`. |
| `qml/delegates/HorizontalRuleDelegate.qml` | `Rectangle` with theme-driven divider color, fixed height. |
| `qml/delegates/ImageDelegate.qml` | `Image` with async source loading; `Text` fallback showing alt-text on load failure. |
| `qml/delegates/CodeBlockDelegate.qml` | `TextEdit` with `readOnly: true` + `org.kde.syntaxhighlighting` `SyntaxHighlighter` keyed by `codeLanguage`, mirroring `SourceEditor.qml`'s wiring. Consults `CodeBlockProcessorRegistry` (foundation, future-extensible — see §6) before falling back to KSyntaxHighlighting. |
| `qml/MarkoffEditor.qml` (modify) | Add `mode` property (`"source"` or `"live"`); swap visible child accordingly. Replace the `// PHASE-2 SEAM` comment with the actual seam. |

### App side

| File | Change |
|---|---|
| `app/main.cpp` | Replace `QGuiApplication` with `QApplication`. Add `--live` CLI flag to start in Live mode (default still Source for v0). |
| `app/CMakeLists.txt` | Link `Qt6::Widgets`. |

### Library side

| File | Change |
|---|---|
| `libs/markoff-view-qml/CMakeLists.txt` | Add `Qt6::Widgets` to `target_link_libraries(... PUBLIC ...)`. Add new sources + new QML files via `qt_add_qml_module`. |
| `libs/markoff-view-qml/CLAUDE.md` | Update Phase-2 section to reflect this is now in flight; add Widget-bridge invariant + selection-model invariant. |

### Tests

| File | Coverage |
|---|---|
| `tests/tst_view_qml_ast_block_diff.cpp` | Pure-function tests on `AstBlockDiff` over sequence pairs. ~15 cases. |
| `tests/tst_view_qml_live_block_model.cpp` | Model emits the right Qt model signals for each diff op kind; rows hold expected role data. ~10 cases. |
| `tests/tst_view_qml_live_selection_model.cpp` | `LiveSelectionModel` correctness: `begin`+`extend` patterns, `rangeForBlock` for every selection shape, `collectSelectedText` against fixtures, `clear` resets state. ~12 cases. |
| `tests/tst_view_qml_live_list_model_binding.cpp` | Integration: real `EditorBackend` + parser + ParsePool. Reset content; await parse; assert model rows. Apply local edit; await next parse; assert minimal diff (e.g. one `dataChanged`, not full rebuild). ~6 cases. |
| `tests/tst_view_qml_live_view_smoke.cpp` | QML integration: load `MarkoffEditor.qml` in `mode: "live"` with a fixture exercising all five v0 block kinds; assert ListView count + delegate kinds; verify context-menu Copy is wired. |

**Total new files: 5 C++ headers + 5 .cpp + 6 .qml + 5 test files = 21 new source files. Plus 4 modified.**

---

## 3. Data flow

### Initial load (cold start)

```
file path (CLI arg / file picker)
  → main.cpp reads bytes
  → EditorBackend.document.resetContent(bytes, FirstOpen)
  → MarkoffDocument applies content; emits contentsChanged
  → ParsePool schedules a parse on its worker thread
  → tree-sitter parses; parseReady
  → MarkoffDocument emits parseUpdated(parsed, version)
  → EditorBackend re-emits as parseUpdatedAt(parsed, atVersion)
  → LiveListModelBinding.onParseUpdated:
      • walks AST top-level blocks → BlockKey list (kind + content-hash)
      • runs AstBlockDiff(prev list, new list) → edit script
      • applies the script as beginInsertRows/Remove/Move + dataChanged
      • if any removed/replaced block was touched by current selection,
        calls LiveSelectionModel.clear()
  → ListView consumes model signals; persists delegates whose key is unchanged,
    creates new ones for inserts, destroys for removes
  → each delegate renders its content per role data
```

### User mouse selection

```
MouseArea.onPressed (mouseX, mouseY)
  → hit(mouseX, mouseY) → {block, offset}     (the spike's pipeline)
  → selModel.begin(block, offset)
  → selectionChanged emitted
  → every delegate's Connections.onSelectionChanged
      • range = selModel.rangeForBlock(this.index)
      • textEdit.select(range.x, min(range.y, textEdit.length)) or .deselect()

MouseArea.onPositionChanged (drag, while pressed)
  → hit(...) → selModel.extend(...)
  → same delegate fan-out
```

### Copy

```
Ctrl+C Shortcut OR right-click → LiveContextMenuHandler.popup → "Copy" QAction
  → selModel.copySelectionToClipboard(blockTexts)
  → QGuiApplication::clipboard()->setText(assembledText)
```

### Future editing path (designed-in, NOT in v0)

```
TextEdit.readOnly = false (per-delegate flag, gated by host)
  → user types in delegate N
  → TextEdit.contentsChange (charsRemoved, charsAdded, qtPos)
  → per-delegate cycle-guarded binding (mirrors SourceTextDocumentBinding pattern)
  → translate qtPos → byte offset in MarkoffDocument
  → MarkoffDocument.applyLocalEdit
  → emits contentsChanged
  → ParsePool reparses; parseUpdatedAt
  → AstBlockDiff → most blocks unchanged → delegate N persists, cursor stays put
  → structural changes (e.g. typing "# " to promote paragraph→heading) emit
    a row dataChanged or row replace; ListView swaps delegate kind via
    DelegateChooser, focus passes to the new delegate per the editing-spec
```

---

## 4. Invariants

These must hold for every commit touching `LiveView`:

1. **Single source of truth: `MarkoffDocument` only.** No per-delegate independent state for content. Each delegate's TextEdit holds its block's text but is a *follower* of the model; never a writer (until editing-spec adds the cycle-guarded write path).

2. **Selection is canonical in `LiveSelectionModel`.** Each TextEdit's `selectionStart`/`selectionEnd` is a derived, applied-imperatively shadow of the model's `rangeForBlock`. `TextEdit.selectByMouse` is `false` everywhere; the top-level `MouseArea` is the single mouse-input owner.

3. **AST-driven model identity is preserved across parses.** `AstBlockDiff` produces the smallest insert/remove/move/dataChanged sequence such that ListView retains delegates whose `(kind, content-hash)` is unchanged. No model-rebuild-from-scratch.

4. **No `setFocusProxy`, no `QApplication::sendEvent`, no inline `QTextObject` text-objects.** The legacy live-render mess is structurally absent. QML's native focus chain handles per-delegate input; block-level renderings (math, tables, mermaid in future phases) are *separate delegates*, not inline objects in a QTextDocument.

5. **Native windows for native concerns.** Context menus, popovers, autocomplete, dialogs go through the KDAB Widget bridge. QML `Menu`/`Popup` is the exception, used only when a true scene-graph item is required.

6. **Parse-pool coalescing applies.** `EditorBackend` already drives the coalesced `ParsePool` from Phase-1's perf work; `LiveListModelBinding` doesn't need its own throttle. Bursty edits collapse to at most one parse-in-flight + one pending; the model sees only the latest result.

7. **`MarkoffDocument` black-boxes the CRDT.** `LiveView`, `LiveListModelBinding`, `LiveBlockModel`, `LiveSelectionModel` consume **only** `Markoff::Document *` (the parsed AST tree from `markoff-parser`) and basic value types. **No file in this Live render uses `CollabText::Crdt::*` types.** Selection is held as `(blockIndex, offsetInBlockText)` tuples, *not* as CRDT anchors. The future "lighter non-CRDT codepath" can swap CRDT for plain rope behind `MarkoffDocument` at zero cost to the view layer.

8. **`rangeForBlock`'s `INT32_MAX` sentinel is the consumer's responsibility to clamp.** Per the spike findings: `TextEdit.select(start, end)` silently no-ops if `end` exceeds `text.length`. Every per-delegate `Connections` handler clamps to `textEdit.length` before calling `select`. The C++ side **never** assumes its value will be passed through unchanged.

### Error / edge cases

| Scenario | Strategy |
|---|---|
| Image fails to load (404, file missing) | `ImageDelegate` shows alt-text in a styled fallback `Text` block. No crash. |
| Code-block language unknown to KSyntaxHighlighting | `CodeBlockDelegate` falls through with no syntax coloring (KSyntaxHighlighter's own behavior). |
| AST parse fails (tree-sitter error / pathological input) | `MarkoffDocument` keeps last-good `parsedDocument()`. `LiveListModelBinding` simply doesn't see a `parseUpdatedAt` for the failure. Model stays in last-good state. |
| `LiveSelectionModel` anchor or active block becomes out of range | `LiveListModelBinding` calls `selModel.clear()` after any diff that removes a touched block. |
| `LiveSelectionModel.rangeForBlock` returns `INT32_MAX` as `end` | Consumer clamps to `textEdit.length` (per invariant 8). |
| ListView delegate destroyed mid-selection (scrolled out of cache) | Re-instantiated delegate's `Component.onCompleted` queries `rangeForBlock` and applies once on creation, in addition to the `Connections.onSelectionChanged`. |
| Mode toggle (Source ↔ Live) mid-edit | Outer `MarkoffEditor.qml` swaps visible child. Foundation's `Session` retains primary selection across the swap. Live's `LiveSelectionModel` is independent of Source's per-block QTextDocument selection in v0; cross-mode selection sync is editing-spec scope. |

---

## 5. Testing strategy

**Unit tests (pure C++, no QML engine):**

| Test target | Coverage |
|---|---|
| `tst_view_qml_ast_block_diff` | `AstBlockDiff` is a pure function over two `QList<BlockKey>` sequences. Test cases: empty→full insert; full→empty delete; single-row insert at start/middle/end; single-row delete at start/middle/end; row replace (kind change); two adjacent rows swapped; multi-row insert; multi-row delete; identity (no change); typical real-world AST update. ~15 cases. |
| `tst_view_qml_live_block_model` | `LiveBlockModel` correctly applies the diff edit script as Qt model signals. For each diff op kind: emits the right `beginInsertRows`/`endInsertRows` etc. and rows after match the new block-key list. Verify roles (`kind`, `text`, `headingLevel`, etc.) hold the right values. ~10 cases. |
| `tst_view_qml_live_selection_model` | The spike's logic, exhaustively tested. `begin` then `extend` produces the right `rangeForBlock` for: single-block selection forward/backward, three-block selection forward/backward, anchor==active (zero-length), anchor and active in the same block at the same offset. Verify `INT32_MAX` sentinel behavior. Verify `collectSelectedText` against fixtures and patterns. Verify `clear()` resets all four fields and emits exactly one `selectionChanged`. ~12 cases. |

**Integration tests (with `EditorBackend` + parser + ParsePool):**

| Test target | Coverage |
|---|---|
| `tst_view_qml_live_list_model_binding` | Wire `LiveListModelBinding` to a real `EditorBackend` with a fresh `MarkoffDocument`. Reset content with a known markdown string; wait for parse; assert model has expected rows + kinds. Apply a small edit; wait for next parse; verify the model emitted the right diff op (e.g. one `dataChanged`, not wholesale rebuild). Cycle-guard check. ~6 cases. |

**QML smoke test (with QML engine + offscreen rendering):**

| Test target | Coverage |
|---|---|
| `tst_view_qml_live_view_smoke` | Load `MarkoffEditor.qml` in `mode: "live"` with a fixture exercising all five v0 block kinds. Verify ListView's `count` matches expected; verify delegate at each index has the right `kind` role; verify a follow-up `applyLocalEdit` produces an updated render. Verify the right-click context menu's `Copy` action is connected (synthetic invocation). |

**Not unit-tested in v0:**

- Cross-block selection via mouse drag — manually verified via the spike; production reuses the spike's `hit()` function as-is. The `LiveSelectionModel` *logic* is exhaustively unit-tested; the QML glue is verified by the smoke test loading without errors.
- Visual rendering quality (font sizes, spacing, theme integration). Out of scope for automated tests; verified by manual inspection during dogfood.
- Performance under a long doc. Phase-1's perf phase eliminated the post-typing-stop drain. Live's per-keystroke main-thread cost will need its own measurement once editing turns on (editing-spec scope).

The spike code at `.spike/cross-block-selection/` stays runnable as a behavioral reference. When the production `LiveSelectionModel` is implemented, devs run the spike to confirm expected behavior, and run the unit tests to verify the production code has identical logic.

---

## 6. Plugin extension points (designed-in for future)

We're not building a plugin system in v0, but the architecture is set up so we **don't preclude one**. Specifically we accommodate Obsidian-equivalent hook points so users porting plugins from Obsidian face a conceptual map (not a runtime map: Obsidian's TypeScript-against-DOM doesn't run on QML).

| Extension point | Mechanism | v0 commitment |
|---|---|---|
| **Code-block processors** (Obsidian's `MarkdownCodeBlockProcessor`) | Plugin registers a custom `CodeBlockDelegate.qml` keyed on language, via `CodeBlockProcessorRegistry` (foundation; already exists). Default delegate consults the registry; falls back to KSyntaxHighlighting if no processor matches. | **`CodeBlockDelegate` consults the registry in v0.** Locks in the pluggable contract even with no plugins yet. |
| **Block-kind transformers** (Obsidian's `MarkdownPostProcessor`) | A list of `IBlockTransformer*` that `LiveListModelBinding` runs over the AST → BlockKey list before the diff. Plugin-installed; transforms structured AST, not strings. | Hook point exists in the binding's contract; no transformers shipped in v0. |
| **Custom block kinds** (Obsidian's embedded notes / dataview / kanban) | New `kind` string in the model + plugin-provided delegate registered with `DelegateChooser` at runtime via `Qt.createComponent`. | `BlockKind` is **string-keyed** so adding kinds doesn't recompile the library. No custom-kind loader in v0. |
| **Themes** (Obsidian's CSS) | Plugin/user theme file driving the `Theme` value type already in foundation. Functionally equivalent to CSS: per-element font, color, spacing, border. Not literal CSS. | Delegates **route fonts/colors through `Theme`** in v0 — no hardcoded styling. Theme-loader plugins are then cosmetic. |

What we deliberately *won't* attempt:

- Loading Obsidian's `manifest.json` / `main.js` directly. Different runtime (DOM+Node vs QML scene-graph+V4).
- Running JavaScript plugins that depend on DOM APIs.
- Mermaid/Math/etc. via Obsidian's plugin ecosystem. We'll implement these as native QML delegates (JKQTMathText for math, pre-rendered SVG or WebEngineView for mermaid). The *spirit* of "code block X gets special rendering" matches; the *implementation* is native.

The community-port story: someone porting a plugin from Obsidian to Markoff rewrites the renderer (DOM → QML) but keeps the same conceptual hooks (register a code-block processor, register a block kind, post-process the AST, ship a theme).

---

## 7. Future evolution — Approach 3 (the hybrid AST + raw-source path)

The walking skeleton uses **Approach 2** (per the brainstorming options): stable-identity ListModel with delegates rendering content from AST-derived data. **Approach 3 — the hybrid where text-bearing delegates carry raw markdown for their block (a byte range into `MarkoffDocument`) and render via Qt's MarkdownText / a custom highlighter** — is **deferred but documented as the destination**.

Why Approach 3 eventually:

- Closer to Obsidian's behavior: the cursor-aware "delimiter visibility" (`**bold**` showing as **bold** with markers hidden, but the markers reappearing when the cursor enters the span) requires the delegate to own the raw source for its block, not a derived rendering.
- Editing without parse-round-trip: typing in a paragraph delegate can mutate the block's source string directly without waiting for a reparse; the parse comes along on debounce; structural promotion (e.g. paragraph → heading on `# `) re-fires the AST diff.
- Fewer indirections in the hot path during typing.

Why Approach 2 first:

- Simpler architecture; one less contract (the byte-range mapping from delegate to canonical doc).
- Doesn't force the foundation to expose a "byte range for AST node" API yet.
- Fewer moving parts during the read-only walking skeleton, where the differences don't matter.

The transition from Approach 2 to Approach 3 is **additive**: replace the `text` role's source from "AST inline serialization" to "byte range into `MarkoffDocument`" + the delegate uses the new range. The model identity layer (`AstBlockDiff`) and selection layer (`LiveSelectionModel`) stay unchanged. The cycle-guarded edit binding (a separate spec) will be defined against Approach 3's contract.

---

## 8. Out of scope

Explicitly deferred. None of these are blockers for the walking skeleton, and the walking skeleton's architecture doesn't preclude any of them.

| Deferred work | Future home |
|---|---|
| Editing (write path; per-delegate cycle-guarded binding) | `editing-spec` |
| Math / mermaid / table / list / blockquote / callout / frontmatter / link / wikilink delegates | per-delegate specs as we add them |
| Plugin loader (runtime QML plugin registration) | `plugin-system-spec` |
| Theme loader (user theme files) | follow-up to plugin spec |
| Cross-mode selection sync (selection in Source mode tracks to Live mode and vice versa via Session anchors) | editing-spec |
| Auto-scroll while dragging selection past viewport edge | editing-spec polish |
| Touch / mobile gestures (gesture arbitration with Flickable) | mobile-spec |
| Selection handles (mobile-style drag handles) | mobile-spec |
| Right-click context menu beyond Copy + Select All (paste, format, headings, table ops, etc.) | editing-spec |

---

## 9. References

- **Spike findings** (this design's load-bearing precedent): `docs/specs/2026-04-29-cross-block-selection-spike-findings.md`
- **Spike code** (runnable reference): `.spike/cross-block-selection/`
- **Phase-2 architectural seam**: `libs/markoff-view-qml/qml/MarkoffEditor.qml` (the `// PHASE-2 SEAM` comment)
- **Source-mode counterpart**: `libs/markoff-view-qml/qml/SourceEditor.qml` (mirror its pattern for the Live delegates' style/highlighter wiring)
- **Foundation design**: `docs/specs/2026-04-28-foundation-design.md` (the `MarkoffDocument` API surface and its CRDT-internal commitment)
- **KDAB Widget-window bridge**: https://www.kdab.com/display-widget-windows-in-qt-quick-applications/
- **Memory: future non-CRDT codepath plan**: agent project memory `project_lightweight_non_crdt_codepath` (consistency with invariant 7).

---

## 10. The bet, restated

The walking skeleton commits to: **delegate-per-AST-block in QML + custom `LiveSelectionModel` + KDAB Widget bridge for native menus/dialogs** is fundamentally different from the Qt-Widgets-era `markoff-live`'s multi-`MarkdownTextItem` + forked `TextControl` + `setFocusProxy`+`sendEvent` architecture. The cross-block-selection spike (see findings doc) confirmed the architectural difference. The walking skeleton implements the smallest read-only render that exercises every load-bearing contract end-to-end.
