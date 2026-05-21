# 2026-05-21 — Find UI port dogfood findings

> **2026-05-21 — RESOLVED.** Live-mode visible highlighting now lands via
> the chain described in §"Recommendation". Implementation:
> - Spec: [`docs/specs/2026-05-21-live-find-highlighting-design.md`](../specs/2026-05-21-live-find-highlighting-design.md)
> - Plan: [`docs/plans/2026-05-21-live-find-highlighting.md`](../plans/2026-05-21-live-find-highlighting.md)
> - Theme work: reused existing `SearchMatchBackground` + `SearchActiveMatchBackground` slots (no new slots needed — they were already populated in `defaultLight()`).
> - Tag candidate `v0.7.0-find-highlights` held pending interactive
>   dogfood from Corbomite `port/foundation-exploration` after the next
>   submodule re-bump.


**Tester:** user (manual dogfood on Corbomite `port/foundation-exploration` + Markoff `exploration/new-foundation`).
**Build SHAs:** Corbomite `a00389d0` (post-test-gating cleanup); Markoff `c34e291` (the docs commit; submodule pin is `7ee6ee7`, before the `resetContent` D2-build fix at `861196c`).

## What works

- **Source mode**: live-highlighting works. Typing a needle into the FindBar lights up matches in the QPlainTextEdit-based source widget as expected. Pressing Next/Prev scrolls the viewport to bring the focused match into view.
- **Find bar UX**: count label updates as you type. Return/Shift+Return navigate. Esc closes. Buttons enable/disable on match count. Behavior matches the spec's acceptance criteria 1–7 in Live mode (modulo highlights — see below) and all 9 in Source mode.
- **Cross-leaf**: the polymorphic `attachFindController(FindController*)` dispatch in Corbomite's `NoteEditorWidget::showFindBar` correctly attaches to whichever leaf is active.

## What's missing — Live-mode visible highlighting

In Live mode, the count label shows the correct match count but **the matches are not visibly highlighted in the rendered view**. Navigation (Next/Prev) still works because the controller emits `navigationRequested`, and Live's adapter parks a non-focusing caret on the match — that drives existing visible-row tracking into scrolling the match into view.

### Root cause

`libs/markoff-live/src/Detail/LiveFindAdapter.cpp` only subscribes to `navigationRequested`. It does not:

1. Subscribe to `FindController::matchesChanged` (the signal emitted when the needle changes and the match list rebuilds).
2. Maintain a "matches by block" cache that delegates can consult.
3. Expose the per-block match ranges through `BlockRecord` or a sibling property.
4. Render highlights — neither via a Theme `FindMatch` color slot nor via per-delegate paint code.

In Source mode the equivalent is "free" because `QPlainTextEdit::setExtraSelections` is a Qt-built-in highlight surface. In Live, the delegates render through QML; we need an explicit channel from the controller to the visible text.

## Recommendation: a follow-up Markoff-side feature

Not a port-first micro-spec (single decision); rather a small focused feature on Markoff-live. Estimated 1-2 dev-days.

**Sketched shape:**

1. **`LiveFindAdapter` subscribes to `matchesChanged`**, rebuilds a `QHash<BlockAnchor, QList<MatchRange>>` cache where `MatchRange = {byteOffset, byteLength, isCurrent}`.
2. **`LiveBlockModel`** gains a per-row `findMatches` role that the adapter populates and invalidates via `dataChanged`.
3. **Text-bearing delegates** (`UnifiedInlineTextDelegate`, `CodeBlockDelegate`, `MathDelegate`, `Heading*Delegate`) read `model.findMatches` and paint highlights — either by extending `InlineHighlighter` with a "find" pass (best fit — it already paints `QTextCharFormat` ranges per E1) or by drawing rectangles over the `TextEdit` viewport.
4. **`Markoff::Theme`** gains two slots: `FindMatch` (all matches; default = pale yellow / E1 highlight color) and `FindMatchCurrent` (the focused match; default = stronger accent). These are reusable by future "highlight all occurrences of selected word" features.
5. **Tests**: extend `tst_live_render_qml_integration` with a slot that types a needle and asserts the matched ranges appear in the rendered delegate's text-format runs.

Estimated work-units:
- a. Adapter cache + matchesChanged subscription (~30 min)
- b. Model role + dataChanged plumbing (~30 min)
- c. Theme slots (~15 min)
- d. InlineHighlighter find-pass + delegate consumption (~3-4 hours)
- e. Test (~1 hour)

**Order:** a → b → c → e (write the test) → d (make it pass).

## Other observations from the dogfood

(None reported by the user beyond the above. The session ended with the user observing the Live-mode missing-highlights gap as expected behavior given the architecture.)

## Items NOT in scope of any follow-up

- Replace UI (Ctrl+H): deferred per the original MVP spec.
- Search flag toggles (case-sensitive, whole-word, regex): deferred.
- Search history dropdown: deferred.
- Wrap-around toast: deferred.

## Cross-references

- Find UI port spec (Corbomite): `/home/clinton/dev/Corbomite/docs/superpowers/specs/2026-05-20-find-ui-port-design.md`
- Find UI port plan (Corbomite): `/home/clinton/dev/Corbomite/docs/superpowers/plans/2026-05-20-find-ui-port.md`
- Find session-scope spec (Markoff): `docs/specs/2026-05-20-find-session-scope-design.md`
- LiveFindAdapter source: `libs/markoff-live/src/Detail/LiveFindAdapter.{h,cpp}`
- Final reviewer prediction (now confirmed): the Find UI port final review flagged "Source-mode highlight rendering" as risk #1 but had the direction reversed — Source works because QPlainTextEdit owns extra-selection rendering; Live is the side that needs explicit per-block highlight plumbing.
