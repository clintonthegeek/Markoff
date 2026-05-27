# `markoff-core` single-document binding robustness — design

**Date:** 2026-05-27
**Status:** Draft, awaiting plan
**Branch:** `master`
**Predecessors:**
- `docs/specs/2026-05-26-markoff-styled-leaf-design.md` (the styled leaf)
- `docs/specs/2026-05-27-markoff-styled-dogfood-fixes-design.md` (v0.1 dogfood fixes)

## 0. Context

`markoff-core` already serves two render models over one source of truth (the
per-block D2 CRDT):

- **Per-block model** — `markoff-live`. Each block is its own QML delegate +
  `QTextDocument`; edits are scoped to a known block via
  `MarkoffDocument::d2ApplyBufferEdit(blockId, …)`. Structural changes (Enter,
  backspace-at-start) are explicit ops (`d2InsertBlock` / `d2RemoveBlock`) in
  `LiveStructuralKeyHandler`. The `\n\n` block separators don't exist as
  editable text anywhere.
- **Single-document model** — `markoff-source`, `markoff-styled`. One
  `QTextDocument` holds the whole `flatView()` (blocks joined by `"\n\n"`).
  Edits flow through `SourceTextDocumentBinding` →
  `MarkoffDocument::applyFlatEdit(globalStart, globalEnd, …)`, which must
  *reverse-derive* which block(s) a global byte range touches.

Dogfooding `markoff-styled` on a real document surfaced three defects, all
rooted in the single-document model's flat↔block translation:

1. **Block-boundary insertion drift.** Typing exactly at a `\n\n` boundary:
   QTextEdit appends the char to the end of block N (before the separator),
   while `applyFlatEdit`'s cursor-edit branch (`MarkoffDocument.cpp:1465`,
   `cursor == oldStart`) prepends it to the start of block N+1. The CRDT and
   the QTextDocument diverge. (Confirmed: `"spec. \n\nMarkoff"` in the
   QTextDocument vs `"spec.\n\n Markoff"` in `flatView()`.)
2. **`setPlainText` destroys view state.** When `toPlainText() != flatView()`,
   `onD2DocumentChanged` fires `m_textDocument->setPlainText(flatView())`,
   which wipes all `QTextCharFormat`/`QTextBlockFormat` and resets the cursor
   to end-of-document. In `markoff-styled` this manifested as "all styling
   disappears and the caret leaps to the bottom when I type." (Drift from
   defect 1 is what makes this fire on local typing.)
3. **Backspace across a separator doesn't merge blocks** (documented gap at
   `SourceTextDocumentBinding.cpp:311-317`).

A three-point verification (read-only investigation + throwaway probes,
2026-05-27) established that the **bimodal foundation is sound**:

- The two edit ingresses are disjoint over one per-block CRDT truth; neither
  caches a competing representation (the legacy flat buffer is dead for D2).
- **Canonical input is a genuine `loadFromMarkdown → flatView()` identity**
  (`"alpha\n\nbeta"` round-trips byte-for-byte).
- **Cursor/session state is portable by construction** — `Selection` stores
  `TextAnchor`s (CRDT byte-anchors carrying `BlockId`, no byte offsets), proven
  to stay logically pinned across edits issued from *either* mode.
- The **only** way the single-document path degrades shared state is by
  producing **non-canonical structure** (internal `\n`, empty blocks). That
  corrupts *canonicality*, not *correctness* — live still reads such blocks
  fine. Normalization closes that gap, and `applyFlatEdit` is the single
  chokepoint.

This spec makes the single-document binding robust enough to be a first-class
peer of the per-block model.

## 1. Goals and non-goals

**Goals:**

- Typing at a block boundary lands where the user typed it (no teleport, no
  drift).
- Local editing never triggers a `setPlainText` wipe; formatting, cursor, and
  scroll survive every edit.
- Remote edits (collab peer, programmatic `resetContent`, external reload) sync
  into the QTextDocument incrementally, preserving formatting/cursor/scroll
  outside the changed span.
- After any `applyFlatEdit`, block structure is canonical (the invariant in
  §2). The two render models therefore agree on what the document is.
- Backspace across a `\n\n` separator merges the two blocks.
- `markoff-source` inherits all of the above (shared binding) with no test
  regressions.

**Non-goals:**

- Moving kind inference (Heading/List/Quote-from-prefix) into core. It stays
  view-side (already working in both leaves) and composes with structural
  normalization. New blocks get a default Paragraph kind; the view refines it.
