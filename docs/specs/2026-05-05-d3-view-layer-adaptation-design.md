# D3 — View-layer adaptation

**Date:** 2026-05-05
**Branch:** `exploration/new-foundation`
**Status:** Approved for plan derivation (brainstorming complete).
**Audience:** the implementer; the user (review gate); fresh-context agents picking up D4 or D5.

**Supersedes:** `docs/specs/2026-05-04-d3-view-layer-adaptation-STUB.md` (stub).

**Predecessors (read first):**
1. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — D-arc orientation
2. `docs/d-arc/collabtext-scope-line.md` — six "won't do" items
3. `docs/specs/2026-05-04-d2-foundation-reshape-design.md` — the D2 spec; D3 consumes its API
4. `docs/d-arc/d-arc-status.md` — live status board

**Convention used throughout.** Every major section ends with a **"Why this and not the alternatives"** subsection.

---

## 0. TL;DR

D3 completes the view-layer rebuild that D2's Phase 11 migration began. The Phase 11 migration was mechanical — call sites updated, dead workarounds deleted — but left several structural gaps: `BlockRecord.inlineSpans/headingLevel/codeLanguage` unpopulated, cursor delivery still routed through a deferred parse cycle, kind-transition detection absent, and only three block kinds editable (Paragraph, Heading, CodeBlock). D3 closes all of these and adds the full block-kind complement: L6 (Heading/CodeBlock/HorizontalRule/Image full delegates), L7 (ListItem and Blockquote structural editing), L8 (Math with `BlockInternalEdit`), and per-block undo UI.

Foundation amendments are minimal: a new `BlockSerializerRegistry` abstract interface, `AttrNames` namespace constants, and fixing `d2InsertBlock/d2RemoveBlock` to fire the `IdListProxy` notification.

---

## 1. Premises (binding)

| # | Decision | Resolution |
|---|---|---|
| 1 | **Scope** | D3 only — view-layer adaptation through L8 + per-block undo gesture. Foundation changes are amendments to make D3 possible; no new foundation APIs beyond those listed in §9. |
| 2 | **Cursor delivery** | `LiveListModelBinding` emits structural signals (`structuralRowsInserted`, `structuralRowRemoved`) derived from the AstBlockDiff result. `LiveCursorState` subscribes to these instead of `QAbstractItemModel::rowsInserted`. `noteParseArrived` retires. |
| 3 | **Kind-transition detection** | View-driven: `onD2Changed` inspects each Equal-op block's text against hardcoded prefix rules; calls `Cmd::changeKind` on mismatch. Descriptor-driven extension deferred to D4/later. |
| 4 | **BlockKindRegistry** | Per-`LiveListModelBinding` (not global singleton). Foundation gets a thin `BlockSerializerRegistry` abstract interface; `BlockKindRegistry` implements it. `LiveListModelBinding` no longer owns a registry value member; wires from `doc->serializerRegistry()`. |
| 5 | **AttrValue scope** | `std::variant<int, QString, bool>` (already in `BlockAttrsMap.h`); confirmed for all D3 block kinds. AttrName constants declared in a new `Markoff::AttrNames` namespace in `markoff-foundation`. |
| 6 | **L7 block granularity** | Each list item is a separate `BlockKind::ListItem` block in IdList; sublists deferred (a list-item containing a nested list holds indented source in one Buffer). Consecutive list-items form a visual list; no structural "list-as-container" in the CRDT model. Blockquotes follow the same pattern: each `> `-prefixed block is a `BlockKind::Blockquote` block; consecutive blockquote blocks are visually grouped. |
| 7 | **L8 / BlockInternalEdit** | Math delegate only in D3. Entry via F2 or double-click; exit via Escape. jkqtmathtext for render mode. |
| 8 | **Per-block undo UI** | Right-click context menu on all block delegates showing "Undo in this block" (calls `MarkoffDocument::undoForBlock`). Full context-menu descriptor system (R9-equivalent) deferred. |
| 9 | **LiveSpeculationLayer** | Not needed. Kind-transition detection in `onD2Changed` is synchronous enough (one event-loop spin) that speculative pre-painting adds no user-visible benefit under D2's per-block CRDT model. |

---

## 2. Architecture overview

The layer stack after D3:

