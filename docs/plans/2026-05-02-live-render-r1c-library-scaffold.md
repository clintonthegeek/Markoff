# R1C — New library scaffold: `libs/markoff-live-render`

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the empty greenfield library for the C-restoration architecture: `libs/markoff-live-render/`, with namespace `Markoff::LiveRender`, QML module URI `org.markoff.live.render 1.0`, build wiring, an empty test executable that proves the test infrastructure is alive, and an empty test app that boots a window. No functionality. R2 onwards fills it in.

**Architecture:** Mirror `libs/markoff-view-qml`'s structure (CMakeLists, `include/`, `src/`, `qml/`, `tests/`, `app/`, per-library `CLAUDE.md`) since that's the project's established pattern. Use `qt_add_qml_module(STATIC ...)`. Link against `markoff_foundation` (downstream consumer; no inverse dependency).

**Tech stack:** C++20, Qt6.8 (Core, Gui, Widgets, Quick, QuickControls2, Test), CMake 3.19+. `QTest` for the placeholder test.

**Reference spec:** `docs/specs/2026-05-02-live-render-restoration-design.md` §11 R1 (third bullet) and §6.2 (file layout target). The full library is built up over R2–R10; R1C only delivers the empty shell.

**Independence:** Fully independent of R1A and R1B. Lands in any order.

---

## File map

**Modified (top-level):**
- `CMakeLists.txt` — add `add_subdirectory(libs/markoff-live-render)`.

**New:**
- `libs/markoff-live-render/CMakeLists.txt` — library + tests + app wiring.
- `libs/markoff-live-render/CLAUDE.md` — per-library guide stub.
- `libs/markoff-live-render/include/markoff/live-render/MarkoffLiveRenderExport.h` — export macro.
- `libs/markoff-live-render/include/markoff/live-render/Version.h` — version + namespace declaration; serves as the public-include anchor.
- `libs/markoff-live-render/src/Version.cpp` — single TU so the static library has at least one object file.
- `libs/markoff-live-render/qml/Placeholder.qml` — minimal QML file so `qt_add_qml_module` has at least one QML source (nothing functional; replaced in R2).
- `libs/markoff-live-render/tests/CMakeLists.txt` — test wiring.
- `libs/markoff-live-render/tests/tst_live_render_skeleton.cpp` — single trivial test (proves the test-infrastructure is wired).
- `libs/markoff-live-render/app/CMakeLists.txt` — test-app wiring.
- `libs/markoff-live-render/app/main.cpp` — `QApplication` + `QQmlApplicationEngine` boot.
- `libs/markoff-live-render/app/Main.qml` — minimal `Window`.

---

## Tasks

### Task 1: Read context

- [ ] **Step 1: Read the patterns we're mirroring**

```
libs/markoff-view-qml/CMakeLists.txt           (the qt_add_qml_module pattern; STATIC; URI; sources/QML files registration)
libs/markoff-view-qml/include/markoff/view/qml/EditorBackend.h  (just the header layout / SPDX / pragma once style)
libs/markoff-view-qml/app/CMakeLists.txt
libs/markoff-view-qml/app/main.cpp             (the QApplication + QQmlApplicationEngine + qml_import bootstrap pattern)
libs/markoff-view-qml/tests/CMakeLists.txt     (test-registration pattern)
libs/markoff-view-qml/CLAUDE.md                (per-library guide style)
CMakeLists.txt                                  (top-level — note where libs/markoff-view-qml is added; we'll append after it)
```

Do not copy-paste verbatim; the new library has different naming and a smaller sources list. Use them as templates.

No code changes in this task.

---

### Task 2: Create the library directory layout

**Files to create (as empty placeholder content; populated in subsequent tasks):**
- `libs/markoff-live-render/CMakeLists.txt`
- `libs/markoff-live-render/include/markoff/live-render/Version.h`
- `libs/markoff-live-render/include/markoff/live-render/MarkoffLiveRenderExport.h`
- `libs/markoff-live-render/src/Version.cpp`
- `libs/markoff-live-render/qml/Placeholder.qml`
- `libs/markoff-live-render/tests/CMakeLists.txt`
- `libs/markoff-live-render/tests/tst_live_render_skeleton.cpp`
- `libs/markoff-live-render/app/CMakeLists.txt`
- `libs/markoff-live-render/app/main.cpp`
- `libs/markoff-live-render/app/Main.qml`
- `libs/markoff-live-render/CLAUDE.md`