- Re-introducing incremental parsing. D4 deleted `ParsePool` /
  `IncrementalParseSession`; normalization is structural (no parser), honoring
  that architecture.
- Changing `applyFlatEdit`'s deliberate next-block bias at `:1465` (paste /
  clipboard depends on it; `tst_live_render_paste_kind_roundtrip` locks it).
- Touching the per-block path (`d2ApplyBufferEdit`, `LiveEditBinding`,
  `LiveStructuralKeyHandler`). Live is untouched.
- CommonMark exactness beyond the structural invariant. Exotic inputs that a
  full parser would normalize differently are out of scope; round-tripping
  through a reload re-parses authoritatively.

## 2. The canonical invariant (write it down)

> **After any `applyFlatEdit` returns, the document is in canonical block
> structure:**
> 1. No block buffer contains an internal `\n`.
> 2. No zero-length (empty) blocks exist, except the single Paragraph block of
>    a genuinely empty document.
> 3. Blocks are separated by exactly one `"\n\n"` in `flatView()`.
>
> Consequence: for canonical input, `loadFromMarkdown(x) → flatView()` is an
> identity, and a styled/source view that applies normalize-on-edit stays on
> the canonical manifold the per-block (live) model also lives on. The two
> models cannot disagree about block structure.

This invariant is the spec's central assertion. It is enforced **only** on the
`applyFlatEdit` ingress (the single-document path), never on
`d2ApplyBufferEdit` / `d2InsertBlock` (the per-block path), so live's
intentional empty paragraph (created by SOB/EOB Enter in
`LiveStructuralKeyHandler`) is never normalized away.

## 3. Architecture — two files, three changes

| # | Change | File / function | Direction |
|---|---|---|---|
| 1 | Boundary attribution (sep-view resolution + explicit-block dispatch) | `SourceTextDocumentBinding::onQtContentsChange` | forward |
| 2 | Normalize-on-edit (structural, no reparse) | `MarkoffDocument::applyFlatEdit` | forward |
| 3 | Incremental reverse sync (common-prefix/suffix text-diff) | `SourceTextDocumentBinding::onD2DocumentChanged` | reverse |

A shared helper, `findBlockAtSepByte(doc, sepByte, biasForward)`, is promoted
from `markoff-source`'s format-op code (`Editor.cpp:226-250`) into the binding
(or a binding-adjacent core helper) so both leaves use one implementation.

## 4. Change #1 — boundary attribution

### 4.1 Why the offset alone can't fix it

In **no-separator** coordinates (what `applyFlatEdit` consumes), block N ends
at byte X and block N+1 starts at byte X — the *same* byte. No offset
distinguishes "end of N" from "start of N+1." The information is destroyed once
the position is collapsed to no-sep space. Therefore the fix must operate in
**separator-view** coordinates, where the `\n\n` occupies distinct positions
`[endN, endN+2)`: `qtPos == endN` is unambiguously end-of-N; `qtPos == endN+2`
is start-of-N+1.

### 4.2 Mechanism

`onQtContentsChange` (forward path) gains a dispatch:

1. Compute the sep-view byte offset of the edit start from `qtPos` (existing
   `qtPosToByteOffset`).
2. Resolve to `(block, offsetWithinBlock)` via
   `findBlockAtSepByte(doc, sepByte, /*biasForward=*/false)`. With
   `biasForward=false`, a boundary position resolves to the **end of the
   previous block** — matching what QTextEdit did visually.
