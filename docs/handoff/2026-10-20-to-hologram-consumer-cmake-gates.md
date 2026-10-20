# Answer to the consumer CMake-gate handoff — all three asks are in

**From:** Markoff maintainers
**To:** Hologram development
**Date:** 2026-10-20
**Re:** your `docs/handoff/2026-08-27-to-markoff-collabtext-consumer-cmake-gates.md`
(and, for completeness, the title-hide ask of the same date)

**Status: implemented on both sides.** This note is the written answer your
handoff asked for — we had shipped the gates but never written back, which
was our omission, not a decision to keep you in the dark. Nothing here
changes your build; it tells you what to set and where the switches live.

## Your three asks

**1. collabtext: a consumer-facing test gate, a matching app gate, and no
`Qt6::Test` in the public link.** All three are in the collabtext repo:

- `option(COLLABTEXT_BUILD_TESTS ...)` and
  `option(COLLABTEXT_BUILD_APPS ...)` (`collabtext/CMakeLists.txt:32,35`),
  defaulting ON so collabtext's own pipeline is untouched.
- `Qt6::Test` is now **found only** when tests or benchmarks are built
  (`collabtext/CMakeLists.txt:72-73`); the base package is Core/Gui
  (`:66`), Widgets only for the optional Qt build (`:70`).
- The `collabtext` target no longer links `Qt6::Test` at all: it is an
  INTERFACE over `collabtext_core` (plus the optional widgets/adapter
  targets), and `Qt6::Test` is linked only into the test-only
  `collabtext_testsupport` target (`libs/collabtext/CMakeLists.txt:276-279`),
  which is created under the tests gate. A consumer that links
  `collabtext` no longer needs the Qt Test component.

**2. markoff-parser: honour `MARKOFF_PARSER_BUILD_TESTS`.** It does now
(`libs/markoff-parser/CMakeLists.txt:98-105`).

**3. Per-leaf `app/` subdirectories honour the top-level apps switch.**
`markoff-canvas`, `markoff-styled` and `markoff-source` now gate their
`app/` on `MARKOFF_BUILD_APPS` (`:104`, `:77`, `:64` respectively), and
`apps/` was already gated. `collabedit` and `collabtext-testapp` are the
collabtext-side counterparts (ask 1).

## The "dream simplification" — it is the shape we shipped

Your optional big sibling landed as the canonical form rather than a
maybe-later. `MARKOFF_BUILD_TESTS` is now the single switch, and it
**seeds every leaf gate**, including the parser one that did not exist
when you asked:

```cmake
option(MARKOFF_BUILD_TESTS "Build the Markoff test suites" ${_MARKOFF_TESTS_DEFAULT})
foreach(_markoff_leaf_tests
        MARKOFF_PARSER_BUILD_TESTS
        MARKOFF_CORE_BUILD_TESTS
        MARKOFF_SOURCE_BUILD_TESTS
        MARKOFF_STYLED_BUILD_TESTS
        MARKOFF_CANVAS_BUILD_TESTS
        MARKOFF_EMBED_BUILD_TESTS)
    if(NOT DEFINED ${_markoff_leaf_tests})
        set(${_markoff_leaf_tests} ${MARKOFF_BUILD_TESTS})
    endif()
endforeach()
```

(`CMakeLists.txt:78-91`; the commit is `76c44019`, "build(tests): one
canonical MARKOFF_BUILD_TESTS switch".) It defaults **ON** for a
standalone Markoff build and **OFF** when Markoff is consumed as a
subdirectory, so our own `scripts/run-tests.sh` baseline is unchanged, and
your existing per-leaf `set(... OFF)` spellings keep working — a leaf gate
you set explicitly is never overridden by the seed.

So the one line you wanted covers parser, core, source, styled, canvas and
embed, and you can drop five spellings:

```cmake
set(MARKOFF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
```

`MARKOFF_BUILD_APPS` (the demo apps) is a separate switch, defaulting OFF
for subdirectory consumers (`CMakeLists.txt:147-153`).

The collabtext half stays two options, because collabtext is its own repo:
`COLLABTEXT_BUILD_TESTS` and `COLLABTEXT_BUILD_APPS`, both defaulting ON
for collabtext's own build.

## The title-hide ask (same date) — closed and answered

Your `2026-08-27-to-markoff-hide-first-heading-as-title.md` was answered on
2026-08-30 (`docs/handoff/2026-08-30-to-hologram-hide-first-heading-as-title.md`):
the flag shipped as `View::setHideMatchingFirstHeadingAsTitle(bool)` plus
the `EditorWidget` pass-through (`9b0138f8`), and Hologram re-pinned and
shipped it on 2026-09-02 (`55204f3`). One divergence from your ask, restated
because it is the one thing you must read before wiring the flag: the
hidden title **stays in the block-index space** (a11y child 0, `invisible`,
matching the fold precedent) rather than being excluded from it — indices
never shift, and the accessibility tree truthfully reports the invisible
title instead of silently losing a block.

## The prefix — and a correction to this note's first version

The first version of this note said your `libs/markoff-family` submodule
"is still pinned at `6250066b` (the `archive/markoff-live-final` era)" and
that a fresh clone "would therefore build the pre-A13 API and fail". **That
reading was wrong**, and Hologram corrected it: `archive/markoff-live-final-<n>-g<sha>`
is what `git describe` prints for Markoff's *rolling* archive tag — `<n>` is
the commit count past that tag, not a version or a branch, and not evidence
that a pin sits in the live-leaf era. Your pin is `master` (re-pinned during
the 2026-10-20 catch-up) and it is the provider-backed document you build.
The same misreading applies to any pin reported that way — Corbomite's, for
instance, reads `archive/markoff-live-final-173-ge0b96bf8`, which is 173
commits *past* the archive marker on `master`. `git describe --tags` and
`git branch --contains <sha>` tell the truth.

The real fresh-clone gap is one level down, and it is not the pin: it is the
**CollabText Core install prefix**. Markoff's document engine *is* that
package, so a clone of Hologram alone has no
`../Markoff/build-collabtext-alpha5/install` to find, and the configure
fails on `find_package(collabtext)`. The family resolves that prefix from
`COLLABTEXT_ROOT` or a sibling `~/dev/Markoff` checkout; making the
requirement explicit rather than leaving CMake's generic "could not find a
package configuration file provided by collabtext" to be decoded is the right
call — thank you for making it explicit in your CMake.

(For the record: the document model moved to a provider-backed
`MarkoffDocument` — opened through a `DocumentStorageProvider` and seeded
with `importInitialMarkdown` — and edits are block-scoped and return an
`ActionPublicationResult` you must check. You have already ported to
that; this note is only so the prefix story and the port do not drift
apart.)

Thanks for the concrete ask — it made the family cheaper for every
consumer, not just yours.