```
┌──────────────────────────────────────────────────────────────────┐
│ L8  Interactive blocks   (Math + BlockInternalEdit)              │  D3
├──────────────────────────────────────────────────────────────────┤
│ L7  Structured text      (ListItem, Blockquote)                  │  D3
├──────────────────────────────────────────────────────────────────┤
│ L6  Other text blocks    (Heading full, CodeBlock full, HR, Img) │  D3
├──────────────────────────────────────────────────────────────────┤
│ L5  Structural keys      (descriptor-based dispatch; extended)   │  D2 Phase 11 → D3 extends
├──────────────────────────────────────────────────────────────────┤
│ L4  Block editing        (LiveEditBinding; thin keystroke→edit)  │  D2 Phase 11 ✓
├──────────────────────────────────────────────────────────────────┤
│ L3  Cursor + selection   (Shape 1; mouse hit-test; copy)         │  D2 Phase 11 ✓
├──────────────────────────────────────────────────────────────────┤
│ L2  Diff-driven model    (Myers over (kind, BlockId))            │  D2 Phase 11 ✓
├──────────────────────────────────────────────────────────────────┤
│ L1  Read-only render     (ListView + delegates)                  │  D2 Phase 11 ✓
├──────────────────────────────────────────────────────────────────┤
│ L0  Coordinate primitives (byte/qtPos/block-local conversions)   │  D2 Phase 11 ✓
└──────────────────────────────────────────────────────────────────┘
```

**D3 does not touch** L0–L4 beyond the cursor delivery seam change (§3) and inline span population (§4). All structural-key handler registrations carry forward; D3 adds new registrations.

---

## 3. Cursor delivery redesign

### 3.1 Problem

`LiveCursorState` listens to `QAbstractItemModel::rowsInserted` via `setSignalModel(QAbstractItemModel *)`. The `noteParseArrived` path (called from `onD2Changed`) and the 2-parse-cycle timeout exist because cursor resolution for merge ops races the deferred `d2DocumentChanged` round-trip. This mechanism is correct but indirects through the wrong signal source.

### 3.2 New signal path

`LiveListModelBinding` adds two signals:

```cpp
Q_SIGNAL void structuralRowsInserted(int first, int last);
Q_SIGNAL void structuralRowRemoved(int row);
```

These are emitted **synchronously in `onD2Changed`** immediately after `model->applyOps(ops, records)`, by iterating the AstBlockDiff ops:

```
for each op in ops:
  Insert(row)  → emit structuralRowsInserted(row, row)
  Delete(row)  → emit structuralRowRemoved(row)
  Equal(row)   → (no structural signal)
```

After emitting, `onD2Changed` drops the `cursorState->noteParseArrived(...)` call.

### 3.3 LiveCursorState changes

- `setSignalModel(QAbstractItemModel *)` method and `m_signalModel` member drop.
- `onRowsInserted(...)` handler drops.
- `noteParseArrived(quint64)` method drops.
- `parseCyclesSeen` field on `PendingRow` drops.
- Constructor gains `LiveListModelBinding *binding` parameter (or `setBinding` setter). Connects `binding->structuralRowsInserted → onStructuralRowsInserted` and `binding->structuralRowRemoved → onStructuralRowRemoved`.
- `onStructuralRowsInserted(int first, int last)`: replaces `onRowsInserted`; resolves row-keyed and byte-keyed pending; resolves anchor-keyed pending (new blocks may contain the wanted anchor).
- `onStructuralRowRemoved(int row)`: resolves anchor-keyed pending (the `mergedInto` anchor was already in the model; `resolvePendingForAnchor()` finds it immediately since the surviving block is still there).

### 3.4 QML cursor-reset fix

When `text` changes on a QML `TextEdit`, Qt resets `cursorPosition` to 0 (or end). The `noteParseArrived` timing hack was a workaround. The correct fix is structural: the delegate explicitly re-asserts `cursorPosition` after the `text` property updates.

In each text-bearing delegate:

```qml
Connections {
    target: cursorState
    function onCursorChanged() {
        if (cursorState.focusedAnchorRow === model.index)
            textEdit.cursorPosition = cursorState.focusedQtPos
    }
}
// Also re-assert on text change:
onTextChanged: {
    if (cursorState.focusedAnchorRow === model.index)
        textEdit.cursorPosition = cursorState.focusedQtPos
}
```

This replaces the implicit timing guarantee that `noteParseArrived` provided.

### 3.5 Foundation fix: IdListProxy notification coverage

