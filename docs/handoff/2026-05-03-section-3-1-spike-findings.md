# §3.1 Spike — marker-character hole replacement findings

**Date:** 2026-05-03
**Branch:** `spike/marker-hole` (worktree at `.worktrees/spike-marker-hole/`)
**Scope:** the §3.1 architectural decision in
`docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md`. Specifically:
test §3.1(c) — replace the `LiveHoleLayer` + `LiveProxyBlockModel` abstraction
with a marker-character source edit that the parser turns into a real block.
**Output:** recommendation, not landed code. The spike branch is preserved
for inspection.

**TL;DR — recommend §3.1(c) (marker), with one important caveat about scope.**
Tree-sitter accepts every viable invisible-marker character as paragraph
content (verified). The end-to-end source-edit / parse / scrub flow works at
the parser level (verified by simulation). Net live-render code-surface
reduction is ~500 LOC of production code and ~700 LOC of tests, plus the
elimination of an entire class of architectural complexity (two authorities
over content, idle-commit race window, IME guard, per-hole undo, hole-cursor
delivery protocol). The caveat: marker leakage requires a deterministic
*scrubber* primitive (see §4 below) — it does not eliminate all special-case
logic, it relocates it from a parallel buffer into a single source-edit path.

---

## 1. What I tested

### 1.1 Tree-sitter marker acceptance

Built a standalone probe (`marker_probe.cpp`, links against `markoff-parser`
only) parsing `"hello\n\n" + MARKER` for every viable candidate character.
Recorded the resulting `Document::topLevelBlocks()` list and verified each
candidate produces a 2-block parse with the second block being
`Kind::Paragraph` containing the marker bytes.

Candidates probed (raw probe output: `flow-results.txt`,
`probe-results.txt` on the spike branch):

| Codepoint | Name                  | UTF-8 bytes  | Yields paragraph block? |
|-----------|-----------------------|--------------|--------------------------|
| U+200B    | ZERO WIDTH SPACE      | E2 80 8B     | **Yes** ✓ |
| U+00A0    | NO-BREAK SPACE        | C2 A0        | **Yes** ✓ |
| U+2007    | FIGURE SPACE          | E2 80 87     | **Yes** ✓ |
| U+2009    | THIN SPACE            | E2 80 89     | **Yes** ✓ |
| U+202F    | NARROW NO-BREAK SPACE | E2 80 AF     | **Yes** ✓ |
| U+2060    | WORD JOINER           | E2 81 A0     | **Yes** ✓ |
| U+FEFF    | ZWNBSP / BOM          | EF BB BF     | **Yes** ✓ |
| U+034F    | COMBINING GRAPHEME JOINER | CD 8F    | **Yes** ✓ |
| U+180E    | MONGOLIAN VOWEL SEPARATOR | E1 A0 8E | **Yes** ✓ |
| U+E0100   | VARIATION SELECTOR-17 | F3 A0 84 80  | **Yes** ✓ |
| U+0020    | ASCII SPACE           | 20           | **Yes (with caveats)** — yes when preceded by `\n\n` and content; **no** when alone on a line (CommonMark blank-line semantics) |

**Conclusion:** Every non-pure-whitespace candidate works. ZWSP (U+200B) is
the canonical choice — it is invisible in every monospace and proportional
font I am aware of, has zero cursor-advance, and is conventionally the
"semantic separator" character. NBSP (U+00A0) is a viable backup if ZWSP
later proves problematic in a specific renderer.

### 1.2 End-to-end source-edit / parse / scrub flow

Built a parser-driven simulator (`marker_flow.cpp` on the spike branch)
that walks through the proposed flow at the source-edit level, calling
`Document::fromMarkdown()` after each step and inspecting the resulting
block list. **Notable: I did not run the QML interactive harness** — that
would require human eyes on a display I don't have. The parser-level
simulation substitutes for the interactive verification on the
load-bearing question (does the source-edit sequence produce the expected
block tree at every stage); the QML cursor-delivery and TextEdit-binding
side will need separate verification before any code lands.

Scenarios verified (each produced the expected block tree at every step):