3. **Dispatch:**
   - **Single-block, structure-neutral edit** (the common case — typing or
     deleting within/at the end of one block, inserted text contains no `\n`,
     range doesn't cross a block boundary): call
     `doc->d2ApplyBufferEdit(block, offsetWithinBlock, removedBytes,
     insertedBytes, transaction)` **directly**, with the block named
     explicitly. The no-sep ambiguity never arises.
   - **Structural or block-spanning edit** (inserted text contains `\n`; or the
     removed range crosses a `\n\n`; or a multi-block paste): route to
     `doc->applyFlatEdit(globalStart, globalEnd, newText, origin)`, which owns
     the structural decomposition + normalization (#2).

### 4.3 Effect

- Typing a space at end-of-block lands in that block (`"spec. "`), not the next
  (`" Markoff"`). No drift; the reverse-path equality check holds; no
  `setPlainText`.
- `applyFlatEdit`'s `:1465` next-block bias is never reached by typing (it only
  sees structural / spanning / paste edits), so paste semantics and
  `tst_live_render_paste_kind_roundtrip` are unaffected.

## 5. Change #2 — normalize-on-edit (structural, in `applyFlatEdit`)

After the buffer mutation, `applyFlatEdit` enforces §2's invariant using
existing D2 primitives only — no parser invocation, BlockIds preserved for
unaffected blocks.

### 5.1 Rules

- **Split internal newlines.** If an inserted run contains `\n`, split at each
  newline-run into block boundaries via `d2InsertBlock`, rather than leaving a
  `\n` in a block buffer. Collapse a run of ≥1 consecutive newlines to a single
  boundary (so `"\n\n\n\n"` produces one boundary, not empty blocks). This
  replaces the current `"\n\n"`-only split logic (`:1531-1542`, `:1589-1600`)
  with split-on-any-newline-run.
- **Drop empty blocks.** Any block left zero-length by the edit — including the
  empty seeds the current split loops can create (`:1556`, `:1615`) — is
  removed via `d2RemoveBlock`. Exception: a genuinely empty document keeps its
  single Paragraph block.
- **Merge on separator deletion (closes the `:311-317` gap).** When the removed
  range spans a `\n\n` separator (deleting the boundary between block N and
  N+1), the two blocks merge: N+1's surviving content is appended to N's buffer
  and N+1 is removed via `d2RemoveBlock`. This falls out of the same structural
  pass.
- **Default kind for new blocks.** Blocks created by a split get
  `BlockKind::Paragraph`. View-side kind-inference refines it on the next
  `d2DocumentChanged` (`Cmd::changeKind`), as it does today.

### 5.2 Scope guard

Normalization runs **only on `applyFlatEdit` return paths**.
`d2ApplyBufferEdit` and `d2InsertBlock`/`d2RemoveBlock` called directly (the
per-block path) do not normalize. This protects live's intentional empty
paragraph.

### 5.3 BlockId / anchor preservation

Splits create new BlockIds for the *new* tail blocks; the original block keeps
its ID. Merges remove the absorbed block's ID; the survivor keeps its ID.
Unaffected blocks are never touched. `TextAnchor`s into surviving blocks remain
valid (they carry `BlockId` + CRDT byte position). This is what keeps
session/cursor state stable across normalization.

## 6. Change #3 — incremental reverse sync

`onD2DocumentChanged` stops calling `setPlainText`.

### 6.1 Mechanism

```cpp
void SourceTextDocumentBinding::onD2DocumentChanged()
{
    if (m_applyingLocalEdit) return;
    if (!m_textDocument || !m_subscribedDoc) return;

    const QString expected = QString::fromUtf8(m_subscribedDoc->flatView());
    const QString actual   = m_textDocument->toPlainText();
    if (actual == expected) return;                       // common case: no-op

    // Minimal contiguous diff: longest common prefix + suffix.
    int p = 0;
    const int maxP = std::min(actual.size(), expected.size());
    while (p < maxP && actual[p] == expected[p]) ++p;
    int s = 0;
    while (s < (std::min(actual.size(), expected.size()) - p)
           && actual[actual.size()-1-s] == expected[expected.size()-1-s]) ++s;
    // (Clamp p/s so they never split a surrogate pair.)

    m_applyingRemoteEdit = true;
    QTextCursor c(m_textDocument);
    c.setPosition(p);
    c.setPosition(actual.size() - s, QTextCursor::KeepAnchor);
    c.insertText(expected.mid(p, expected.size() - s - p));
    m_applyingRemoteEdit = false;
}
```

### 6.2 Properties

- **Formatting outside the changed span is preserved** — the QTextDocument
  keeps char/block formats on untouched characters. Only the differing span is
  replaced.
- **Cursor doesn't jump to end** — QTextCursor positions auto-adjust across the
  edit; the existing `syncFromSession()` anchor restore handles edits that land
  before the caret.
- **Scroll is untouched.**
- **Composes with the StyleApplier hash gate** — after the targeted edit, the
  StyleApplier's `d2DocumentChanged` subscription re-styles only the block(s)
  whose content hash changed; untouched blocks keep their formatting. No
  flicker, no full restyle.
- **Surrogate safety** — `p`/`s` clamped to not split a UTF-16 surrogate pair.
- **Degenerate full-replacement** (no shared prefix/suffix, e.g. `resetContent`
  to wholly different content) collapses to one whole-document span applied via
  `insertText` over a selection — still better than `setPlainText` (preserves
  the editing model; lets the hash gate restyle).

## 7. Data flow + cycle correctness

**Local edit (forward):**
```
keystroke → QTextDocument::contentsChange
  → onQtContentsChange  (m_applyingLocalEdit = true)
      → resolve sep-view block; dispatch:
          single-block  → d2ApplyBufferEdit(block, …)
          structural    → applyFlatEdit(…) → normalize (canonical)
      (m_applyingLocalEdit = false)
  → debounced d2DocumentChanged
      → onD2DocumentChanged: toPlainText() == flatView() → RETURN (no-op)
      → StyleApplier::onD2Changed → hash-gated restyle of changed block(s)
```
The forward fix keeps the QTextDocument and CRDT in lockstep, so the reverse
path is a no-op on local edits. No `setPlainText`, no wipe, no caret jump.

**Remote edit (reverse):**
```
remote/programmatic mutation → d2DocumentChanged
  → onD2DocumentChanged: toPlainText() != flatView()
      → common-prefix/suffix diff → QTextCursor.insertText (targeted span)
        (m_applyingRemoteEdit guards the echo)
  → StyleApplier::onD2Changed → hash-gated restyle of changed block(s)
```

**Guards:** `m_applyingLocalEdit` (forward, suppresses synchronous echo) and
`m_applyingRemoteEdit` (reverse, suppresses the `contentsChange` from
`insertText` looping back into `onQtContentsChange`) — both already exist; their
roles are unchanged. No new re-entrance guards introduced.

## 8. `markoff-source` impact

The binding is shared, so `markoff-source` gets all three changes. Expected
effects:

- **Improvement:** boundary typing no longer drifts; remote edits (Corbomite
  external-reload, collab) no longer reset the cursor; backspace merges blocks.
- **Risk:** behavior change in a shared component. Mitigation: the full
  `tst_source_widget_*` suite must stay green, plus a new boundary-typing test
  on the source binding (none exists today — `tst_source_widget_binding_roundtrip`
  only types into a fresh empty doc). Source's format-op helper
  `findBlockAtSepByte` is the donor for the promoted shared helper; verify the
  promotion doesn't change format-op behavior.

## 9. Testing

All `markoff-core` tests run headless; styled/source widget tests under
`QT_QPA_PLATFORM=offscreen` via `scripts/run-tests.sh`.

### 9.1 Core: the canonical invariant (`tst_d2_normalize_on_edit`)

New `markoff-core` test binary. Each slot loads a doc, applies a flat edit, and
asserts §2's invariant on the result:

- **Boundary insert** — `"alpha\n\nbeta"`, insert `"X"` at the end-of-alpha
  boundary → block 0 == `"alphaX"`, block 1 == `"beta"` (NOT `"Xbeta"`).
- **Newline split** — insert `"\n\n"` mid-block → splits into two blocks, no
  internal `\n`, no empty block.
- **Newline-run collapse** — insert `"\n\n\n\n"` → one boundary, no empty
  blocks; `flatView()` has a single `"\n\n"` there.
- **Empty-block suppression** — an edit that would leave a zero-length block
  removes it.
- **Separator-delete merge** — `"alpha\n\nbeta"`, delete the `"\n\n"` → one
  block `"alphabeta"`.
- **Identity round-trip** — `loadFromMarkdown(x) → flatView() == x` for several
  canonical `x`; and after an identity edit, `flatView()` is stable.
- **Anchor stability** — a `TextAnchor` into a surviving block resolves to the
  same logical character after a split/merge elsewhere.

These are falsifiable invariant tests (break the normalization in a stub → they
must fail), per `docs/INVARIANTS.md` §4.

### 9.2 Core: paste regression guard

`tst_live_render_paste_kind_roundtrip` (existing) must stay green — proves
`applyFlatEdit`'s next-block bias is untouched.

### 9.3 Binding: boundary typing through the binding (`tst_*_binding_boundary`)

New slots (styled and/or source) that type a character at an inter-block
boundary *through the QTextEdit* (not by calling `applyFlatEdit` directly) and
assert `flatView()` matches `toPlainText()` afterward — the regression guard
that was missing for the original bug.

### 9.4 Binding: incremental reverse sync

- A remote `applyFlatEdit` (or `d2ApplyBufferEdit`) that changes one block →
  the QTextDocument updates, the cursor does not jump to end, and formatting on
  other blocks is preserved (styled-side check: a heading block keeps its size
  after a remote edit to a different paragraph).
- `resetContent` to wholly different text → QTextDocument matches, no crash.

### 9.5 Styled integration

- The `tst_styled_*` suite stays green.
- New: type a space at a block boundary in a multi-block styled doc → no
  formatting wipe, no caret leap, scroll preserved (the end-to-end guard for
  the original dogfood report).

### 9.6 Source regression

Full `tst_source_widget_*` suite green; new boundary-typing slot added (§8).

## 10. Risks and open questions

- **Forward-path dispatch complexity.** Deciding "single-block structure-neutral
  vs structural/spanning" must be correct at the edges (e.g., a paste of plain
  text with no newline into the middle of a block is single-block; the same
  with a newline is structural). The plan will enumerate the dispatch
  conditions precisely. Risk: a missed case routes a structural edit to
  `d2ApplyBufferEdit`, leaving an internal `\n`. Mitigation: the §9.1 invariant
  test on `flatView()` catches any internal `\n` regardless of path — but note
  that direct `d2ApplyBufferEdit` does NOT normalize, so the dispatch must be
  right. **Open question for the plan:** should `d2ApplyBufferEdit` from the
  binding also assert-no-newline in debug builds, to fail loudly if the
  dispatch misroutes? (Recommend yes.)