The current `onD2Changed` comment notes that `idListProxy::structureChanged` does not fire from `d2InsertBlock/d2RemoveBlock` (only from `applyStructural`). D3 fixes this: `d2InsertBlock` and `d2RemoveBlock` call `m_idListProxy->notifyChanged()` after the underlying IdList op (same path `applyStructural` uses). This ensures the proxy's `editSequence` is accurate and future consumers of `idListProxy` work correctly without `onD2Changed` as an intermediary.

### 3.6 Why this and not the alternatives

Emitting structural signals from within `onD2Changed` (rather than connecting to `idListProxy::structureChanged`) is right for two reasons: (1) `idListProxy::structureChanged` is a no-argument signal — it doesn't tell the cursor which row changed, so `LiveCursorState` would need to diff the model anyway, which is exactly what `onD2Changed` already does; (2) `idListProxy::structureChanged` currently doesn't fire for the D2 Cmd::* paths anyway, requiring the foundation fix noted in §3.5. Re-emitting from `onD2Changed` is simpler, row-informative, and correct for both D2's edit paths and future D5 remote-op paths (D5 will trigger `onD2Changed` via `applyRemoteOps → d2DocumentChanged`).

---

## 4. Inline span consumption

### 4.1 BlockRecord population

`onD2Changed` populates all three previously-empty fields:

```cpp
rec.inlineSpans  = doc->inlineSpansFor(id);
rec.headingLevel = std::get_if<int>(blockAttr(doc, id, "level")) != nullptr
                   ? std::get<int>(*blockAttr(doc, id, "level")) : 0;
rec.codeLanguage = std::get_if<QString>(blockAttr(doc, id, "infoString")) != nullptr
                   ? std::get<QString>(*blockAttr(doc, id, "infoString")) : QString{};
```

`doc->inlineSpansFor(id)` is synchronous and cache-hits on non-edited blocks (per D2 §6.1); the per-block call in the loop is bounded to O(changed_blocks).

### 4.2 BlockWalker retirement

`BlockWalker.{h,cpp}` and `BlockWalker` usages (currently dead code since Phase 11) are deleted. The `tlb.inlineSpans` path existed only to serve BlockWalker; it has no living callers after this deletion.

---

## 5. Kind-transition detection

### 5.1 Inference rules

A free function `inferBlockKind(const QString &text) → Markoff::BlockKind` in `markoff-live-render/src/KindTransition.cpp`:

Rules are evaluated in order; first match wins.

| Rule | Detected kind |
|---|---|
| text starts with 1–6 `#` chars followed by a space or EOL | `Heading` |
| text starts with ```` ``` ```` or `~~~` (3+ chars) | `CodeBlock` |
| text trimmed is `---`, `***`, or `___` | `HorizontalRule` |
| text starts with `![` | `Image` |
| text starts with `$$` | `Math` (displayMode=true) |
| text starts with `$` | `Math` (displayMode=false) |
| text matches list-item prefix (`[-*+] ` or `\d+[.)]\s`) | `ListItem` |
| text starts with `> ` | `Blockquote` |
| (none of the above) | `Paragraph` |

### 5.2 Detection point

In `onD2Changed`, after building `records`, iterate `ops`. For each `Equal(row)` op (kind identity preserved; only text may have changed):

```
BlockKind inferred = inferBlockKind(records[row].text);
BlockKind stored   = doc->blockKind(id);
if (inferred != stored) {
    QList<QByteArray> attrNames;
    QList<AttrValue>  attrVals;
    // Populate attrs for inferred kind (e.g. heading level)
    if (inferred == BlockKind::Heading) {
        attrNames << "level";
        attrVals  << int(countLeadingHashes(records[row].text));
    }
    Cmd::changeKind(*doc, id, inferred, attrNames, attrVals);
}
```

`Cmd::changeKind` fires another `d2DocumentChanged` on the next event-loop spin. `onD2Changed` re-runs; this time the kind matches; no recursion.

### 5.3 Heading level update

When a heading's text changes but remains a heading (still an `Equal` op from the diff's perspective), the level attr may have changed (user added/removed a `#`). Detect via `countLeadingHashes(text) != rec.headingLevel` and call `Cmd::changeKind` with the updated level even if the kind is still `Heading`.

### 5.4 Why this and not the alternatives

