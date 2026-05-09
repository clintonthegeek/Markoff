# E-arc — Status Board

**Live status of the E (live-render maximalist prototype) arc. Update after every commit, every spec amendment, every plan written, every dogfood pass.**

**Last updated:** 2026-05-08 (E2 plan landed; ready for fresh-agent execution).
**Working tree:** `.worktrees/foundation-exploration/`
**Branch:** `exploration/new-foundation`
**Active phase:** **E2** — cursor-aware view (auto-hide + cross-block nav). Spec + plan `plan-approved` 2026-05-08.

---

## TL;DR — what to do *right now*

> **E1 is ready to execute.** Substantive spec + bite-sized implementation plan both landed 2026-05-08; user-approved.
>
> **Fresh-agent execution start:**
>
> 1. Read `docs/specs/2026-05-08-e1-inline-highlighter-design.md` — substantive design. Read §0.2 amendment first (records codebase-discovery corrections).
> 2. Read `docs/plans/2026-05-08-e1-inline-highlighter.md` — bite-sized task list with concrete code, build commands, and TDD discipline. Skim the plan header for build cap (`-j 8`), commit convention, and per-task cadence.
> 3. Choose execution mode: subagent-driven-development (recommended; fresh subagent per task with review checkpoints) or executing-plans (inline batch execution).
> 4. Execute task-by-task. Each task ends green-tree + commit; each phase ends with full-suite ctest run; closeout (Task G1) requires user dogfood pass before tagging.
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
| **E2** | `plan-approved` (2026-05-08) | [E2 spec](../specs/2026-05-08-e2-cursor-aware-view-design.md) | [E2 plan](../plans/2026-05-08-e2-cursor-aware-view.md) | Cursor-aware view: auto-hide markers (true zero-width collapse) + full-parity cross-block keyboard nav. Scope expanded from framing-doc to fold in arrow-nav (was a regression-of-omission against `2026-04-30-live-editing-design.md`). Depends on E1's per-span `QTextCharFormat` carrier. ~36 bite-sized tasks across 9 phases (A pre-flight, B auto-hide impl, C delegate QML wiring, D nav controller skeleton, E four-arrow nav, F extended nav, G shift-extend, H perf, I dogfood/tag). User pre-approved both spec and plan. |
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
