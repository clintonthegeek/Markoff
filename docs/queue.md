# Session queue — 2026-05-10

> Items queued for sessions where interactive dogfood isn't available
> (remote/SSH/etc). Ordered by **descending execution difficulty** —
> a fresh agent should pick the topmost item that fits the available
> time/energy budget.
>
> **For a fresh agent landing here:** each item below names enough
> context to draft a spec/plan. Use `superpowers:brainstorming` to
> resolve open questions, then `superpowers:writing-plans` to write
> the plan, then execute task-by-task. Specs live in `docs/specs/`,
> plans in `docs/plans/`, both dated `YYYY-MM-DD-<slug>.md`. Add a
> back-reference here once the plan exists.
>
> **Current branch:** `exploration/new-foundation` (worktree at
> `.worktrees/foundation-exploration/`).
> **Tags held:** `v0.7.0-e2.5` (S1/S2/S3 cursor fixes, commits `463fc36..6c44a07`) and `v0.7.0-e2.6` (theme + zoom, commits `9fff98d..b73c4ae`) — both pending interactive dogfood at user's local desktop. Dogfood checklists: e-arc-status.md TL;DR (E2.5) and `docs/handoff/2026-05-10-e2.6-dogfood-request.md` (E2.6).
>
> **2026-05-10 — Item #5 closed.** Code review of `463fc36..6c44a07`
> ran clean (1 MED, 2 LOW). MED + 1 LOW fixed inline in this session
> (comment expansion in `LiveEditBinding.cpp:165` documenting the
> safety of the cursorChanged emission during the model update;
> null-check ordering aligned across 5 delegates). The duplicated
> qtPos clamp at `LiveListModelBinding.cpp:405-406, 522-523` is
> exactly queue #2 concern #11 and is **folded into #2** — when #2's
> spec is written, treat that as one of the surface points to
> consolidate. 186/186 fast tests pass post-fixes.
>
> **2026-05-10 — Item #1 implemented.** Spec
> `docs/specs/2026-05-10-e2.6-theme-zoom-design.md`; plan
> `docs/plans/2026-05-10-e2.6-theme-zoom.md`. Tag candidate
> `v0.7.0-e2.6`; held until interactive dogfood signs off
> (request: `docs/handoff/2026-05-10-e2.6-dogfood-request.md`).
> 190/190 fast tests green.
>
> **2026-05-10 — Item #3 implemented.** Spec
> `docs/specs/2026-05-10-qml-integration-test-harness-design.md`; plan
> `docs/plans/2026-05-10-qml-integration-test-harness.md`. New target
> `tst_live_render_qml_integration` runs offscreen; eight slots green
> (two QEXPECT_FAIL guards for queue #4 chop-\n). Full ctest still
> green.

---

## Discipline Log

> Append-only log of invariant violations encountered in passing.
> One line per smell. **No fix required in the same session** — the
> point is that the smell becomes visible to the next agent who
> reads the queue. Mechanism prescribed by `docs/INVARIANTS.md`
> invariant 8.
>
> Format: `- YYYY-MM-DD <file:line> — inv #N — <one phrase of context>`
>
> Closing an entry: prepend `~~` and append `→ fixed in <commit>` or
> `→ folded into queue #N`. Do not delete entries; they become the
> trail showing the seam settling over time.
>
> Bulk discovery is fine. If you find ten `Qt.callLater` sites in
> one read, log them as ten entries; the next refactor that touches
> the seam will use the count as evidence.

- 2026-05-11 `libs/markoff-live/src/LiveBlockModel.cpp:106` — inv #7 — `applyOps` now detects a Delete+Insert-at-same-row kind-change pattern and synthesises `beginResetModel`/`endResetModel` because `DelegateChooser` won't swap delegate type on `dataChanged` and pool-reuses the old template under plain rowsRemoved+rowsInserted. Heavy hammer; works but is the kind of pattern-match workaround that papers over a deeper L1/L2 modelling question. Worth revisiting if the chooser ever gains explicit kind-swap support, or if we move to a single delegate that internally renders by kind.
- 2026-05-11 `libs/markoff-live/src/LiveCursorState.cpp:419-440` — inv #7 — `tryResolvePending` now sets `m_cursor` directly (bypassing `request()`'s registry-based variant validation) before invoking `takeFocus`. Pre-update is needed because `takeFocus`'s `cursorPosition = qtPos` is sometimes a no-op (empty new paragraph at pos=0) and won't echo back via `syncFromTextEdit`. Bypassing `request` keeps unit-test fixtures (no registry) from segfaulting. Functional but smells: the chokepoint quietly maintains two ways of mutating `m_cursor`. *(Expanded in commit `9b30d75` to also pick variant TextCaret-vs-BlockSelected via the registry — same code path, broader responsibility. Note for the next refactor: the chokepoint now duplicates a slice of `validateVariant`'s logic. Either merge them or document that `request()` is the legacy entry point and `tryResolvePending` is the new authoritative one.)*
- 2026-05-13 `libs/markoff-live/src/BlockKindRegistry.cpp:Math` — inv #8 — `isBlockOnly` is explicit-false for Math despite Math having no `TextCaret` in `supportedCursorVariants`. Transitional asymmetry; will be removed when Math becomes text-bearing in its own spec.
- 2026-05-15 `libs/markoff-live/src/LiveCursorState.cpp:459-487` — inv #2 — `tryResolvePending` bypasses `request()`'s `validateVariant` per the in-comment rationale "reject some valid transient states during a structural cascade". Those transient states are not specified anywhere in the spec/post-mortem/handoff record — tier 2 deferred concern #9 on the strength of this gap. Tier 3 should investigate and either document the transients or eliminate them.

---

## #1 — E2.6: theme wire-up + zoom ✅ IMPLEMENTED 2026-05-10 (dogfood pending)

**Effort:** ~1 week. **Status:** implemented; interactive dogfood pending.

The `Markoff::Theme` infrastructure exists and ships
`defaultLight()`/`defaultDark()`, per-slot fonts/colours, and a
`fontSizeMultiplier(Slot)` getter — but every QML delegate hardcodes
its `font.pixelSize`/`font.family`/heading-level switch. The theme
object is in practice a decoration over `InlineHighlighter` only. No
zoom infrastructure exists either.

**Scope (from `docs/handoff/2026-05-09-post-e2-scope.md` §2.E2.6):**

- Route `font.family`/`pixelSize`/`bold`/`italic` for every text-bearing
  delegate through `Markoff::Theme`. Concrete files:
  `qml/delegates/{Paragraph,Heading,Blockquote,CodeBlock,ListItem,Math}Delegate.qml`.
- Replace `HeadingDelegate.qml`'s literal switch (28/24/20/18/16/14)
  with theme-driven `font(Heading) * fontSizeMultiplier(HeadingN)`.
- Add `fontScale` Q_PROPERTY on `LiveListModelBinding` (or a sibling
  controller). All delegate font-size reads multiply by it.
- Wire `Ctrl+=` / `Ctrl+-` / `Ctrl+0` (reset) / `Ctrl+wheel` zoom into
  `LiveActionController`'s QAction set.
- Light/dark toggle action.

**Reading order for a fresh agent:**

1. `docs/handoff/2026-05-09-post-e2-scope.md` §1.2, §1.3, §2.E2.6 —
   audit + scope statement.
2. `libs/markoff-core/include/markoff/core/Theme.h` — what's already
   shipped.
3. `libs/markoff-live/qml/delegates/*.qml` — every delegate that needs
   updating; survey current font usage before designing.
4. `libs/markoff-live/src/InlineHighlighter.cpp` — already consumes
   `Theme::charFormat`; reference for the consumption pattern.
5. `libs/markoff-live/src/LiveActionController.cpp` — where zoom QActions
   should live (alongside Cut/Copy/Paste/Bold/etc).

**Open design questions to brainstorm:**

- Where does `fontScale` live? `LiveListModelBinding` (per-binding) or a
  new `LiveZoomController`? Per-binding matches `Capabilities` flag
  subtractability (E-arc framing §5).
- Is `fontScale` a multiplier on theme sizes, or does it feed into
  `Theme::fontSizeMultiplier` somehow?
- Light/dark toggle: a per-binding QAction or an app-level setting? How
  does the test app decide which to ship at launch?
- Does theme switching invalidate any caches in `InlineHighlighter`?

**Definition of done:** Tag `v0.7.0-e2.6`. All delegates draw fonts
through `Theme`; Ctrl+= and Ctrl+- visibly resize text; light/dark
toggle works; an interactive dogfood pass signs off.

---

## #2 — Cursor architecture cleanup

> **2026-05-15 — Tier 2 implemented.** Spec
> `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md`;
> plan `docs/plans/2026-05-15-tier-2-cursor-typing-authority.md`.
> Concerns **#1** (docstring honesty), **#2** (cachedQtPos rename),
> **#6** (per-keystroke invariant test — 5 slots on
> `LiveRealisticInputHarness`) all closed. Falsifiability proof
> committed + reverted per invariant 4. Concern **#9** deferred to
> tier 3 with discipline-log entry naming the unspecified
> transients in `tryResolvePending`. Remaining concerns: #3, #4, #5,
> #10, #12.
>
> **2026-05-11 — Tier 1 (focus-chokepoint) implemented.** Concerns
> **#1 (partial — structural side)**, **#7**, **#8**, **#11** are
> resolved. See `docs/specs/2026-05-11-focus-chokepoint-design.md`,
> `docs/plans/2026-05-11-focus-chokepoint.md`, and the dogfood
> request at `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md`.
> Tag `v0.8.0-focus-chokepoint` held pending interactive sign-off.
> Remaining concerns (#1 full, #2, #3, #4, #5, #6 full, #9, #10,
> #12) tier into tier 2/3/4 per spec §10; no spec yet, gated on
> tier-1 dogfood. Bug C (click-path chokepoint bypass) surfaced
> only during the Task 16 revert and is also fixed; falsifiability
> proof now covers both sides of the seam (`establishFocus` stub
> `20dcaee` + `takeFocus` stub `2d609ba`).
>
> **2026-05-11 (dogfood re-pass):** D-fc-1 (HR focus loss) and
> D-fc-2 (new-heading impermeable to arrow keys) both fixed in
> commit `4fb711f`. The deeper root cause for D-fc-2 was an
> architectural one: `DelegateChooser` does not swap delegates on
> `dataChanged`, and the pool reuses the wrong template under
> remove+insert at the same row. `LiveBlockModel::applyOps` now
> detects the kind-change pattern and emits a `beginResetModel`/
> `endResetModel` for it. Chokepoint invariant suite 21/21.
>
> **2026-05-11 (D-fc-3):** Follow-on dogfood found arrows still
> didn't move past *already-existing* HRs. Root cause was the
> chokepoint always staging a `TextCaret` cursor variant —
> invalid for non-text-bearing kinds (HR, Image). Clicking on or
> near an HR left the cursor in an invalid TextCaret state that
> the HR delegate's `isSelected` guard rejected, breaking arrow
> navigation entirely. Fixed in commit `9b30d75` by making the
> chokepoint *variant-aware* via the existing
> `BlockKindRegistry.supportedCursorVariants` — the generalisable
> rule is "the chokepoint never stages a variant the target
> can't honour." `focusedAnchorRow()` extended to all variants
> for the same reason. Image and Math gain the same affordance
> for free. Chokepoint suite now 28 tests (was 21), with the new
> tests pinning the rule.
>
> **2026-05-13 — Block-only kinds spec + plan landed.**
> Spec: `docs/specs/2026-05-13-block-only-kinds-design.md`
> Plan: `docs/plans/2026-05-13-block-only-kinds.md`
> Work: 14-task chain on `exploration/new-foundation`. Delivers:
> - `BlockKindDescriptor::isBlockOnly` explicit flag (HR + Image = true, Math = false)
> - `BlockKindRegistry::isBlockOnly()` predicate — single decision point
> - `BlockOnlyDelegateBase.qml` shared base (HR + Image inherit; Math unchanged)
> - Navigation land-and-step (R-arrow-into/out): HR + Image are cursor stops
> - Merge fence (R-backspace/delete-adjacent): fixes D-fc-4 orphaned cursor
> - R-delete/enter/type/tripleclick: full block-only UX complete
> - 16 new invariant tests (8 HR + 8 Image), falsifiability stub in history

**Effort:** ~3 days. **Status:** critique captured (verbally during
S1/S2/S3 pass), no spec, no plan.

The S1/S2/S3 fix surfaced 12 architectural concerns in
`LiveCursorState`. The Tier 2 attempt to fix them all at once
overreached and caused multiple production regressions (typing reverses
chars; Shift+Enter inert; arrow up skips paragraphs; cursor lost on
Enter; column preservation broken). The partial revert in `6c44a07`
left these concerns standing.

**The 12 concerns** (from the critique notes; full text was in the
pre-clear conversation, but the headlines + repro live below; the agent
should read the relevant code to recover detail):

1. `LiveCursorState`'s docstring claims "single canonical cursor value"
   but `m_cursor` was never updated during typing pre-Tier-2; Tier 2
   added the sync but the architecture still assumes pre-Tier-2 in
   places.
2. `TextCaret::cachedByteOffset` is named for bytes but receives `qtPos`
   (UTF-16 code units) at every assignment site. Silent footgun for
   non-ASCII content. `Coordinates::qtPosToByte` exists in
   `LiveEditBinding.cpp:151` but isn't applied to TextCaret writes.
3. Three `requestTextCaretAt*` APIs with overlapping resolution
   semantics:
   - `requestTextCaretAtRow(row, qtPos)` — resolves immediately if row
     exists, else pending on `rowsInserted`.
   - `requestTextCaretAtNewRow(row, qtPos)` — pure-pending row-keyed.
   - `requestTextCaretAtAnchor(anchor, qtPos)` — pure-pending
     anchor-keyed.
   The choice depends on understanding the diff that's about to fire,
   with docstring tripwires ("do not use after d2ApplyBufferEdit that
   changes content") that aren't enforced.
4. The pending-request slot is single-valued (`std::optional<PendingRow>`).
   Latest-request-wins by convention, not by type.
5. `flushPendingD2Changed` inside `requestTextCaretAtRow`
   (`LiveCursorState.cpp:115`) is the cursor API poking the document's
   internal flush plumbing — leaky abstraction.
6. No invariant test asserts that `cursorState.focusedQtPos` matches the
   focused `TextEdit`'s `cursorPosition` after every keystroke.
7. `Component.onCompleted`'s match check is captured at construction
   time; if the structural signal resolves AFTER, the new delegate never
   focuses (suspected source of "cursor gone after Enter").
8. `Connections { onCursorChanged }` doesn't `forceActiveFocus` —
   non-VisualLineHint cross-block requests set `edit.cursorPosition` but
   don't migrate Qt focus. Same suspicion as #7.
9. `validateVariant` reads model state on read, not on write. A cursor
   with a wrong variant for the current kind is silently swallowed.
10. Selection state (`LiveSelectionView`) and cursor state
    (`LiveCursorState`) are independent canonical stores with their own
    sync paths; they overlap in concept.
11. The "kind transition return point clamps to `rec.text.size()`"
    pattern (just landed in `6c44a07`) is correct surgically but the
    same clamp is needed everywhere TextCaret is consumed in a
    delegate. Currently scattered between `focusEditAt`'s
    `if (qtPos <= edit.length)` guard and the request-site clamp.
12. `m_cursor` is exposed by-value (`Cursor cursor() const`) — callers
    in `LiveListModelBinding.cpp` had to `std::get_if<TextCaret>(&cur)`
    against a local copy. A typed `currentTextCaret() -> std::optional<TextCaret>`
    would be clearer.

**Reading order for a fresh agent:**

1. `libs/markoff-live/include/markoff/live/LiveCursorState.h` — the
   contract (read first, includes the API surface).
2. `libs/markoff-live/src/LiveCursorState.cpp` — implementation.
3. `libs/markoff-live/src/LiveListModelBinding.cpp` — kind-transition
   call sites and the `cursor()` consumers.
4. `libs/markoff-live/src/LiveEditBinding.cpp` — `syncFromTextEdit`
   producer.
5. `libs/markoff-live/qml/delegates/{Paragraph,Heading,Blockquote,CodeBlock,ListItem}Delegate.qml`
   — the QML consumer pattern (`Connections { onCursorChanged }` and
   `Component.onCompleted` both consume `focusedAnchorRow`/`focusedQtPos`).

**Open design questions to brainstorm:**

- Is concern #2 (bytes vs qtPos) a rename-only fix or does it require
  semantic migration (some call sites might actually want bytes)?
- Concerns #3 + #4 — is consolidating the three request APIs to one
  with a `ResolveMode` enum strictly an improvement, or does the
  variation reflect real differences worth preserving?
- Concern #8 — should `Connections.onCursorChanged` call
  `forceActiveFocus` on the matching delegate? What guards against
  stealing focus during transient cursorChanged emissions?
- Are concerns #7/#8 actually bugs in production, or theoretical? An
  interactive dogfood probe (queue item #4 might surface evidence)
  should inform priority within this plan.

**Definition of done:** All 12 items either fixed or deliberately
deferred with rationale in the spec. Invariant test added (#6).
Existing 186/186 tests still pass. No new functional regressions in an
interactive dogfood pass.

---

## #3 — QML integration-test harness ✅ IMPLEMENTED 2026-05-10

**Effort:** ~1 day. **Status:** implemented in
`tst_live_render_qml_integration` (commit chain ending Task 11).

Eight slots cover the queue-listed regression class. The harness loads
production Main.qml via the markoff-live-app-internal STATIC library
and drives input through LiveRealisticInputHarness (now wired up for
the first time since it was authored). Three-layer assertion
convention enforced: every edit asserts on buffer/model/delegate so
failures pinpoint the broken pipeline link.

Follow-ups:
- queue.md #4 (chop-\n): surfaced and guarded by
  `shift_enter_creates_visible_newline`'s two QEXPECT_FAIL markers.
- Theme QML method registration in test env may limit pixelSize
  assertions in `ctrl_wheel_zooms_font_scale` (graceful degradation).

---

## #4 — Chop-trailing-`\n` investigation + fix

**Effort:** 0.5–2 days depending on what investigation finds.
**Status:** suspicion captured, no plan.

While debugging the cursor regressions, the chop in
`LiveListModelBinding::onD2Changed:311–312` turned up as suspicious:

```cpp
QByteArray raw = doc->blockText(id);
// Trim trailing newline. Per-item ListItem blocks have content-only
// buffers (the parser strips all trailing newlines from the per-item
// content). The single trailing '\n' is the block-delimiter convention.
if (raw.endsWith('\n'))
    raw.chop(1);
r.text = QString::fromUtf8(raw);
```

For a paragraph that just received a soft break (Shift+Enter), buffer
becomes `"Heading\n"`. The chop strips the `\n` → `model.text = "Heading"`
→ delegate's `edit.length = 7` → cursor cannot reach `qtPos=8` (Qt's
`QQuickTextEdit::setCursorPosition` rejects `pos > characterCount-1`).
Symptom: Shift+Enter+typing setext underlines lands the underline
character at the wrong byte offset (before the soft break, not after),
producing `Heading=\n` instead of `Heading\n=`.

The `typeShiftEnterDashes_producesSetextH2` unit test passes only
because it bypasses the cursor logic and calls `d2ApplyBufferEdit` at
byte 8 directly.

**Investigation tasks:**

1. Confirm with a test (either new harness from #3 or a
   focused-typing test in existing infrastructure) that production
   Shift+Enter+typing-`-` produces `Heading=\n` not `Heading\n-`.
2. Read `MarkoffDocument::loadFromMarkdown` and the parser's
   block-buffer convention — does load add trailing `\n` to all blocks?
   If yes, the chop is correct *for loaded blocks*. If no, the chop's
   premise is wrong.
3. Decide on a fix shape:
   - **Option A:** kind-aware chop. Paragraphs with internal `\n`
     content keep the trailing `\n`; other kinds chop as today.
   - **Option B:** change the buffer convention so blocks never end
     with `\n` (load + insertSoftBreak both normalise).
   - **Option C:** stop chopping; let the trailing `\n` show. Audit
     every consumer of `BlockRecord.text`.

**Reading order:**

1. `libs/markoff-live/src/LiveListModelBinding.cpp:308-313` — the chop.
2. `libs/markoff-core/src/Cmd/D2.cpp:49-54` — `insertSoftBreak`.
3. `libs/markoff-core/src/MarkoffDocument.cpp` (search for
   `loadFromMarkdown` / block insertion) — the load convention.
4. `libs/markoff-core/tests/d2/tst_d2_cmd_decomposition.cpp:50-56` —
   confirms `insertSoftBreak` produces a trailing `\n` buffer.
5. Every consumer of `BlockRecord.text` (grep `recordAt` /
   `record.text`) — Option C audit surface.

**Open design questions to brainstorm:**

- Is the original "block-delimiter convention" comment (in the chop
  code) still accurate? Was it ever?
- If Option B (normalise convention), what about `loadFromMarkdown` of
  documents already containing trailing `\n` — does the parser
  preserve them by intent?
- Can we test #1 without #3's harness, or is the harness a hard
  prerequisite?

**Definition of done:** Either the chop is provably correct (close
this item with a regression test guarding the convention) or it's
fixed (with a regression test for Shift+Enter+typing-`-` →
`Heading\n-` in the buffer).

---

## #5 — Code review of recent cursor-fix commits ✅ CLOSED 2026-05-10

**Effort:** <1 hour. **Status:** done. See banner at top for disposition.

Three commits implement the S1/S2/S3 fix and its pivots:

- `463fc36` — Tier 2 (canonical sync, focusedQtPos clamp, delegate hooks).
- `9ca7cb0` — defense-in-depth (silent qtPos-only emit, flush-then-sync).
- `6c44a07` — partial revert (drop clamp + silent-emit, restore Tier 1
  surgical fix on top of canonical sync).

**To execute:** dispatch the `qt-code-reviewer` agent in foreground:

> Review commits `463fc36..6c44a07` inclusive on branch
> `exploration/new-foundation` in
> `/home/clinton/dev/Markoff/.worktrees/foundation-exploration/`.
> Context: implementing setext/ATX kind-transition cursor re-anchoring
> (S1/S2/S3 from `docs/handoff/2026-05-09-setext-dogfood-findings.md`).
> The chain pivoted twice; please flag any leftover dead code,
> unit-test gaps, suspicious cross-call ordering, qtPos vs byte-offset
> confusion, and Qt6 best-practice deviations. Specifically scrutinise
> `LiveCursorState::syncFromTextEdit`, the kind-transition return
> blocks in `LiveListModelBinding::onD2Changed`, and the
> `onCursorPositionChanged` handlers added to the 5 text-bearing
> delegates. Read-only review; report findings as a punch list under
> 400 words.

**Disposition rule:** findings that overlap queue item #2 (cursor
architecture cleanup) get folded into that plan when it gets written.
Findings that don't (style, dead code, isolated bugs) get fixed inline
before the next dogfood.

---

## When this queue is empty / superseded

Delete the file or move it to `docs/archive/`. The CLAUDE.md banner
that points here should be removed at the same time.
