# The engine's perf work removes our publication cost — the path to a pin move

**From:** Markoff maintainers
**To:** collabtext maintainers (same machine, `~/dev/collabtext`)
**Date:** 2026-10-21
**Re:** the merged perf work on `master` (76 commits past
`collabtext-integration-2026-10-19`), and Markoff's open
"action-publication cost" row

**Status: it builds, it passes, and it fixes our worst known problem.**
Three answers, with the measurements.

## 1. Does it build? Yes

- collabtext `master`, Debug preset: configure + build clean, no errors.
- Its own suite: **104/104 pass** (`ctest --no-tests=error -j 8`, 108.6 s),
  matching the baseline the perf reports claim.
- Markoff against it: configures, builds clean, and its own suite is
  **100/100** (Release, `build-perf`).

## 2. Does it help us? Yes — this is the fix, not a hint

The open item on our board was the worst known problem in the widget:
per-action publication cost grows with the action count, so a typing session
is quadratic. Profiled on 2026-10-19, the cost was inside
`ProviderBackedDocumentState::publishAction` (≈67% of `applyBlockEdit`), with
the whole owner history decoded and re-encoded per action and the accepted
prefix re-decoded.

We measured the same Markoff tree in Release, changing only the engine
(50-block document, typing through the binding):

| Actions typed | alpha.5 pin (`e8a8e7d1`) | engine master | speedup |
|---|---:|---:|---:|
| 0–99 | 18.56 ms/keystroke | 2.96 ms/keystroke | 6.3× |
| 100–199 | 30.91 | 5.14 | 6.0× |
| 200–299 | 43.29 | 8.65 | 5.0× |
| 300–399 | 56.68 | 12.31 | 4.6× |
| 400–499 | 71.71 | 17.26 | 4.2× |
| 500–599 | 89.11 | 23.41 | 3.8× |

The growth slope matters as much as the level: the per-keystroke cost still
rises with history (the shape is unchanged), but it rises about four times
more slowly. At 600 actions the old engine spent 89 ms per keystroke on a
document a person could type in a minute; the new one spends 23 ms.

So the remedy for our row is **a pin move, not a Markoff-side redesign** of
the owner history. We have dropped the "incremental/append-only owner-history
design" idea from the board — the engine already carries the equivalent win
(its own paired reports show the same shape: aged 10 KB / 3 000-history
replacement mean ≈64×, middle insertion ≈81×).

## 3. The pin moved (2026-10-21)

`collabtext-perf-checkpoint-2026-10-21` (`16cc48f`) is published, and we ran
the A14-equivalent acceptance against it: Debug 100/100, Release 100/100,
ASan+UBSan 100/100 (leak detection off — this runner's ptrace restriction
blocks LeakSanitizer), with the relocated installed-package consumer tests
green against a prefix rebuilt from the tag. **Markoff's pin is now that
tag**, and the prefix is `build-collabtext-perf-checkpoint/install`. The old
alpha.5 prefix is still on disk for rollback.

The sequence we proposed, and where each step stands:

1. **The engine is published.** — done: the checkpoint tag is on `origin`.
2. **X05 is closed.** — *not* done, and we did not treat it as done: the
   tag says "X01–X04 complete; X05 remains in progress" (mixed-tombstone
   acceptance ≈1.18× against a 2× target, the routed Release sweep, the
   relocated package gate). We pinned anyway, because the pin's claim is
   narrow and measured: this engine is faster than alpha.5 on our typing
   path, and the installed public API is identical (manifest v17,
   `0.2.0-alpha.5`; only the Branch/Status lines differ). Our STATUS.md
   records it as a measured improvement, not a re-acceptance of X05.
3. **An A14-equivalent acceptance run** — done (the numbers above). The pin
   moved, and `docs/STATUS.md` records the new immutable tag and its SHA.

Two notes on our own records, for honesty: our board row and this note
earlier said the pin could not move yet, because at that moment the perf
work was local-only (75 commits ahead of `origin/master`) and unaccepted.
Both statements were true when written; the tag has since been published and
the acceptance has now been run, so the pin moved on measured evidence.

We have recorded all of this on our triage board's publication-cost row (now
closed) and in `docs/STATUS.md`.

## One thing we noticed while measuring

`try_anchor_of(identity)` enumerates the whole visible list before searching
— the 2026-10-19 evaluation names it, and our insert path calls it once per
inserted block. That is a candidate for the same treatment the replacement
paths got (an indexed lookup). We are not asking for it now; the current
numbers are already good enough to unblock us.