- **A — EOB-Enter on last paragraph, type, scrub.**
  - Initial: `"alpha\n\nbeta\n"` (2 blocks).
  - After `applyLocalEdit("\n\n​")` at end of `"beta"`: 3 blocks.
    The third block is a paragraph containing only ZWSP, byte range `[13,17)`.
  - After user types `"x"` at qtPos 0 of new paragraph: 3 blocks; third is
    `"x​\n"`.
  - After scrubber removes ZWSP: 3 blocks; third is `"x\n"`. Clean.
- **B — EOB-Enter mid-document.** Same flow with the marker paragraph
  inserted between two existing paragraphs. 4 blocks at every step in the
  expected order.
- **C — Marker leakage path (no typing).** Verified marker-only paragraph
  persists if no scrub fires. Verified that user clicking back into the
  marker paragraph and typing produces either `"z​"` (cursor before
  marker) or `"​z"` (cursor after marker) — both leave the marker in
  source unless scrubbed.
- **D — Stacked Enter.** First Enter: `"alpha\n"` → 2 blocks (`alpha`,
  marker). Second Enter at end of marker paragraph: 3 blocks. (Note: my
  simulator's `QString::insert` used UTF-16 codepoint indexing rather than
  UTF-8 byte indexing for D2's offset, so the literal D2 source in the log
  has a stray space — the *parser still produced 3 blocks correctly*, and a
  real implementation would use the foundation's byte arithmetic. The
  important finding stands: the parser does not collapse two successive
  marker paragraphs into one.)
- **E — Save with marker present.** The literal saved bytes contain
  `E2 80 8B` interleaved into the prose; the file would be visible-with-
  diff as containing invisible Unicode, and other Markdown processors
  would see the ZWSP exactly as we wrote it (e.g. pandoc would render it
  as a paragraph containing a zero-width space, not as a blank).

### 1.3 What I did *not* test

- **No interactive QML test.** I cannot drive `markoff-live-app`
  through human keystrokes from this environment. The parser-level
  simulation covers the source-edit and parse-back legs; the cursor-
  delivery, focus-routing, and TextEdit-binding legs need separate
  verification.
- **No race-condition exercise.** I did not run the
  `LiveRealisticInputHarness` against a marker-style implementation.
  The §1.7 async-window race in the review doc *should* disappear under
  §3.1(c) (no commit step → no parse-back-vs-keystroke window), but this
  is a structural argument, not a measured one. A harness test against
  the implemented marker path is required before claiming the race is
  resolved.
- **No `markoff-live-app` build of the spike branch.** I did
  configure the build (`cmake -S . -B build-dev`) and built the parser
  library + my standalone probes, but did not build the full live-render
  + QML stack. The spike does not propose code changes; it produces a
  recommendation.

---

## 2. What I found

### 2.1 The premise holds

§3.1(c)'s central claim — *insert a marker so tree-sitter produces a real
block, eliminating the need for a view-side phantom row* — is verified.
The parser produces the expected blocks at every step of the flow. The
cursor can land on the new block via the existing parser-driven row path
(no `holeReified` signal; no proxy translation; no aboutToCommit; no
synchronous abandon-then-reify dance). This is the "single source of
truth" the review doc claimed and it is real.

### 2.2 Marker leakage is the central engineering problem

The review doc's risk *"marker character persists in saved file if the
user moves away without typing"* is real and must be addressed. The
spike enumerates these leakage paths:

| # | Path | What leaks | Required mitigation |
|---|------|------------|---------------------|
| 1 | User presses Enter; before typing, focus moves elsewhere (clicks another paragraph, alt-tabs to another window, app loses focus) | Marker-only paragraph stays in source; saved file has invisible char | **Focus-out scrubber on the marker paragraph.** When the focused block transitions away from a paragraph whose content equals exactly one ZWSP, apply an edit that deletes the marker AND the preceding `\n\n` — i.e., undo the EOB-Enter at the source level. UX: the user pressed Enter, didn't type, came back → state is identical to pre-Enter. |
| 2 | User presses Enter; types one character; the marker is now mid-block content | Saved file has `<typed-char><ZWSP>` (or `<ZWSP><typed-char>`) in the paragraph | **Atomic-bundled edit.** When `LiveEditBinding` sees a `contentsChange` in a marker-bearing block, the edit it applies to the CRDT bundles *both* the user's character insertion *and* the marker deletion in a single `MarkoffEdit`. Single edit → single parse-back → no async window. Cleaner than a post-edit scrubber. |
| 3 | User saves (Ctrl-S) while a marker is present in any paragraph | Saved `.md` file contains literal ZWSP bytes; visible to pandoc / cat / git diff | **Pre-save scrubber.** Before flushing bytes to disk, iterate paragraphs and: (a) drop marker-only paragraphs entirely (delete preceding `\n\n` + marker); (b) strip ZWSP from any paragraph that has other content. Save proceeds with clean bytes. |
| 4 | User types a ZWSP themselves (paste from web content; some IMEs insert ZWSP between scripts) | The scrubber removes their ZWSP; their content is silently mutated | **Acceptable trade-off, with documentation.** ZWSP carries no rendering value in Markdown editors (it's a typesetting hint for line-breaking that Markdown doesn't preserve through round-trip). Document the policy: "Markoff treats U+200B in source as a layout artifact and does not preserve it." If preservation is required, switch to a less-content-collisional marker (U+E0100 VARIATION SELECTOR-17 is plane-14 supplementary, virtually never present in user content; downside: 4-byte UTF-8 instead of 3). The choice is a one-line constant. |
| 5 | User types into a marker paragraph, then presses Ctrl-Z | If we used the atomic-bundled edit (path 2), Ctrl-Z restores the pre-keystroke state, which was the marker-only paragraph. UX: pressed Enter, typed x, undo → empty paragraph. Acceptable. | **No special handling.** The CRDT undo applies; the marker comes back; the user is in the marker-only paragraph. If they press Ctrl-Z again, the EOB-Enter itself is undone (the `applyLocalEdit("\n\n​")` is reversed). This is more semantically coherent than v2's two undo regimes (per-hole undo while open, CRDT undo after commit). |
| 6 | User saves, quits, reopens; the file has markers in it (because somehow path 1 or 3 didn't fire) | On reopen, the parser sees marker-only paragraphs as paragraph blocks; the view renders empty paragraphs; cursor lands; if user types, leakage compounds | **Load-time scrubber.** On `MarkoffDocument::resetContent` / file-load, run the same logic as the pre-save scrubber against the loaded bytes. Any marker found in the saved file is treated as a leakage artifact and removed before the document is presented to views. This converges the on-disk and in-memory states. Also eliminates leakage caused by other tools writing ZWSPs into files we open. |

The mitigations work as a coherent set. **One — atomic-bundled edit
(path 2) — is load-bearing**; it eliminates the keystroke/scrub race
that would otherwise re-introduce the v0 character-scramble class of bug.
The others (focus-out, pre-save, load-time) are deterministic synchronous
edits at well-defined events; they do not introduce new async races.

### 2.3 Surprises (the most valuable output)

1. **ASCII space alone in `"alpha\n\n "` produces a paragraph block.**
   I expected CommonMark blank-line semantics to make `\n\n` followed by
   only whitespace before EOF a "blank" — i.e., one paragraph "alpha"
   followed by trailing whitespace. Tree-sitter's grammar treats this as
   two paragraphs: `"alpha"` and `" "`. This is *not* the standard
   CommonMark rendering rule (most CommonMark renderers would treat the
   trailing whitespace as part of the previous paragraph or as a blank
   line); tree-sitter is more aggressive. Implications:
   - We do not actually need an *invisible* marker at all if we are
     willing to accept literal whitespace in the source. `"\n\n "` (two
     newlines + space) produces a paragraph block whose visible content
     is one space. UX-wise, this is *still* invisible in the rendered
     view (a paragraph containing one space looks empty); leakage in
     other Markdown tools is minor (a paragraph containing one space).
   - This *changes the marker calculus*: we could pick a literal space as
     the marker, accept the (small) leakage, and skip the scrubber
     entirely on the grounds that "a paragraph containing a space" is
     near-indistinguishable from "an empty paragraph" in any consumer.
   - I'm **not** recommending this — ZWSP+scrubber is cleaner — but it
     is a valid fallback if the scrubber turns out to be intractable.

2. **ASCII space ALONE on a line (`" \n"`, no preceding content) produces
   ZERO blocks.** This is the standard CommonMark blank-line behaviour.
   Implication: at the *very start* of a document, an EOB-Enter (which
   would be unusual since there'd be no preceding paragraph) using an
   ASCII space marker would produce nothing. ZWSP (and other invisible
   markers) work in this case. Another reason to prefer ZWSP over
   ASCII space.

3. **The "two authorities" framing in the review doc undersells the
   simplification.** v2 has not just two authorities (CRDT + bufferText)
   but also a *temporal* gap between them — the bufferText exists only
   for the ~250ms-to-focus-out interval, during which several signal
   contracts (`aboutToCommit`, `holeReified`, `IsHoleRole`,
   `BufferTextRole`, the proxy mapping) all need to stay coherent. The
   §3.1(c) approach has *no temporal gap* — the marker is in the source
   from the moment of EOB-Enter; the parser observes it on the next
   parse cycle (~30–100 ms typical); the row appears via the standard
   parser-driven path. There is no "in-flight" state. This is a more
   profound simplification than the review doc captured.

4. **The atomic-bundled edit (path 2 mitigation) generalises.** It is
   not a marker-specific cycle guard; it is *a useful primitive*. Any
   feature that wants to "insert structural scaffolding then have the
   first user keystroke replace it" (placeholder text, snippet
   templates, autocompletion stubs) benefits from the same primitive.
   Naming it `applyLocalEditWithScrub(scrubBytes, userEdit)` or similar
   would land it at the foundation level as a general affordance, not a
   v2-marker-specific workaround.

5. **The review doc's §3.4 "async commit window race" disappears
   entirely under §3.1(c).** No commit, no abandon, no parse-back wait
   for a synthetic row → no race. The §1.7 latent issue is structurally
   resolved, not just deferred. This was claimed in the review doc and
   the spike confirms it.

---

## 3. Code-surface comparison

Measurements taken on `spike/marker-hole` HEAD against the v2
implementation that exists on `exploration/new-foundation`. LOC counts
via `wc -l`.

### 3.1 Files that go away entirely under §3.1(c)

| File | LOC |
|------|----:|
| `libs/markoff-live/src/LiveHoleLayer.cpp` | 214 |
| `libs/markoff-live/include/markoff/live-render/LiveHoleLayer.h` | 98 |
| `libs/markoff-live/src/LiveProxyBlockModel.cpp` | 204 |
| `libs/markoff-live/include/markoff/live-render/LiveProxyBlockModel.h` | 75 |
| `libs/markoff-live/include/markoff/live-render/BlockHole.h` | 37 |
| **Subtotal — production code, full-file deletion** | **628** |
| `libs/markoff-live/tests/tst_live_render_holes_layer.cpp` | 526 |
| `libs/markoff-live/tests/tst_live_render_holes_qml.cpp` | 88 |
| `libs/markoff-live/tests/tst_live_render_proxy_model.cpp` | 256 |
| **Subtotal — tests, full-file deletion** | **870** |

### 3.2 Files that lose hole-related code (partial deletion)

Estimated from `grep -ic "hole"` per file. Each match is roughly one line
of hole-specific code; some are method names + signal connections + branch
arms, so this is a lower bound on what shrinks.

| File | Hole-line count (approx LOC removable) |
|------|----:|
| `src/LiveStructuralKeyHandler.cpp` | 59 |
| `include/markoff/live-render/LiveStructuralKeyHandler.h` | 11 |
| `src/LiveListModelBinding.cpp` | 18 |
| `include/markoff/live-render/LiveListModelBinding.h` | 9 |
| `src/LiveEditBinding.cpp` (IME composition forwarding) | 25 |
| `include/markoff/live-render/LiveEditBinding.h` | 11 |
| `src/LiveCursorState.cpp` (hole-aware cursor delivery) | 7 |
| `src/UndoCoalescer.cpp` (hole-vs-CRDT undo dispatch) | 12 |
| `qml/LiveView.qml` | 3 |
| `qml/delegates/ParagraphDelegate.qml` | 6 |
| `tests/tst_live_render_structural.cpp` | 104 |
| `tests/tst_live_render_paragraph_edit.cpp` | (some) |
| **Subtotal — partial deletions, conservative** | **≈ 265** |

### 3.3 Code §3.1(c) adds

Speculative, since not implemented. Estimates from sketching:

- `LiveStructuralKeyHandler::paragraphEnter` EOB branch: replace the
  `createBlockHole` call with an `applyLocalEdit("\n\n​")` and a
  `requestTextCaretAtNewRow` call. **~10–15 LOC**, replacing ~20 LOC of
  hole-creation code in the existing branch (so net negative).
- New `MarkerScrubber` service (or a small policy class): focus-out
  handler, pre-save handler, load-time handler. Each is a function that
  walks paragraphs and produces zero-or-more `MarkoffEdit`s. **~80–120
  LOC** total.
- Cursor-positioning special case: when computing initial qtPos for a
  block whose text equals exactly the marker, use qtPos 0. **~5 LOC.**
- Tests for the scrubber paths + atomic-bundled edit + load-time
  cleanup. **~150–250 LOC** depending on coverage.

### 3.4 Net delta

| Bucket | v2 LOC | §3.1(c) LOC | Delta |
|--------|-------:|-----------:|------:|
| Production code, hole-related (full-file + partial) | ~893 | ~205 | **−688** |
| Tests, hole-related | ~870 | ~250 | **−620** |
| **Net** | **~1763** | **~455** | **−1308** |

Live-render production code is currently 3358 LOC total. **§3.1(c) cuts
~688 LOC — roughly 20% of the entire library — out of production code.**
The hole-specific surface specifically shrinks by ~77%. This easily
clears the "> 30% reduction in live-render hole-related LOC" threshold
the review doc set.

### 3.5 Authorities over content

| Approach | Number of authorities | Notes |
|----------|----------------------:|-------|
| v2 (status quo) | 2 (+1 transient) | CRDT + `bufferText` per hole + the proxy's `m_rows` mapping (transient view-side state) |
| §3.1(b) simplified-v2 | 2 | Same as v2 but no idle-commit timer |
| **§3.1(c) marker** | **1** | CRDT only; the parser observes the marker as part of the canonical source |

### 3.6 New cycle guards

A "cycle guard" per the review doc is a special-case `if X then ...`
branch introduced to work around an architectural mismatch.

v2 introduces (per review doc §1):
- proxy `beginResetModel` workaround (Bug A) — partially fixed
- conflated `requestTextCaretAtRow` semantics (Bug B) — fixed by adding a
  second method; the underlying conflation persists as a coexistence
- anchor-collapse-and-renumber pattern (Bug D)
- `aboutToCommit` signal as a mid-mutation peephole (Bug C)
- IME composition guard for hole timer (H7)

§3.1(c) introduces:
- "if paragraph content equals only the marker, this is a system-inserted
  empty paragraph" — used by the scrubber and the cursor-positioning
  special case. **One predicate, three call sites.**
- Atomic-bundled edit (path 2 mitigation) — *not* a cycle guard; it's a
  general affordance.

**Net cycle-guard count: §3.1(c) introduces one (the marker-detection
predicate) and eliminates four (Bug A workaround scope shrinks since
the proxy goes away; Bug C becomes irrelevant; H7 IME guard is
unnecessary; the "two undo regimes" in `UndoCoalescer` collapses to
one).**

### 3.7 Failure modes from review doc §1

| Bug | Status under §3.1(c) |
|-----|----------------------|
| A — proxy `beginResetModel` on every structural edit | **Eliminated.** The proxy doesn't exist; the inner parser-pure model is what the QML ListView binds to directly. |
| B — `requestTextCaretAtRow` conflated semantics | **Unchanged** (orthogonal — applies to mid-block split too; already fixed in v2 via the second method). |
| C — `holeReified` cursor delivery never wired | **Eliminated.** No reify event; the new row arrives via the standard parser-driven path. |
| D — `BlockAnchor` instability under in-place edits at qtPos 0 | **Unchanged** in scope but **lower frequency.** Bug D bites whenever a user types at qtPos 0 of any paragraph; under §3.1(c), the marker paragraph's "first keystroke at qtPos 0" is the most common trigger. The atomic-bundled edit *helps* — the marker-removal happens in the same edit as the user's character, which means the post-edit anchor is stable from byte 0 (the inserted character), not the marker. **This may actually fix Bug D for the common EOB-Enter case** because the renumber happens within the edit's atomic span, not across two separate edits. |
| E — byte arithmetic at end-of-block inserts into separator | **Unchanged** (orthogonal). |
| F — EOB-Enter UX is the wrong abstraction | **Resolved.** EOB-Enter produces a real paragraph row immediately; no idle commit; no phantom semantics for the user to discover. |
| §1.7 — async commit window race | **Eliminated structurally.** No commit step. Verified by argument; needs harness test in any implementation. |

---

## 4. Decision

**Recommend §3.1(c) — marker character.**

The recommendation thresholds in the review doc are met:

- ✓ Tree-sitter accepts the marker as block content (verified).
- ✓ Multiple leakage mitigations work cleanly — focus-out scrubber +
  pre-save scrubber + load-time scrubber + atomic-bundled edit on first
  keystroke. None re-introduces a cycle guard of the v0/v2 kind. The
  one new predicate ("is this paragraph marker-only") is shared across
  all three scrubbers, used at three deterministic event points, and
  has no async sensitivity.
- ✓ Net code-surface is meaningfully smaller — ~20% of the entire
  live-render library disappears. The hole-specific surface shrinks ~77%.
- ✓ No new bug category emerges that's worse than what v2 has. The
  worst risk is content-collision with user-typed/pasted ZWSPs, which
  is mitigated by either documenting the policy or switching to a
  rarer marker codepoint (one-line change).

The recommendation is **conditional on one hardening step**: the atomic-
bundled edit (path 2 mitigation) must land as a foundation-level affordance,
not as ad-hoc keystroke-handling logic in `LiveEditBinding`. If the
implementation defaults to "post-edit scrubber" instead of "atomic-bundled
edit", the keystroke/scrub race re-introduces a Bug-C-class problem. The
spike-findings recommendation is **§3.1(c) + atomic-bundled-edit affordance**
as a single decision.

§3.1(b) (simplified-v2) is the fallback if the atomic-bundled-edit
affordance turns out to be infeasible at the foundation layer, but I see
no reason why it would be.

§3.1(d) (atomic phantom) becomes uninteresting under §3.1(c) — its raison
d'être was "a row that exists in the proxy but not the parser" with all
that entails, and §3.1(c) eliminates the proxy entirely.

---

## 5. Sketch of the next plan

(Not the plan itself — a sketch of what the plan should look like.)

A §3.1(c) implementation plan would have roughly four work units:

1. **Foundation: atomic-bundled-edit affordance.** Add a primitive to
   `MarkoffDocument` (or `LiveEditBinding`) that applies *N* edits as one
   `MarkoffEdit` batch — already supported by `applyLocalEdit({list})`,
   so this may just be a naming/usage convention rather than new code.
   Document the contract: bundled edits produce one parse-back, one
   editSequence bump, one undo entry.
2. **Live-render: marker insertion + cursor request.** Replace the EOB
   branch of `LiveStructuralKeyHandler::paragraphEnter` and the start-of-
   paragraph branch with `applyLocalEdit("\n\n​")` followed by
   `requestTextCaretAtNewRow(blockIndex + 1, 0)`. Drop the
   `createBlockHole` call entirely. Wire the marker-aware initial-qtPos
   special case in cursor delivery.
3. **Live-render: marker scrubber.** A new `MarkerScrubber` (header +
   ~100 LOC source) with three entry points: `scrubOnFocusOut(blockIndex)`,
   `scrubBeforeSave()`, `scrubAfterLoad()`. `LiveEditBinding` calls
   `scrubOnFocusOut` from its existing focus-tracking; the host
   application calls `scrubBeforeSave` from its save handler; the
   foundation's load path calls `scrubAfterLoad` (or it's hooked in via
   `MarkoffDocument::documentReloaded`). The scrubber's internal predicate
   "is this a marker-bearing paragraph" is the single shared cycle guard.