- **`findBlockAtSepByte` promotion.** Moving it out of `markoff-source` must not
  change source format-op behavior. Verify with source's format-op tests.
- **Common-prefix/suffix diff minimality.** For a remote edit that changes two
  far-apart regions, the diff collapses to one big span (correct but
  over-replaces, briefly losing formatting on the spanned-but-unchanged middle
  until the hash gate restyles). Acceptable for v1; a multi-span diff is a
  future optimization if collab pressure shows it matters.
- **Backspace-merge + kind.** When two blocks merge, the survivor keeps its
  kind. If a Heading and a Paragraph merge (delete separator between `# H` and
  `body`), the result is one block with text `"# Hbody"` kept as the survivor's
  kind; view-side kind-inference re-evaluates. Confirm this matches user
  expectation in dogfood; not a correctness issue.
- **`tst_styled_d2_integration` reverse-path slots** currently assume
  `setPlainText` semantics in places; they may need updating to the incremental
  model (rename-to-match-new-shape per the project's test-evolution convention,
  not retrofit).

## 11. References

- Bug + verification provenance (this session):
  - Boundary bug confirmed via `[Binding] setPlainText FIRING` diagnostic.
  - Three-point bimodal verification report (normalization load-time-only;
    canonical round-trip identity; anchor portability proven).
