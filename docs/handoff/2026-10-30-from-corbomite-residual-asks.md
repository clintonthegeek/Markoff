# Residual asks from Corbomite — what we still want, after your 2026-10-19 reply

**From:** Corbomite (`/home/clinton/dev/Corbomite`)
**To:** Markoff maintainers
**Date:** 2026-10-30
**Re:** your [`2026-10-19-to-corbomite-open-asks-response.md`](2026-10-19-to-corbomite-open-asks-response.md)
against our consolidated open asks (`Corbomite/docs/handoff/2026-10-18-to-markoff-consolidated-open-asks.md`)

**What we run: Markoff `v0.7.1` (`2c58088a`), collabtext engine `16cc48f`
(`collabtext-perf-checkpoint-2026-10-21`) installed into
`Markoff/build-collabtext-alpha5/install`. Suite 281/281 with
`ctest -E "benchmark|perf"`.**

## What your reply settled, which we verified at our pins

Your re-triage is accepted. Our consolidated document's markoff-core rows cited
files (`MarkoffDocument.cpp`, `MarkoffDocumentPrivate.h`,
`SourceTextDocumentBinding.cpp`) that your A13 rewrite deleted; two conclusions
in particular (M2, M22) changed when the cited structures stopped existing. We
will only re-raise what reproduces at `v0.7.1`, and will cite only files that
exist at the current pin.

Verified at our pins:

- **M37 card half delivered** — `setEmbeddedMode(bool)` at
  `markoff-canvas/include/markoff/canvas/EditorWidget.h:138`, with the
  documented embedded contract (window onto the document, wheel left
  unaccepted for the proxy's zoom, scroll range live, read-only default).
- **M38 option 1 confirmed** — one card document shared by paint and edit;
  paint holds a non-owning `const MarkoffDocument *`.
- **Collabtext engine** — `IdListEngine::try_remove_at`/`try_insert_after`
  return typed rejections and the miss-causes are logged green at the pin
  (that letter is theirs, we only confirm our binary).
- Wave 3 hot path, M3, M4, M8, M9, M11, M20, M21, M23, M24, M25, M28, M30,
  M32, M34, M35, M36, M33, M16-offset, M27 items 1–3 — all landed per your
  fix table, most with acceptance tests we can run.

The card editor swap is no longer blocked. Anything remaining on that thread
is host-side and ours.

## What we are asking (everything that still matters to us)

1. **M7 — theme in the Styled leaf. Schedule it.** We accept that it is not a
   bug: it is a feature in a leaf you keep bug-fix-only, so it needs the
   explicit exception task. Please schedule it. The shape we agreed on:
   theme reads at every colour site with a light fallback for headless use,
   `Theme::operator==` so restyle can compare instead of recreate,
   `theme` in `computeBlockHash` so only changed blocks re-render, and
   `StyleApplier::setTheme` storing by value. Why it is the single largest
   visible gap on our side: in Reading mode a dark theme and user themes have
   **no effect on content**; find highlights re-theme while the body does not;
   and we re-render every leaf wholesale because nothing says the theme to
   render changed. Deleting the second `Theme` member in both leaves
   (`markoff-styled/include/markoff/styled/Editor.h:135`,
   `markoff-source/include/markoff/source/Editor.h:146` — both still present at
   `v0.7.1`) belongs to the same change.

2. **M31 — incremental reparse. Schedule it behind the map builder, and tell
   us when it has a commit.** Whole-document re-encode plus a fresh `4N`-byte
   byte→char map per parse, with the previous tree passed as `nullptr` so
   tree-sitter's incremental reuse is disabled (`TreeSitterParser.cpp` — as
   re-verified 2026-10-18; the file is rewritten upstream since, so re-check
   against the current shape). Our consumer parses whole vaults and
   `libs/bases` re-parses per filter evaluation. Nothing in our ask depends on
   the line numbers; the ask is the capability.

3. **M26 (export macros as no-ops) and M27 item 4 (the `Markoff::Engine`
   PUBLIC dependency). Give the deferral a date or a sentence.** If the answer
   is "when a non-submodule or MSVC consumer exists", say that once in
   `ALPHA_CAPABILITIES.md` so we stop wondering; if the condition is "someone
   files the packaging", we will file it — Corbomite ships an Arch package and
   can be that consumer. Why it matters: our libraries are SHARED, so on a
   toolchain where exports matter the no-op macros are not harmless.

4. **M39's narrow case — invitation accepted.** The two concrete cases we
   expect to hit: the card editor pasting multi-block content mid-block, and
   multi-block template insertion. When either reproduces we will file it with
   a failing engine-level paste test rather than a Corbomite repro. If you
   would rather have the test from the card case specifically, we will gate
   the file on the card editor's landing (host-side, ours).

5. **Pin planning.** Keep the annotated-tag convention — `v0.7.1` made our
   re-pin a single step, and it worked. Before the next release-shaped tag,
   tell us in one line whether the API surface moves (a `v0.8`-grade change
   versus a fix release) so a re-pin stays planned instead of incident-driven.

## Smaller build-hygiene rows, still open at `v0.7.1` and ours to handoff rather than re-ask

- `markoff-family/CMakeLists.txt:50` — `set(RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS
  ON CACHE BOOL "" FORCE)`. The option is legitimate; the `FORCE` overrides a
  consumer's explicit `-D`. A plain `set(... CACHE BOOL "")` plus a status
  message when unset, or a guard against an already-set value, keeps the
  contract without the override.
- The `jkqtmathtext` rows from the consolidated table (vendored twice;
  PUBLIC include dir is its own parent; installs the `.so` but not the
  headers) and the `rapidyaml` changelog gap (copy is 0.11.1, changelog stops
  at 0.9.0). No repro on our side, only the report notes; treat as hygiene,
  not as a block.
- `set_entries`'s full `IdListTree` rebuild and the `QuadTree` rows in our
  consolidated table belong to collabtext/Graffodil and are filed there.

## How to answer

Edit this file (bullet-list replies are fine), or open a reply doc in this
directory and point at it. Where a row is closed, say what closed it; where it
is scheduled, name the order. Where something we asked turns out to be already
true at `v0.7.1`, "already true" is the best possible answer — we would rather
discover it than keep carrying a workaround.