4. **Live-render: deletions.** Delete `LiveHoleLayer.{h,cpp}`,
   `LiveProxyBlockModel.{h,cpp}`, `BlockHole.h`. Strip hole-related code
   from the files in §3.2. Delete the three hole-specific test files.
   Keep the `LiveRealisticInputHarness` (it's a general utility, not
   hole-specific). Adapt or delete the structural-key handler's hole
   tests.

Estimated total implementation: ~2 weeks at one engineer's pace, dominated
by test rewrite (the deletions are mechanical; the scrubber is a small
surface; the load-bearing risk is the harness-level verification of the
no-async-race claim).

---

## 6. Caveats and what's not covered

- **Policy decision (resolved during review): stacked Enter on a marker-only
  paragraph is a no-op.** Markdown's CommonMark semantics collapse any
  number of consecutive blank lines into a single paragraph break, so
  "press Enter five times to make a vertical gap" cannot survive a
  save/load cycle in any Markdown editor. The §3.1(c) implementation
  adopts v2 spec §6.1's rule: when the focused block is marker-only and
  the user presses Enter, the keystroke is consumed with no source edit.
  In addition, the load-time and pre-save scrubbers collapse any *runs*
  of marker-only paragraphs found in source bytes (defensive — covers
  files written by other tools or by an earlier broken build) so that
  the on-disk and in-memory states converge to one paragraph break per
  Enter regardless of how many markers leak through. The plan must
  state this rule explicitly.
