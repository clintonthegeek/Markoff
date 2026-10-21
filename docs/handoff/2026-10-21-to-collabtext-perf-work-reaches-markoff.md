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

## 3. What is still owed before the pin can move

We are **not** moving the pin on a local build, and the pin stays at
`collabtext-markoff-core-2026-10-13-alpha5` (`e8a8e7d1`) until:

1. **The engine is published.** `master` is 75 commits ahead of
   `origin/master` (`origin/master` is `collabtext-integration-2026-10-19-1-gd04f151`),
   and the perf work is local. We are not publishing on your behalf — the
   candidate tag, the manifest bump and the push are your repo's to make.
2. **X05 is closed.** Your own reports record it as in progress: the
   mixed-tombstone acceptance (5 KB / 50% tombstones improved only ≈1.18×,
   below the 2× target), the complete routed Release sweep, and the relocated
   installed-package gate.
3. **An A14-equivalent acceptance run** of Markoff's Core against the new
   candidate (Debug, Release, ASan/UBSan, and the installed-consumer checks),
   then the pin moves and `docs/STATUS.md` records the new immutable tag and
   its SHA.

We have recorded all of this on our triage board's publication-cost row, and
our `v0.7.1` release notes say plainly that the alpha.5 acceptance record
covers the engine, not our Core, and that a fresh acceptance run is owed.

## One thing we noticed while measuring

`try_anchor_of(identity)` enumerates the whole visible list before searching
— the 2026-10-19 evaluation names it, and our insert path calls it once per
inserted block. That is a candidate for the same treatment the replacement
paths got (an indexed lookup). We are not asking for it now; the current
numbers are already good enough to unblock us.
