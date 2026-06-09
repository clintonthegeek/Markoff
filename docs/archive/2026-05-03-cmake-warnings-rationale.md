# CMake configure-time warnings — diagnosis and proposed fixes

**Date:** 2026-05-03
**Branch:** `exploration/new-foundation`
**Author:** local diagnosis pass (Opus 4.7)
**Status:** PROPOSED — not applied to the tree. Cross-check against the
remote/master build environment before committing. The remote agents are
the active owners of `libs/`; this note is a reviewable proposal, not a
change.

> Important framing: `cmake -S . -B build-dev` exits **0**. The build is
> green. There are no errors. Everything below is `CMake Warning` output
> (45 messages total at this commit). They reduce tooling fidelity
> (qmllint can't resolve module paths, `qt_import_qml_plugins` skips
> plugins, AUTOGEN is enabled on non-Qt third-party targets) but do
> not block configure, build, or test.

## Build environment of this diagnosis

| Component | Version |
|---|---|
| OS | Debian GNU/Linux 13 (trixie) |
| Kernel | Linux 6.12.74+deb13+1-amd64 |
| Arch | x86_64 |
| CMake | 3.31.6 |
| GCC | 14.2.0 (Debian 14.2.0-19) |
| Qt6 | 6.8.2 (`qt6-base-dev` 6.8.2+dfsg-9+deb13u1, `qt6-declarative-dev` 6.8.2+dfsg-7) |
| Qt6 install prefix | `/usr` (system Qt) |
| KF6 SyntaxHighlighting | 6.13.0-1 (`libkf6syntaxhighlighting-dev`, `qml6-module-org-kde-syntaxhighlighting`) |
| Qt6 plugin packages installed | `qt6-qmllint-plugins`, `qt6-qmlls-plugins`, `qt6-qmltooling-plugins`, `qt6-qpa-plugins`, `qt6-svg-plugins`, all 6.8.2 |

`CMakeLists.txt` declares `cmake_minimum_required(VERSION 3.19)` and
`find_package(Qt6 6.8 REQUIRED ...)`, so Qt 6.8+ is the assumed floor.

**Cross-check questions for the other environment:**
- Is Qt also 6.8.x, or older (6.5/6.6/6.7) / newer (6.9)? Several fixes
  hinge on Qt-policy availability — see per-fix notes.
- Distro Qt or upstream Qt build? Some Debian Qt packages omit dev-only
  CMake macros that upstream Qt ships.
- CMake version — the deprecated-policy floor matters if you bump
  `cmake_minimum_required`.

## Warning inventory (categorised)

| # | Class | Count | Source |
|---|---|---|---|
| 1 | "qml plugin X is a dependency of Y, but link target Qt6::X does not exist in current scope. Plugin will not be linked." | ~42 | `qt_import_qml_plugins(...)` in both `app/CMakeLists.txt` files |
| 2 | "QML module target path doesn't match OUTPUT_DIRECTORY" | 2 | `qt_add_qml_module()` in each library CMakeLists |
| 3 | "qmldir file not found at .../app/<uri-path>" | 2 | downstream of #1 (the `qt_import_qml_plugins` call doing the lookup) |
| 4 | "Qt policy QTP0004 is not set" (dev warning) | 2 | `qt_add_qml_module()` — triggered by extra `.qml` files in `qml/delegates/` subdir |
| 5 | "AUTOGEN: No valid Qt version found for target ryml / c4core" (dev warning) | 2 | root `CMakeLists.txt` setting `CMAKE_AUTOMOC` / `CMAKE_AUTOUIC` ON before `add_subdirectory(libs/rapidyaml)` |

