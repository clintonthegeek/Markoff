# Phase B — Corbomite Migration Plan

> **For agentic workers:** this plan is mostly executed in the Corbomite
> repo at `/home/clinton/dev/Corbomite/`, with one enabling commit on
> the Markoff side first. Use superpowers:subagent-driven-development
> or superpowers:executing-plans to work through tasks.

**Goal:** bump Corbomite's Markoff submodule pin to `v0.2.0` (the Phase A merge), flip CorbomiteApp onto the new tri-view target names (`Markoff::Live` / `Markoff::Source` / `Markoff::Reading`), delete Corbomite's in-tree copies of qutepart-corbomite and readingview, and reconnect markoff-reading to the real `Corbomite::Core` / `mmdr` deps via a CMake option override.

**Reference spec:** `docs/specs/2026-04-20-phase-b-corbomite-migration.md` (at Markoff top level). Read the spec first — it contains the breaking-change manifest, the file-by-file migration map, and recorded decisions (switch mechanism, mmdr ownership, Storage linkage, version tag).

**Tech stack:** Qt6.8+, C++20, CMake 3.19+, Rust toolchain for mmdr (Corbomite-side only). Tests use QtTest with `QT_QPA_PLATFORM=offscreen`.

**Scope discipline:** this plan is intentionally a target-rename + header-path migration. No behavior changes. No new features. No `MarkoffDocument` adoption (Phase C does that). No abstract-interface extraction for `Corbomite::Core` types (Phase C does that). If a task tempts you to redesign anything, **stop and escalate** — scope creep here blocks the clean Phase C starting point.

---

## Prerequisites

- Markoff repo at `/home/clinton/dev/Markoff/` has Phase A merged to `master` and tagged `v0.2.0`.
- Corbomite working tree is clean enough to start a branch (modified files outside the migration zone are fine; uncommitted changes under `src/editor/`, `src/CMakeLists.txt`, `libs/qutepart-corbomite/`, or `libs/readingview/` must be committed or stashed first).
- The in-flight Corbomite agent working in `src/plugins/bookmarks/` does not overlap the migration zone. No coordination needed there.

---

## Task 1 (Markoff-side): add `MARKOFF_READING_USE_REAL_COREDEPS` CMake option

