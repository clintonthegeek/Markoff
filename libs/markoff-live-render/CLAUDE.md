# markoff-live-render — library guide

The C-restoration live render. Built side-by-side with the existing
`markoff-view-qml` per restoration spec §11 (decision β). When this
library reaches dogfood-stability (end of R10), `markoff-view-qml`'s
live-mode files are retired; source mode stays in markoff-view-qml
unchanged.

**Status (R1C):** Empty scaffold. One trivial test, one window-shaped
test app, no architecture yet. R2 onwards builds it up.

## Architecture

The full architecture lives in
`docs/specs/2026-05-02-live-render-restoration-design.md`. Read that
before adding any code.

## Building

Standalone (within the project's existing presets):

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff_live_render -j 8
cmake --build build-dev --target markoff-live-render-app -j 8
```

Run the test app:

```bash
./build-dev/bin/markoff-live-render-app
```

(Until R2 wires in real content, this just opens an empty window.)

## Testing

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure
```

R1C ships one test (`tst_live_render_skeleton`). R2–R10 add per-layer
test executables per spec §10.2.

## QML module + import URI

- Library URI: `org.markoff.live.render 1.0`
- App URI: `org.markoff.live.render.app 1.0` (private to `app/`).

## Conventions

- C++20, Qt 6.8+.
- `// SPDX-License-Identifier: GPL-3.0-or-later` on every file.
- C++ namespace: `Markoff::LiveRender`.
- Test prefix `tst_live_render_*`.
- Public headers under `include/markoff/live-render/`; consumers include
  via `#include <markoff/live-render/HeaderName.h>`.
- No `Corbomite`-named types in the public API (matches the master-branch
  invariant; carried forward).
