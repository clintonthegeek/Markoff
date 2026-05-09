# E-arc — Status Board

**Live status of the E (live-render maximalist prototype) arc. Update after every commit, every spec amendment, every plan written, every dogfood pass.**

**Last updated:** 2026-05-09 (E2 dogfood in progress — diagnosis revised; click-bottom-line and Down-arrow-to-last-line are the same bug, still outstanding).
**Working tree:** `.worktrees/foundation-exploration/`
**Branch:** `exploration/new-foundation`
**Active phase:** **E2** — cursor-aware view (auto-hide + cross-block nav). All 9 phases (A–I) implemented; dogfood in progress (I1). One outstanding bug (mis-diagnosed previously, see below) blocks tag.

---

## TL;DR — what to do *right now*

> **E2 dogfood in progress — one bug outstanding, blocks tag.** 32/32 C++ tests pass but no test exercises the QML callsite at fault, so the green bar is misleading.
>
> **Outstanding bug (click → bottom line, Down-arrow → bottom line — same root cause):** All 5 text-bearing delegates (`ParagraphDelegate.qml`, `HeadingDelegate.qml`, `ListItemDelegate.qml`, `BlockquoteDelegate.qml`, `CodeBlockDelegate.qml`) read `cs.pendingVisualLineHint` **without parentheses** in 10 places. `LiveCursorState::pendingVisualLineHint()` is `Q_INVOKABLE`, **not a `Q_PROPERTY`** — so QML hands back the JS function reference, never an integer. Empirical probe (added to `ParagraphDelegate.onCompleted`, run against `/tmp/probe.md` under `markoff-live-app`, journal-captured 2026-05-09):
>
> ```
> [probe] typeof pendingVisualLineHint = function
>         bareValue                    = function() { [native code] }
>         called()                     = 0
>         (bare !== 0)                 = true
>         (bare === 1)                 = false
>         (called() === 1)             = false
> ```
>
> Consequence: every gate `if (cs.pendingVisualLineHint !== 0 && cs.desiredVisualX >= 0)` is true whenever `desiredVisualX >= 0`, and every ternary `(hint === 1) ? FirstLine : LastLine` resolves to LastLine. So `focusEditAt` plants the caret at `targetY = edit.contentHeight - lineH * 0.5` — the bottom line — for any cursor change that occurs after a prior Up/Down arrow set `desiredVisualX` (clicks do not clear it; only Left/Right and word-boundary moves do, via `LiveNavigationController::clearDesiredVisualX`).
>
> **Fix (do first in next session):** change all 10 callsites in the 5 delegates from `cs.pendingVisualLineHint` to `cs.pendingVisualLineHint()`. Cleaner alternative: convert the C++ side to a real `Q_PROPERTY visualLineHint` wrapping `m_pendingVlhint` (the `visualLineHintChanged` NOTIFY signal already exists at `LiveCursorState.cpp:195/231/238`); drop the `Q_INVOKABLE`. Either way, add a QML-driven test covering the focusEditAt visual-line-hint path so the C++ green bar can't go on lying.
>
> **Why `47612b0` ("add Q_INVOKABLE") didn't actually fix the Down-arrow bug:** Adding `Q_INVOKABLE` made the method *callable* from QML but did **not** auto-invoke it on property access. `cs.pendingVisualLineHint` was `undefined` before (`undefined !== 0` is true; `undefined === 1` is false → always-LastLine) and is now a function reference (`function !== 0` is true; `function === 1` is false → still always-LastLine). Behaviour from JS is identical pre- and post-commit. The dogfood claim that Down-arrow was fixed is unsound — re-test it after applying the actual fix above.
>
> **Secondary, separate bug (real but not the cause of the bottom-line symptom):** All 5 text-bearing delegates have `function positionAt(x, y) { return edit.positionAt(x - edit.leftPadding, y - edit.topPadding) }`. Qt's `QQuickTextEdit::positionAt(x, y)` takes item-local coordinates (padding included) and internally subtracts `xoff = leftPadding` and `yoff = topPadding` (Qt 6.11 source confirms `xoff`/`yoff` privates wired to padding+alignment). The delegate's subtraction is therefore double-counted, producing ~4–8 px y-shift — at most one visual line off, not "always bottom line". Fix in a separate commit after the primary bug is gone, and verify against a single TextEdit experiment before committing the form: `xoff`/`yoff` may also include alignment offsets that the naïve `edit.positionAt(x, y)` form would miss in non-default alignments.
>
> After fixing the primary bug: re-dogfood (specifically Up/Down arrow + click), add a QML-side test, then tag `v0.7.0-e2` and update phase board.
>
> **Next phase after E2:** E3 (Wikilinks, embeds, tags, callouts). Start with `superpowers:brainstorming` before drafting spec.
>
> **Background reading** (for first principles / constitutional context):
>
> 1. `docs/specs/2026-05-08-e-arc-framing.md` — **constitutional framing for E-arc.** Read §0.1 amendment first. §5.1 is E1's worked subtractability example.
> 2. `docs/e-arc/2026-05-08-e-arc-roadmap.md` — orientation, phase summary, binding constraints.
> 3. `docs/handoff/2026-05-08-defer-46-to-e-arc.md` — decision record for the §4.6 deferral / E-arc activation.
> 4. `docs/handoff/2026-05-07-pivot-to-d5-first.md` — D-arc-era pivot doc (banner in §4.6 records the deferral; §4.7 banner notes E-arc begins).
> 5. `docs/handoff/2026-05-07-live-binding-developmental-history.md` — pipeline-feature provenance; §A.7 erratum names `inlineSpansFor` as load-bearing E1 infrastructure.
> 6. `docs/d-arc/d-arc-status.md` — D-arc status board (closed at §4.5).