- [ ] **Step 1: Create the directory tree**

```bash
mkdir -p libs/markoff-live-render/include/markoff/live-render
mkdir -p libs/markoff-live-render/src
mkdir -p libs/markoff-live-render/qml
mkdir -p libs/markoff-live-render/tests
mkdir -p libs/markoff-live-render/app
```

The remaining tasks fill files into these directories.

---

### Task 3: Public include skeleton

**Files:**
- Create: `libs/markoff-live-render/include/markoff/live-render/MarkoffLiveRenderExport.h`
- Create: `libs/markoff-live-render/include/markoff/live-render/Version.h`

- [ ] **Step 1: Write the export macro header**

Create `libs/markoff-live-render/include/markoff/live-render/MarkoffLiveRenderExport.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Reserved for symbol-visibility decoration when the library is shared.
// As of R1C the library is STATIC, so this expands to nothing. Public
// classes that go in the export-protected boundary will use this macro
// when exported (R2 onwards).
#define MARKOFF_LIVE_RENDER_EXPORT
```

The minimal version is fine; `markoff-foundation` uses a similar pattern with a CMake-generated header. Keep this hand-written and trivial — switching to the CMake generator can be a later refinement.

- [ ] **Step 2: Write the version + namespace anchor header**

Create `libs/markoff-live-render/include/markoff/live-render/Version.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>

#include <QtGlobal>

/// `Markoff::LiveRender` is the namespace for the live-render library.
/// The library is delivered as a Qt6 QML module under URI
/// `org.markoff.live.render 1.0`.
///
/// Architecture spec: docs/specs/2026-05-02-live-render-restoration-design.md
namespace Markoff::LiveRender {

/// Library version number, integer-encoded `major * 10000 + minor * 100 + patch`.
/// R1 ships at 0 (= 0.0.0).
MARKOFF_LIVE_RENDER_EXPORT quint32 version() noexcept;

}  // namespace Markoff::LiveRender
```

- [ ] **Step 3: Write the matching .cpp**

Create `libs/markoff-live-render/src/Version.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/Version.h>

namespace Markoff::LiveRender {

quint32 version() noexcept
{
    return 0;
}

}  // namespace Markoff::LiveRender
```

This is the single TU that ensures the static library has at least one object file. R2 will start adding real classes alongside.

---

### Task 4: Library CMakeLists.txt

**Files:**
- Create: `libs/markoff-live-render/CMakeLists.txt`

- [ ] **Step 1: Write the library CMakeLists**

Create `libs/markoff-live-render/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.19)
project(markoff_live_render VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Widgets Quick QuickControls2 Qml Test)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_qml_module(markoff_live_render
    URI org.markoff.live.render
    VERSION 1.0
    STATIC
    SOURCES
        src/Version.cpp
        include/markoff/live-render/MarkoffLiveRenderExport.h
        include/markoff/live-render/Version.h
    QML_FILES
        qml/Placeholder.qml
)

target_include_directories(markoff_live_render
    PUBLIC include/
    # Bare-filename `#include <Version.h>` resolution for Qt's QML
    # type-registration generated code, mirroring markoff-view-qml's
    # private include directory pattern.
    PRIVATE include/markoff/live-render/
)

target_link_libraries(markoff_live_render PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    markoff_foundation
)

add_subdirectory(app)

if(NOT DEFINED MARKOFF_LIVE_RENDER_BUILD_TESTS)
    set(MARKOFF_LIVE_RENDER_BUILD_TESTS ON)