View-driven detection (over foundation-emitted `KindAdvisor`) keeps the foundation CRDT-only. Kind semantics are a display concern — the view layer already knows what `#` and ` ``` ` mean. Hardcoded prefix rules (over descriptor-declared) satisfy D3 scope without YAGNI; descriptors can wrap these rules later. One-event-loop-spin latency (over synchronous kind change on every edit) avoids re-entrant CRDT ops within a single `d2DocumentChanged` handler.

---

## 6. BlockKindRegistry per-document injection

### 6.1 BlockSerializerRegistry interface (new, in markoff-foundation)

```cpp
// markoff-foundation/include/markoff-foundation/BlockSerializerRegistry.h
namespace Markoff {

class BlockSerializerRegistry {
public:
    virtual ~BlockSerializerRegistry() = default;
    virtual QByteArray serialize(BlockKind kind,
                                 const QByteArray &text,
                                 const QHash<AttrName, AttrValue> &attrs) const = 0;
};

}  // namespace Markoff
```

`MarkoffDocument` gains an optional constructor parameter:
```cpp
explicit MarkoffDocument(quint16 replicaId,
                         const BlockSerializerRegistry *registry = nullptr,
                         QObject *parent = nullptr);
```
and an accessor `const BlockSerializerRegistry *serializerRegistry() const`.

When null, `MarkoffDocument::save()` dispatches to a built-in serializer table keyed on `BlockKind` enum values. When non-null, the provided registry is consulted first; falls back to built-in for unregistered kinds.

### 6.2 BlockKindRegistry changes

`BlockKindRegistry` (in `markoff-live-render`) adds `: public Markoff::BlockSerializerRegistry` to its inheritance list and implements `serialize(...)` by dispatching to its registered `BlockKindDescriptor::serializer` callbacks (new field on `BlockKindDescriptor`).

### 6.3 LiveListModelBinding changes

`Private::registry` changes from `BlockKindRegistry registry` (value member) to `const BlockKindRegistry *registry = nullptr` (pointer). `setDocument(doc)` retrieves it from `doc->serializerRegistry()` cast to `const BlockKindRegistry *`. If the host has not set a registry, `LiveListModelBinding` falls back to constructing a default-initialized internal registry and calling `BlockKindRegistry::registerBuiltins()` on it.

### 6.4 Why this and not the alternatives

Per-document over global singleton: tests can construct isolated registries without state leaks between test cases; partial-registry testing is possible. Interface over moving `BlockKindRegistry` into the foundation: view-specific descriptor fields (`consumedStructuralKeys`, `supportedCursorVariants`, `contextMenuActions`) don't belong in the foundation; keeping them in `markoff-live-render` preserves the dependency direction.

---

## 7. AttrNames namespace

New header `markoff-foundation/include/markoff-foundation/AttrNames.h`:

```cpp
namespace Markoff::AttrNames {
    inline const AttrName Level       = "level";        // Heading: int 1–6
    inline const AttrName InfoString  = "infoString";   // CodeBlock: QString
    inline const AttrName MarkerStyle = "markerStyle";  // ListItem: QString
    inline const AttrName IndentLevel = "indentLevel";  // ListItem: int 0-based
    inline const AttrName Checked     = "checked";      // ListItem: bool (task list)
    inline const AttrName Src         = "src";          // Image: QString
    inline const AttrName Alt         = "alt";          // Image: QString
    inline const AttrName Title       = "title";        // Image: QString (optional)
    inline const AttrName DisplayMode = "displayMode";  // Math: bool
}
```

All D3 view-layer code uses these constants instead of string literals.

---

## 8. L6 — Other text blocks

### 8.1 Heading (full delegate)

**Current state**: Heading is registered in `LiveStructuralKeyHandler` with paragraph-identical Enter/Backspace/Delete handlers. No level-change gesture. No distinct delegate.

**D3 additions**:

1. **HeadingDelegate.qml** — renders `# ` prefix in a muted color distinct from content; font-size scales with `rec.headingLevel`; edit mode is a single-line TextEdit (same `LiveEditBinding` pattern as ParagraphDelegate, no newlines accepted).

2. **Structural key change**: Enter at EOB on a Heading creates a new **Paragraph** (not a new Heading). The current `paragraphEnter` handler already does this correctly — no handler change needed; only the QML rendering differs.

3. **Level-change gesture**: `Cmd+Shift+1`–`Cmd+Shift+6` on a focused Heading calls `Cmd::changeKind(*doc, id, BlockKind::Heading, {"level"}, {targetLevel})`. Registered as consumed keys in `BlockKindDescriptor`. `Cmd+Shift+0` demotes to Paragraph.

4. **Inline format highlighting** via `inlineSpans` from `BlockRecord`: bold/italic spans highlighted within the heading text, consistent with ParagraphDelegate.