---

## Phase board

| Phase | Status | Spec | Plan | Notes |
|---|---|---|---|---|
| **E1** | `complete` (2026-05-08, tag `v0.7.0-e1`) | [E1 spec](../specs/2026-05-08-e1-inline-highlighter-design.md) | [E1 plan](../plans/2026-05-08-e1-inline-highlighter.md) | Inline-format highlighter in QML delegates. Reads `BlockRecord::inlineSpans` via `LiveBlockModel::spansAtRow(row)`. 143/143 tests pass. Dogfood signed off. Tag: `v0.7.0-e1`. |
| **E2** | `dogfood` (2026-05-09) | [E2 spec](../specs/2026-05-08-e2-cursor-aware-view-design.md) | [E2 plan](../plans/2026-05-08-e2-cursor-aware-view.md) | Cursor-aware view: auto-hide markers (true zero-width collapse) + full-parity cross-block keyboard nav. 32/32 C++ tests pass; QML callsite at fault is not test-covered. Dogfood: prior 2026-05-09 entries claimed Down-arrow-to-last-line was fixed by `47612b0` and click-position was a separate double-padding bug — **both wrong**. Empirically (probe in `ParagraphDelegate.onCompleted`, run 2026-05-09): `cs.pendingVisualLineHint` is read as a JS function ref (no parens; method is `Q_INVOKABLE`, not `Q_PROPERTY`), so `(bare !== 0)` is always true and `(bare === 1)` always false → LastLine path always taken once `desiredVisualX >= 0`. Same root cause for both symptoms. Fix: parenthesise the 10 callsites or add a real `Q_PROPERTY`. Double-padding `positionAt` is a real-but-secondary 4-px-shift issue. See TL;DR. |
| **E3** | `pending` | TBW | TBW | Wikilinks, embeds, tags, callouts (Obsidian affordances). |
| **E4** | `pending` | TBW | TBW | Tables, frontmatter, footnote rendering. |
| **E5** | `pending` | TBW | TBW | Math / Mermaid Live-mode parity with Reading mode. |
| **E6** | `pending` | TBW | TBW | Distillation — view-construction recipe + capability matrix + delegate-authoring template. |

**Phase status legend.** `pending` (not yet started) · `spec-in-brainstorm` · `spec-approved` · `plan-approved` · `in-progress` · `dogfood` · `complete`.

---

## Recent-changes log

Append-only chronological record. Each entry: date, commit short SHA (when committed), one-sentence summary. Never edit prior entries — corrections are new entries that supersede.