Distinct plugin names mentioned in #1 (per app target):
`qtquick2plugin`, `qmlplugin`, `modelsplugin`, `workerscriptplugin`,
`qtquickcontrols2plugin`, `qtquickcontrols2fusionstyleplugin`,
`qtquickcontrols2materialstyleplugin`,
`qtquickcontrols2imaginestyleplugin`,
`qtquickcontrols2universalstyleplugin`,
`qtquickcontrols2basicstyleplugin`, `qtquicktemplates2plugin`,
`qtquickcontrols2implplugin`, `qtquickcontrols2fusionstyleimplplugin`,
`quickwindowplugin`, `qtquickcontrols2materialstyleimplplugin`,
`qtquickcontrols2imaginestyleimplplugin`,
`qtquickcontrols2universalstyleimplplugin`,
`qtquickcontrols2basicstyleimplplugin`, `qmlshapesplugin`,
`qquicklayoutsplugin`.

---

## Fix 1 — Drop the `qt_import_qml_plugins(...)` calls

**Files:**
- `libs/markoff-view-qml/app/CMakeLists.txt` line 20
- `libs/markoff-live/app/CMakeLists.txt` line 19

**Proposed change:** delete the `qt_import_qml_plugins(<target>)` line in
both files.

**Rationale:** With Qt 6.5+ `qt_add_qml_module(... STATIC ...)`, plugin
importation for the executable's own static QML modules (and for the
QML library targets it links against) is handled automatically by
`qt_add_executable` + `qt_add_qml_module` machinery. The explicit
`qt_import_qml_plugins(target)` call walks the executable's transitive
QML-plugin deps and tries to resolve them as `Qt6::<plugin>` link
targets in the *current directory scope*. Qt's plugin targets
(`Qt6::qtquick2plugin`, `Qt6::qtquickcontrols2plugin`, every style impl
plugin, etc.) are imported only when each plugin's individual CMake
package config is loaded — `find_package(Qt6 COMPONENTS Quick
QuickControls2)` imports the *module* targets, not the per-plugin
targets. Hence the ~42 "link target ... does not exist in current
scope" warnings: every plugin the executable transitively needs but
whose target isn't visible here.