**Repo:** `/home/clinton/dev/Markoff/` (a worktree or `master` directly, implementer's choice).

**Files:**
- Modify: `libs/markoff-reading/CMakeLists.txt`

**Step 1: Add the option**

Near the top of `libs/markoff-reading/CMakeLists.txt`, after `project(...)`:

```cmake
option(MARKOFF_READING_USE_REAL_COREDEPS
    "Drop the Corbomite::Core / mmdr stubs and link the real implementations \
provided by a consuming project (e.g. CorbomiteApp). Off for standalone \
builds; on when Markoff is a submodule inside a host that supplies these \
types."
    OFF)
```

**Step 2: Conditionally swap stubs for real deps**

Find the block that adds `stubs/` to `target_include_directories(markoff_reading ...)`. Wrap it:

```cmake
if(MARKOFF_READING_USE_REAL_COREDEPS)
    # Real deps mode — host must provide Corbomite::Core, and mmdr target.
    target_link_libraries(markoff_reading PRIVATE Corbomite::Core mmdr)
else()
    # Standalone mode — use the stubs that ship with Markoff.
    target_include_directories(markoff_reading PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stubs>)
endif()
```

Adjust the exact phrasing to match what the current CMakeLists looks like (it likely exposes stubs as `PUBLIC` already — check before editing). Preserve whatever visibility the current code has.

**Step 3: Verify both configurations build**

```bash
cd /home/clinton/dev/Markoff
rm -rf build-dev
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev
cd build-dev && ctest --output-on-failure
```

Expected: default-OFF build is identical to post-Phase-A (70/70 tests pass). The ON path will be validated from Corbomite in Task 3 — don't try to build it standalone because Markoff has no `Corbomite::Core` target.

**Step 4: Commit + tag**

```
markoff-reading: add MARKOFF_READING_USE_REAL_COREDEPS option

Configure-time switch between stubs (standalone) and host-supplied
Corbomite::Core + mmdr (CorbomiteApp). Default OFF preserves the
Phase A build exactly; ON drops stubs from the include path and
links the real targets.

This is the Phase B bridge; Phase C replaces it with a
dependency-injection seam in markoff-reading's public API.

Per docs/specs/2026-04-20-phase-b-corbomite-migration.md.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

Then:

```bash
git tag -a v0.2.0 -m "Phase A (tri-view unified API) + Phase B stub override"
```

---

## Task 2 (Corbomite-side): bump submodule pin, enable override, fix build

**Repo:** `/home/clinton/dev/Corbomite/`.

Start a branch: `git checkout -b feature/markoff-v0.2.0-migration`.

### Step 1: Bump the Markoff pin

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git fetch
git checkout v0.2.0
cd /home/clinton/dev/Corbomite
git add libs/markoff-family
```

Don't commit yet — Task 2 is a single commit with the pin bump + CMake changes.

### Step 2: Enable the real-deps switch in Corbomite's top-level CMakeLists

Locate Corbomite's top-level `CMakeLists.txt` (`/home/clinton/dev/Corbomite/CMakeLists.txt`). Find where `libs/markoff-family` is added (likely `add_subdirectory(libs/markoff-family)`). Immediately before that line, add:

```cmake
# Tell markoff-reading to use Corbomite::Core + mmdr rather than its stubs.
set(MARKOFF_READING_USE_REAL_COREDEPS ON CACHE BOOL "" FORCE)
```

If there's a conventional place higher up where Corbomite sets similar cache flags, put it there instead.

### Step 3: Update CorbomiteApp link line

Edit `src/CMakeLists.txt` (around line 73):

```diff
 target_link_libraries(CorbomiteApp PRIVATE
     ...
-    Markoff::Markoff
-    Corbomite::ReadingView
-    Corbomite::QutepartSource
+    Markoff::Live
+    Markoff::Reading
+    Markoff::Source
     ...
 )
```

### Step 4: Build

```bash
cmake --build build
```

Expect compile errors from `src/editor/MarkdownView.cpp` and `src/editor/NoteEditorWidget.{h,cpp}` because their includes and namespace usages haven't been renamed yet. That's Task 3. For now, verify the CMake errors are clean (linkage resolves, targets exist). If CMake can't find `Markoff::Reading` or `Markoff::Source`, stop — something is wrong with the pin bump or the submodule include path.

### Step 5: Commit the partial state

```
Phase B step 1: bump Markoff to v0.2.0 and flip CMake targets

Bumps libs/markoff-family from <old SHA> to v0.2.0. Enables
MARKOFF_READING_USE_REAL_COREDEPS so markoff-reading links Corbomite::Core
and mmdr instead of its stub implementations.

CorbomiteApp link line flipped:
  Markoff::Markoff          -> Markoff::Live
  Corbomite::ReadingView    -> Markoff::Reading
  Corbomite::QutepartSource -> Markoff::Source

Source changes (include paths, namespace renames) follow in the next
commit.

Per Markoff/docs/specs/2026-04-20-phase-b-corbomite-migration.md.
```

---

## Task 3 (Corbomite-side): rename includes + namespaces in `src/editor/`

**Files:**
- `src/editor/MarkdownView.cpp`
- `src/editor/NoteEditorWidget.h`
- `src/editor/NoteEditorWidget.cpp`
- `src/editor/SourceEditor.{h,cpp}` — **expected no-op**, but verify (see spec §"Migration map" #4)

### Step 1: Rename the readingview include

Two files to edit:

```diff
- #include <corbomite/readingview/ReadingView.h>
+ #include <markoff/reading/ReadingView.h>
```

Apply to `src/editor/MarkdownView.cpp:10` and `src/editor/NoteEditorWidget.cpp:16` (line numbers approximate — grep to confirm).

### Step 2: Rename the namespace

```bash
cd /home/clinton/dev/Corbomite
grep -rln 'Corbomite::ReadingView' src/ | while read -r f; do
    sed -i 's|Corbomite::ReadingView|Markoff::Reading|g' "$f"
done
```

Review `git diff`. Expected hits: a few type references in `NoteEditorWidget.h` and `MarkdownView.cpp`. Nothing in plugins.

### Step 3: Verify SourceEditor is unchanged

```bash
grep -n '#include' src/editor/SourceEditor.{h,cpp}
```

`#include <qutepart.h>` stays as is. Markoff's `Markoff::Source` re-exposes the same include path. If this file needed any other Markoff/Corbomite-namespace change, the spec didn't miss it — escalate.

### Step 4: Build + test

```bash
cmake --build build
cd build && ctest --output-on-failure
```

Expected: clean build, all tests pass at parity with pre-migration.

### Step 5: Commit

```
Phase B step 2: rename readingview includes + namespace in src/editor/

Corbomite::ReadingView::ReadingView -> Markoff::Reading::ReadingView
<corbomite/readingview/ReadingView.h> -> <markoff/reading/ReadingView.h>

Applies to src/editor/MarkdownView.cpp and NoteEditorWidget.{h,cpp}.
SourceEditor.{h,cpp} unchanged — Markoff::Source preserves the
<qutepart/qutepart.h> include path and the Qutepart:: namespace.

Per Markoff/docs/specs/2026-04-20-phase-b-corbomite-migration.md.
```

---

## Task 4 (Corbomite-side): delete `libs/qutepart-corbomite/` and `libs/readingview/`

After Task 3 CorbomiteApp builds and runs cleanly on the Markoff-hosted leaf widgets. The in-tree copies are now dead code.

### Step 1: Remove from CMake

Find the `add_subdirectory(libs/qutepart-corbomite)` and `add_subdirectory(libs/readingview)` lines (probably in `libs/CMakeLists.txt` or the top-level) and delete them. Also remove any `Corbomite::QutepartSource` / `Corbomite::ReadingView` aliases or `add_library(... ALIAS ...)` lines referencing the old targets.

### Step 2: Delete the directories

```bash
cd /home/clinton/dev/Corbomite
git rm -r libs/qutepart-corbomite libs/readingview
```

### Step 3: Sanity-grep for stragglers

```bash
grep -rln 'qutepart-corbomite\|Corbomite::QutepartSource\|Corbomite::ReadingView\|corbomite/readingview' \
    --exclude-dir=.worktrees \
    --exclude-dir=.git \
    .
```

Expected: zero hits (or only hits in doc files / changelogs, which are informational and fine). If any live `.cpp`/`.h`/CMakeLists hits remain, Task 3 missed something.

### Step 4: Build + test + commit

```
Phase B step 3: remove Corbomite copies of qutepart-corbomite and readingview

Both are now provided by Markoff (Markoff::Source and Markoff::Reading).
CorbomiteApp adopted them in the previous commits; these directories are
dead code.

Per Markoff/docs/specs/2026-04-20-phase-b-corbomite-migration.md.
```

---

## Task 5 (Corbomite-side): validate Storage linkage drop

Per the spec, `Corbomite::Storage` is linked by markoff-reading's CMakeLists but not used by its sources. Confirm empirically.

### Step 1: Check whether current build still links Storage

In Markoff's `libs/markoff-reading/CMakeLists.txt`, find the `target_link_libraries(markoff_reading ... Corbomite::Storage ...)` line. Remove it.

### Step 2: Rebuild

If CorbomiteApp still builds and every test passes, drop Storage permanently (land a follow-up commit on the Markoff side) and delete `libs/markoff-reading/stubs/corbomite/storage/`.

If something fails, revert Step 1 and document the caller in the commit message. Storage gets re-added in Phase C (proper absorption).

### Step 3: Commit (Markoff side if changes stick)

```
markoff-reading: drop Corbomite::Storage link (unused)

Phase A survey flagged the Storage link as unused by readingview
sources; Phase B Task 5 confirmed empirically by building CorbomiteApp
with Storage omitted. Removing the dead link + the stubs/corbomite/storage/
shim headers.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

If confirmation was the only outcome (link actually needed), commit a no-op note in the spec instead.

---

## Task 6 (Corbomite-side): re-enable the Phase A-skipped reading tests

With `MARKOFF_READING_USE_REAL_COREDEPS=ON`, the four tests that depend on real Core/mmdr should pass.

### Step 1: Edit the Markoff-side test registration

In `libs/markoff-reading/tests/CMakeLists.txt`, find the four entries commented with `# TODO Phase B:` and guard them with the same switch:

```cmake
if(MARKOFF_READING_USE_REAL_COREDEPS)
    add_executable(tst_sectionlayout_mermaid tst_sectionlayout_mermaid.cpp)
    ...
endif()
```

Repeat for `tst_readingview_mermaid_registered`, `tst_readingview_embedrenderer`, `tst_readingview_embed_builtins`.

### Step 2: Run full ctest from a Corbomite build

All four previously-skipped tests should now be discovered and pass. If any fail, root-cause — it's likely a stub vs. real shape mismatch that needs the Markoff-side stub header widened, or a real bug in readingview surfaced by the absorption.

### Step 3: Commit

```
markoff-reading: guard Phase-B-conditional tests on real-coredeps switch

Four tests (Mermaid, embed-renderer, embed builtins) require
Corbomite::Core + mmdr at link time. They now build and run whenever
MARKOFF_READING_USE_REAL_COREDEPS=ON — i.e. inside CorbomiteApp's
build — and are skipped in standalone Markoff builds.

Per docs/specs/2026-04-20-phase-b-corbomite-migration.md.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Phase B — Acceptance

- Corbomite's `libs/markoff-family` submodule is pinned to Markoff `v0.2.0`.
- `MARKOFF_READING_USE_REAL_COREDEPS=ON` is set in Corbomite's CMake.
- `CorbomiteApp` builds clean and every Corbomite test passes.
- CorbomiteApp's link line uses `Markoff::Live` + `Markoff::Source` + `Markoff::Reading`; `Corbomite::ReadingView` and `Corbomite::QutepartSource` are gone.
- `libs/qutepart-corbomite/` and `libs/readingview/` are removed from the Corbomite tree (deleted on branch; merge drops them from `main`).
- Grep confirms zero live references to `Corbomite::ReadingView::`, `Corbomite::QutepartSource`, or `<corbomite/readingview/...>` anywhere in Corbomite outside docs/changelogs.
- The four Phase-A-skipped markoff-reading tests pass when run inside a CorbomiteApp build.
- Mermaid rendering, embed subpath slicing, and `MarkdownRenderChild` mounting behave at parity with pre-migration Corbomite (stub runtime-brokenness documented in Phase A is gone in the real-deps build).

## Deferred to Phase C

- Replace `MARKOFF_READING_USE_REAL_COREDEPS` with a DI seam in markoff-reading's public API (`IEmbedRegistry`, `ICodeBlockProcessorRegistry`, `IVaultResourceProvider`, etc.). Retire the CMake option + the stub header tree.
- Absorb `Corbomite::Core`-shaped types (or their abstracted interfaces) into Markoff proper.
- Consolidate `Theme` / `ResourceProvider` / `LinkResolver` across the three leaf widgets — same refactor pass.
- Markoff becomes content-authoritative: `MarkoffDocument` adoption in all three leaves, `setDocument()` wiring actual text binding.
- Async parse worker, precise pixel↔visual-line scroll conversion, source-offset↔per-block cursor translation.
- Vendor mmdr into Markoff? (Revisit if Mermaid support needs to ship from Markoff-only consumers.)
