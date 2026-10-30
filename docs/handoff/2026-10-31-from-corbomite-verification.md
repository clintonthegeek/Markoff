# Verified and integrated — Corbomite runs your residual work at `89a16a8b`

**From:** Corbomite
**To:** Markoff maintainers
**Date:** 2026-10-31
**Re:** your [`2026-10-31-to-corbomite-residual-asks-reply.md`](2026-10-31-to-corbomite-residual-asks-reply.md)

Everything in your reply checked out at the source, before we re-pointed. Item
by item, what we read and what it changed on our side.

## What we verified by reading the code

**M7 (`6493ca5c`) — the theme plumbing, all five pieces:**

- `operator==`/`!=` as value equality over every slot, `hasColor(Slot)`,
  `contentHash()` — present in `markoff/core/Theme.h` as claimed.
- The three new slots sit **at the end** of the enum with the comment saying
  why (number-serialized JSON), exactly the constraint that made mid-enum
  insertion dangerous.
- The duplicate `Theme` member is gone from both leaves — `Editor.h` in
  styled and source no longer declare their own `m_theme`; the base's store
  is the one authority.
- The resolver pattern (hasColor-guarded reads, light fallback) and
  `StyleApplier` storing by value are in the diff; the theme-less render
  being byte-identical is backed by your 98 lines of new theme tests.

**M31 (`4e470f0c`) — both halves:**

- `ParserSession` is public (`include/markoff/parser/ParserSession.h`) with
  the no-edit-tracking contract, the same-snapshot guarantee, the failed
  re-parse fallback, and the same-table-and-tree move documented in the
  header itself.
- The incremental byte→char map (resize + tail shift, replaced region only)
  reads as described in the commit; the two overlap bugs caught by the oracle
  are named there so they stay findable.
- `tst_parser_session` (9 slots) and `tst_markoff_parser_validity` both
  present; your paired-bench numbers noted, and treat-as-load-bands agreed.

**Build rows:**

- `73fa8250` — the `RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS` no longer `FORCE`s;
  it seeds the cache default and a consumer's explicit `-D` wins, with the
  status message and the "the breakage is too" contract comment about setting
  it OFF. Good shape and we take the warning as written.
- The `jkqtmathtext` re-statement is accepted and our records updated: at the
  pin the library is `STATIC` with hidden visibility (`jkqtmathtext/CMakeLists.txt:60,70`),
  linked `PRIVATE` by its consumer (`markoff-canvas/CMakeLists.txt:73`), so
  our `.so`-install and public-include rows go; what survives is the
  double-compile bloat row, filed as exactly that.
- `ALPHA_CAPABILITIES.md` deferral triggers, the M26 condition (MSVC or
  hidden-visibility), and the corrected M27-item-4 shape (value-typed
  `Markoff::VNext::Result`/`Error` mirror — agreed a by-value return cannot
  sit behind a forward declaration) all read as written.

## What we did on our side

- **Re-pinned `libs/markoff-family` `2c58088a` → `89a16a8b`** and ran
  everything: your own suite at the tip **101/101** offscreen (one over your
  100 — a test was added after your reply was written; nothing failed), and
  Corbomite's full offscreen suite on the new pin **281/281**
  (`ctest -E "benchmark|perf"`). Commit `19b8b037`.
- **No Corbomite code changed for M7** — theme propagation flows through the
  styled leaf as before, and with `operator==` + `contentHash` the restyle
  you do costs less; our Reading-leaf full re-render is unchanged short-term
  and worth revisiting once we profile.
- **M31 adoption is queued, not rushed.** Our initially heaviest reparse site,
  `MetadataParser::parse` (`libs/storage/src/MetadataParser.cpp:347`), is
  already short-circuited by a content-hash check — unchanged files never
  parse — so the headline same-text win buys little there. The candidate that
  actually re-parses the same body repeatedly is `LinkUtils.cpp:240`
  (`#heading` subpath resolution: only headings need the AST and it cold-parses
  the full document per call). Tracked as punch
  `[storage][parsing][P2][follows 4e470f0c]` with **measure-before-wiring**
  acceptance using your bench harness shape.
- The `set_entries`/`QuadTree` rows are filed with collabtext and Graffodil
  respectively, as the split of the residual table says.

## Threads still open on our ledger

- **M39 narrow case stays ours** — the card editor + template insertion is the
  path; a failing engine-level paste test comes with it when we hit it.
- **M26/M27(4)** stay deferred by your stated triggers; the trigger we might
  actually fire is the Arch package if it ships as a standalone consumer.
- **Pin planning**: v0.7.1 predates all of this, so `89a16a8b` is a bare-tip
  pin on our side. When the next release-shaped tag lands, its one-line
  `API:` statement (now a rule in your `STATUS.md` — good adoption) gives us
  the routine-vs-bumping signal we asked for.

Nothing further from us this round. Thanks for shipping both asks with
reproducible gates rather than talking points.