This is purely the warning's cause. The actual plugin linkage for
running the test apps is being handled elsewhere (the apps run today;
that's not up for debate). The explicit call is a holdover from
pre-6.5 Qt patterns and is now noise.

**Risk:** Low on Qt 6.8.2. Verify on the other environment:
- `qt_add_qml_module(STATIC ...)` was hardened in Qt 6.5; if the other
  env is < 6.5 (unlikely given `find_package(Qt6 6.8 REQUIRED)`),
  removing the call could regress runtime plugin loading for
  `markoff-*-app`. The find_package floor of 6.8 makes this a
  non-concern in practice.
- After applying, run `./build-dev/bin/markoff-view-qml-app
  /path/to/file.md` and `./build-dev/bin/markoff-live-app` and
  confirm no QML import errors appear at startup (e.g., missing
  `QtQuick.Controls.Fusion` style). If any do, `qt_import_plugins(<tgt>
  INCLUDE Qt6::QQuick2Plugin ...)` listing each by name is the
  fallback — but that's brittle and version-coupled.

**Alternative:** keep the call and silence the warnings by importing
each plugin target's package up front
(`find_package(Qt6 REQUIRED COMPONENTS QuickControls2 ... )` already
runs in the library; the issue is that plugin-specific subpackages
like `Qt6QtQuick2Plugin` aren't discovered by name). Not worth the
maintenance burden; deletion is cleaner.

**Eliminates:** ~42 of #1, plus #3 (the qmldir-not-found warning is
emitted from inside `qt_import_qml_plugins`'s lookup logic).

## Fix 2 — `OUTPUT_DIRECTORY` aligned with the QML module URI

**Files:**
- `libs/markoff-view-qml/CMakeLists.txt` `qt_add_qml_module(markoff_view_qml ...)` block (line 12)
- `libs/markoff-live/CMakeLists.txt` `qt_add_qml_module(markoff_live_render ...)` block (line 13)

**Proposed change:** add `OUTPUT_DIRECTORY` argument to each call:

```cmake
qt_add_qml_module(markoff_view_qml
    URI org.markoff.view.qml
    VERSION 1.0
    STATIC
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/org/markoff/view/qml
    SOURCES ...
    QML_FILES ...
)
```

```cmake
qt_add_qml_module(markoff_live_render
    URI org.markoff.live.render
    VERSION 1.0
    STATIC
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/org/markoff/live/render
    SOURCES ...
    QML_FILES ...
)
```

**Rationale:** Qt's QML tooling (qmllint, qmldir lookup, the Qt Creator
QML import resolver) expects a QML module's build-tree output to live
at `<some-dir>/<URI as path>/`. With no explicit `OUTPUT_DIRECTORY`,
`qt_add_qml_module` defaults to `${CMAKE_CURRENT_BINARY_DIR}`, which is
`build-dev/libs/markoff-view-qml/` — that path doesn't end in
`org/markoff/view/qml`, hence the warning at
`Qt6QmlMacros.cmake:1417`. Aligning the output dir to the URI subpath
makes qmllint happy and lets `qt_import_qml_plugins` (and its
descendants) find the qmldir during dependency resolution. Even after
removing the `qt_import_qml_plugins` call (Fix 1), this change still
helps qmllint and IDE tooling work correctly.

**Alternative on Qt 6.5+:** `qt_policy(SET QTP0001 NEW)` near the top
of each library's CMakeLists changes the default OUTPUT_DIRECTORY
behaviour to incorporate the URI path automatically. QTP0001 was
introduced in Qt 6.5; safe on the 6.8.2 we have here. The explicit
`OUTPUT_DIRECTORY` argument is more local and self-documenting; the
policy is more uniform across many modules. Pick one.

**Risk:** Low. Output paths under `build-dev/` change; nothing in the
source tree depends on them by hard-coded path. CMake clean rebuild
recommended after the change to clear the old layout. Verify install
rules (none in this tree currently) don't bake in the old path.

**Eliminates:** #2 (both warnings).

## Fix 3 — qmldir-not-found

This warning is emitted from inside `qt_import_qml_plugins` while it
walks dependencies. It disappears as a side-effect of either Fix 1
(removing the call) or Fix 2 (the qmldir then exists at the path the
lookup walks to). No standalone change needed.

## Fix 4 — `qt_policy(SET QTP0004 ...)`

**Files:** same two library CMakeLists files as Fix 2.

**Background:** QTP0004 (introduced in Qt 6.8) requires every directory
that contributes `.qml` files to a module to have its own `qmldir`.
Both libraries put delegate `.qml` files under `qml/delegates/`, which
has no `qmldir`. Not setting the policy = Qt warns and uses legacy
behaviour.

**Two options, pick one:**

**Option A (forward-compatible).** Set the policy NEW and add empty
qmldir files:

```cmake
qt_policy(SET QTP0004 NEW)
```

then create `libs/markoff-view-qml/qml/delegates/qmldir` and
`libs/markoff-live/qml/delegates/qmldir`, each containing at
minimum:
```
module org.markoff.view.qml.delegates
```
(or whatever sub-URI Qt expects — the CMake error if you get this
wrong is informative). This is the recommended Qt 6.8+ way and
becomes mandatory in some future Qt release.

**Option B (silence-only).** Set the policy OLD to keep legacy
behaviour and no extra qmldir files:

```cmake
qt_policy(SET QTP0004 OLD)
```

Pragmatic if you don't want to chase the qmldir naming details; you
*will* eventually have to do Option A when Qt removes the legacy path.

**Risk:** `qt_policy(SET QTP0004 ...)` will warn ("unknown Qt policy
QTP0004") on Qt 6.7 and earlier. If the other build environment is on
Qt < 6.8, gate the call:

```cmake
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()
```

**Eliminates:** #4 (both warnings).

## Fix 5 — Disable AUTOGEN on `ryml` and `c4core`

**File:** root `CMakeLists.txt`.

**Cause:** Lines 19-20 set `CMAKE_AUTOMOC ON` and `CMAKE_AUTOUIC ON` at
project scope, *before* `add_subdirectory(libs/rapidyaml)` at line 37.
ryml and its bundled c4core are non-Qt third-party C++ libraries; they
inherit the ON flag, fail to find Qt for themselves, and emit dev
warnings.

**Proposed change (least invasive):** after the rapidyaml add_subdirectory,
opt those targets out:

```cmake
add_subdirectory(libs/rapidyaml)

# rapidyaml/c4core are non-Qt; opt out of inherited AUTOGEN.
if(TARGET ryml)
    set_target_properties(ryml PROPERTIES AUTOMOC OFF AUTOUIC OFF)
endif()
if(TARGET c4core)
    set_target_properties(c4core PROPERTIES AUTOMOC OFF AUTOUIC OFF)
endif()
```

The `if(TARGET ...)` guards keep this safe if the parent project
provides ryml/c4core via `find_package` (the existing
`if(NOT TARGET collabtext)` pattern at line 42 establishes that
defensive precedent).

**Alternative:** move the `set(CMAKE_AUTOMOC ON)` /
`set(CMAKE_AUTOUIC ON)` lines below `add_subdirectory(libs/rapidyaml)`
so the third-party subtree never sees the flag. Equivalent in effect;
slightly less explicit.

**Risk:** None for the in-tree build (ryml has no Qt code). For the
parent-project case, the guarded form is safe — if the parent has
already configured ryml as a Qt target for some reason, our
unconditional `set_target_properties` would clobber that, but the
`if(TARGET ryml)` form still does, so the alternative
(`add_subdirectory`-ordering fix) is marginally safer for parent
projects. Pick whichever the remote agents prefer.

**Eliminates:** #5 (both warnings).

## Suggested order if landing all five

1. Fix 1 (delete `qt_import_qml_plugins`) — kills the bulk of the noise
   (~44 warnings) immediately.
2. Fix 2 (`OUTPUT_DIRECTORY`) — cleans up the remaining QML module
   warnings and helps qmllint/IDE.
3. Fix 4 (`QTP0004`) — pick OLD or NEW based on appetite for the qmldir
   work. NEW + empty qmldirs is the durable answer.
4. Fix 5 (AUTOGEN on ryml/c4core) — independent of the others, can
   land standalone.
5. Verify with a clean configure: `rm -rf build-dev && cmake -S . -B
   build-dev && cmake --build build-dev -j && ctest --test-dir
   build-dev -j -E "tst_realistic|tst_benchmark"`. Expect zero
   `CMake Warning` lines (ignoring the `-Wno-dev` class if you choose
   to leave QTP0004 OLD).

## What this diagnosis did NOT touch

- No source files modified.
- No CMakeLists files modified.
- No new files outside `docs/` and `build-dev/`.
- The `build-dev/` configure was rerun (in-place; idempotent) to
  capture the warning text verbatim.

## Cross-environment open questions

Please reply to / annotate these before applying:

1. What Qt version is the master/CI environment on? (Affects QTP0001
   and QTP0004 availability.)
2. Are the two test apps (`markoff-view-qml-app`,
   `markoff-live-app`) actually launched in CI, or are they
   build-only? (Determines how aggressively we should test Fix 1's
   plugin-loading behaviour.)
3. Is there an existing `qt_policy` call elsewhere I should align
   with? (`grep -rn qt_policy libs/ apps/` came up empty here, but
   the remote agents may have something pending.)
4. Any objection to the `OUTPUT_DIRECTORY` explicit form vs.
   `qt_policy(SET QTP0001 NEW)`? Explicit form was chosen above for
   self-documentation; the policy form is shorter and uniform if
   more QML modules are coming.
