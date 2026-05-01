# markoff-view-qml — POC QML view

A QML-first Markdown editor library. Phase-1 delivers a solid source-mode editor: `TextArea` + `KSyntaxHighlighting`, CRDT-backed undo/redo, find bar, emoji completion, and an AST inspector test app. The architecture is the seed for an Obsidian-equivalent live editor (Phase 2) — every load-bearing decision is made with Phase 2 in mind so Phase 2 is a build-on, not a rewrite.

This library lives on branch `exploration/new-foundation` and is not yet part of the v0.x release series. It depends on `markoff-foundation` (the CRDT + document layer) rather than `markoff-core`.

## Phase-1 scope (what ships now)

- Source-mode Markdown editing via QML `TextArea`.
- Markdown syntax highlighting via `KSyntaxHighlighting` (`KSyntaxHighlighter` attached to `TextArea.textDocument`).
- CRDT-backed undo/redo (no QTextDocument-side undo stack — disabled by `SourceTextDocumentBinding`).
- Cursor + selection lifted to `Session::primarySelection()` always. `EditorBackend.cursorAnchor` / `selectionAnchor` / `selectionActive` round-trip through the Session; they are never stored only as TextArea ints.
- `parseUpdatedAt(parsed, parseSequence, blockAnchors)` signal relay for AST consumers. `parseSequence` is a `quint64` local-monotonic counter from `MarkoffDocument::parseSequence()` so stale parses can be discarded; `blockAnchors` is a `QList<Markoff::BlockAnchor>` aligned to the parse's top-level blocks.
- `copySelectionAsMarkdown()` Q_INVOKABLE (raw-markdown contract, not HTML).
- In-place find bar via `SearchBackend` wrapping `SearchEngine`.
- Emoji completion popup via `CompletionPopupModel` + `EmojiCompletionProvider`.
- Test app at `app/main.cpp` with an AST inspector pane (parse count + parse version).

## Phase-2 v0 walking skeleton (in code)

The Phase-2 read-only walking skeleton landed on `exploration/new-foundation` (plan: `docs/plans/2026-04-29-live-render-walking-skeleton.md`, design: `docs/specs/2026-04-29-live-render-design.md`). `LiveView.qml` is now a sibling of `SourceEditor.qml` inside `MarkoffEditor.qml` and slots into the `// PHASE-2 SEAM`. `EditorBackend`, `Session`, the CRDT undo stack, and `ParsePool` are unchanged — only the new binding + view layer was added.

**v0 block kinds rendered (read-only):**

- `paragraph` (TextEdit with `textFormat: TextEdit.MarkdownText`)
- `heading` (level-driven font sizing via `Theme`)
- `hr` (themed divider)
- `image` (async `Image` with alt-text fallback)
- `code-block` (TextEdit + `org.kde.syntaxhighlighting` keyed by `codeLanguage`; consults `CodeBlockProcessorRegistry` first)

**C++ exports (Tasks 2-8 of the plan):**