### 8.2 CodeBlock (full delegate)

**Current state**: CodeBlock is registered for Backspace/Delete merge only. No language-tag UI. No Enter consumed.

**D3 additions**:

1. **CodeBlockDelegate.qml** — monospace font, KF6::SyntaxHighlighting based on `rec.codeLanguage`; multi-line TextEdit; fence delimiters (`` ``` `` lines) shown in muted color as non-editable visual chrome (the actual text is source-faithful, fence markers included).

2. **Tab key**: Insert 4 spaces (or a literal tab — follow existing editor preference; hard-coded to 4 spaces for D3). Registered as consumed key in `BlockKindDescriptor`.

3. **Language-tag editing**: clicking the language tag label (the identifier after `` ``` ``) enters a small inline TextEdit for the info-string; on commit, calls `Cmd::changeKind(*doc, id, BlockKind::CodeBlock, {"infoString"}, {newLang})`.

4. **Enter**: NOT consumed — native `\n` insertion via `LiveEditBinding` is correct for code blocks.

### 8.3 HorizontalRule

1. **HrDelegate.qml** — renders a horizontal line; no TextEdit.
2. **BlockKindDescriptor**: `TextCaret` not supported; `BlockSelected` is the only cursor variant.
3. **Structural keys**: when selected (`BlockSelected`), Delete or Backspace removes the block via `Cmd::backspaceMerge` or `Cmd::deleteMerge` (treating the hr as zero-text). Navigation: Up/Down arrow keys move cursor to the adjacent block.
4. **Consumed keys**: `{Qt::Key_Delete, Qt::Key_Backspace, Qt::Key_Up, Qt::Key_Down}` in `BlockKindDescriptor`.

### 8.4 Image

1. **ImageDelegate.qml** — renders the image from `rec.blockAttrs["src"]`; falls back to a placeholder for missing/unloadable src; alt text shown below or on hover.
2. **BlockKindDescriptor**: `BlockSelected` (default) + `BlockInternalEdit("image", "alt-edit")`.
3. **Alt-edit mode**: Enter or double-click on a focused image transitions to `BlockInternalEdit("image", "alt-edit")`; the delegate switches to a TextEdit for the alt-text string; commit via Enter, cancel via Escape.
4. **Src editing**: right-click → context menu → "Edit source URL" (part of per-block undo §11 context menu; deferred). For D3, src is set only via kind-transition detection (when user types `![alt](src)` in a paragraph, it becomes an Image block with attrs populated from the parse).

### 8.5 Why this and not the alternatives

No `LiveSpeculationLayer`: under D2's per-block CRDT, kind-transition detection in `onD2Changed` fires one event-loop spin after the triggering edit. The QML model update follows immediately on the same spin's queued events. This is fast enough that speculative pre-painting produces no user-visible benefit; the C-restoration needed speculation because its async parse could take multiple event-loop spins.

---

## 9. L7 — Structured text blocks

### 9.1 ListItem

#### 9.1.1 Data model

