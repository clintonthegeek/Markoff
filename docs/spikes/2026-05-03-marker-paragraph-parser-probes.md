# Marker-paragraph parser probes (spike artifacts)

**Date:** 2026-05-03
**Origin:** the `spike/marker-hole` worktree (retired 2026-05-04 alongside the
C-restoration bookend; see `docs/handoff/2026-05-04-c-restoration-bookend-d-pivot.md`).
**Companion findings doc:** `docs/handoff/2026-05-03-section-3-1-spike-findings.md`

## What these are

Two standalone C++ programs that linked against `libs/markoff-parser` and
exercised tree-sitter directly to answer two questions for the (now-retired)
marker-paragraph design:

- **`marker_probe.cpp`** — for each of ~12 candidate "invisible" Unicode
  characters (U+200B ZWSP, U+2060 word joiner, U+FEFF BOM, U+00A0 NBSP, etc.),
  parse `"hello\n\n<MARKER>"` and report the resulting block list. The question:
  which markers does the tree-sitter Markdown grammar treat as a
  paragraph-bearing character (so the parser produces a real second paragraph)
  versus stripping as whitespace? The probe identified U+200B ZWSP as the
  cleanest fit and ruled out NBSP (rendered visible) and U+2060/U+FEFF (parser
  collapses).
- **`marker_flow.cpp`** — simulates the full EOB-Enter → type → scrub flow at
  the source level for four scenarios (end-of-document EOB-Enter, mid-document
  EOB-Enter, abandon-without-typing leakage, stacked-Enter, and what the file
  on disk looks like with a marker present). Used to validate that the
  marker-paragraph design's source-edit contract was internally consistent
  before any view-layer code was written.

## Why they're preserved

The marker-paragraph design is retired (D-evolution pivot 2026-05-04 — block
boundaries become structural-CRDT state, eliminating the parser-vs-CRDT race
that motivated markers in the first place). These probes therefore have no
forward role.

They are kept as an example of a useful diagnostic spike pattern — a
single-translation-unit program that links the parser library and asks the
parser one specific question outside the test framework. If a future D2
design question needs to probe tree-sitter behaviour, the structure here is
the model to follow.

## How they were built

In the original `spike/marker-hole` worktree, each was compiled standalone
against the parser library with something like:

```bash
g++ -std=c++20 -O0 -g \
    $(pkg-config --cflags Qt6Core) \
    -I libs/markoff-parser/include \
    marker_probe.cpp \
    -L build-dev/libs/markoff-parser -lmarkoff-parser \
    $(pkg-config --libs Qt6Core) \
    -o marker_probe
```

(The exact invocation wasn't recorded; the binaries lived in the worktree
and the worktree is now removed.)

To rebuild from these source files: copy them into a working directory,
ensure `markoff-parser` is built, point the include and library paths at
the build artifacts, and link.
