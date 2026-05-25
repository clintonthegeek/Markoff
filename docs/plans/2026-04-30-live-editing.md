# Live editing — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Hard precondition:** This plan does NOT start until the foundation `BlockAnchor` + `TextAnchor` work has landed. See [`docs/plans/2026-04-30-block-anchor-foundation.md`](2026-04-30-block-anchor-foundation.md). The first task assumes the foundation API is callable; do not begin without it.

**Goal:** Turn the read-only `LiveView` walking skeleton into a structurally-complete editor per `docs/specs/2026-04-30-live-editing-design.md`. Type into delegates, cross-block structural edits, clipboard, undo, IME, cross-mode selection, app-layer save with dirty tracking. Retire three pieces of walking-skeleton tech debt along the way: `TextEdit.MarkdownText`, content-hash `BlockKey`, and standalone `LiveSelectionModel`.

**Architecture:** `TextEdit.PlainText` everywhere with markoff-parser-AST-driven `QTextCharFormat` highlighter; per-delegate cycle-guarded write path mirroring `SourceTextDocumentBinding`'s pattern; CRDT-anchor identity via `(kind, BlockAnchor)`; `Session::primarySelection` as cross-mode source of truth; shared `MarkoffDocument` undo stack; two narrow speculative paths (inline open delimiters at the highlighter, code-block fence at the editor); source-faithful gap model.

