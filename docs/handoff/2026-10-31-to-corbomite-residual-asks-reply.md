# Reply to the residual asks (2026-10-30)

**From:** Markoff maintainers
**To:** Corbomite
**Re:** your `2026-10-30-from-corbomite-residual-asks.md`

**Your verification stands, and every residual ask is actioned.** The short
version, per ask; the internal record is our triage board
(`docs/plans/2026-10-19-corbomite-open-asks-triage.md`), which carries each
row's commit and evidence.

## 1. M7 — theme in the Styled leaf: SCHEDULED, first in the queue

The exception you just granted is the gate the board was waiting on, and the
shape is as we agreed. All anchors verified live at `v0.7.1`:

- `FormatPass.cpp:217` — `computeBlockHash` has no theme input, so the hash
  gate (`:464`) cannot see a theme change;
- `StyleApplier.cpp:39-43` — `setTheme(const Theme *)` stores a pointer and
  pointer-compares, so a *changed* theme at the *same* address is invisible;
- `Theme.h` — no `operator==` (verified: none exists);
- the hardcoded colours in `FormatPass` become the light fallback for
  headless use (`DocumentRenderer::renderInto` has no theme);
- the duplicate `Theme` member in both leaves —
  `markoff-styled/include/markoff/styled/Editor.h:134` and
  `markoff-source/include/markoff/source/Editor.h:146` — is deleted; the
  base's own store (`MarkdownView::setTheme`/`theme()`) becomes the single
  authority.

Scheduled **first** in the residual queue. The board records the exception
task by name before code.

## 2. M31 — incremental reparse: SCHEDULED, second

The reuse API already landed (`reparse(text, editStart, oldEnd, newEnd)` at
`TreeSitterParser.cpp:239`, with an equivalence oracle against a fresh parse)
but had no caller. Your ask names the capability you actually need, so the
remaining half is now scoped as:

- a **persistent parser capability on the parser leaf's public API** — an
  object that holds the previous tree and last text, with `parse()` and
  `reparse(text, editStart, oldEnd, newEnd)`, so a whole-vault re-parse per
  filter evaluation reuses unchanged subtrees without our tracking your
  edits;
- the byte→char map built incrementally (resized on the edit span, not
  re-derived as a fresh `4N`-byte table per parse);
- a parse benchmark before and after — the board's own exit criterion.

We will tell you the commit when it lands, per your ask. Scheduled **second**,
behind M7 only because M7 is the larger visible gap you named first; if the
vault re-parse cost bites harder than Reading-mode theming, say so and we
will swap them.

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
