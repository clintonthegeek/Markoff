# markoff-view-qml — POC QML view

A QML-first Markdown editor library. Phase-1 delivers a solid source-mode editor: `TextArea` + `KSyntaxHighlighting`, CRDT-backed undo/redo, find bar, emoji completion, and an AST inspector test app. The architecture is the seed for an Obsidian-equivalent live editor (Phase 2) — every load-bearing decision is made with Phase 2 in mind so Phase 2 is a build-on, not a rewrite.

This library lives on branch `exploration/new-foundation` and is not yet part of the v0.x release series. It depends on `markoff-foundation` (the CRDT + document layer) rather than `markoff-core`.

## Phase-1 scope (what ships now)

- Source-mode Markdown editing via QML `TextArea`.
- Markdown syntax highlighting via `KSyntaxHighlighting` (`KSyntaxHighlighter` attached to `TextArea.textDocument`).
- CRDT-backed undo/redo (no QTextDocument-side undo stack — disabled by `SourceTextDocumentBinding`).
- Cursor + selection lifted to `Session::primarySelection()` always. `EditorBackend.cursorAnchor` / `selectionAnchor` / `selectionActive` round-trip through the Session; they are never stored only as TextArea ints.
- `parseUpdatedAt(parsed, atVersion)` signal relay for AST consumers. `atVersion` is the `MarkoffDocument::version()` at parse time so stale parses can be discarded.
- `copySelectionAsMarkdown()` Q_INVOKABLE (raw-markdown contract, not HTML).
- In-place find bar via `SearchBackend` wrapping `SearchEngine`.
- Emoji completion popup via `CompletionPopupModel` + `EmojiCompletionProvider`.
- Test app at `app/main.cpp` with an AST inspector pane (parse count + parse version).

## Phase-2 plan (not in this code — architecture is positioned for it)

- `LiveEditor.qml` will be a sibling of `SourceEditor.qml` inside `MarkoffEditor.qml`. The explicit growth seam is the `// PHASE-2 SEAM` comment in `qml/MarkoffEditor.qml`.
- `LiveListModelBinding` will be a sibling of `SourceTextDocumentBinding`, owning a `ListView` + `DelegateChooser` over an AST model fed by `parseUpdatedAt`.
- All 16 content types from the legacy `markoff-live` editor must eventually render: headings, bold/italic/strikethrough, inline code, fenced code blocks, inline math, display math, pipe tables (editable), images, wikilinks, external links, callouts, task checkboxes, horizontal rules, YAML frontmatter, blockquotes, tags. Plus 2 missing in legacy: embedded notes (`![[note]]`), mermaid blocks.
- `EditorBackend`, `Session`, and the CRDT undo stack all stay the same in Phase 2 — only the binding + view layer changes.

## Architectural invariants (do not violate)

These are load-bearing. Breaking them re-introduces the legacy `markoff-live` failure modes documented in the audit (§2.5 of the POC plan).

1. **Single source of truth: `MarkoffDocument` only.** No per-block QTextDocuments. No forked Qt private TextControl.

2. **Document-level CRDT undo only.** `MarkoffDocument::undo()` / `redo()`. The QTextDocument's own undo stack is disabled (`setUndoRedoEnabled(false)`) inside `SourceTextDocumentBinding`. Never re-enable it.

3. **Selection always lifted to Session anchors.** Even in source mode. `EditorBackend.cursorAnchor` / `selectionAnchor` / `selectionActive` round-trip through `Session::setPrimarySelection()`. Never store selection only in TextArea ints.

4. **Focus protocol: Session is canonical, QML `activeFocusItem` is follower.** When Phase-2 lands and ListView delegates can each be focusable, the focused delegate writes its focus to Session; Session signals back which delegate should hold focus. Never introduce `setFocusProxy` chains or manual `QApplication::sendEvent` re-dispatch — those produced the legacy SEGV class.

5. **No inline `Q_OBJECT` text objects on `QTextDocument`.** Math, callouts, embeds, table widgets — all are block-level delegates in Phase 2 (NeoChat pattern), not character-position objects on a text document. The legacy `MathTextObject` / `CheckboxTextObject` reentrancy-guard hell stays dead.

6. **Bidirectional cycle guards on every cross-domain seam.** `m_applyingLocalEdit` (forward) + `m_applyingRemoteEdit` (reverse) on the QTextDocument bridge. `m_applyingSessionSelection` on the EditorBackend selection. `m_applyingBackendCursor` on the int↔anchor bridge. Setters check the flag before acting; receivers set/clear it around their own emits.

7. **`parseUpdatedAt` is version-tagged.** Phase-2 consumers must compare the parse's `atVersion` against `MarkoffDocument::version()` before rendering. Stale parses must be discarded. `EditorBackend::parseUpdatedAt(parsed, atVersion)` carries the version; T0 in the POC plan added this tag to `MarkoffDocument::parseUpdated`.

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
./build-dev/bin/markoff-view-qml-app /path/to/file.md
```

The AST inspector pane in the app shows parse count and parse version — useful for verifying the `parseUpdatedAt` relay.

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
- Signal: `parseUpdatedAt(QVariant parsed, quint64 atVersion)`.

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
- `qml/MarkoffEditor.qml` — outer shell. Exposes `editorBackend` via alias so sibling components (search bar, completion popup, AST inspector) can bind to it. The `// PHASE-2 SEAM` comment marks where `LiveEditor.qml` plugs in.

## Conventions

- C++20, Qt 6.8+, KF6.
- `// SPDX-License-Identifier: GPL-3.0-or-later` on every file.
- C++ namespace: `Markoff::View::Qml` for types in this library.
- Test prefix `tst_view_qml_*`.
- `Q_DECLARE_METATYPE(CollabText::Crdt::Anchor)` is at file scope in `EditorBackend.h` — first place the metatype was needed; do not duplicate it.
- Commit subjects are bare (no `Co-Authored-By` trailer) — matches the style of the foundation work on this branch.

## Known limitations / deferred

- No `EditorHighlighter` (spec §9.5's second `QSyntaxHighlighter` for code-block content overlay). Attaching two highlighters to the same `QTextDocument` is risky; deferred indefinitely.
- No live-preview formatting — Phase 2.
- No file save / save-as / file picker — POC takes one file from `argv`.
- No multi-cursor / secondary selection UI — foundation supports it, POC doesn't expose it.
- No replace UI — `ReplaceController` exists in the foundation but no QML wrapper.
- No link activation (Ctrl-click) — `LinkService` is wired in the services bundle but no QML hook yet.
- No async completion — emoji is synchronous; cross-thread metatype registration deferred.
- No CommandFacade-driven toolbar (bold/italic/heading buttons) — `CommandFacade` exists; QML toolbar would be a follow-up commit.
- AST inspector pane shows parse-count and parse-version only; full AST tree rendering is Phase 2.
- `Theme::operator==` not implemented; theme setters emit `themeChanged` unconditionally (verified harmless in tests).

## Plan reference

The plan that produced this POC: `~/.claude/plans/nah-a-is-fine-fuzzy-backus.md`. The branch covers T0–T23; review with:

```bash
git log master..HEAD --oneline
```

Design reframing brief (2026-04-28): *"the legacy of failure pre-QML is our guide"* — the new architecture must position for everything `markoff-live` tried and failed to deliver, while shipping Phase-1 as a solid, un-exciting source-mode editor.