- `BlockKind` — string-keyed kind constants (open set; no closed enum so plugin-registered kinds don't recompile the library).
- `BlockRecord` / `BlockKey` — value types. `BlockKey = (kind, content-hash)` is the diff identity unit.
- `BlockWalker` — top-level AST → `BlockRecord` walker.
- `AstBlockDiff` — pure C++ Myers/LCS diff over `QList<BlockKey>`. Output: edit script (`Equal | Insert | Delete | Move`).
- `LiveSelectionModel` — `(anchorBlock, anchorOffset, activeBlock, activeOffset)` state. `Q_INVOKABLE` `begin/extend/clear/rangeForBlock/collectSelectedText/copySelectionToClipboard`. Structurally identical to the spike at `.spike/cross-block-selection/SelectionModel.{h,cpp}`.
- `LiveBlockModel` — `QAbstractListModel`. Roles: `kind`, `text`, plus kind-specific (`headingLevel`, `imageSrc`/`imageAlt`/`imageTitle`, `codeLanguage`, `codeText`).
- `LiveListModelBinding` — `QObject` exposed to QML. Subscribes to `EditorBackend::parseUpdatedAt`, owns the `LiveBlockModel`, drives `AstBlockDiff` to emit minimal Qt model signals, clears `LiveSelectionModel` if a touched block is removed.
- `LiveContextMenuHandler` — KDAB Widget bridge. Owns `std::unique_ptr<QMenu>`. v0 actions: Copy, Select All. The library + test app now link `Qt6::Widgets`; `main.cpp` uses `QApplication`.

**QML files added:**

- `qml/LiveView.qml` — top-level. `ListView` + `DelegateChooser` (from `Qt.labs.qmlmodels`) + a top-level `MouseArea` driving `LiveSelectionModel`. The `hit(mouseX, mouseY)` JS function from the spike findings is the load-bearing piece.
- `qml/delegates/ParagraphDelegate.qml`
- `qml/delegates/HeadingDelegate.qml`
- `qml/delegates/HorizontalRuleDelegate.qml`
- `qml/delegates/ImageDelegate.qml`
- `qml/delegates/CodeBlockDelegate.qml`

`MarkoffEditor.qml` now exposes a `mode` property (`"source"` or `"live"`) and swaps the visible child accordingly. The test app's `--live` flag sets `mode: "live"` at startup; default is still `"source"` for v0.

The legacy `markoff-live` editor's 16 content types are still the eventual target (math, mermaid, tables, lists, blockquotes, callouts, links/wikilinks, embedded notes, frontmatter, tags, etc.). v0 ships the five above; everything else is deferred per the design's `## 8. Out of scope`.

**Cross-references:**

- Design doc: `docs/specs/2026-04-29-live-render-design.md` (load-bearing — read this before touching anything Live-side).
- Spike findings: `docs/specs/2026-04-29-cross-block-selection-spike-findings.md` (the `hit()` function's edge cases + the `INT32_MAX`-clamp lesson).
- Spike code (runnable reference): `.spike/cross-block-selection/`.
- Implementation plan: `docs/plans/2026-04-29-live-render-walking-skeleton.md`.

### Architectural invariants (editing — must hold for every commit touching `LiveView`)

These ten invariants are from `docs/specs/2026-04-30-live-editing-design.md` §4. They supersede the read-only walking-skeleton invariants from the v0 design doc.

1. **Source-faithful text representation.** Each text-bearing delegate's QTextDocument text equals the canonical UTF-8 source bytes for its block range (modulo UTF-16 conversion). qtPos in the delegate translates to canonical byte offset by simple arithmetic, never by reverse-mapping rendered text to source.

2. **Single source of truth: `MarkoffDocument` only.** No per-delegate independent state for content. The cycle-guarded `LiveEditBinding` writes go through `applyLocalEdit`; reads go through the model's `text` role. Speculative kind changes are model-layer state reconciled with parser truth on every parse return; speculation is never authoritative.

3. **`Session::primarySelection` is the single canonical selection across modes.** View projections to per-delegate ranges are derived, never authoritative. (Walking-skeleton `LiveSelectionModel` is superseded by `LiveSelectionView`, which projects Session state into block/offset pairs for delegate use.)

4. **No `TextEdit.MarkdownText`.** Every text-bearing delegate uses `TextEdit.PlainText`. Inline rendering goes through `InlineFormatHighlighter` driven by markoff-parser.

5. **BlockAnchor byte ranges are stable up to the next parse.** Between parses, a delegate's block start is adjusted by the local edit's delta so qtPos translation continues to work during the parse round-trip. The view consumes `BlockAnchor` as opaque.

6. **CRDT types are opaque to the view layer.** `markoff-view-qml` headers must not `#include <crdt/...>`. `Markoff::TextAnchor` is the only CRDT-typed value visible at the view layer, and it is never inspected, constructed, or compared — only passed back to `MarkoffDocument` translation APIs.

7. **No focus-proxy, no `QApplication::sendEvent`, no QML focus chain workarounds.** Structural keys are intercepted by `LiveStructuralKeyHandler` via QML `Keys.onPressed`. No re-dispatch hacks.

8. **Cycle-guard is per-delegate.** Each `LiveEditBinding` owns its own reentrancy guard (`m_applyingModelUpdate`). Writes from `MarkoffDocument` to a delegate's TextEdit are flagged and ignored by the binding's `contentsChange` slot. Mirrors `SourceTextDocumentBinding`'s pattern.

9. **IME composition is held off.** No `applyLocalEdit` fires while a delegate's TextEdit is in active composition. Parse-driven text updates for that row are deferred until composition ends.

10. **Undo entries are coalesced per a deterministic policy.** View-side policy: consecutive printables in same focus context coalesce; broken by non-printables, movement, mode switch, paste, structural change, or idle threshold (1 second). No user-configurable settings in v0.

## Architectural invariants (cross-cutting — do not violate)

These are load-bearing. Breaking them re-introduces the legacy `markoff-live` failure modes documented in the audit (§2.5 of the POC plan).

1. **Single source of truth: `MarkoffDocument` only.** No per-block QTextDocuments. No forked Qt private TextControl. Delegates are followers of the model; never writers.

2. **Document-level CRDT undo only.** `MarkoffDocument::undo()` / `redo()`. The QTextDocument's own undo stack is disabled (`setUndoRedoEnabled(false)`) inside `SourceTextDocumentBinding`. Never re-enable it.

3. **Selection always lifted to Session anchors.** Even in source mode. `EditorBackend.cursorAnchor` / `selectionAnchor` / `selectionActive` round-trip through `Session::setPrimarySelection()`. Never store selection only in TextArea ints.

4. **Focus protocol: Session is canonical, QML `activeFocusItem` is follower.** When Phase-2 lands and ListView delegates can each be focusable, the focused delegate writes its focus to Session; Session signals back which delegate should hold focus. Never introduce `setFocusProxy` chains or manual `QApplication::sendEvent` re-dispatch — those produced the legacy SEGV class.

5. **No inline `Q_OBJECT` text objects on `QTextDocument`.** Math, callouts, embeds, table widgets — all are block-level delegates in Phase 2 (NeoChat pattern), not character-position objects on a text document. The legacy `MathTextObject` / `CheckboxTextObject` reentrancy-guard hell stays dead.

6. **Bidirectional cycle guards on every cross-domain seam.** `m_applyingLocalEdit` (forward) + `m_applyingRemoteEdit` (reverse) on the QTextDocument bridge. `m_applyingSessionSelection` on the EditorBackend selection. `m_applyingBackendCursor` on the int↔anchor bridge. Setters check the flag before acting; receivers set/clear it around their own emits.

7. **`parseUpdatedAt` is parse-sequence-tagged.** Phase-2 consumers must compare the parse's `parseSequence` against `MarkoffDocument::parseSequence()` before rendering. Stale parses must be discarded. `EditorBackend::parseUpdatedAt(parsed, parseSequence, blockAnchors)` carries the local-monotonic counter and the per-top-level-block `QList<Markoff::BlockAnchor>`; the original tag was added in the POC plan and reshaped by the block-anchor-foundation work to drop `Crdt::Global` from the public surface and emit `BlockAnchor`s directly.

## Projection layer

`LiveProjectionLayer` owns all view-side state that is *not* present in the source rope. The defining sentence (from `docs/specs/2026-05-01-live-projection-layer.md`): **the editing model is a superset of the source model; the projection downward is lossy by design; the layer owns the difference.**

Two item kinds (both planned; only predictions ship at branch tip):

- **Predictions** (latency-bridging) — *shipped*. View runs ahead of parser on bytes that *are* already in source: open `**`/`*`/`` ` ``/`~~`/`==` rendered as bold/italic/etc. before the parser confirms (`InlineFormatHighlighter`); ```` ``` ```` flips a paragraph row's kind to `code_block` before the close fence arrives (`LiveSpeculativeFenceController`). Reconcile against parser truth — confirm or drop.
- **Holes** (intent-holding) — *deferred*. View holds intent that source cannot represent yet (empty paragraph after Enter at EOB; empty list item; empty code-fence interior; etc.). The first hole — empty paragraph — was attempted as v0 and reverted (see spec §3.6 for the five failure modes that surfaced under dogfood). The v1 redesign (IME-preedit pattern) is in spec §3.2-§3.5 and the plan's "Stage 4 (v1)" section. Read the handoff brief `docs/handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md` before re-attempting.

Architectural invariants the layer enforces (numbered 11-16; extend the seven cross-cutting invariants above):

11. **Source rope is canonical.** Projections never write to the rope except through `applyLocalEdit` at commit time. Save flushes any pending preedit buffer first, then serializes the rope.
12. **Projections are not in the CRDT undo stack.** Pre-commit holes are pure view state; their drop is not an undo entry. Commit produces a normal CRDT edit which enters the stack normally.
13. **Anchor opacity holds for projection anchors too.** A projection's `origin` is a `BlockAnchor` value; delegates pass it through opaquely. Pre-commit holes carry no real CRDT position — they hold a `reifyOffset` (a byte index in source where commit will land) instead.
14. **One layer instance per `LiveListModelBinding`.** No globals. The layer's lifetime equals the binding's. The binding owns the layer; controllers (`LiveSpeculativeFenceController`, `InlineFormatHighlighter`) and the model (`LiveBlockModel`) take a non-owning pointer.
15. **Reconciliation runs synchronously on its trigger.** Parse return → reconcile predictions before emitting model signals. No deferred-reconciliation queues.
16. **Collab safety.** When a remote peer's edit invalidates a projection's anchor, drop the projection and route focus to the nearest live neighbor.

Producer / consumer split (Stages 1-3, shipped):

- Producers call `layer->createXxx(...)` / `layer->dropXxx(...)`. They do not subscribe to `parseUpdatedAt`; the layer does.
- `LiveProjectionLayer::onParseUpdated` clears both block-kind and inline prediction registries on each parse return. The model's own `applyOps` snaps speculatively-changed rows back if the parser disagrees.
- `InlineFormatHighlighter` keeps a fallback prediction list internally so the standalone unit test works without a layer; the runtime path always goes through the layer.

Spec: `docs/specs/2026-05-01-live-projection-layer.md`. Plan: `docs/plans/2026-05-01-live-projection-layer.md`. Handoff: `docs/handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md`.

## Layer map

```
QML (SourceEditor.qml, SearchBar.qml, CompletionPopup.qml, MarkoffEditor.qml)
  │
  ├─ EditorBackend          — document + session + undo/redo + parse relay
  │    └─ MarkoffDocument   — CRDT rope + undo/redo (markoff-foundation)
  │    └─ Session           — multi-cursor + selection (markoff-foundation)
  │
  ├─ SourceTextDocumentBinding  — QTextDocument ↔ MarkoffDocument bridge
  │    (forward: QTextDoc contentsChanged → MarkoffDoc::applyEdit)
  │    (reverse: MarkoffDoc::contentChanged → rebuild QTextDoc)
  │
  ├─ SearchBackend          — SearchEngine wrapper, exposed to QML
  └─ CompletionPopupModel   — QAbstractListModel over CompletionRegistry
```

Arrows between layers are one-way by design. QML reads properties and calls invokables; it does not poke document internals directly.

## Building

Configure from the worktree root (or repo root with presets):

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff_view_qml -j
cmake --build build-dev --target markoff-view-qml-app -j
```

Run the test app:

```bash
./build-dev/bin/markoff-view-qml-app /path/to/file.md          # Source mode (default)
./build-dev/bin/markoff-view-qml-app --live /path/to/file.md   # Live mode (Phase-2 v0)
```

The AST inspector pane in the app shows parse count and parse version — useful for verifying the `parseUpdatedAt` relay. The `--live` flag boots `MarkoffEditor` with `mode: "live"`, exercising the new `LiveView.qml` walking skeleton.

## Testing

Foundation tests must stay green:

```bash
ctest --test-dir build-dev -R '^tst_(markoff_edit|anchor_json|selection|fold_ref|foundation_.*)$' --output-on-failure
```

View-qml tests:

```bash
ctest --test-dir build-dev -R '^tst_view_qml_' --output-on-failure
```

5 executables at v0.3-POC:

| Executable | What it covers |
|---|---|
| `tst_view_qml_editor_backend` | document/session wiring, parseUpdatedAt relay, undo/redo, copySelectionAsMarkdown |
| `tst_view_qml_source_binding` | QTextDoc↔MarkoffDoc round-trip, cycle guards, UTF-16/UTF-8 conversion |
| `tst_view_qml_search_backend` | find-all, find-next/prev, match-count |
| `tst_view_qml_completion_popup` | CompletionPopupModel roles, emoji provider |
| `tst_view_qml_integration` | QML engine smoke: MarkoffEditor loads, edits round-trip, search via backend |

All tests use `QTEST_MAIN` (not `QTEST_APPLESS_MAIN`) — the QML engine and `QSignalSpy::wait` need a real event loop.

## QML module + import URI

- Library URI: `org.markoff.view.qml 1.0` — registered via `qt_add_qml_module(STATIC ...)`.
- App URI: `org.markoff.view.qml.app 1.0` (private to `app/`; not part of the public API).
- KSyntaxHighlighting: `import org.kde.syntaxhighlighting` (no version pin needed for KF6).

## QML elements exposed

**`EditorBackend`** — domain layer, the primary handle for host QML.

- Properties: `document` (read/write), `session` (read-only, derived), `theme`, `cursorAnchor`, `selectionAnchor`, `selectionActive`.
- Q_INVOKABLEs: `undo()`, `redo()`, `copySelectionAsMarkdown()`.
- Signal: `parseUpdatedAt(QVariant parsed, quint64 parseSequence, QList<Markoff::BlockAnchor> blockAnchors)`.

**`SourceTextDocumentBinding`** — bridges QTextDocument to MarkoffDocument.

- Properties: `editorBackend`, `qtQuickDocument` (the `TextArea.textDocument` handle), `cursorPosition`, `selectionStart`, `selectionEnd`.
- Static helpers: `qtPosToByteOffset(doc, pos)`, `byteOffsetToQtPos(doc, offset)`.

**`SearchBackend`** — find-bar backend.

- Properties: `editorBackend`, `needle`, `flags`, `matchCount`.
- Q_INVOKABLEs: `findAll()`, `findNext()`, `findPrevious()`, `clear()`.

**`CompletionPopupModel`** — `QAbstractListModel` over `CompletionRegistry`.

- Roles: `display`, `insertion`, `detail`, `iconName`, `priority`.
- Q_INVOKABLEs: `requestCompletions(context)`, `clear()`.

## QML files

- `qml/SourceEditor.qml` — `TextArea` + `KSyntaxHighlighter` attachment + `SourceTextDocumentBinding` bindings.
- `qml/SearchBar.qml` — find UI; calls `SearchBackend` invokables.
- `qml/CompletionPopup.qml` — emoji completion popup. **Important:** the delegate uses `model.display` access (the qualified form), not `required property string display`. Qt6 `ItemDelegate` has a FINAL `display` property that conflicts with a `required` property of the same name — the `required` declaration wins the name but breaks `ItemDelegate`'s own text binding. Found during T22; use `model.display` everywhere.
- `qml/MarkoffEditor.qml` — outer shell. Exposes `editorBackend` via alias so sibling components (search bar, completion popup, AST inspector) can bind to it. Has a `mode` property (`"source"` / `"live"`) that swaps the visible child between `SourceEditor.qml` and `LiveView.qml` (the former `// PHASE-2 SEAM` is now wired).
- `qml/LiveView.qml` — Phase-2 v0 read-only live render. `ListView` + `DelegateChooser` + top-level `MouseArea` driving `LiveSelectionModel`. See "Phase-2 v0 walking skeleton" above.
- `qml/delegates/{Paragraph,Heading,HorizontalRule,Image,CodeBlock}Delegate.qml` — the five v0 block delegates.

## Conventions

- C++20, Qt 6.8+, KF6.
- `// SPDX-License-Identifier: GPL-3.0-or-later` on every file.
- C++ namespace: `Markoff::View::Qml` for types in this library.
- Test prefix `tst_view_qml_*`.
- `Q_DECLARE_METATYPE(Markoff::TextAnchor)` is at file scope in `EditorBackend.h` — first place the metatype was needed; do not duplicate it. (Retyped from `CollabText::Crdt::Anchor` in the block-anchor-foundation work — view-layer surfaces no longer mention `Crdt::*`.) `Q_DECLARE_METATYPE(QList<Markoff::BlockAnchor>)` is also at file scope in `EditorBackend.h` (added so the new `parseUpdatedAt` 3-arg signal can ship `blockAnchors` through `QSignalSpy` / `QMetaObject::invokeMethod`).
- Commit subjects are bare (no `Co-Authored-By` trailer) — matches the style of the foundation work on this branch.

## Known limitations / deferred

- No `EditorHighlighter` (spec §9.5's second `QSyntaxHighlighter` for code-block content overlay). Attaching two highlighters to the same `QTextDocument` is risky; deferred indefinitely.
- Live-preview renders **read-only** for paragraph / heading / hr / image / code-block only at v0. Editing in Live mode + the other ~14 block kinds (math, mermaid, tables, lists, blockquotes, callouts, links/wikilinks, embedded notes, frontmatter, tags, etc.) are deferred to subsequent specs (editing-spec + per-delegate specs).
- No file save / save-as / file picker — POC takes one file from `argv`.
- No multi-cursor / secondary selection UI — foundation supports it, POC doesn't expose it.
- No replace UI — `ReplaceController` exists in the foundation but no QML wrapper.
- No link activation (Ctrl-click) — `LinkService` is wired in the services bundle but no QML hook yet.
- No async completion — emoji is synchronous; cross-thread metatype registration deferred.
- No CommandFacade-driven toolbar (bold/italic/heading buttons) — `CommandFacade` exists; QML toolbar would be a follow-up commit.
- AST inspector pane shows parse-count and parse-version only; full AST tree rendering is Phase 2.
- `Theme::operator==` not implemented; theme setters emit `themeChanged` unconditionally (verified harmless in tests).

## Performance

The POC has a known, **substantial** typing-latency problem on long documents. Reproducer: open `docs/specs/2026-04-28-foundation-design.md` (~16 KB), place cursor near the end, type rapidly. The CPU pegs at 100% on one core and the UI freezes for tens of seconds while the keyboard buffer drains.

### Suspected cost centres (unprofiled — top suspects only)

1. **`InlineFormatHighlighter::rebuildSpans()` constructs a `TreeSitterParser` per source change**. O(delegates) full parses per model update. Each inline-format span rebuild triggers a fresh parser instantiation and full tree walk. Deferred pending profiling.

2. **`SourceTextDocumentBinding::onQtContentsChange` does 3× full-document copies per keystroke**: `doc->toMarkdownUtf8()` → `QString::fromUtf8(preBytes)` → `m_qtDoc->toPlainText()`. For a 16 KB doc that's ~80 KB of allocation per keystroke, before the foundation does any work. The pure-insertion case (`charsRemoved == 0`, the typing case) can be fast-pathed to skip the pre-state fetch — the prefix up to `qtPos` is unchanged in the post-state. Not yet implemented.

3. **Foundation's `ParsePool` parses the whole document on every change**. No incremental tree-sitter parsing yet (`ts_tree_edit()`). Documented as deferred in the foundation design (`docs/specs/2026-04-28-foundation-design.md`).

4. **KSyntaxHighlighter re-highlight scope**. KF6's QML `SyntaxHighlighter` may rehighlight affected blocks only, or may scan further. Unverified; needs measurement.

5. **AST inspector pane updates a Label on every `parseUpdatedAt`**. Individually cheap but contributes during sustained typing.

### Done so far

- Commit `5b116be` made `qtPosToByteOffset` / `byteOffsetToQtPos` allocation-free (in-place UTF-16↔UTF-8 walks). Did NOT eliminate the freeze, so this isn't the dominant cost.

### Investigation owed before further fixes

Before piling on more speculative "fixes", profile under `perf record` while typing into a long document. Compare CPU breakdown with a tiny document (100 lines) to determine whether the cost is per-character constant or scales with document size. Then apply targeted fixes; don't optimize blindly.

A separate plan should be written for the perf phase before implementation begins. See `docs/handoff/2026-04-28-post-poc-perf-SESSION-BRIEF.md`.

## Plan reference

The plan that produced this POC: `~/.claude/plans/nah-a-is-fine-fuzzy-backus.md`. The branch covers T0–T23; review with:

```bash
git log master..HEAD --oneline
```

Design reframing brief (2026-04-28): *"the legacy of failure pre-QML is our guide"* — the new architecture must position for everything `markoff-live` tried and failed to deliver, while shipping Phase-1 as a solid, un-exciting source-mode editor.