| Date | Commit | Summary |
|---|---|---|
| 2026-05-09 | — | **Diagnosis correction (supersedes the two 2026-05-09 entries below).** Empirical probe in `ParagraphDelegate.onCompleted` (run against `markoff-live-app` + `/tmp/probe.md`, output captured from systemd journal — Wayland session routes Qt warnings there, not stderr) showed `typeof cs.pendingVisualLineHint = function`, `bareValue = function() { [native code] }`, `called() = 0`, `(bare !== 0) = true`, `(bare === 1) = false`. The QML reads `cs.pendingVisualLineHint` *without parentheses* in 10 places across the 5 text-bearing delegates, but the C++ side declares it `Q_INVOKABLE` (not `Q_PROPERTY`) — so JS gets the function reference, never the int. `(bare !== 0)` is unconditionally true; `(bare === 1)` is unconditionally false → LastLine path always taken once `desiredVisualX >= 0`. This is **the same root cause** for both the click-bottom-line and Down-arrow-bottom-line symptoms. Commit `47612b0` (adding `Q_INVOKABLE`) made the method callable from QML but did not change behaviour at any of the 10 missing-parens callsites: `undefined !== 0` was true, `function !== 0` is also true. The "32/32 tests pass" green bar reflects C++ tests calling the method correctly with parens; no QML test exercises the failing path. Fix: parenthesise the 10 callsites, or convert the C++ side to a real `Q_PROPERTY` with the existing `visualLineHintChanged` NOTIFY signal. Double-padding-subtraction in delegate `positionAt` is a real but secondary 4-px-shift bug — fix separately, after re-testing. |
| 2026-05-09 | — | **E2 dogfood bug: click-position double-padding (OUTSTANDING).** Clicks land on last line of block. All 5 text-bearing delegates call `edit.positionAt(x - leftPadding, y - topPadding)` but Qt's `QQuickTextEdit::positionAt` takes LOCAL item coords and subtracts padding internally — double-subtraction pushes effective y out of text range, snapping to last char. Fix: `positionAt(x, y) { return edit.positionAt(x, y) }` plus add `topPadding` to `targetY` in `focusEditAt` hint path (since `desiredVisualX` is already TextEdit-local from `cursorRectangle.x()`). [SUPERSEDED by 2026-05-09 diagnosis-correction entry above — this diagnosis is wrong; the 4-px shift cannot produce the always-bottom-line symptom.] |
| 2026-05-09 | `47612b0` | **E2 dogfood bug fixed: Down-arrow always landed at last line.** `pendingVisualLineHint()` was not `Q_INVOKABLE`; QML got `undefined`, hint comparison `undefined !== 0` always true → always-LastLine. Added `Q_INVOKABLE`. 32/32 tests still pass. [SUPERSEDED by 2026-05-09 diagnosis-correction entry above — adding `Q_INVOKABLE` is necessary (otherwise QML can't call the method at all) but insufficient: the QML callsites still don't use parens, so the JS-side comparison `function !== 0` remains true and the LastLine path is still always taken. Down-arrow bug is **not** actually fixed; tests pass because they're C++ and call with parens.] |
| 2026-05-08 | `ad33c40` | **E2 H1+H2 landed:** caret-move benchmark (100-block doc, p50=0.008ms p99=0.012ms, gate <5ms pass); caret-inline rehighlight benchmark (10-span block, p50=0.007ms p99=0.010ms, gate <33ms pass); E1 typing-perf no regression (p99=0.003ms). 32/32 `tst_live_render_` tests green. |
| 2026-05-08 | `b4b024c` | **E2 G1+G2 landed:** `LiveNavigationController` extended with `LiveSelectionView *` param; Shift+Up/Down/Left/Right extends selection; Ctrl+Shift+Left/Right word-extend across blocks. 14 new test cases in `tst_live_render_e2_nav_shift_extend`. 30/30 tests green. |
| 2026-05-08 | `78c9bca` | **E2 F4 landed:** Page-Up/Down via `LiveView.hit(x, y)` QML invocation from C++; delegate `Keys.onPressed` catches Page keys; coordinate mapping through `editItem->parent()->property("y")` + cursor-rect. |
| 2026-05-08 | `8b78c8f` | **E2 F3 landed:** Ctrl+Left/Right cross-block word-boundary nav (cross-block only; within-block delegated to TextEdit natively). |
| 2026-05-08 | — | **E2 F1+F2 landed:** Home/End native pass-through verified; Ctrl+Home/End jump to document first/last text-bearing row. `LiveNavigationController::findFirstTextBearingRow` + `findLastTextBearingRow` using `BlockKindRegistry`. |
| 2026-05-08 | — | **E2 E1–E7 landed:** Up/Down/Left/Right with column preservation via `VisualLineHint` enum + `requestTextCaretAtRowVisualX`; `LiveCursorState::desiredVisualX`; all 5 text-bearing delegates wire arrow keys; `focusEditAt` honors `pendingVisualLineHint` via `positionAt`. |
| 2026-05-08 | — | **E2 D1+D2 landed:** `LiveNavigationController` class scaffold; `previousNavigableRow` / `nextNavigableRow`; `LiveListModelBinding::navigationController()` accessor. |
| 2026-05-08 | — | **E2 A–C landed:** `Theme::Slot::HiddenMarker`; zero-width measurement test; `LiveCursorState::desiredVisualX`; `InlineHighlighter::setLocalCaretPosition` + `setSelectionRange`; `InlineHighlighterAttached` caret/selection props; all 5 text-bearing delegates wired. |
| 2026-05-08 | (this commit) | **E2 plan landed:** `docs/plans/2026-05-08-e2-cursor-aware-view.md`. ~36 bite-sized tasks across 9 phases (A pre-flight: Theme HiddenMarker slot, zero-width mechanism measurement test, LiveCursorState desiredVisualX field, InlineHighlighter caret/selection slots; B auto-hide TDD per kind: symmetric kinds, caret-adjacent regression, link/wikilink atomic reveal, tag always-shown, heading prefix, code fence, list/blockquote always-shown, nested spans, selection cover, peer cursor invariant; C delegate QML wiring: InlineHighlighterAttached caret+selection props, per-delegate wiring; D nav controller skeleton: class scaffold + binding accessor + navigable-row helpers; E four-arrow nav: Up/Down/Left/Right with column preservation, focusEditAt visual-line-hint extension, key-forward delegate wiring; F extended nav: Home/End passthrough, Ctrl+Home/End, Ctrl+Left/Right word-boundary across blocks, Page-Up/Down via LiveView.hit; G shift-extend: Shift+Arrow extends LiveSelectionView, Ctrl+Shift+Arrow word-extend; H perf: caret-move benchmark + E1 typing-perf no-regression; I dogfood + tag v0.7.0-e2). TDD discipline (failing test → run → impl → run-pass → commit). Build cap `-j 8`. New API: `LiveCursorState::requestTextCaretAtRowVisualX(row, hint)` + `VisualLineHint` enum (FirstLine/LastLine) for column preservation, `setDesiredVisualX/clearDesiredVisualX`; new `LiveNavigationController` (sibling to `LiveStructuralKeyHandler`); `Theme::Slot::HiddenMarker`; `InlineHighlighter::setLocalCaretPosition` + `setSelectionRange`. Implementation risk surfaced as Task A2 measurement test (negative `QFont::letterSpacing` mechanism); halts plan if mechanism unreliable. Phase board E2 → `plan-approved`. |
| 2026-05-08 | (earlier commit) | **E2 spec landed:** `docs/specs/2026-05-08-e2-cursor-aware-view-design.md`. User pre-approved during brainstorm (visual companion used for caret-adjacent / block-prefix / link-wikilink-tag / nested-spans policy decisions). Scope: auto-hide of inline markers + heading prefixes + code-fence lines (true zero-width collapse — line widens/shrinks on caret-in/out) AND full-parity cross-block keyboard navigation (Up/Down/Left/Right with column preservation, Home/End, Ctrl+Home/End, Ctrl+Left/Right word boundaries, Page-Up/Down, Shift- and Ctrl+Shift- selection extension). Scope expanded from framing-doc-named "delimiter visibility" to fold in arrow-nav, which was named in `2026-04-30-live-editing-design.md` (`LiveStructuralKeyHandler` "Ctrl+Home/End/arrow crossings") but never delivered — a regression-of-omission, not new feature. Architecture: new `LiveNavigationController` (sibling to `LiveStructuralKeyHandler`); extend `InlineHighlighter` with `setLocalCaretPosition`; extend `LiveCursorState` with `desiredVisualX` for column preservation; extend `Theme` with `HiddenMarker` slot. Implementation risk flagged: zero-width via negative `QFont::letterSpacing` is font-dependent; fallback to combine with `setStretch(1)` if needed; display-buffer fallback unauthorized without explicit user decision. Test surface: ~14 test files split between auto-hide and nav. Perf gates: typing perf must not regress E1's `<33ms p99`; caret-move `<5ms` regression guard, `<1ms` target. Tag on completion: `v0.7.0-e2`. Phase board E2 → `spec-approved`. |
| 2026-05-08 | (earlier commit) | **E1 complete:** InlineHighlighter + InlineHighlighterAttached landed. 8 inline kinds (bold/italic/strike/code/highlight/link/wikilink/tag) paint in all 4 text-bearing delegates. 143/143 tests pass. Dogfood signed off. Two pre-existing issues noted (pipeline perf on large files; tight-list+code-block parser edge case). Tag: `v0.7.0-e1`. Phase board E1 → `complete`. E2 (cursor-aware delimiter visibility) is next. |
| 2026-05-08 | (earlier commit) | E1 plan landed: `docs/plans/2026-05-08-e1-inline-highlighter.md`. ~13 bite-sized tasks across 7 phases (Phase A pre-flight: SourceSpan operator==, InlineSpansRole, applyOps spans-comparison; Phase B InlineHighlighter TDD per flag-family; Phase C QML attached shim; Phase D delegate integration + cross-delegate tests; Phase E edge cases; Phase F perf benchmark; Phase G dogfood + tag + closeout docs). TDD discipline (failing test → run → impl → run-pass → commit). Build cap `-j 8`. Phase board E1 → `plan-approved` — ready for fresh-agent execution. |
| 2026-05-08 | (this commit) | Spec corrected post-codebase-discovery (added §0.2 amendment): `Markoff::SourceSpan` is flag-based not enum-based; `Theme` already has all 8 needed `Slot`s; `BlockRecord::inlineSpans` and `LiveBlockModel::spansAtRow` already exist; `BlockRecord::operator==` excludes `inlineSpans` (E1 adds explicit spans-comparison in `applyOps`); tests live in `libs/markoff-live/tests/` with `tst_live_render_inline_*` naming. Design decisions unchanged; implementation specifics simplified — most data-path infrastructure already in place. |
| 2026-05-08 | (this commit) | E1 substantive spec landed: `docs/specs/2026-05-08-e1-inline-highlighter-design.md`. User pre-approved 2026-05-08 in brainstorm. Scope: all 8 inline kinds, render-only (no navigation contract). Architecture: per-delegate `Markoff::Live::InlineHighlighter` (`QSyntaxHighlighter` subclass) reading `BlockRecord::inlineSpans`. No extension API. 8 inline kinds via existing `Markoff::Theme::Slot` palette. Test surface: ~20 behavioral slots + perf benchmark (`<33ms p99` CI gate, `<16ms` dev aspiration). Tag on completion: `v0.7.0-e1`. Phase board E1 → `spec-approved`. Q4 added to open questions (footnote pre-existing render). |
| 2026-05-08 | (this commit) | Framing-doc audit pass landed. §1.2 mobile/a11y/i18n out-of-scope paragraph; §4 prereq list strikethrough on §4.6 with §0.1 cross-reference; §5 collab-correctness invariant added; new §5.1 (subtractability-note template + worked example + violation cadence), §5.2 (capability granularity rule — one row per major sub-deliverable, ~11 rows total at arc close), §5.3 (recipe deliverable shape — docs-only, worked-example pattern, no E6 code scaffold). Open-questions table populated with Q1 (test fixture pattern), Q2 (block math E4 vs E5), Q3 (mid-arc handoff doc location). |
| 2026-05-08 | (this commit) | E-arc opened. Pivot-doc §4.6 deferred until Corbomite ready (see `docs/handoff/2026-05-08-defer-46-to-e-arc.md`); E1 (inline-format highlighter) is the active phase. Status board created. Framing-doc §0.1 amendment landed; roadmap §4.1 prerequisite updated; D-arc status board closed at §4.5; CLAUDE.md banner updated. |

---

## Open architectural questions across the arc

Deferred decisions noted at design time that the relevant phase will resolve. Not blockers.

| # | Question | Phase to resolve |
|---|---|---|
| Q1 | Test fixture pattern: cumulative pack under `tests/e-arc/` extended each phase, or per-phase isolated packs? Default: cumulative. | E1 spec time (decides for the arc). |
| Q2 | Block math (`$$...$$`) — placement at E5 vs E4. E5 reads natural for "math/mermaid bundle"; E4's "tables/frontmatter/footnote-block" pattern is closer to block-math's shape. Inline math (`$x^2$`) stays at E5 either way. | E4 spec time (sanity check before E5 begins). |
| Q3 | Mid-arc handoff doc location: keep using top-level `docs/handoff/` (current default; this audit's records both live there), or create `docs/e-arc/handoff/`? | First mid-arc handoff to land (likely during E1 spec brainstorm or post-E2 review). |
| Q4 | Footnote rendering — extent of pre-existing functionality. User observed 2026-05-08 that "footnote rendering in some naive sense already works." E4 spec should investigate the existing path before drafting; coverage may already partly exist and E4 either shrinks or supplements. | E4 spec time. |

---

## Subtractability audit log

Per the framing-doc §5 invariant, every E-phase ships a "subtractability note" answering: how would a view that doesn't need this capability avoid linking it / instantiating it / paying its runtime cost? E6 audits these notes against the recipe.

| Phase | Subtractability note (one line) | E6 audit verdict |
|---|---|---|
| *(populated as E-phases land)* | | |

---

## Spec amendment log

When an E-phase spec is amended (after spec approval but before retiring), record the amendment here with date, section affected, and reason.

*(No amendments yet.)*