endif()
if(MARKOFF_LIVE_RENDER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Add a placeholder QML file**

Create `libs/markoff-live-render/qml/Placeholder.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

/// Placeholder QML file required by qt_add_qml_module (which expects at
/// least one QML source). Replaced in R2 by the real LiveView.qml and
/// per-block delegates per spec §6.2.
Item {
    objectName: "markoff-live-render-placeholder"
}
```

---

### Task 5: Test infrastructure

**Files:**
- Create: `libs/markoff-live-render/tests/CMakeLists.txt`
- Create: `libs/markoff-live-render/tests/tst_live_render_skeleton.cpp`

- [ ] **Step 1: Write the placeholder test**

Create `libs/markoff-live-render/tests/tst_live_render_skeleton.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/live-render/Version.h>

class TstLiveRenderSkeleton : public QObject {
    Q_OBJECT
private Q_SLOTS:

    /// Confirms the library links and the test-infrastructure is wired.
    /// Replaced by real tests as R2 onwards add real public surfaces.
    void library_links_and_version_is_zero() {
        QCOMPARE(Markoff::LiveRender::version(), quint32{0});
    }
};

QTEST_MAIN(TstLiveRenderSkeleton)
#include "tst_live_render_skeleton.moc"
```

- [ ] **Step 2: Write the tests CMakeLists**

Create `libs/markoff-live-render/tests/CMakeLists.txt`:

```cmake
# R1C provides one trivial test that proves the test infrastructure is
# alive. R2 onwards replaces this with the per-layer test executables
# named in spec §10.2 (tst_live_render_coords, tst_live_render_block_model,
# tst_live_render_cursor, …).

qt_add_executable(tst_live_render_skeleton
    tst_live_render_skeleton.cpp
)

target_link_libraries(tst_live_render_skeleton PRIVATE
    Qt6::Core
    Qt6::Test
    markoff_live_render
)

add_test(NAME tst_live_render_skeleton COMMAND tst_live_render_skeleton)
```

---

### Task 6: Test app skeleton

**Files:**
- Create: `libs/markoff-live-render/app/CMakeLists.txt`
- Create: `libs/markoff-live-render/app/main.cpp`
- Create: `libs/markoff-live-render/app/Main.qml`

- [ ] **Step 1: Write the app's main**

Create `libs/markoff-live-render/app/main.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QQmlApplicationEngine>

/// Test app for markoff-live-render. R1C ships a window with placeholder
/// content. R2 onwards wires in EditorBackend, the LiveListModelBinding,
/// and the LiveView.qml sibling of source mode.
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.loadFromModule("org.markoff.live.render.app", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
```

- [ ] **Step 2: Write the app's QML**

Create `libs/markoff-live-render/app/Main.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    width: 800
    height: 600
    visible: true
    title: qsTr("markoff-live-render (scaffold)")

    Rectangle {
        anchors.fill: parent
        color: "#fafafa"
        Label {
            anchors.centerIn: parent
            text: qsTr("markoff-live-render: scaffold (R1C). Real UI lands in R2.")
        }
    }
}
```

- [ ] **Step 3: Write the app CMakeLists**

Create `libs/markoff-live-render/app/CMakeLists.txt`:

```cmake
qt_add_executable(markoff-live-render-app
    main.cpp
)

qt_add_qml_module(markoff-live-render-app
    URI org.markoff.live.render.app
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(markoff-live-render-app PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    markoff_live_render
    markoff_live_renderplugin
    markoff_foundation
)

qt_import_qml_plugins(markoff-live-render-app)
```

(Note: `markoff_live_renderplugin` — the QML plugin auto-target Qt generates from the library's `qt_add_qml_module` — must match the underscore-name `markoff_live_render` used in the library's own `qt_add_qml_module`. If you used a different library target name, adjust accordingly.)

---

### Task 7: Per-library guide

**Files:**
- Create: `libs/markoff-live-render/CLAUDE.md`

- [ ] **Step 1: Write a brief CLAUDE.md**

Create `libs/markoff-live-render/CLAUDE.md`:

```markdown
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
```

---

### Task 8: Hook into top-level CMakeLists

**Files:**
- Modify: `CMakeLists.txt` (at repo root)

- [ ] **Step 1: Add the new library's `add_subdirectory` line**

Open `CMakeLists.txt`. Find the existing line:

```cmake
add_subdirectory(libs/markoff-view-qml)
```

(currently around line 47.) Add the new line *after* it:

```cmake
add_subdirectory(libs/markoff-view-qml)
add_subdirectory(libs/markoff-live-render)
add_subdirectory(libs/markoff-source-widget)
```

Order matters: `markoff-live-render` depends on `markoff-foundation` (already added earlier). It does not depend on `markoff-view-qml`.

---

### Task 9: Configure, build, run

- [ ] **Step 1: Reconfigure CMake**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Expected: configure succeeds. The new library is registered. If a "duplicate target" error appears, check that you didn't repeat any executable / library names; especially verify `tst_live_render_skeleton` is unique.

- [ ] **Step 2: Build the library, the test, and the app**

```bash
cmake --build build-dev --target markoff_live_render -j 8
cmake --build build-dev --target tst_live_render_skeleton -j 8
cmake --build build-dev --target markoff-live-render-app -j 8
```

Expected: all three build cleanly. If anything fails, the most likely causes:
- A typo in `qt_add_qml_module(markoff_live_render ...)` URI or VERSION.
- `Markoff::LiveRender::version()` declared but the .cpp not added to SOURCES.
- The QML module's `URI` not matching what `loadFromModule` expects (URIs are dot-separated; verify both ends use `org.markoff.live.render`).

- [ ] **Step 3: Run the test**

```bash
ctest --test-dir build-dev -R '^tst_live_render_skeleton$' --output-on-failure
```

Expected: pass.

- [ ] **Step 4: Briefly run the test app**

```bash
./build-dev/libs/markoff-live-render/app/markoff-live-render-app
```

(Or whatever path the build produced — `find build-dev -name 'markoff-live-render-app'` if you're not sure.)

Expected: a window opens with the placeholder text. Close it. The point is just that the app boots and the QML loads — not that anything is functional.

If the binary doesn't run on a headless build server, that's fine; the build-and-link verification is the meaningful gate.

- [ ] **Step 5: Run the full fast-tier suite**

```bash
ctest --test-dir build-dev -E "tst_realistic|tst_benchmark|tst_view_qml_live_view_qml" --output-on-failure -j 8
```

Expected: all green. Existing tests untouched; new test count = previous baseline + 1.

---

### Task 10: Commit

- [ ] **Step 1: Review the diff**

```bash
git status
git diff CMakeLists.txt
```

Expected:
- `CMakeLists.txt` modified (one new `add_subdirectory` line).
- `libs/markoff-live-render/` directory created with all the files from Tasks 2–7.

- [ ] **Step 2: Commit**

```bash
git add CMakeLists.txt libs/markoff-live-render

git commit -m "$(cat <<'EOF'
feat(live-render): scaffold libs/markoff-live-render

Empty side-by-side library for the C-restoration live render. Builds
clean as a static QML module under URI org.markoff.live.render 1.0;
ships one trivial test (tst_live_render_skeleton) and a window-shaped
test app (markoff-live-render-app). No architecture yet — R2 onwards
builds the library up per spec §11.

Existing markoff-view-qml stays in service of source mode and the
existing live mode (regression reference) until end of R10, when this
library reaches dogfood-stability and the old live-mode files retire.

Spec: docs/specs/2026-05-02-live-render-restoration-design.md §11 R1.
EOF
)"
```

Verify:

```bash
git log --oneline -3
git status
```

---

## Self-review

After completing all tasks:

- **Spec coverage.** §11 R1's "New library scaffold" sub-bullet is delivered: namespace pinned, URI pinned, test-prefix pinned, build wires up. ✓
- **Independence.** This plan touched no files in `markoff-foundation`, `markoff-parser`, or `markoff-view-qml`. R1A and R1B can land before, after, or in parallel. ✓
- **No premature architecture.** No real classes added; no QML beyond a placeholder; no test beyond a `version() == 0` assert. R1C is deliberately small. ✓
- **Type consistency.** `Markoff::LiveRender::version()` returns `quint32`; the test asserts `quint32`. ✓
- **URI consistency.** Library URI and `loadFromModule` call both use `org.markoff.live.render`. App URI is `org.markoff.live.render.app`. ✓ Note: this differs from a stray reference in the C-spec to "org.markoff.live-render" — the dotted form is canonical Qt QML convention and is what the implementation uses; an editorial fix to the spec text can land separately if desired.

---

## Acceptance criterion

This plan is complete when:

1. `tst_live_render_skeleton` passes (1/1 green).
2. `markoff-live-render-app` builds and links cleanly.
3. The full fast-tier suite passes — N+1 expected.
4. One commit on the branch (the scaffold commit).

R1A + R1B + R1C together complete the R1 phase per spec §11. After all three land, the next phase (R2 — read-only render with diff) becomes actionable, and the R2 plan gets written.
