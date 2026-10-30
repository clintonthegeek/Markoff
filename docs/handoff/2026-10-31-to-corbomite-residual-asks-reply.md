# Reply to the residual asks (2026-10-30)

**From:** Markoff maintainers
**To:** Corbomite
**Re:** your `2026-10-30-from-corbomite-residual-asks.md`

**Your verification stands, and every residual ask is actioned.** The short
version, per ask; the internal record is our triage board
(`docs/plans/2026-10-19-corbomite-open-asks-triage.md`), which carries each
row's commit and evidence.

## 1. M7 — theme in the Styled leaf: **DONE** (`6493ca5c`)

The exception you granted was the gate the board was waiting on; the shape
is exactly as we agreed, and every piece is in:

- `Theme::operator==`/`!=` (value equality over every slot), plus
  `hasColor(Slot)` and `contentHash()` — the last is an order-insensitive
  signature over all five slot hashes, which is what the per-block hash
  gate mixes in now, so **a theme change restyles the blocks that
  changed**.
- Every colour site reads the theme through a `hasColor`-guarded resolver:
  a theme overrides only the slots it defines, and the fallback is the
  light value the site hardcoded before — so **a theme-less (headless)
  render is byte-identical to what you have today**.
- Three slots were appended for the sites that had none —
  `InlineCodeBackground`, `FootnoteRef`, `HorizontalRule` — appended at the
  END of the enum on purpose: Theme's JSON serializes slots by *number*,
  so inserting mid-enum would have silently re-coloured every persisted
  theme. Light values match the literals they replaced bit for bit.
- `StyleApplier::setTheme` stores the theme **by value** and compares by
  content — a changed theme at the same address now restyles.
- The duplicate `Theme` member is deleted in both leaves
  (`markoff-styled/include/markoff/styled/Editor.h:134`,
  `markoff-source/include/markoff/source/Editor.h:146`); the base's store
  is the one authority.

Scheduled **first** in the residual queue — done first, too.

## 2. M31 — incremental reparse: **DONE** (`4e470f0c`), the capability plus the map

Both halves Corbomite asked for landed, and the board's exit criterion (a
paired benchmark) is in the commit message:

- **The persistent capability: `Markoff::ParserSession`** (public header
  `include/markoff/parser/ParserSession.h`). `parse(source)` returns the same
  snapshot `Document::fromMarkdown()` produces, and you never track edits:
  the session diffs the new body against the last one, derives the minimal
  scalar-aligned UTF-8 edit itself, and carries the tree, the byte map and
  the query results together. A failed re-parse falls back to a fresh parse
  instead of publishing an empty snapshot; a frontmatter-only change never
  reaches the parser; and re-reading the *same* body (a filter re-evaluation
  where the vault text did not change) skips the parse *and* the CST walk —
  the cached query results become a snapshot copy.

- **The byte→char map is now built incrementally**: on a re-parse the table
  is resized and repaired (tail shifted by the byte/char deltas, only the
  replaced region re-walked) — no zero-fill rebuild per parse. Every offset,
  sentinel included, is pinned equal to a fresh parse in
  `tst_markoff_parser_validity`; the work also caught two overlap bugs
  (a heap smash on growth edits and a double-bumped tail from a stale
  shift), both documented in the commit so they are findable later.

- **The paired benchmark** (42 KiB vault, 30 iterations, offscreen; the box
  was sharing CPU with other builds, so treat absolute numbers as load
  bands, not precision):

  | protocol | per call |
  |---|---|
  | one-shot `fromMarkdown` (today's path) | 200–430 ms |
  | session, one contiguous edit | 200–425 ms |
  | session, same text re-read | **6.8–8.0 ms** |
  | session, frontmatter-only edit | **~8 ms** |

Scheduled **second**, behind M7 only because M7 is the larger visible gap you
named first; if the vault re-parse cost bites harder than Reading-mode
theming, say so and we will swap them.

## 3. M26 / M27 item 4 — split, because they have different triggers

- **M26 (export macros as no-ops): deferred, with the sentence.** The
  deferral's condition is now stated precisely on the board and in
  `docs/STATUS.md`: the macros matter when a build needs them — **MSVC, or a
  hidden-visibility build** (`-fvisibility=hidden` / `CXX_VISIBILITY_PRESET
  hidden`). A Linux/GCC `Arch` package of shared libraries does not: default
  visibility exports everything, which is why the no-ops are harmless here
  and why the board called them latent. The moment you want Windows builds
  or we turn visibility on, the work is scheduled — it is mechanical
  (annotate every public symbol in core/canvas/source/styled with the usual
  `Q_DECL_EXPORT`/`Q_DECL_IMPORT` switch) but wide, so we will not do it
  speculatively.
- **M27 item 4 (the engine dependency): SCHEDULED** — your volunteering as a
  non-submodule consumer is exactly the trigger the old gate was waiting
  for. The shape is unchanged from the ask: forward-declare the engine types
  in `MarkoffDocument.h` (dropping the `<collabtext/...>` includes at
  `:18-20`), make the engine link PRIVATE, and let the installed package
  stop dragging collabtext's include tree into every consumer. Sized S–M;
  scheduled third.

## 4. M39's narrow case — yours, as agreed

The invitation stands as written: when the card editor's mid-block
multi-block paste or a template insertion reproduces the trim/split/tail
behaviour in a way that hurts you, file it with a failing engine-level paste
test and we will treat it on its merits. No action on our side until then;
the documented behaviour and its test remain as shipped.

## 5. Pin planning — convention adopted

Every release-shaped tag now states in its notes, in one line, whether the
API surface moves:

```
API: unchanged          (a fix release; a re-pin is routine)
API: moved (...)        (what moved and what must be recompiled)
```

`v0.7.1` already carried this informally ("BREAKING for out-of-tree embed
backends: the embed contract moves 2 → 3"); the convention is now written
into `docs/STATUS.md`'s Pin section so it is a rule rather than a habit.

## Build-hygiene rows

- **The `FORCE` override is fixed** — `markoff-family/CMakeLists.txt:50`
  (our root) no longer forces `RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS`; it
  seeds the cache default (ON — our parser's error contract requires it) and
  a consumer's explicit `-D` now wins, with a status message when the value
  is inherited rather than set.
- **The jkqtmathtext rows are stale at `v0.7.1`** — worth re-reading before
  you carry them: the library is `STATIC` (there is no `.so` to install —
  the `ARCHIVE` destination ships the `.a`), it is linked `PRIVATE` by both
  consumers so no consumer needs its headers at all, and the exit-time
  use-after-free that made the double compilation dangerous is fixed
  (`673b1727`, hidden visibility per consumer). What survives is one true
  row: the sources are compiled twice, so two `.so` binaries carry private
  copies — binary bloat, not a correctness or packaging defect.
- **rapidyaml changelog** — recorded only; the copy is upstream's 0.11.1 and
  the changelog gap is upstream's.
- **`set_entries` / `QuadTree`** — agreed, filed where they live.

## How we run your checks

Everything above was verified at our `v0.7.1` (`2c58088a`) with the collabtext
engine pin at `collabtext-perf-checkpoint-2026-10-21` (`16cc48f`): offscreen
`scripts/run-tests.sh` 100/100 on the dev tree, the Release configuration
100/100, ASan+UBSan 100/100, and the canvas/embed gates clean.
