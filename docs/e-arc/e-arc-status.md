# E-arc — Status Board

**Live status of the E (live-render maximalist prototype) arc. Update after every commit, every spec amendment, every plan written, every dogfood pass.**

**Last updated:** 2026-05-09 (E2 dogfood in progress — 2 bugs found, 1 fixed, 1 outstanding).
**Working tree:** `.worktrees/foundation-exploration/`
**Branch:** `exploration/new-foundation`
**Active phase:** **E2** — cursor-aware view (auto-hide + cross-block nav). All 9 phases (A–I) implemented; dogfood in progress (I1). One outstanding bug blocks tag.

---

## TL;DR — what to do *right now*

> **E2 dogfood in progress — one bug outstanding, blocks tag.** 32/32 tests pass.
>
> **Outstanding bug (click-position):** Clicks consistently land on the last line of the target block instead of the clicked visual line. Root cause: all 5 text-bearing delegates have `function positionAt(x, y) { return edit.positionAt(x - edit.leftPadding, y - edit.topPadding) }`. This double-subtracts padding because Qt's `QQuickTextEdit::positionAt(x, y)` already takes LOCAL TextEdit coordinates (i.e. the item's (0,0) origin, padding included) and internally subtracts `xoff = leftPadding` and `yoff = topPadding` before calling the text layout's `hitTest`. The delegate subtraction shifts the effective y upward by `topPadding` (4–8 px), pushing all clicks out of content-space — clicks near the bottom of a delegate exceed text content height and snap to the last character.
>
> **Fix (do first in next session):**
> - In all 5 text-bearing delegates (`ParagraphDelegate.qml`, `HeadingDelegate.qml`, `ListItemDelegate.qml`, `BlockquoteDelegate.qml`, `CodeBlockDelegate.qml`) change `positionAt` to: `function positionAt(x, y) { return edit.positionAt(x, y) }`.
> - In the same delegates, the `focusEditAt` hint path currently does `edit.positionAt(desiredX - edit.leftPadding, targetY)` where `targetY` is content-relative (0 = top of text). Both are wrong: `desiredVisualX` comes from `cursorRectangle.x()` (already TextEdit-local, i.e. includes leftPadding), and `targetY` needs topPadding added. Fix: `edit.positionAt(desiredX, targetY + edit.topPadding)`.
> - Also `focusEditAt` in the `Connections { target: cursorState }` block uses the same `edit.positionAt` call — apply the same fix there.
>
> **Fixed bug (Down-arrow → last-line):** `LiveCursorState::pendingVisualLineHint()` was not `Q_INVOKABLE`. QML received `undefined`, causing `undefined !== 0` (true) to always trigger the hint path and `undefined === 1` (false) to always use LastLine formula. Added `Q_INVOKABLE`. Commit: `47612b0`. 32/32 tests still pass.
>
> After fixing the click bug: re-dogfood, then tag `v0.7.0-e2` and update phase board.
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
| **E2** | `dogfood` (2026-05-09) | [E2 spec](../specs/2026-05-08-e2-cursor-aware-view-design.md) | [E2 plan](../plans/2026-05-08-e2-cursor-aware-view.md) | Cursor-aware view: auto-hide markers (true zero-width collapse) + full-parity cross-block keyboard nav. 32/32 tests pass. Dogfood in progress: Down-arrow-to-last-line fixed (`47612b0`, `Q_INVOKABLE` on `pendingVisualLineHint`); click-position bug outstanding (double-padding subtraction in all 5 delegate `positionAt` functions — see TL;DR fix). |
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
| 2026-05-09 | — | **E2 dogfood bug: click-position double-padding (OUTSTANDING).** Clicks land on last line of block. All 5 text-bearing delegates call `edit.positionAt(x - leftPadding, y - topPadding)` but Qt's `QQuickTextEdit::positionAt` takes LOCAL item coords and subtracts padding internally — double-subtraction pushes effective y out of text range, snapping to last char. Fix: `positionAt(x, y) { return edit.positionAt(x, y) }` plus add `topPadding` to `targetY` in `focusEditAt` hint path (since `desiredVisualX` is already TextEdit-local from `cursorRectangle.x()`). |
| 2026-05-09 | `47612b0` | **E2 dogfood bug fixed: Down-arrow always landed at last line.** `pendingVisualLineHint()` was not `Q_INVOKABLE`; QML got `undefined`, hint comparison `undefined !== 0` always true → always-LastLine. Added `Q_INVOKABLE`. 32/32 tests still pass. |
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