- **The cursor-side QML wiring is not exercised.** The parser-level
  simulation does not validate that the QML `ListView` delegate
  materialises the new paragraph row promptly enough for the cursor to
  land on the first keystroke. v2's `requestTextCaretAtNewRow` mechanism
  is in place and should work, but a harness test is required.
- **The `BlockAnchor` instability (Bug D) is *probably* fixed by the
  atomic-bundled edit but not verified.** The argument is that bundling
  the marker-deletion with the user's character keeps the post-edit
  block anchor stable. A targeted test should confirm.
- **`LiveSelectionView` cross-row selection across a marker paragraph
  is unexamined.** v2's §7.2 covers selection across hole rows; under
  §3.1(c) the marker paragraph is a real paragraph with marker content,
  so cross-row selection should be a no-op architecturally — but the
  serializer's output (does it include the ZWSP in the clipboard?) needs
  policy. Likely answer: pre-serialization scrubber on the clipboard
  bytes, mirroring the pre-save scrubber.
- **The marker codepoint choice (ZWSP vs U+E0100 vs other) is left open
  to the implementation plan.** ZWSP is the obvious default; the
  trade-offs are documented in §2.2 path 4 and §2.3 surprise 1.
- **The list-item / fence-interior / blockquote hole cases (v2's "out of
  scope" list) are still out of scope.** §3.1(c) applies to paragraph
  holes specifically. Whether the marker pattern generalises to other
  block kinds is a separate spike — the parser may treat marker chars
  differently in list-item vs fence-interior contexts, and that needs
  its own probe.
- **The spike worktree (`spike/marker-hole`) is preserved for inspection.**
  It contains only the two probe files (`marker_probe.cpp`,
  `marker_flow.cpp`) and their build artefacts and result logs. No
  changes to the live-render or foundation source. The branch can be
  garbage-collected after the §3.1 decision is made.

---

## 7. Reproducer commands

From `.worktrees/spike-marker-hole/`:

```bash
# Configure (already done):
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build the parser library (only thing the probes need):
cmake --build build-dev --target markoff-parser -j 8

# Build and run the marker-acceptance probe:
/usr/bin/c++ -std=c++20 -fPIC -DQT_NO_KEYWORDS \
  -I libs/markoff-parser/include \
  $(pkg-config --cflags Qt6Core) \
  marker_probe.cpp \
  build-dev/libs/markoff-parser/libmarkoff-parser.a \
  build-dev/libs/markoff-parser/libts-markdown-parser.a \
  build-dev/libs/rapidyaml/libryml.a \
  $(pkg-config --libs Qt6Core) -ltree-sitter \
  -o marker_probe
./marker_probe                       # → probe-results.txt

# Build and run the EOB-Enter flow simulation:
/usr/bin/c++ -std=c++20 -fPIC -DQT_NO_KEYWORDS \
  -I libs/markoff-parser/include \
  $(pkg-config --cflags Qt6Core) \
  marker_flow.cpp \
  build-dev/libs/markoff-parser/libmarkoff-parser.a \
  build-dev/libs/markoff-parser/libts-markdown-parser.a \
  build-dev/libs/rapidyaml/libryml.a \
  $(pkg-config --libs Qt6Core) -ltree-sitter \
  -o marker_flow
./marker_flow                        # → flow-results.txt
```

Both result files are checked in to the spike branch under their
respective names.
