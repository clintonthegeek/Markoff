# markoff-live — library guide

The active QML view leaf. D2's foundation (per-block CRDT buffers, sibling
causal-LWW maps) is consumed here through `LiveListModelBinding` /
`LiveBlockModel` / `LiveCursorState` / `LiveStructuralKeyHandler` /
`LiveEditBinding` / `BlockKindRegistry`.

## Status

- D2 (foundation reshape): consumed.
- D3 (view-layer adaptation): implemented and dogfooded for non-ListItem
  block kinds. ListItem path is mid-correction — see the active spec.

## Read before editing

In order:

1. `../../docs/d-arc/2026-05-04-d-arc-roadmap.md` — D-arc orientation
2. `../../docs/d-arc/d-arc-status.md` — live status
3. `../../docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` — original
   D3 spec; non-ListItem sections still authoritative
4. `../../docs/specs/2026-05-06-per-item-listitem-blocks-design.md` —
   **active corrective spec for ListItem.** Do not patch
   `LiveStructuralKeyHandler.cpp`'s ListItem section without reading this
   first. Commits `cc62280`, `799eb94`, `21b2ce3` are over-fits to be
   replaced; do not extend their lineage.

## Architecture

The layer stack (post-D3, per `2026-05-05-d3-view-layer-adaptation-design.md`
§2):

```
L8  Interactive blocks   (Math + BlockInternalEdit)
L7  Structured text      (ListItem, Blockquote)
L6  Other text blocks    (Heading, CodeBlock, HR, Image)
L5  Structural keys      (LiveStructuralKeyHandler dispatch)
L4  Block editing        (LiveEditBinding)
L3  Cursor + selection   (Shape 1 discriminated cursor)
L2  Diff-driven model    (Myers over (kind, BlockId))
L1  Read-only render     (ListView + delegates)
L0  Coordinate primitives
```

**Cursor model.** Three variants (`TextCaret | BlockSelected |
BlockInternalEdit`) — see `markoff/live-render/Cursor.h` and the C-spec
section §3 carry-forward in the D3 spec.

**Cursor delivery.** `LiveListModelBinding` emits
`structuralRowsInserted(int first, int last)` and `structuralRowRemoved(int
row)` from `onD2Changed` after `applyOps`. `LiveCursorState` resolves
pending row-keyed and anchor-keyed cursor requests on these signals. The
parse-cycle path is gone.

**Kind transition.** View-driven, in `KindTransition.cpp`'s
`inferBlockKind`. Runs against each Equal-op block's text in
`LiveListModelBinding::onD2Changed`; calls `Cmd::changeKind` on mismatch.
Hardcoded prefix rules. The ListItem prefix detection becomes simpler
under per-item granularity (single-line text, not multi-line).

**Block kind dispatch.** `BlockKindRegistry` (per-document, not global)
implements `Markoff::BlockSerializerRegistry`. `BlockKindDescriptor`
declares per-kind `delegateUrl`, `consumedStructuralKeys`,
`supportedCursorVariants`, `serializer`. `LiveView.qml`'s
`DelegateChooser` picks the QML delegate by `model.kind` string.

## Building

Within the worktree:

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff_live -j 8
cmake --build build-dev --target markoff-live-app -j 8
```

Run the test app:

```bash
./build-dev/bin/markoff-live-app <markdown-file>
```

## Testing

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure
```

Live-render test executables: `tst_live_render_{coords, registry,
block_model, cursor, structural, kind_transition, paragraph_edit,
context_menu, skeleton}`. Per-layer per-test contract per the C spec
(`Lₙ` tests `L_{k<n}` as real, `L_{k>n}` absent).

## QML module + import URI

- Library URI: `org.markoff.live 1.0`
- App URI: `org.markoff.live.app 1.0` (private to `app/`).

## Conventions

- C++20, Qt 6.8+.
- `// SPDX-License-Identifier: GPL-3.0-or-later` on every file.
- C++ namespace: `Markoff::Live`.
- Test prefix `tst_live_render_*`.
- Public headers under `include/markoff/live-render/`; consumers include
  via `#include <markoff/live-render/HeaderName.h>`.
- No `Corbomite`-named types in the public API.