**Tech Stack:** C++20, Qt 6.8+ (Quick, QuickControls2, Qml, Widgets), KF6::SyntaxHighlighting, CMake 3.19+. Test framework Qt Test for C++ + QML smoke. Build with `-j 8` (no bare `-j`, which freezes the user's machine).

**Branch:** `exploration/new-foundation` (worktree `.worktrees/foundation-exploration/`).

**Related docs:**
- Spec: [`docs/specs/2026-04-30-live-editing-design.md`](../specs/2026-04-30-live-editing-design.md)
- Foundation precondition: [`docs/specs/2026-04-30-block-anchor-foundation-design.md`](../specs/2026-04-30-block-anchor-foundation-design.md)
- Walking skeleton spec (parent): [`docs/specs/2026-04-29-live-render-design.md`](../specs/2026-04-29-live-render-design.md)
- Source widget cycle-guard pattern: `libs/markoff-core/src/SourceTextDocumentBinding.cpp`
- Library guide: [`libs/markoff-view-qml/CLAUDE.md`](../../libs/markoff-view-qml/CLAUDE.md)

**Out of scope:** Format toggles (Ctrl+B/Ctrl+I/Ctrl+K), cursor-aware delimiter hiding, HTML→MD conversion, image paste, drag-and-drop, spell-check, math/mermaid/table/list/blockquote/callout/frontmatter delegates. See spec §8.

---

## File Structure

### Files created

```
libs/markoff-view-qml/
  src/
    LiveEditBinding.h
    LiveEditBinding.cpp
    LiveStructuralKeyHandler.h
    LiveStructuralKeyHandler.cpp
    LiveSelectionView.h            (replaces LiveSelectionModel)
    LiveSelectionView.cpp          (replaces LiveSelectionModel.cpp)
    LiveSpeculativeFenceController.h
    LiveSpeculativeFenceController.cpp
    InlineFormatHighlighter.h
    InlineFormatHighlighter.cpp
    LiveClipboardController.h
    LiveClipboardController.cpp
    UndoCoalescePolicy.h            (small helper struct, can live in LiveEditBinding)
  tests/
    tst_view_qml_live_edit_binding.cpp
    tst_view_qml_live_structural_keys.cpp
    tst_view_qml_live_speculative_fence.cpp
    tst_view_qml_inline_format_highlighter.cpp
    tst_view_qml_live_selection_view.cpp     (replaces tst_view_qml_live_selection_model.cpp)
    tst_view_qml_live_clipboard.cpp
    tst_view_qml_live_undo.cpp
    tst_view_qml_live_ime.cpp
```

### Files modified

```
libs/markoff-view-qml/
  src/
    LiveBlockModel.{h,cpp}                 BlockKey identity change + speculativelyChangeKind
    LiveListModelBinding.{h,cpp}           BlockAnchor-based BlockKey rebuild + version tracking
    AstBlockDiff.{h,cpp}                   BlockKey identity equality
    LiveContextMenuHandler.{h,cpp}         Cut + Paste actions
    EditorBackend.{h,cpp}                  Q_INVOKABLE wrappers for foundation translation APIs
  qml/
    LiveView.qml                           Keys handler; selection writes via LiveSelectionView
    delegates/ParagraphDelegate.qml        readOnly: false; PlainText; InlineFormatHighlighter
    delegates/HeadingDelegate.qml          readOnly: false; PlainText; InlineFormatHighlighter
    delegates/CodeBlockDelegate.qml        readOnly: false; Tab inserts literal; Enter inserts \n
    delegates/HorizontalRuleDelegate.qml   click routes to neighbour text delegate
    delegates/ImageDelegate.qml            click routes to neighbour text delegate
  tests/
    tst_view_qml_ast_block_diff.cpp        BlockAnchor identity tests
    tst_view_qml_live_view_qml.cpp         end-to-end editing smoke
  CMakeLists.txt                           new sources/tests
libs/markoff-view-qml/
  CLAUDE.md                                editing invariants

libs/markoff-view-qml/app/
  main.cpp                                 Ctrl+S, dirty tracking, window title
```

### Files deleted

```
libs/markoff-view-qml/src/LiveSelectionModel.{h,cpp}
libs/markoff-view-qml/tests/tst_view_qml_live_selection_model.cpp
```

(Replaced by `LiveSelectionView` and its test, respectively.)

---

## Task 1: Confirm foundation precondition is in tree

Before any editing work begins, verify the BlockAnchor + TextAnchor foundation API is present and tests pass.

- [ ] **Step 1.1: Confirm foundation symbols exist**

```bash
grep -E "BlockAnchor|TextAnchor|parseSequence|editSequence" libs/markoff-core/include/markoff-foundation/MarkoffDocument.h
grep -E "blockAt|offsetInBlock|textAnchorAt.*BlockAnchor" libs/markoff-core/include/markoff-foundation/MarkoffDocument.h
```

Expected: at least one match for each. If missing, STOP — the foundation work is not yet landed.

- [ ] **Step 1.2: Run foundation tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_foundation_block_anchor' --output-on-failure -j 8
```

Expected: BlockAnchor foundation tests pass.

- [ ] **Step 1.3: Confirm walking-skeleton tests still green**

```bash
ctest --test-dir build-dev -R 'tst_view_qml_' --output-on-failure -j 8
```

Expected: all walking-skeleton view-qml tests pass against the new foundation. If any fail, root-cause before proceeding (foundation API may have changed signatures the walking skeleton consumes).

---

## Task 2: BlockKey identity migration — `(kind, BlockAnchor)`

The first walking-skeleton retirement. Touch `BlockKey`, `AstBlockDiff`, `LiveBlockModel`, `LiveListModelBinding` together so the build stays green.

**Files:**
- Modify: `libs/markoff-view-qml/src/AstBlockDiff.{h,cpp}`
- Modify: `libs/markoff-view-qml/src/LiveBlockModel.{h,cpp}`
- Modify: `libs/markoff-view-qml/src/LiveListModelBinding.{h,cpp}`
- Modify: `libs/markoff-view-qml/tests/tst_view_qml_ast_block_diff.cpp`

- [ ] **Step 2.1: Update `BlockKey` definition**

Where `BlockKey` is defined (currently `AstBlockDiff.h` per spec; verify with `grep -rn "struct BlockKey" libs/markoff-view-qml/`), change the second field from a content-hash to a `Markoff::BlockAnchor`. Equality is anchor-based plus kind-string-based.

- [ ] **Step 2.2: Update `LiveListModelBinding::onParseUpdated`**

The new `parseUpdated` signal ships a parallel `QList<BlockAnchor>` alongside the parsed `Document`, plus a `quint64 parseSequence` replacing the prior `Crdt::Global atVersion` (per the BlockAnchor foundation spec §2). Update the slot signature accordingly. For each top-level block in the Document, read its BlockAnchor from the parallel list at the same index. Replace the content-hash computation with this anchor read. Record the `parseSequence` for any subsequent ordering checks the binding needs.

- [ ] **Step 2.3: Update `AstBlockDiff` equality**

The diff algorithm shape (Myers/LCS) is unchanged. Only the BlockKey equality predicate changes. Move detection becomes more accurate because anchors survive position shifts.

- [ ] **Step 2.4: Update `tst_view_qml_ast_block_diff` cases**

Existing test fixtures pass two BlockKey lists; update them to use `BlockAnchor` values. Add new cases that specifically exercise identity preservation under content edits and splits/merges.

- [ ] **Step 2.5: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_(ast_block_diff|live_block_model|live_list_model_binding)' --output-on-failure -j 8
```

Expected: all three test suites pass with the new BlockKey shape.

- [ ] **Step 2.6: Commit**

```bash
git add libs/markoff-view-qml/src/AstBlockDiff.{h,cpp} \
         libs/markoff-view-qml/src/LiveBlockModel.{h,cpp} \
         libs/markoff-view-qml/src/LiveListModelBinding.{h,cpp} \
         libs/markoff-view-qml/tests/tst_view_qml_ast_block_diff.cpp
git commit -m "refactor(view-qml): BlockKey identity = (kind, BlockAnchor)"
```

---

## Task 3: Replace `LiveSelectionModel` with `LiveSelectionView`

Selection becomes Session-canonical. `LiveSelectionModel` retires.

**Files:**
- Create: `libs/markoff-view-qml/src/LiveSelectionView.{h,cpp}`
- Delete: `libs/markoff-view-qml/src/LiveSelectionModel.{h,cpp}`
- Create: `libs/markoff-view-qml/tests/tst_view_qml_live_selection_view.cpp`
- Delete: `libs/markoff-view-qml/tests/tst_view_qml_live_selection_model.cpp`
- Modify: `libs/markoff-view-qml/qml/LiveView.qml`
- Modify: `libs/markoff-view-qml/qml/delegates/*.qml` (Connections targets)
- Modify: `libs/markoff-view-qml/CMakeLists.txt`
- Modify: `libs/markoff-view-qml/src/EditorBackend.{h,cpp}` (Q_INVOKABLE wrappers)

- [ ] **Step 3.1: Add foundation translation Q_INVOKABLE wrappers to `EditorBackend`**

So QML can call `editorBackend.blockAt(anchor)` etc. without including `<markoff-foundation/...>` from QML headers. Each wrapper is one line forwarding to `MarkoffDocument`.

- [ ] **Step 3.2: Implement `LiveSelectionView`**

Subscribes to `Session::primarySelectionChanged`. Provides `Q_INVOKABLE QPair<int,int> rangeForBlock(BlockAnchor)`. Mouse-drag write methods (`begin(BlockAnchor, int offset)`, `extend(BlockAnchor, int offset)`, `clear()`) translate to TextAnchors via foundation API and call `Session::setPrimarySelection`.

- [ ] **Step 3.3: Update QML wiring**

In `LiveView.qml`, change references from `selectionModel` to `selectionView` (or whatever is exposed). In each text-bearing delegate's `Connections`, change the source object accordingly.

- [ ] **Step 3.4: Write `tst_view_qml_live_selection_view`**

Cover: subscribe to `primarySelectionChanged`, project to per-delegate ranges; cross-block correctness; round-trip via `setPrimarySelection`; mode-toggle preserves selection.

- [ ] **Step 3.5: Delete old model files + test**

```bash
git rm libs/markoff-view-qml/src/LiveSelectionModel.{h,cpp} \
       libs/markoff-view-qml/tests/tst_view_qml_live_selection_model.cpp
```

- [ ] **Step 3.6: Update CMakeLists**

Remove deleted files from sources + tests; add new ones.

- [ ] **Step 3.7: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_' --output-on-failure -j 8
```

Expected: all view-qml tests pass; the new selection_view test passes.

- [ ] **Step 3.8: Commit**

```bash
git commit -m "refactor(view-qml): LiveSelectionView projects Session::primarySelection"
```

---

## Task 4: `InlineFormatHighlighter` (read-only, no speculation yet)

Replace `TextEdit.MarkdownText` with the parser-driven highlighter. Defer speculative open-delim styling to Task 9.

**Files:**
- Create: `libs/markoff-view-qml/src/InlineFormatHighlighter.{h,cpp}`
- Create: `libs/markoff-view-qml/tests/tst_view_qml_inline_format_highlighter.cpp`
- Modify: `libs/markoff-view-qml/qml/delegates/ParagraphDelegate.qml`
- Modify: `libs/markoff-view-qml/qml/delegates/HeadingDelegate.qml`
- Modify: `libs/markoff-view-qml/CMakeLists.txt`

- [ ] **Step 4.1: Implement `InlineFormatHighlighter`**

A `QObject` per delegate. Takes the delegate's `QTextDocument` (via `TextEdit.textDocument`) + the parser's inline tree for the row. Walks the tree, applies `QTextCharFormat` ranges via `QTextCursor::mergeCharFormat`. Reapplies whenever the inline tree changes.

- [ ] **Step 4.2: Wire it into `ParagraphDelegate.qml` and `HeadingDelegate.qml`**

Flip `textFormat: TextEdit.MarkdownText` to `TextEdit.PlainText`. Instantiate `InlineFormatHighlighter` per delegate (via `QML_ELEMENT`-registered C++ type or a property exposed from `LiveListModelBinding`). Pass the inline tree from the model row's role data.

- [ ] **Step 4.3: Test cases**

Bold, italic, inline code, strike, highlight, links — render the right `QTextCharFormat` ranges. Test verifies format ranges, not pixel output.

- [ ] **Step 4.4: Build + tests + manual visual check**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_inline_format_highlighter' --output-on-failure -j 8
./build-dev/bin/markoff-testapp --live tests/fixtures/some-formatted-doc.md  # visual sanity
```

- [ ] **Step 4.5: Commit**

```bash
git commit -m "feat(view-qml): InlineFormatHighlighter replaces TextEdit.MarkdownText (read-only)"
```

---

## Task 5: `LiveEditBinding` — per-delegate cycle-guarded write path

The editing core. Make text-bearing delegates writable; route TextEdit changes to `applyLocalEdit`.

**Files:**
- Create: `libs/markoff-view-qml/src/LiveEditBinding.{h,cpp}`
- Create: `libs/markoff-view-qml/tests/tst_view_qml_live_edit_binding.cpp`
- Modify: `libs/markoff-view-qml/qml/delegates/ParagraphDelegate.qml`
- Modify: `libs/markoff-view-qml/qml/delegates/HeadingDelegate.qml`
- Modify: `libs/markoff-view-qml/qml/delegates/CodeBlockDelegate.qml`
- Modify: `libs/markoff-view-qml/CMakeLists.txt`

- [ ] **Step 5.1: Implement `LiveEditBinding`**

Per-delegate. Constructed with a TextEdit reference + the row's `BlockAnchor`. Connects to `TextEdit::contentsChange(qtPos, removed, added)`. With cycle-guard:
1. If currently re-applying a model-driven update, bail.
2. Convert qtPos to UTF-8 byte offset (use existing `EditorBackend` helper).
3. Compute canonical edit at `MarkoffDocument` byte coords using the BlockAnchor's current byte range start.
4. Call `MarkoffDocument::applyLocalEdit({MarkoffEdit{...}})`.

The "model-driven update" path: when `LiveListModelBinding` calls `dataChanged` for this row, `LiveEditBinding` sets the cycle-guard, sets `TextEdit::text`, clears the guard.

- [ ] **Step 5.2: Flip delegates to `readOnly: false`**

Each text-bearing delegate. Attach a `LiveEditBinding` instance.

- [ ] **Step 5.3: Test cases**

Type a character in row N → exactly one `applyLocalEdit` with the right edit. Reentrancy: model-driven update doesn't recursively trigger applyLocalEdit. UTF-16/8 translation cases (ASCII, multibyte).

- [ ] **Step 5.4: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_live_edit_binding' --output-on-failure -j 8
```

- [ ] **Step 5.5: Manual smoke test**

```bash
./build-dev/bin/markoff-testapp --live tests/fixtures/empty.md
```

Type into a paragraph; observe the source mutating (use the AST inspector pane from Phase-1 to verify). Cursor stays put. Other rows unaffected.

- [ ] **Step 5.6: Commit**

```bash
git commit -m "feat(view-qml): LiveEditBinding per-delegate cycle-guarded write path"
```

---

## Task 6: `LiveStructuralKeyHandler` — block-boundary edits

Backspace at offset 0, Enter at end / mid / offset 0, Delete at end-of-block, Tab in code block.

**Files:**
- Create: `libs/markoff-view-qml/src/LiveStructuralKeyHandler.{h,cpp}`
- Create: `libs/markoff-view-qml/tests/tst_view_qml_live_structural_keys.cpp`
- Modify: `libs/markoff-view-qml/qml/LiveView.qml`
- Modify: `libs/markoff-view-qml/CMakeLists.txt`

- [ ] **Step 6.1: Implement `LiveStructuralKeyHandler`**

A `QObject` exposed to QML. `Q_INVOKABLE bool tryHandle(int key, int modifiers, BlockAnchor row, int qtPos, bool selectionEmpty)`. Returns true if it consumed the key. Cases:
- Backspace + qtPos==0 + has prev block + selection empty → applyLocalEdit deleting one byte before block start.
- Delete + qtPos==length + has next block + selection empty → applyLocalEdit deleting one byte at next block start.
- Enter + inside code block → return false (let TextEdit insert literal `\n`).
- Enter + at end of non-code block → applyLocalEdit inserting `\n` at end of row's range (next reparse may or may not split based on whether `\n\n` now present).
- Enter + mid block → applyLocalEdit inserting `\n` at qtPos.
- Tab + inside code block → return false (let TextEdit insert literal tab).

- [ ] **Step 6.2: Wire into QML**

In `LiveView.qml`, top-level `Keys.onPressed` calls `tryHandle` for each event before the focused delegate gets it; consume the event if handled. (Alternative: per-delegate `Keys.onPressed` with `Keys.priority: Keys.AfterItem` so TextEdit gets first crack — pick whichever the QML focus chain makes cleaner.)

- [ ] **Step 6.3: Test cases per Q4**

All defaults from spec §4 / Q4: demote-then-join Backspace, code-block fences inserting literal newlines, etc.

- [ ] **Step 6.4: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_live_structural_keys' --output-on-failure -j 8
```

- [ ] **Step 6.5: Manual smoke**

Type a paragraph, press Enter twice, type another paragraph; verify two paragraphs in AST. Backspace at start of second paragraph; verify merge.

- [ ] **Step 6.6: Commit**

```bash
git commit -m "feat(view-qml): LiveStructuralKeyHandler block-boundary edit semantics"
```

---

## Task 7: Cursor restoration on parser-driven kind change

When typing `# ` promotes paragraph→heading, the delegate Item is destroyed + recreated. Restore focus + cursor position to the new delegate.

**Files:**
- Modify: `libs/markoff-view-qml/src/LiveListModelBinding.{h,cpp}`
- Modify: `libs/markoff-view-qml/src/LiveEditBinding.{h,cpp}` (or via a small new helper)
- Add cases to: `libs/markoff-view-qml/tests/tst_view_qml_live_structural_keys.cpp` (or a new test file if it grows)

- [ ] **Step 7.1: Track focused row + cursor pos**

In `LiveListModelBinding`, maintain "currently focused (BlockAnchor, qtPos)" state, updated when the focused delegate's TextEdit's cursor changes.

- [ ] **Step 7.2: On `dataChanged` with kind change, schedule focus restoration**

When the diff produces a row whose kind changed (or — equivalent — a row that was insertedAt the focused anchor's position), schedule (via QML `Component.onCompleted` or a `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`) a focus restoration: set focus on the new delegate and position the cursor at the recorded qtPos (clamped to new TextEdit length).

- [ ] **Step 7.3: Test cases**

- Type `# ` at start of paragraph → after parse, heading delegate is focused, cursor immediately after the `# `.
- Type ``` ``` ``` opening a code block (covered by Task 8 speculative path; cursor land in the new code-block delegate is the same restoration mechanism).

- [ ] **Step 7.4: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_live_(structural_keys|list_model_binding)' --output-on-failure -j 8
```

- [ ] **Step 7.5: Commit**

```bash
git commit -m "feat(view-qml): cursor restoration across delegate-kind changes"
```

---

## Task 8: `LiveSpeculativeFenceController` — code-block fence

Editor-side speculative kind change for ``` and ~~~ fences.

**Files:**
- Create: `libs/markoff-view-qml/src/LiveSpeculativeFenceController.{h,cpp}`
- Create: `libs/markoff-view-qml/tests/tst_view_qml_live_speculative_fence.cpp`
- Modify: `libs/markoff-view-qml/src/LiveBlockModel.{h,cpp}` (add `speculativelyChangeKind`/`revertSpeculativeKind`)
- Modify: `libs/markoff-view-qml/src/LiveListModelBinding.{h,cpp}` (reconcile speculative state on parse return)
- Modify: `libs/markoff-view-qml/CMakeLists.txt`

- [ ] **Step 8.1: Add model-side speculative API**

`LiveBlockModel::speculativelyChangeKind(int row, QString newKind)` updates the row's kind role and tags it as "speculative". `revertSpeculativeKind(int row)` restores the previous kind. The tag is consulted by reconciliation logic in `LiveListModelBinding`.

- [ ] **Step 8.2: Implement `LiveSpeculativeFenceController`**

Connects to `LiveEditBinding::editApplied(BlockAnchor row, ...)` (a new signal — easy to add). Detects: row kind is paragraph + the row's text now contains ```` ``` ```` (or `~~~`) at start of a line + no prior unclosed fence in the doc. On detection, `model.speculativelyChangeKind(row, "code_block")`.

- [ ] **Step 8.3: Reconcile in `LiveListModelBinding::onParseUpdated`**

For each row with speculative kind: if parser produced same kind, drop the speculative tag; if parser produced a different kind, revert speculative.

- [ ] **Step 8.4: Test cases**

Open fence flips kind before parse return. Closing fence + parse confirms; speculation drops. Wrong speculation (e.g. user undoes the fence): revert.

- [ ] **Step 8.5: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_live_speculative_fence' --output-on-failure -j 8
```

- [ ] **Step 8.6: Commit**

```bash
git commit -m "feat(view-qml): LiveSpeculativeFenceController for code-block fences"
```

---

## Task 9: Speculative open-delimiter styling in `InlineFormatHighlighter`

Renderer-side. Open `**`, `__`, `*`, `_`, `` ` ``, `~~`, `==` style content speculatively until the parser confirms.

**Files:**
- Modify: `libs/markoff-view-qml/src/InlineFormatHighlighter.{h,cpp}`
- Add cases to: `libs/markoff-view-qml/tests/tst_view_qml_inline_format_highlighter.cpp`

- [ ] **Step 9.1: Implement open-delimiter scan**

Within the highlighter's per-block format pass, after applying parser-confirmed ranges, scan the source bytes for unclosed delimiters and apply the matching `QTextCharFormat`. Care: don't double-format ranges already covered by parser-confirmed nodes; don't speculatively style inside parser-confirmed inline-code spans.

- [ ] **Step 9.2: Test cases**

Open `**hello` → trailing `hello` is bold-formatted. Closing `**` → parser confirms; visual format is identical (test verifies `QTextCharFormat` equality between speculative pass and confirmed pass). All seven delimiters. Nested `**bold *italic***`. Inline code does not interpret nested formatting.

- [ ] **Step 9.3: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_inline_format_highlighter' --output-on-failure -j 8
```

- [ ] **Step 9.4: Commit**

```bash
git commit -m "feat(view-qml): InlineFormatHighlighter speculative open-delimiter styling"
```

---

## Task 10: `LiveClipboardController` — Cut, Copy, Paste

Markdown source on the wire. Cut+Paste actions added to the right-click menu.

**Files:**
- Create: `libs/markoff-view-qml/src/LiveClipboardController.{h,cpp}`
- Create: `libs/markoff-view-qml/tests/tst_view_qml_live_clipboard.cpp`
- Modify: `libs/markoff-view-qml/src/LiveContextMenuHandler.{h,cpp}` (add Cut + Paste actions)
- Modify: `libs/markoff-view-qml/qml/LiveView.qml` (Ctrl+X / Ctrl+V Shortcuts)
- Modify: `libs/markoff-view-qml/CMakeLists.txt`

- [ ] **Step 10.1: Implement `LiveClipboardController`**

`Q_INVOKABLE` `copy()`, `cut()`, `paste()`. `copy()` reads `Session::primarySelection`, computes byte range via `resolveAnchor`, slices `MarkoffDocument::toMarkdownUtf8()`, sets clipboard text. `cut()` = `copy()` + `applyLocalEdit` deleting that range. `paste()` reads clipboard text-only, applies replace at current selection (insert if empty selection).

- [ ] **Step 10.2: Wire into context menu + Shortcuts**

`LiveContextMenuHandler` adds Cut + Paste. `LiveView.qml` adds `Shortcut { sequence: StandardKey.Cut }` etc.

- [ ] **Step 10.3: Test cases**

Single-block copy: clipboard has source markdown for selected range. Cross-block copy: clipboard has full source including structural separators. Cut applies delete. Paste replaces selection. Paste of multi-line markdown decomposes structurally on next reparse.

- [ ] **Step 10.4: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_live_clipboard' --output-on-failure -j 8
```

- [ ] **Step 10.5: Manual round-trip with Source mode**

Copy from Source mode, paste into Live mode → identical text. Copy from Live mode, paste into Source mode → identical markdown source.

- [ ] **Step 10.6: Commit**

```bash
git commit -m "feat(view-qml): LiveClipboardController Cut/Copy/Paste with markdown source"
```

---

## Task 11: Undo grouping policy

View-side coalesce policy in `LiveEditBinding`.

**Files:**
- Modify: `libs/markoff-view-qml/src/LiveEditBinding.{h,cpp}`
- Create: `libs/markoff-view-qml/tests/tst_view_qml_live_undo.cpp`

- [ ] **Step 11.1: Implement coalesce state machine**

After every `applyLocalEdit`, decide: should this entry coalesce with the previous one? Coalesce iff: previous edit was a printable + this edit is a printable + same focused row + last edit was less than 1 second ago + neither was structural (paste, cross-block) + no movement / focus change between them. If yes, call `MarkoffDocument::coalesceLastUndo()`.

- [ ] **Step 11.2: Test cases**

Type 5 chars → 1 undo entry. Type, wait 1.1s, type → 2 entries. Type, Backspace, type → 3 entries (Backspace breaks). Type, click elsewhere, type → 2 entries. Cross-mode: type in Live, switch to Source, Ctrl+Z undoes the Live edit; cursor lands at edit site in current mode.

- [ ] **Step 11.3: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_live_undo' --output-on-failure -j 8
```

- [ ] **Step 11.4: Commit**

```bash
git commit -m "feat(view-qml): undo coalesce policy in LiveEditBinding"
```

---

## Task 12: IME composition handling

Defer `applyLocalEdit` during composition. Defer `dataChanged` text updates for the composing row.

**Files:**
- Modify: `libs/markoff-view-qml/src/LiveEditBinding.{h,cpp}`
- Modify: `libs/markoff-view-qml/src/LiveListModelBinding.{h,cpp}`
- Create: `libs/markoff-view-qml/tests/tst_view_qml_live_ime.cpp`

- [ ] **Step 12.1: Suppress applyLocalEdit while composing**

In `LiveEditBinding`, check `TextEdit::inputMethodComposing` (or subscribe to composition begin/end). While true, the `contentsChange` slot doesn't call `applyLocalEdit`. On composition end, the next `contentsChange` fires for the committed text; that one goes through normally.

- [ ] **Step 12.2: Defer dataChanged for composing row**

In `LiveListModelBinding`, when applying a `dataChanged` for a row whose delegate's TextEdit is composing, queue the update; on `compositionEnded` (signalled from the delegate), flush queued updates. Highlight-format updates are not queued (they don't replace text content).

- [ ] **Step 12.3: Test cases**

Synthetic `QInputMethodEvent` injection: preedit visible, no `applyLocalEdit`. During composition, parse-driven `dataChanged` for that row is deferred; format updates aren't. Commit composition: deferred update applies (or is superseded by the new edit's parse).

- [ ] **Step 12.4: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_live_ime' --output-on-failure -j 8
```

- [ ] **Step 12.5: Manual IME smoke (if reviewer has Korean/Japanese/Chinese IME available)**

Compose a syllable, observe preedit; commit; observe canonical doc updates correctly.

- [ ] **Step 12.6: Commit**

```bash
git commit -m "feat(view-qml): IME composition deferral in edit + model bindings"
```

---

## Task 13: HR / image delegate click routing

Click on HR or image routes focus to neighbour text delegate.

**Files:**
- Modify: `libs/markoff-view-qml/qml/delegates/HorizontalRuleDelegate.qml`
- Modify: `libs/markoff-view-qml/qml/delegates/ImageDelegate.qml`
- Add cases to: `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp` (or a new `tst_view_qml_click_routing.cpp` if it grows)

- [ ] **Step 13.1: Implement routing**

In each delegate's `MouseArea.onClicked`, compute which neighbour to focus (preceding text delegate by default, or following if the delegate is at top of doc). Programmatic focus via `LiveView`'s helper.

- [ ] **Step 13.2: Test cases**

Click on HR → preceding text delegate gets focus, cursor at end. Click on HR at top of doc → following text delegate, cursor at start. Click on image: same.

- [ ] **Step 13.3: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_' --output-on-failure -j 8
```

- [ ] **Step 13.4: Commit**

```bash
git commit -m "feat(view-qml): HR/image click routes focus to neighbour text delegate"
```

---

## Task 14: Test app save / dirty / window title

App-layer save in `markoff-view-qml/app/main.cpp`.

**Files:**
- Modify: `libs/markoff-view-qml/app/main.cpp`

- [ ] **Step 14.1: Track lastSavedSequence + dirty**

After `resetContent` on file open, record `lastSavedSequence = doc.editSequence()`. Subscribe to `contentsChanged`; recompute dirty as `doc.editSequence() != lastSavedSequence`. (Do not call `doc.version()` from app-layer code — that returns `Crdt::Global` and is foundation-internal per the BlockAnchor spec §2.)

- [ ] **Step 14.2: Window title `[modified]` indicator**

Update title on dirty change.

- [ ] **Step 14.3: Ctrl+S handler**

On Ctrl+S, write `doc.toMarkdownUtf8()` to the file path (or open Save-As if no path); update `lastSavedSequence` to `doc.editSequence()`.

- [ ] **Step 14.4: Close-prompt on dirty**

If dirty and user attempts to close window, prompt Save / Discard / Cancel.

- [ ] **Step 14.5: Manual smoke**

Open a file, edit, observe `[modified]`, save with Ctrl+S, observe clean. Edit again, close → prompt. Discard → close. Save → close.

- [ ] **Step 14.6: Commit**

```bash
git commit -m "feat(testapp): save / dirty / window title for live editing"
```

---

## Task 15: Empty-doc focus + first-keystroke materialisation

When the model has zero rows, `LiveView` itself accepts focus; first keystroke materialises a paragraph.

**Files:**
- Modify: `libs/markoff-view-qml/qml/LiveView.qml`
- Modify: `libs/markoff-view-qml/src/LiveEditBinding.{h,cpp}` (or a small new piece)
- Add cases to: `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp`

- [ ] **Step 15.1: Top-level focus when empty**

In `LiveView.qml`, when `listView.count === 0`, the `LiveView` Item itself takes focus + is `Keys.onPressed` enabled. First printable triggers `MarkoffDocument::applyLocalEdit` inserting that character at byte 0.

- [ ] **Step 15.2: Focus transfer after materialisation**

After the parse returns and the new paragraph row is created, focus transfers to the new delegate with cursor after the inserted character.

- [ ] **Step 15.3: Test cases**

Empty doc + type 'a' → paragraph row, delegate focused, cursor at offset 1.

- [ ] **Step 15.4: Build + tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R 'tst_view_qml_live_view_qml' --output-on-failure -j 8
```

- [ ] **Step 15.5: Commit**

```bash
git commit -m "feat(view-qml): empty-doc focus + first-keystroke materialisation"
```

---

## Task 16: End-to-end editing smoke + dogfood checklist

Finalize the integrated stack. Update existing smoke test to exercise editing; document a manual dogfood checklist.

**Files:**
- Modify: `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp`
- Modify: `libs/markoff-view-qml/CLAUDE.md` (editing invariants section)

- [ ] **Step 16.1: Extend QML smoke test**

Cover: type into a paragraph, observe doc mutating; Backspace at offset 0 of second paragraph joins; Cut/Copy/Paste round-trip; mode-toggle Source ↔ Live preserves selection + undo stack; Ctrl+Z undoes a series of edits.

- [ ] **Step 16.2: Update `libs/markoff-view-qml/CLAUDE.md`**

Document the editing invariants from spec §4. Note that walking-skeleton invariants 7+8 are amended (TextAnchor as opaque handle; rangeForBlock semantics now derive from Session selection).

- [ ] **Step 16.3: Run full test suite**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -E 'tst_realistic|tst_benchmark' --output-on-failure -j 8
```

Expected: all non-slow tests pass. Run `tst_realistic` separately to confirm no regression there.

- [ ] **Step 16.4: Manual dogfood**

Run through the manual dogfood checklist from spec §5. File any findings as bugs against this branch.

- [ ] **Step 16.5: Commit**

```bash
git commit -m "test(view-qml): end-to-end editing smoke + invariant docs"
```

---

## Task 17: Tag a release

Once dogfood is clean, mark the milestone.

- [ ] **Step 17.1: Verify clean state**

```bash
git status                    # clean
ctest --test-dir build-dev -E 'tst_realistic|tst_benchmark' --output-on-failure -j 8
```

- [ ] **Step 17.2: Tag**

Pick the tag name in consultation with the human (likely `live-editing-v0` or a milestone version). Never force-push a tag.

```bash
git tag -a <name> -m "Live editing v0 — structurally-complete editor per docs/specs/2026-04-30-live-editing-design.md"
git push --tags
```

---

## Verification gates

Before declaring this plan complete:

1. All 8 new test executables pass (`tst_view_qml_live_edit_binding`, `..._structural_keys`, `..._speculative_fence`, `..._inline_format_highlighter`, `..._selection_view`, `..._clipboard`, `..._undo`, `..._ime`).
2. All modified existing tests pass (`tst_view_qml_ast_block_diff`, `tst_view_qml_live_view_qml`).
3. `tst_realistic` passes (no perf regression).
4. `LiveSelectionModel` is removed from the tree (`grep -r LiveSelectionModel libs/markoff-view-qml/` returns empty).
5. `TextEdit.MarkdownText` is not used anywhere in `libs/markoff-view-qml/qml/` (`grep -r 'TextEdit.MarkdownText' libs/markoff-view-qml/qml/` returns empty).
6. `BlockKey` no longer carries content-hash (`grep -E 'contentHash|content_hash' libs/markoff-view-qml/src/` returns empty for that purpose).
7. Manual dogfood checklist passes (see spec §5 "Manual / dogfood").