- Key code:
  - `libs/markoff-core/src/SourceTextDocumentBinding.cpp` —
    `onQtContentsChange` (`:353`), `onD2DocumentChanged` (`:383`),
    `sepViewToNoSepByte` (`:318`), backspace-merge gap (`:311-317`).
  - `libs/markoff-core/src/MarkoffDocument.cpp` — `applyFlatEdit` (`:1410`),
    cursor-edit boundary bias (`:1465`), split loops (`:1531`, `:1589`),
    empty-seed creation (`:1556`, `:1615`), `d2ApplyBufferEdit` (`:1318`),
    `flatView` (`:2085`), `serializeForSave` (`:1998`).
  - `libs/markoff-source/src/Editor.cpp` — `findBlockAtSepByte` donor
    (`:226-250`), format-op bypass rationale (`:205-213`, `:522-531`).
  - `libs/markoff-live/src/LiveStructuralKeyHandler.cpp` — per-block structural
    Enter (`:311`), intentional empty paragraph (`:268-289`); the path this
    spec must NOT disturb.
  - `libs/markoff-live/tests/tst_live_render_paste_kind_roundtrip.cpp` — the
    locked next-block-bias test.
- Architecture: `libs/markoff-core/CLAUDE.md` (D4 removed incremental parse;
  `serializeForSave` is canonical egress), `docs/INVARIANTS.md` (§4 falsifiable
  invariant tests; §6/§7 smells).
- Bimodal consistency caveats for the implementer:
  1. `MarkoffDocument::blockAt(TextAnchor)` is `latestBlockRanges`-broken under
     D2 — use `anchor.block()`.
  2. Flat-byte→anchor biases to the earlier block at boundaries — use the
     block-relative `textAnchorAt(blockId, 0, …)` overload for "start of block
     N".
  3. #1 and #2 share `applyFlatEdit`/its callers — sequence them together.