Each list item is a `BlockKind::ListItem` block in IdList. A "list" is a maximal run of consecutive `list-item` blocks. Tight/loose distinction and nested sublists (where one item's Buffer holds indented markdown) are deferred to a post-D3 pass.

`BlockRecord` for list items:
- `kind = "list-item"`
- `text` = source-faithful buffer content **including** the marker prefix (e.g. `- item text`). `blockText(id)` returns this verbatim. The delegate strips the marker for the editable region and shows it as visual chrome — consistent with CodeBlock (fence delimiters in buffer, shown as chrome) and HorizontalRule.
- `headingLevel` not used (0)
- `codeLanguage` not used (empty)
- Attrs read from `doc->blockAttrs(id)`: `markerStyle`, `indentLevel`, `checked`

#### 9.1.2 ListItemDelegate.qml

- Left column: marker glyph (bullet `-`/`*`/`+`, number `1.`/`1)`, or checkbox for task lists)
- Right column: `TextEdit` via `LiveEditBinding` on the item text (marker-stripped for editing)
- Indentation from `indentLevel` attr (16px per level)
- Consecutive list-item rows in the ListView are visually grouped (no dividers between them; shared background tint)

#### 9.1.3 Structural keys

Registered in `LiveStructuralKeyHandler` via `registerHandler(BlockKind::ListItem, ...)`:

| Key | Condition | Action |
|---|---|---|
| Enter | non-empty block | `d2InsertBlock(currentBlock, BlockKind::ListItem, t)` + `Cmd::changeKind` to set inherited markerStyle + indentLevel attrs — all in one transaction; cursor to new block start |
| Enter | empty block + `indentLevel > 0` | Outdent: `Cmd::changeKind` with `indentLevel - 1`; cursor stays in same block |
| Enter | empty block + `indentLevel == 0` | Exit list: `Cmd::changeKind` to Paragraph, clearing list attrs; cursor stays in block |
| Backspace at start | `indentLevel > 0` | Outdent: `Cmd::changeKind` with `indentLevel - 1` |
| Backspace at start | `indentLevel == 0` | `Cmd::backspaceMerge` (merge into previous block) |
| Delete at end | any | `Cmd::deleteMerge` (merge next block into this one) |
| Tab | any | Indent: `Cmd::changeKind` with `indentLevel + 1` (max 6) |
| Shift+Tab | `indentLevel > 0` | Outdent: `Cmd::changeKind` with `indentLevel - 1` |
| Shift+Tab | `indentLevel == 0` | No-op |

#### 9.1.4 Marker style cycling

`Cmd+Shift+L` (or right-click → cycle marker) cycles the `markerStyle` attr through `-` → `*` → `+` → back. For ordered lists, `1.` ↔ `1)`. This calls `Cmd::changeKind` with the same kind and updated markerStyle.

#### 9.1.5 Kind-transition for list items

`inferBlockKind` returns `ListItem` for text matching `^[ \t]{0,3}([-*+]|\d+[.)]) `. Kind-transition detection (§5) fires `Cmd::changeKind` with attrs: `markerStyle` extracted from the prefix match, `indentLevel = 0` (initial).

### 9.2 Blockquote

#### 9.2.1 Data model

Each `> `-prefixed block is a `BlockKind::Blockquote` block. Consecutive blockquote blocks are visually grouped (shared left border; common indented area). Nested blockquotes (`>> `) are deferred (D3 treats them as a single blockquote block whose text contains the `>` prefix).

`blockText(id)` returns the source-faithful content including `> ` prefix(es). The delegate strips the outermost `> ` for the editable region.

#### 9.2.2 BlockquoteDelegate.qml

- 3px left border in accent color; content indented 16px
- `TextEdit` via `LiveEditBinding` on the stripped text
- Consecutive rows share the visual container (no border/spacing between them)

#### 9.2.3 Structural keys

| Key | Condition | Action |
|---|---|---|
| Enter | non-empty block | `Cmd::enterAtEnd` → new Blockquote block; cursor to start |
| Enter | empty block | Exit: `Cmd::changeKind` to Paragraph; cursor stays |
| Backspace at start | any | `Cmd::backspaceMerge` |
| Delete at end | any | `Cmd::deleteMerge` |

### 9.3 Why this and not the alternatives

First-class list-item entries in IdList (per D2 premise 4) over a "one block per paragraph inside a list" model: the IdList already provides stable CRDT identity per entry; mapping a list to its items directly is natural. A "list block containing paragraph sub-blocks" would require a recursive CRDT composition that the collabtext maintainers explicitly declined (scope line item 5). Deferring sublists (item's Buffer holds indented source) is correct for D3 scope; full recursive nesting is a D3+ refinement.

---

## 10. L8 — Interactive blocks

### 10.1 BlockInternalEdit cursor variant

The `Cursor` type already includes `BlockInternalEdit{block, mode}`. `LiveCursorState::validateVariant` already checks `desc->supportedCursorVariants`. D3 adds:

- `LiveStructuralKeyHandler` gets a new dispatch path for `BlockInternalEdit` mode: F2 on a `BlockSelected` block (if the descriptor declares `BlockInternalEdit`) transitions to internal-edit mode; Escape on a `BlockInternalEdit` cursor exits back to `BlockSelected`.
- The key routing is QML-side: the delegate in `BlockSelected` state catches F2 and calls `cursorState.request(BlockInternalEdit)` via an exposed Q_INVOKABLE; in `BlockInternalEdit` state the delegate catches Escape and calls `cursorState.request(BlockSelected)`.

### 10.2 Math block

#### 10.2.1 Data model

`BlockKind::Math`. `blockText(id)` holds the LaTeX source (including delimiters: `$...$` for inline, `$$...$$` for display; or `\[...\]`/`\(...\)` — all stored source-faithfully). `attrs[AttrNames::DisplayMode]` = true if block is a display-math block.

Kind-transition detection: ordered longest-match check — text starts with `$$` → `Math` (display, `displayMode=true`); else text starts with `$` → `Math` (inline, `displayMode=false`). The `inferBlockKind` rule table (§5.1) is updated accordingly: `$$` prefix precedes `$` prefix in evaluation order.

#### 10.2.2 MathDelegate.qml

**Render mode** (`BlockSelected`):
- jkqtmathtext renders the LaTeX source to a pixmap; displayed in a QML Image element.
- Focus ring shown around the block.
- Double-click or F2 transitions to edit mode.

**Edit mode** (`BlockInternalEdit("math", "editing-latex")`):
- A `TextEdit` backed by `LiveEditBinding` becomes visible, showing the raw LaTeX source.
- Real-time re-render: a debounced (250ms) timer re-invokes jkqtmathtext and updates the preview below the TextEdit.
- Escape commits the edit and returns to render mode.
- No separate "cancel" — all edits are live CRDT operations and are undoable.

#### 10.2.3 jkqtmathtext integration

`JKQTMathTextLabel` (or direct `JKQTMathText` render-to-pixmap API) lives in `libs/jkqtmathtext` (already a project sibling). A new C++ class `MathRenderer` in `markoff-live-render/src/MathRenderer.{h,cpp}` wraps `JKQTMathText`, providing:

```cpp
class MathRenderer : public QObject {
    Q_OBJECT
public:
    explicit MathRenderer(QObject *parent = nullptr);
    // Synchronous render; returns null pixmap on parse failure.
    QPixmap render(const QString &latex, bool displayMode, qreal pointSize) const;
};
```

QML exposes this via a singleton registered in the QML module.

#### 10.2.4 Why this and not the alternatives

F2/double-click entry + Escape exit (over auto-toggling on focus): explicit entry matches the `BlockInternalEdit` design principle — a block in its own internal-edit mode fully owns key routing. Auto-toggling would require the view to decide "is this focus-in intentional for editing?" which is ambiguous.

Debounced live preview (over render-on-commit): LaTeX rendering is fast for typical expressions (< 10ms); live preview with 250ms debounce gives immediate feedback without per-keystroke rendering overhead.

---

## 11. Per-block undo UI

### 11.1 Context menu

A new `LiveContextMenu.qml` component, exposed via `LiveListModelBinding::contextMenu()` (Q_PROPERTY, `LiveContextMenu *`). It is instantiated lazily on first right-click.

Right-click on any block delegate invokes `contextMenu.showForBlock(blockAnchor, globalPos)`, which displays a `Menu {}` with:

- **"Undo in this block"** — calls `document.undoForBlock(blockAnchor)` (exposed to QML via `MarkoffDocument`'s Q_INVOKABLE). Grayed-out if no undo history exists for this block (`document.canUndoForBlock(blockAnchor)` → new Q_INVOKABLE).
- **"Undo"** — document-level undo.
- **"Redo"** — document-level redo.
- *(Separator)*
- **"Copy"** — standard.
- **"Cut"** — standard.
- **"Paste"** — standard.
- *(Block-kind-specific actions deferred to a future R9 pass.)*

### 11.2 Foundation additions

Two new `Q_INVOKABLE` methods on `MarkoffDocument`:

```cpp
Q_INVOKABLE bool canUndoForBlock(Markoff::BlockAnchor blockAnchor) const;
Q_INVOKABLE void undoForBlock(Markoff::BlockAnchor blockAnchor);
```

`canUndoForBlock` checks whether `UndoLog` has any entry whose targets include this block. `undoForBlock` calls the existing `MarkoffDocument::undoForBlock(BlockId)` (D2 API) internally.

### 11.3 Why this and not the alternatives

Right-click menu over a dedicated keyboard shortcut: right-click is discoverable without documentation; a keyboard shortcut (`Cmd+Opt+Z` or similar) can be added later as an accelerator. QML `Menu` over a custom overlay: Qt's `Menu` handles platform conventions (positioning, dismiss, accessibility) correctly. Lazy instantiation over always-present: context menus are infrequently used; lazy avoids layout overhead for every delegate.

---

## 12. Foundation amendments summary

All minimal; none change the D2 spec's architectural decisions.

| Amendment | Location | Change |
|---|---|---|
| `BlockSerializerRegistry` interface | `markoff-foundation` (new header) | §6.1 |
| `MarkoffDocument` constructor | `markoff-foundation` | add optional `const BlockSerializerRegistry *` param |
| `MarkoffDocument::serializerRegistry()` | `markoff-foundation` | new accessor |
| `MarkoffDocument::canUndoForBlock` | `markoff-foundation` | new Q_INVOKABLE |
| `MarkoffDocument::undoForBlock` (QML-visible) | `markoff-foundation` | existing API, add `Q_INVOKABLE` |
| `d2InsertBlock` / `d2RemoveBlock` | `markoff-foundation` | fire `idListProxy->notifyChanged()` after the CRDT op |
| `AttrNames` namespace + constants | `markoff-foundation` (new header) | §7 |

---

## 13. Test strategy

### 13.1 Per-layer test contract

Each layer Lₙ is tested with all L_{k<n} as real implementations and L_{k>n} absent. Existing `tst_live_render_*` suites extend naturally:

| Test executable | Layer | New coverage in D3 |
|---|---|---|
| `tst_live_render_coords` | L0 | None (unchanged) |
| `tst_live_render_block_model` | L1–L2 | ListItem / Blockquote / Math rows in diff scenarios |
| `tst_live_render_cursor` | L3 | Structural signal path (structuralRowsInserted/Removed) replaces QAbstractItemModel path; merge cursor delivery; BlockInternalEdit transitions |
| `tst_live_render_paragraph_edit` | L4 | None (unchanged) |
| `tst_live_render_structural` | L5–L6 | Level-change gesture; hr/image navigation; CodeBlock Tab; lang-tag edit |
| `tst_live_render_structural` (extended) | L7 | ListItem Enter/Backspace/Tab/outdent; Blockquote Enter/exit |
| `tst_live_render_math` (new) | L8 | Math kind-transition; BlockInternalEdit entry/exit; render/edit mode round-trip |
| `tst_live_render_kind_transition` (new) | L5 infra | inferBlockKind rule coverage; one-spin kind-change loop terminates |
| `tst_live_render_context_menu` (new) | UI | canUndoForBlock gating; undoForBlock dispatch |

### 13.2 QML integration tests

The existing `tst_live_view_qml` tests (currently 3 pre-existing failures noted in dogfood, unrelated to D2) are extended with:

- Math render mode → edit mode → render mode round-trip (visual diff of jkqtmathtext output vs. stored pixmap).
- List-item structural keys via simulated key events.
- Context menu appearance and "Undo in this block" action.

### 13.3 Kind-transition idempotency

A dedicated test asserts: any sequence of `Cmd::changeKind` calls triggered by kind-transition detection converges in ≤ 2 `d2DocumentChanged` cycles (the cycle guard: if after applying a `changeKind` the re-evaluation produces the same kind, no further `changeKind` is called).

---

## 14. Open questions

No blockers to spec approval. All deferrable.

| # | Question | Phase |
|---|---|---|
| Q1 | Sublist rendering (indent levels > 0 with visual nesting of list-item runs) | D3 post-ship |
| Q2 | Image src editing UX (inline text field vs. modal dialog) | D3 post-ship |
| Q3 | Math block — error display when LaTeX fails to parse (red border? raw source shown?) | D3 implementation-time |
| Q4 | Marker-style cycling keyboard shortcut (`Cmd+Shift+L`) — confirm key binding | D3 implementation-time |
| Q5 | Blockquote nested level support (stripping outer `>` exposes inner `>`) | D4/later |
| Q6 | `BlockKindDescriptor::serializer` callback signature — confirm matches `BlockSerializerRegistry::serialize` | D3 implementation-time |

---

## 15. References

### D-arc

- `docs/d-arc/2026-05-04-d-arc-roadmap.md` — orientation
- `docs/d-arc/d-arc-status.md` — status board (D3 active after this spec lands)
- `docs/d-arc/collabtext-scope-line.md` — six "won't do" items

### Antecedent designs

- `docs/specs/2026-05-04-d2-foundation-reshape-design.md` — D2 spec (D3 consumes its API)
- `docs/specs/2026-05-02-live-render-restoration-design.md` — C-restoration spec (L0–L3 carry-forward authoritative source)
- `docs/specs/2026-05-04-d3-view-layer-adaptation-STUB.md` — superseded stub

### Future D-arc designs (stubs)

- `docs/specs/2026-05-04-d4-parser-scope-reduction-STUB.md`
- `docs/specs/2026-05-04-d5-collab-activation-STUB.md`

---

*End of D3 spec. Implementation plan to follow via `superpowers:writing-plans` skill.*
