> **Status: completed.** All 25 `tst_foundation_*` tests pass; `markoff_core` is feature-complete per spec §12. See TODO.md "Phase 13 acceptance passed" entry for confirmation. Do not execute.

# Markoff Foundation Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the `markoff-foundation` library — a CRDT-backed canonical text + AST + theme + commands + services library that replaces the leaky `markoff-core` contract from the existing Markoff family.

**Architecture:** Foundation depends on `~/dev/collabtext`'s `Buffer` for canonical text storage and `Anchor` for cursor primitives. Sessions on `MarkoffDocument` track per-view ephemeral state (cursor, selections, scroll, folds). Editing is via free `Markoff::Cmd::*` functions plus a `CommandFacade` Q_OBJECT for QML. Search, code-block highlighting, link routing, and completion detection are foundation services. Views (built in a separate plan) are subscribers — no view base class.

**Tech Stack:** C++20, Qt6 (Core, Gui, Test), KF6::SyntaxHighlighting, CMake 3.19+. Test framework Qt Test. CRDT engine `~/dev/collabtext` (sibling lib, symlinked).

**Branch:** `exploration/new-foundation` (worktree `.worktrees/foundation-exploration`).

**Related docs:**
- Spec: [`docs/specs/2026-04-28-foundation-design.md`](../specs/2026-04-28-foundation-design.md)
- Audit input: [`docs/2026-04-28-codebase-audit.md`](../2026-04-28-codebase-audit.md)
- collabtext architecture: `~/dev/collabtext/docs/ARCHITECTURE.md`

**Out of scope for this plan:** the `markoff-view-qml` POC view (separate follow-on plan). This plan produces the foundation library + unit tests; viability validation through QML happens in plan 2.

---

## File Structure

### Files created

```
libs/collabtext                                   (symlink → ~/dev/collabtext)
libs/markoff-core/
  CMakeLists.txt
  include/markoff-foundation/
    Origin.h
    MarkoffEdit.h
    AnchorJson.h               — Anchor toJson/fromJson free functions
    Selection.h
    FoldRef.h
    Session.h
    SessionParams.h
    MarkoffDocument.h
    Theme.h
    LinkKind.h
    LinkActivation.h
    LinkService.h
    DefaultLinkService.h
    CodeTokenKind.h
    CodeSpan.h
    SyntaxHighlightService.h
    Kf6SyntaxHighlightService.h
    RenderedBlock.h
    CodeBlockProcessor.h
    CodeBlockProcessorRegistry.h
    CompletionTrigger.h
    CompletionContext.h
    CompletionCandidate.h
    CompletionDetector.h
    CompletionProvider.h
    CompletionRegistry.h
    EmojiCompletionProvider.h
    SearchEngine.h
    ReplaceController.h
    TextSpan.h
    CommandFacade.h
    Cmd.h                        (aggregate include)
    Cmd/InlineFormat.h
    Cmd/Block.h
    Cmd/Insert.h
    Cmd/Edit.h
    MarkoffServices.h
    MarkoffFoundationExport.h
  src/
    AnchorJson.cpp
    MarkoffEdit.cpp
    Selection.cpp
    FoldRef.cpp
    Session.cpp
    MarkoffDocument.cpp
    MarkoffDocumentPrivate.h     (impl-side header)
    ParsePoolWorker.cpp
    ParsePoolWorker.h
    ParsePool.cpp
    Theme.cpp
    LinkService.cpp
    DefaultLinkService.cpp
    SyntaxHighlightService.cpp   (interface vtable anchor)
    Kf6SyntaxHighlightService.cpp
    CodeBlockProcessorRegistry.cpp
    CompletionDetector.cpp
    CompletionRegistry.cpp
    EmojiCompletionProvider.cpp
    EmojiData.h                  (header-only emoji table)
    SearchEngine.cpp
    ReplaceController.cpp
    Cmd/InlineFormat.cpp
    Cmd/Block.cpp
    Cmd/Insert.cpp
    Cmd/Edit.cpp
    Cmd/Helpers.cpp
    CommandFacade.cpp
  tests/
    CMakeLists.txt
    fixtures/
      simple.md
      headings.md
      code_blocks.md
      tables.md
      mixed_inline.md
      frontmatter.md
    tst_anchor_json.cpp
    tst_markoff_edit.cpp
    tst_selection.cpp
    tst_fold_ref.cpp
    tst_markoff_document.cpp
    tst_markoff_document_property.cpp
    tst_session.cpp
    tst_theme.cpp
    tst_link_service.cpp
    tst_cmd_inline_format.cpp
    tst_cmd_block.cpp
    tst_cmd_insert.cpp
    tst_search_engine.cpp
    tst_replace_controller.cpp
    tst_syntax_highlight_service.cpp
    tst_code_block_processor_registry.cpp
    tst_completion_detector.cpp
    tst_completion_registry.cpp
    tst_parse_pool.cpp
```

### Files modified

```
CMakeLists.txt        — add libs/collabtext + libs/markoff-core entries
```

### Files NOT modified

The existing `libs/markoff-core/`, `libs/markoff-live/`, `libs/markoff-source/`, `libs/markoff-reading/`, and `libs/markoff-parser/` directories are untouched on this branch. Master continues to maintain them.

---

## Phase 1 — Scaffolding (Tasks 1–3)

### Task 1: Wire collabtext as a sibling library

**Files:**
- Create: `libs/collabtext` (symlink)
- Modify: `CMakeLists.txt` (top-level)

- [ ] **Step 1: Create the symlink to collabtext**

```bash
ln -s /home/clinton/dev/collabtext libs/collabtext
ls -l libs/collabtext  # verify symlink target
```

Expected: `libs/collabtext -> /home/clinton/dev/collabtext`.

- [ ] **Step 2: Update top-level CMakeLists.txt to add collabtext + foundation**

Open `CMakeLists.txt`. Locate the `add_subdirectory` block (currently lines around `add_subdirectory(libs/rapidyaml)` through the existing Markoff libs). Insert collabtext + foundation BEFORE the existing markoff libs:

Add the following after `add_subdirectory(libs/markoff-parser)`:

```cmake
# collabtext — CRDT engine, sibling library symlinked from /home/clinton/dev/collabtext.
# Defensive guard against double-add by parent projects.
if(NOT TARGET collabtext)
    add_subdirectory(libs/collabtext)
endif()

# markoff-foundation — new foundation library replacing markoff-core's role.
# Lives alongside the existing markoff-core, which remains master-maintained.
add_subdirectory(libs/markoff-core)
```

- [ ] **Step 3: Verify cmake configure picks up the new path**

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
rm -rf build-dev
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -30
```

Expected: cmake configure fails (because `libs/markoff-core/` doesn't exist yet — that's Task 2). The collabtext add_subdirectory itself should succeed (collabtext has its own CMakeLists.txt).

If you see `Cannot find source directory libs/markoff-core`, that's expected. Move to Task 2.

- [ ] **Step 4: Commit**

```bash
git add libs/collabtext CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(foundation): wire collabtext sibling lib + scaffold foundation entry

Adds libs/collabtext as a symlink to /home/clinton/dev/collabtext and
adds the collabtext/markoff-foundation add_subdirectory entries to the
top-level CMakeLists. The defensive `if(NOT TARGET collabtext)` guard
matches the existing pattern for jkqtmathtext on master.

The foundation library directory itself is created in the next task.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Scaffold libs/markoff-core/ skeleton

**Files:**
- Create: `libs/markoff-core/CMakeLists.txt`
- Create: `libs/markoff-core/include/markoff-foundation/MarkoffFoundationExport.h`
- Create: `libs/markoff-core/src/.gitkeep`

- [ ] **Step 1: Create the directory tree**

```bash
mkdir -p libs/markoff-core/include/markoff-foundation/Cmd
mkdir -p libs/markoff-core/src/Cmd
mkdir -p libs/markoff-core/tests/fixtures
```

- [ ] **Step 2: Create the export macro header**

Create `libs/markoff-core/include/markoff-foundation/MarkoffFoundationExport.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Foundation is built STATIC; the export macro is a no-op for now.
// Kept as a defined macro so a future SHARED build can populate it
// without changing every header.
#define MARKOFF_FOUNDATION_EXPORT
```

- [ ] **Step 3: Create the library CMakeLists**

Create `libs/markoff-core/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.19)
project(markoff_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)

# Library target: STATIC so it can be embedded in test apps + future views.
# Sources are added incrementally as tasks land. Initial skeleton is just
# the export header so the target builds.
add_library(markoff_core STATIC
    include/markoff-foundation/MarkoffFoundationExport.h
)

target_include_directories(markoff_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(markoff_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        KF6::SyntaxHighlighting
        collabtext
        MarkoffParser::MarkoffParser
)

# Alias under the Markoff:: namespace for consumers.
add_library(Markoff::Core ALIAS markoff_core)

# Tests.
if(NOT DEFINED MARKOFF_FOUNDATION_BUILD_TESTS)
    set(MARKOFF_FOUNDATION_BUILD_TESTS ON)
endif()
if(MARKOFF_FOUNDATION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 4: Create the tests CMakeLists skeleton**

Create `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.19)
project(markoff_core_tests LANGUAGES CXX)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Test executables are added incrementally as tasks land.
# Convention:
#   add_executable(tst_FOO tst_foo.cpp)
#   add_test(NAME tst_FOO COMMAND tst_FOO)
#   target_link_libraries(tst_FOO PRIVATE Qt6::Test markoff_core)
#   set_tests_properties(tst_FOO PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 5: Add a placeholder src file so the static lib has at least one .o**

Create `libs/markoff-core/src/foundation_anchor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Placeholder translation unit so the markoff_core static lib
// has at least one object file before real sources land. Removed as
// soon as the first real source file is added.
namespace Markoff::Detail {
[[maybe_unused]] int foundation_anchor_marker = 0;
}
```

Add it to the library target. Edit `libs/markoff-core/CMakeLists.txt`'s `add_library` block to read:

```cmake
add_library(markoff_core STATIC
    include/markoff-foundation/MarkoffFoundationExport.h
    src/foundation_anchor.cpp
)
```

- [ ] **Step 6: Configure + build to verify the skeleton compiles**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff_core -j 2>&1 | tail -10
```

Expected: target `markoff_core` builds successfully. Output includes a libmarkoff_core.a (or platform equivalent).

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/
git commit -m "$(cat <<'EOF'
feat(foundation): scaffold markoff-foundation library skeleton

CMakeLists, export macro, placeholder anchor source. Library target
markoff_core links Qt6, KF6::SyntaxHighlighting, collabtext, and
MarkoffParser::MarkoffParser. Tests directory and CMakeLists scaffolded
but no test targets yet — added incrementally.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Add the .clangd hint and update .gitignore for the worktree

**Files:**
- Modify: `.clangd` (top-level, points clangd at the build dir)
- Verify: `.gitignore`

- [ ] **Step 1: Verify .clangd points at build-dev**

```bash
cat .clangd
```

Expected: `CompilationDatabase: build-dev` (already correct on this branch — it was set up in master and inherited).

- [ ] **Step 2: Verify compile_commands.json symlink**

```bash
ls -l compile_commands.json
```

Expected: `compile_commands.json -> build-dev/compile_commands.json`. If absent:

```bash
ln -sf build-dev/compile_commands.json compile_commands.json
```

- [ ] **Step 3: No commit needed if .clangd was already correct**

If you made the symlink in step 2, commit:

```bash
git add compile_commands.json
git commit -m "chore: ensure compile_commands.json symlink"
```

---

## Phase 2 — Core value types (Tasks 4–8)

### Task 4: Add Origin enum

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/Origin.h`

- [ ] **Step 1: Create the Origin header**

Create `libs/markoff-core/include/markoff-foundation/Origin.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// Origin of a content reset on MarkoffDocument. Determines undo-stack
/// behavior on resetContent() — see MarkoffDocument.h for full semantics.
enum class Origin {
    FirstOpen,              ///< empty undo stack, no command pushed
    ExternalReloadClean,    ///< stack cleared (file changed; no pending edits)
    ExternalReloadResolved, ///< stack cleared (post-merge-modal resolution)
    UserRevertToSaved,      ///< pushes one mega edit so Ctrl+Z reverses it
    TestFixture,            ///< stack cleared (test setup)
};

}  // namespace Markoff
```

- [ ] **Step 2: No test needed yet — Origin is exercised via MarkoffDocument**

Origin is a plain enum; it has no behavior to test in isolation. Tests for `MarkoffDocument::resetContent` (Task 16) cover it via the Origin parameter.

- [ ] **Step 3: Add header to library target so AUTOMOC sees it**

Edit `libs/markoff-core/CMakeLists.txt`'s `add_library` block. Add `include/markoff-foundation/Origin.h` to the source list:

```cmake
add_library(markoff_core STATIC
    include/markoff-foundation/MarkoffFoundationExport.h
    include/markoff-foundation/Origin.h
    src/foundation_anchor.cpp
)
```

- [ ] **Step 4: Verify build**

```bash
cmake --build build-dev --target markoff_core -j 2>&1 | tail -5
```

Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/Origin.h \
        libs/markoff-core/CMakeLists.txt
git commit -m "feat(foundation): add Origin enum"
```

---

### Task 5: Add MarkoffEdit struct + JSON roundtrip + tests

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/MarkoffEdit.h`
- Create: `libs/markoff-core/src/MarkoffEdit.cpp`
- Create: `libs/markoff-core/tests/tst_markoff_edit.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/tst_markoff_edit.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QJsonObject>
#include <QJsonDocument>

#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstMarkoffEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void insertion_classifies_correctly() {
        MarkoffEdit e;
        e.oldStart = 5;
        e.oldEnd = 5;
        e.newText = "x";
        QVERIFY(e.isInsertion());
        QVERIFY(!e.isDeletion());
        QVERIFY(!e.isReplacement());
    }

    void deletion_classifies_correctly() {
        MarkoffEdit e;
        e.oldStart = 3;
        e.oldEnd = 7;
        e.newText.clear();
        QVERIFY(!e.isInsertion());
        QVERIFY(e.isDeletion());
        QVERIFY(!e.isReplacement());
    }

    void replacement_classifies_correctly() {
        MarkoffEdit e;
        e.oldStart = 3;
        e.oldEnd = 7;
        e.newText = "abc";
        QVERIFY(!e.isInsertion());
        QVERIFY(!e.isDeletion());
        QVERIFY(e.isReplacement());
    }

    void json_roundtrip_insertion() {
        MarkoffEdit a;
        a.oldStart = 12;
        a.oldEnd = 12;
        a.newText = QByteArray("héllo", 6);  // UTF-8: é is 2 bytes
        const QJsonObject json = a.toJson();
        const MarkoffEdit b = MarkoffEdit::fromJson(json);
        QCOMPARE(b.oldStart, a.oldStart);
        QCOMPARE(b.oldEnd, a.oldEnd);
        QCOMPARE(b.newText, a.newText);
    }

    void json_roundtrip_replacement() {
        MarkoffEdit a;
        a.oldStart = 0;
        a.oldEnd = 11;
        a.newText = "goodbye";
        const QJsonObject json = a.toJson();
        const MarkoffEdit b = MarkoffEdit::fromJson(json);
        QCOMPARE(b.oldStart, a.oldStart);
        QCOMPARE(b.oldEnd, a.oldEnd);
        QCOMPARE(b.newText, a.newText);
    }

    void json_roundtrip_empty_deletion() {
        MarkoffEdit a;
        a.oldStart = 4;
        a.oldEnd = 9;
        a.newText.clear();
        const QJsonObject json = a.toJson();
        const MarkoffEdit b = MarkoffEdit::fromJson(json);
        QCOMPARE(b.oldStart, a.oldStart);
        QCOMPARE(b.oldEnd, a.oldEnd);
        QCOMPARE(b.newText, a.newText);
    }
};

QTEST_MAIN(TstMarkoffEdit)
#include "tst_markoff_edit.moc"
```

- [ ] **Step 2: Add test target to tests CMakeLists**

Edit `libs/markoff-core/tests/CMakeLists.txt`. Append:

```cmake
add_executable(tst_markoff_edit tst_markoff_edit.cpp)
add_test(NAME tst_markoff_edit COMMAND tst_markoff_edit)
target_link_libraries(tst_markoff_edit PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_edit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Verify test fails to build (no MarkoffEdit yet)**

```bash
cmake --build build-dev --target tst_markoff_edit -j 2>&1 | tail -10
```

Expected: build error: `markoff-foundation/MarkoffEdit.h: No such file or directory`.

- [ ] **Step 4: Create the header**

Create `libs/markoff-core/include/markoff-foundation/MarkoffEdit.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QtGlobal>

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// A surgical edit in OLD-text byte coordinates (UTF-8). Wrapper over
/// CollabText::Crdt::TextEdit for foundation-side use; carries the
/// minimum needed for views to apply the change to their display
/// representation.
///
/// oldStart and oldEnd are UTF-8 byte offsets. oldEnd >= oldStart.
/// newText is UTF-8 bytes (empty for pure deletion).
struct MARKOFF_FOUNDATION_EXPORT MarkoffEdit {
    quint32    oldStart = 0;
    quint32    oldEnd = 0;
    QByteArray newText;

    bool isInsertion() const { return oldStart == oldEnd && !newText.isEmpty(); }
    bool isDeletion() const { return oldStart != oldEnd && newText.isEmpty(); }
    bool isReplacement() const { return oldStart != oldEnd && !newText.isEmpty(); }

    QJsonObject toJson() const;
    static MarkoffEdit fromJson(const QJsonObject &);
};

}  // namespace Markoff
```

- [ ] **Step 5: Create the implementation**

Create `libs/markoff-core/src/MarkoffEdit.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/MarkoffEdit.h>

#include <QJsonValue>
#include <QString>

namespace Markoff {

QJsonObject MarkoffEdit::toJson() const
{
    QJsonObject obj;
    obj.insert("oldStart", static_cast<qint64>(oldStart));
    obj.insert("oldEnd", static_cast<qint64>(oldEnd));
    // newText is UTF-8 bytes; encode as base64 for binary-safe JSON.
    obj.insert("newText", QString::fromLatin1(newText.toBase64()));
    return obj;
}

MarkoffEdit MarkoffEdit::fromJson(const QJsonObject &obj)
{
    MarkoffEdit e;
    e.oldStart = static_cast<quint32>(obj.value("oldStart").toInteger());
    e.oldEnd = static_cast<quint32>(obj.value("oldEnd").toInteger());
    e.newText = QByteArray::fromBase64(obj.value("newText").toString().toLatin1());
    return e;
}

}  // namespace Markoff
```

- [ ] **Step 6: Add to library target**

Edit `libs/markoff-core/CMakeLists.txt`. Update `add_library` to include the new header + source. Also remove the placeholder anchor file (now we have a real source):

```cmake
add_library(markoff_core STATIC
    include/markoff-foundation/MarkoffFoundationExport.h
    include/markoff-foundation/Origin.h
    include/markoff-foundation/MarkoffEdit.h
    src/MarkoffEdit.cpp
)
```

Delete `libs/markoff-core/src/foundation_anchor.cpp`:

```bash
rm libs/markoff-core/src/foundation_anchor.cpp
```

- [ ] **Step 7: Build and run the test**

```bash
cmake --build build-dev --target tst_markoff_edit -j 2>&1 | tail -5
ctest --test-dir build-dev -R '^tst_markoff_edit$' --output-on-failure
```

Expected: builds clean. All 6 test cases pass.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/MarkoffEdit.h \
        libs/markoff-core/src/MarkoffEdit.cpp \
        libs/markoff-core/tests/tst_markoff_edit.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git rm libs/markoff-core/src/foundation_anchor.cpp
git commit -m "feat(foundation): add MarkoffEdit value type with JSON roundtrip"
```

---

### Task 6: Add Anchor JSON helpers + tests

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/AnchorJson.h`
- Create: `libs/markoff-core/src/AnchorJson.cpp`
- Create: `libs/markoff-core/tests/tst_anchor_json.cpp`

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/tst_anchor_json.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QJsonObject>

#include <markoff-foundation/AnchorJson.h>
#include <crdt/Anchor.h>

using namespace Markoff;
using CollabText::Crdt::Anchor;
using CollabText::Crdt::Bias;

class TstAnchorJson : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void roundtrip_basic() {
        Anchor a(7, 42, Bias::Left);
        const QJsonObject json = anchorToJson(a);
        const Anchor b = anchorFromJson(json);
        QCOMPARE(b.replica_id, a.replica_id);
        QCOMPARE(b.char_value, a.char_value);
        QCOMPARE(static_cast<int>(b.bias), static_cast<int>(a.bias));
    }

    void roundtrip_right_bias() {
        Anchor a(99, 1234, Bias::Right);
        const QJsonObject json = anchorToJson(a);
        const Anchor b = anchorFromJson(json);
        QCOMPARE(b.replica_id, a.replica_id);
        QCOMPARE(b.char_value, a.char_value);
        QCOMPARE(static_cast<int>(b.bias), static_cast<int>(a.bias));
    }

    void roundtrip_min_sentinel() {
        const Anchor a = Anchor::min();
        const QJsonObject json = anchorToJson(a);
        const Anchor b = anchorFromJson(json);
        QVERIFY(b.is_min());
    }

    void roundtrip_max_sentinel() {
        const Anchor a = Anchor::max();
        const QJsonObject json = anchorToJson(a);
        const Anchor b = anchorFromJson(json);
        QVERIFY(b.is_max());
    }
};

QTEST_MAIN(TstAnchorJson)
#include "tst_anchor_json.moc"
```

- [ ] **Step 2: Add test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_anchor_json tst_anchor_json.cpp)
add_test(NAME tst_anchor_json COMMAND tst_anchor_json)
target_link_libraries(tst_anchor_json PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_anchor_json PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Verify test fails to build**

```bash
cmake --build build-dev --target tst_anchor_json -j 2>&1 | tail -5
```

Expected: error: `markoff-foundation/AnchorJson.h: No such file or directory`.

- [ ] **Step 4: Create the header**

Create `libs/markoff-core/include/markoff-foundation/AnchorJson.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>

#include <crdt/Anchor.h>

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// Serialize a CollabText Anchor to JSON. Round-trips losslessly for all
/// anchor values including the min/max sentinels.
MARKOFF_FOUNDATION_EXPORT
QJsonObject anchorToJson(const CollabText::Crdt::Anchor &);

/// Deserialize a CollabText Anchor from JSON.
MARKOFF_FOUNDATION_EXPORT
CollabText::Crdt::Anchor anchorFromJson(const QJsonObject &);

}  // namespace Markoff
```

- [ ] **Step 5: Create the implementation**

Create `libs/markoff-core/src/AnchorJson.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/AnchorJson.h>

namespace Markoff {

using CollabText::Crdt::Anchor;
using CollabText::Crdt::Bias;

QJsonObject anchorToJson(const Anchor &a)
{
    QJsonObject obj;
    obj.insert("rid", static_cast<qint64>(a.replica_id));
    obj.insert("cv",  static_cast<qint64>(a.char_value));
    obj.insert("bias", a.bias == Bias::Right ? "R" : "L");
    return obj;
}

Anchor anchorFromJson(const QJsonObject &obj)
{
    Anchor a;
    a.replica_id = static_cast<uint16_t>(obj.value("rid").toInteger());
    a.char_value = static_cast<uint32_t>(obj.value("cv").toInteger());
    a.bias = (obj.value("bias").toString() == QStringLiteral("R"))
        ? Bias::Right : Bias::Left;
    return a;
}

}  // namespace Markoff
```

- [ ] **Step 6: Add to library target**

Edit `libs/markoff-core/CMakeLists.txt`. Add the header + source:

```cmake
add_library(markoff_core STATIC
    include/markoff-foundation/MarkoffFoundationExport.h
    include/markoff-foundation/Origin.h
    include/markoff-foundation/MarkoffEdit.h
    include/markoff-foundation/AnchorJson.h
    src/MarkoffEdit.cpp
    src/AnchorJson.cpp
)
```

- [ ] **Step 7: Build and run the test**

```bash
cmake --build build-dev --target tst_anchor_json -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_anchor_json$' --output-on-failure
```

Expected: builds clean, all 4 cases pass.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/AnchorJson.h \
        libs/markoff-core/src/AnchorJson.cpp \
        libs/markoff-core/tests/tst_anchor_json.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): add Anchor JSON serialization helpers"
```

---

### Task 7: Add Selection struct + tests

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/Selection.h`
- Create: `libs/markoff-core/src/Selection.cpp`
- Create: `libs/markoff-core/tests/tst_selection.cpp`

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/tst_selection.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QColor>

#include <markoff-foundation/Selection.h>
#include <crdt/Anchor.h>

using namespace Markoff;
using CollabText::Crdt::Anchor;
using CollabText::Crdt::Bias;

class TstSelection : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_when_anchor_equals_active() {
        Selection s;
        s.anchor = Anchor(1, 10, Bias::Left);
        s.active = Anchor(1, 10, Bias::Left);
        QVERIFY(s.isEmpty());
    }

    void not_empty_when_different() {
        Selection s;
        s.anchor = Anchor(1, 10, Bias::Left);
        s.active = Anchor(1, 12, Bias::Right);
        QVERIFY(!s.isEmpty());
    }

    void default_kind_is_primary() {
        Selection s;
        QCOMPARE(s.kind, Selection::Kind::Primary);
    }

    void presence_carries_metadata() {
        Selection s;
        s.kind = Selection::Kind::Presence;
        s.participantId = QStringLiteral("alice");
        s.participantLabel = QStringLiteral("Alice");
        s.presenceColor = QColor(Qt::magenta);
        s.cursorVersion = 42;
        QCOMPARE(s.kind, Selection::Kind::Presence);
        QCOMPARE(s.participantId, QStringLiteral("alice"));
        QCOMPARE(s.cursorVersion, quint64(42));
    }

    void json_roundtrip_primary() {
        Selection s;
        s.anchor = Anchor(2, 100, Bias::Left);
        s.active = Anchor(2, 105, Bias::Right);
        s.kind = Selection::Kind::Primary;
        const QJsonObject json = s.toJson();
        const Selection r = Selection::fromJson(json);
        QCOMPARE(r.anchor.replica_id, s.anchor.replica_id);
        QCOMPARE(r.anchor.char_value, s.anchor.char_value);
        QCOMPARE(r.active.replica_id, s.active.replica_id);
        QCOMPARE(r.active.char_value, s.active.char_value);
        QCOMPARE(r.kind, s.kind);
    }

    void json_roundtrip_presence() {
        Selection s;
        s.anchor = Anchor(7, 50, Bias::Left);
        s.active = Anchor(7, 50, Bias::Left);
        s.kind = Selection::Kind::Presence;
        s.participantId = QStringLiteral("bob");
        s.participantLabel = QStringLiteral("Bob");
        s.presenceColor = QColor(0, 255, 0);
        s.cursorVersion = 123;
        const QJsonObject json = s.toJson();
        const Selection r = Selection::fromJson(json);
        QCOMPARE(r.kind, Selection::Kind::Presence);
        QCOMPARE(r.participantId, s.participantId);
        QCOMPARE(r.participantLabel, s.participantLabel);
        QCOMPARE(r.presenceColor.name(), s.presenceColor.name());
        QCOMPARE(r.cursorVersion, s.cursorVersion);
    }
};

QTEST_MAIN(TstSelection)
#include "tst_selection.moc"
```

- [ ] **Step 2: Add test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_selection tst_selection.cpp)
add_test(NAME tst_selection COMMAND tst_selection)
target_link_libraries(tst_selection PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Create the header**

Create `libs/markoff-core/include/markoff-foundation/Selection.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <crdt/Anchor.h>

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// A kinded selection between two anchors. Foundation tracks zero or
/// more on each Session: one Primary, plus any number of Secondary,
/// SearchMatch, or Presence selections.
///
/// Anchor + active are CRDT anchors; they survive concurrent edits.
struct MARKOFF_FOUNDATION_EXPORT Selection {
    enum class Kind {
        Primary,        ///< editable, the typing one (one per session)
        Secondary,      ///< editable, multi-cursor (commands apply to all)
        SearchMatch,    ///< not editable, search controller manages
        Presence,       ///< not editable, remote-session indicator
    };

    CollabText::Crdt::Anchor anchor;
    CollabText::Crdt::Anchor active;
    Kind                     kind = Kind::Primary;

    // Only meaningful for Kind::Presence
    QString participantId;
    QString participantLabel;
    QColor  presenceColor;
    quint64 cursorVersion = 0;

    bool isEmpty() const;
    bool isReversed() const;

    QJsonObject toJson() const;
    static Selection fromJson(const QJsonObject &);
};

}  // namespace Markoff
```

- [ ] **Step 4: Create the implementation**

Create `libs/markoff-core/src/Selection.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Selection.h>

#include <markoff-foundation/AnchorJson.h>

namespace Markoff {

bool Selection::isEmpty() const
{
    return anchor.replica_id == active.replica_id
        && anchor.char_value == active.char_value
        && anchor.bias == active.bias;
}

bool Selection::isReversed() const
{
    // Without a Buffer to resolve to byte offsets, we can't deterministically
    // order anchors. This returns whether the cursor head sits "before" the
    // anchor in lexicographic (char_value, replica_id) order — a coarse
    // proxy. Callers that need byte-offset semantics should resolve via the
    // Buffer and compare byte offsets directly.
    if (active.char_value != anchor.char_value)
        return active.char_value < anchor.char_value;
    return active.replica_id < anchor.replica_id;
}

namespace {
const char *kindToString(Selection::Kind k)
{
    switch (k) {
    case Selection::Kind::Primary:     return "primary";
    case Selection::Kind::Secondary:   return "secondary";
    case Selection::Kind::SearchMatch: return "searchMatch";
    case Selection::Kind::Presence:    return "presence";
    }
    return "primary";
}

Selection::Kind kindFromString(const QString &s)
{
    if (s == QStringLiteral("secondary"))   return Selection::Kind::Secondary;
    if (s == QStringLiteral("searchMatch")) return Selection::Kind::SearchMatch;
    if (s == QStringLiteral("presence"))    return Selection::Kind::Presence;
    return Selection::Kind::Primary;
}
}  // namespace

QJsonObject Selection::toJson() const
{
    QJsonObject obj;
    obj.insert("anchor", anchorToJson(anchor));
    obj.insert("active", anchorToJson(active));
    obj.insert("kind", QString::fromLatin1(kindToString(kind)));
    if (kind == Kind::Presence) {
        obj.insert("participantId", participantId);
        obj.insert("participantLabel", participantLabel);
        obj.insert("presenceColor", presenceColor.name(QColor::HexArgb));
        obj.insert("cursorVersion", static_cast<qint64>(cursorVersion));
    }
    return obj;
}

Selection Selection::fromJson(const QJsonObject &obj)
{
    Selection s;
    s.anchor = anchorFromJson(obj.value("anchor").toObject());
    s.active = anchorFromJson(obj.value("active").toObject());
    s.kind = kindFromString(obj.value("kind").toString());
    if (s.kind == Kind::Presence) {
        s.participantId = obj.value("participantId").toString();
        s.participantLabel = obj.value("participantLabel").toString();
        s.presenceColor = QColor(obj.value("presenceColor").toString());
        s.cursorVersion = static_cast<quint64>(obj.value("cursorVersion").toInteger());
    }
    return s;
}

}  // namespace Markoff
```

- [ ] **Step 5: Add to library target**

Edit `libs/markoff-core/CMakeLists.txt`. Add Selection header + source:

```cmake
add_library(markoff_core STATIC
    include/markoff-foundation/MarkoffFoundationExport.h
    include/markoff-foundation/Origin.h
    include/markoff-foundation/MarkoffEdit.h
    include/markoff-foundation/AnchorJson.h
    include/markoff-foundation/Selection.h
    src/MarkoffEdit.cpp
    src/AnchorJson.cpp
    src/Selection.cpp
)
```

- [ ] **Step 6: Build and run**

```bash
cmake --build build-dev --target tst_selection -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_selection$' --output-on-failure
```

Expected: all 6 cases pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/Selection.h \
        libs/markoff-core/src/Selection.cpp \
        libs/markoff-core/tests/tst_selection.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): add Selection value type with kinds + JSON roundtrip"
```

---

### Task 8: Add FoldRef struct + tests

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/FoldRef.h`
- Create: `libs/markoff-core/src/FoldRef.cpp`
- Create: `libs/markoff-core/tests/tst_fold_ref.cpp`

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/tst_fold_ref.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QStringList>

#include <markoff-foundation/FoldRef.h>
#include <crdt/Anchor.h>

using namespace Markoff;
using CollabText::Crdt::Anchor;
using CollabText::Crdt::Bias;

class TstFoldRef : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void default_is_heading_kind() {
        FoldRef f;
        QCOMPARE(f.kind, FoldRef::Kind::Heading);
        QCOMPARE(f.headingLevel, 0);
    }

    void heading_with_path() {
        FoldRef f;
        f.kind = FoldRef::Kind::Heading;
        f.start = Anchor(1, 50, Bias::Left);
        f.headingPath = QStringList { "Intro", "Chapter 1", "Section A" };
        f.headingLevel = 3;
        QCOMPARE(f.headingPath.size(), 3);
        QCOMPARE(f.headingLevel, 3);
    }

    void block_kind() {
        FoldRef f;
        f.kind = FoldRef::Kind::Block;
        f.start = Anchor(2, 200, Bias::Left);
        QCOMPARE(f.kind, FoldRef::Kind::Block);
    }

    void json_roundtrip_heading() {
        FoldRef a;
        a.kind = FoldRef::Kind::Heading;
        a.start = Anchor(1, 50, Bias::Left);
        a.headingPath = QStringList { "Intro", "Chapter 1" };
        a.headingLevel = 2;

        const QJsonObject json = a.toJson();
        const FoldRef b = FoldRef::fromJson(json);
        QCOMPARE(b.kind, a.kind);
        QCOMPARE(b.start.replica_id, a.start.replica_id);
        QCOMPARE(b.start.char_value, a.start.char_value);
        QCOMPARE(b.headingPath, a.headingPath);
        QCOMPARE(b.headingLevel, a.headingLevel);
    }

    void json_roundtrip_block() {
        FoldRef a;
        a.kind = FoldRef::Kind::Block;
        a.start = Anchor(3, 300, Bias::Right);

        const QJsonObject json = a.toJson();
        const FoldRef b = FoldRef::fromJson(json);
        QCOMPARE(b.kind, a.kind);
        QCOMPARE(b.start.replica_id, a.start.replica_id);
        QCOMPARE(b.start.char_value, a.start.char_value);
    }
};

QTEST_MAIN(TstFoldRef)
#include "tst_fold_ref.moc"
```

- [ ] **Step 2: Add test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_fold_ref tst_fold_ref.cpp)
add_test(NAME tst_fold_ref COMMAND tst_fold_ref)
target_link_libraries(tst_fold_ref PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_fold_ref PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Create the header**

Create `libs/markoff-core/include/markoff-foundation/FoldRef.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <crdt/Anchor.h>

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// An anchor-bound fold reference. Survives concurrent CRDT edits and
/// arbitrary parse drift. Replaces the lossy (line, level) FoldSpec
/// from the existing Markoff family (audit §3.6).
struct MARKOFF_FOUNDATION_EXPORT FoldRef {
    enum class Kind {
        Heading,    ///< fold heading + everything until next heading at <= level
        Block,      ///< fold a specific block range
    };

    Kind                      kind = Kind::Heading;
    CollabText::Crdt::Anchor  start;        ///< anchor at the fold start
    QStringList               headingPath;  ///< for Heading kind
    int                       headingLevel = 0;

    QJsonObject toJson() const;
    static FoldRef fromJson(const QJsonObject &);
};

}  // namespace Markoff
```

- [ ] **Step 4: Create the implementation**

Create `libs/markoff-core/src/FoldRef.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/FoldRef.h>

#include <QJsonArray>

#include <markoff-foundation/AnchorJson.h>

namespace Markoff {

QJsonObject FoldRef::toJson() const
{
    QJsonObject obj;
    obj.insert("kind", kind == Kind::Block ? "block" : "heading");
    obj.insert("start", anchorToJson(start));
    if (kind == Kind::Heading) {
        QJsonArray path;
        for (const QString &p : headingPath)
            path.append(p);
        obj.insert("headingPath", path);
        obj.insert("headingLevel", headingLevel);
    }
    return obj;
}

FoldRef FoldRef::fromJson(const QJsonObject &obj)
{
    FoldRef f;
    f.kind = obj.value("kind").toString() == QStringLiteral("block")
        ? Kind::Block : Kind::Heading;
    f.start = anchorFromJson(obj.value("start").toObject());
    if (f.kind == Kind::Heading) {
        const QJsonArray path = obj.value("headingPath").toArray();
        for (const QJsonValue &v : path)
            f.headingPath << v.toString();
        f.headingLevel = obj.value("headingLevel").toInt();
    }
    return f;
}

}  // namespace Markoff
```

- [ ] **Step 5: Add to library target**

Edit `libs/markoff-core/CMakeLists.txt`:

```cmake
add_library(markoff_core STATIC
    include/markoff-foundation/MarkoffFoundationExport.h
    include/markoff-foundation/Origin.h
    include/markoff-foundation/MarkoffEdit.h
    include/markoff-foundation/AnchorJson.h
    include/markoff-foundation/Selection.h
    include/markoff-foundation/FoldRef.h
    src/MarkoffEdit.cpp
    src/AnchorJson.cpp
    src/Selection.cpp
    src/FoldRef.cpp
)
```

- [ ] **Step 6: Build + run**

```bash
cmake --build build-dev --target tst_fold_ref -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_fold_ref$' --output-on-failure
```

Expected: 5 test cases pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/FoldRef.h \
        libs/markoff-core/src/FoldRef.cpp \
        libs/markoff-core/tests/tst_fold_ref.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): add FoldRef value type with anchor binding"
```

---

## Phase 3 — MarkoffDocument core (Tasks 9–17)

### Task 9: MarkoffDocument construction + replicaId/version

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`
- Create: `libs/markoff-core/src/MarkoffDocument.cpp`
- Create: `libs/markoff-core/src/MarkoffDocumentPrivate.h`
- Create: `libs/markoff-core/tests/tst_markoff_document.cpp`

- [ ] **Step 1: Write the failing test (only construction + identity, this task)**

Create `libs/markoff-core/tests/tst_markoff_document.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>

using namespace Markoff;

class TstMarkoffDocument : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void constructed_with_replica_id() {
        MarkoffDocument doc(/*replicaId=*/42);
        QCOMPARE(doc.replicaId(), quint16(42));
    }

    void empty_document_has_zero_length() {
        MarkoffDocument doc(1);
        QCOMPARE(doc.visibleLength(), quint32(0));
        QVERIFY(doc.toMarkdownUtf8().isEmpty());
        QVERIFY(doc.toMarkdown().isEmpty());
    }

    void replica_ids_independent() {
        MarkoffDocument a(7);
        MarkoffDocument b(13);
        QCOMPARE(a.replicaId(), quint16(7));
        QCOMPARE(b.replicaId(), quint16(13));
    }
};

QTEST_MAIN(TstMarkoffDocument)
#include "tst_markoff_document.moc"
```

- [ ] **Step 2: Add test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_document tst_markoff_document.cpp)
add_test(NAME tst_markoff_document COMMAND tst_markoff_document)
target_link_libraries(tst_markoff_document PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Create the public header**

Create `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <optional>
#include <vector>

#include <crdt/Anchor.h>
#include <crdt/Clock.h>
#include <crdt/Operations.h>

#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

class Document;       // markoff-parser
class Session;        // forward; defined in Session.h after this task
struct SessionParams; // forward

/// Canonical text + AST + sessions. Owns a CollabText::Crdt::Buffer
/// internally; views are subscribers to this object's signals.
class MARKOFF_FOUNDATION_EXPORT MarkoffDocument : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MarkoffDocument)
public:
    /// Construct an empty document with the given replica ID. ReplicaId is
    /// the CRDT identity for this MarkoffDocument instance; for single-user
    /// use, a random quint16 is fine.
    explicit MarkoffDocument(quint16 replicaId, QObject *parent = nullptr);
    ~MarkoffDocument() override;

    // ===== Reads =====
    QByteArray toMarkdownUtf8() const;        ///< Buffer::text() as QByteArray
    QString    toMarkdown() const;            ///< UTF-8 → QString convenience
    quint32    visibleLength() const;         ///< UTF-8 byte length

    /// Returns the most-recently parsed Document (from markoff-parser),
    /// or nullptr if no parse has completed yet.
    const Markoff::Document *parsedDocument() const;

    /// True when a parse is currently scheduled or running.
    bool parseIsPending() const;

    // ===== CRDT identity =====
    quint16 replicaId() const;
    CollabText::Crdt::Global version() const;

    // ===== Local writes =====
    /// Apply a list of local edits as a single batched local edit. Edits are
    /// in OLD-text byte coordinates; ranges must be non-overlapping; if
    /// multiple edits, ordering must be ascending by oldStart. Returns the
    /// resulting Operation for broadcast (CRDT future). Emits contentsChanged.
    CollabText::Crdt::Operation
        applyLocalEdit(const QList<MarkoffEdit> &edits);

    // ===== Undo / redo =====
    std::optional<CollabText::Crdt::Operation> undo();
    std::optional<CollabText::Crdt::Operation> redo();
    int  undoDepth() const;
    bool coalesceLastUndo();

    // ===== Remote ops =====
    void applyRemoteOps(const std::vector<CollabText::Crdt::Operation> &ops);

    // ===== Wholesale reload =====
    void resetContent(const QByteArray &newContent, Origin origin);

    // ===== Anchors =====
    CollabText::Crdt::Anchor
        anchorAt(quint32 byteOffset, CollabText::Crdt::Bias bias) const;
    quint32 resolveAnchor(const CollabText::Crdt::Anchor &) const;

    // ===== Sessions (filled in Task 23) =====
    Session *createSession(const SessionParams &params = {});
    void     destroySession(Session *);
    QList<Session *> sessions() const;
    Session *sessionForParticipant(const QString &participantId) const;

    // ===== Garbage collection =====
    qsizetype collectGarbage();
    qsizetype compact(const CollabText::Crdt::Global &watermark);

    // ===== Coalescing =====
    void setCoalescingIdleMs(int ms);
    int  coalescingIdleMs() const;

Q_SIGNALS:
    void contentsChanged(QList<Markoff::MarkoffEdit> edits);
    void parseUpdated(const Markoff::Document *parsed);
    void documentReloaded();
    void sessionCreated(Markoff::Session *);
    void sessionDestroyed(Markoff::Session *);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff
```

- [ ] **Step 4: Create the impl-side private header**

Create `libs/markoff-core/src/MarkoffDocumentPrivate.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include <memory>

#include <crdt/Buffer.h>

namespace Markoff {

class Session;

struct MarkoffDocument::Private {
    explicit Private(uint16_t replicaId)
        : buffer(replicaId)
        , replicaId(replicaId)
    {}

    CollabText::Crdt::Buffer buffer;
    quint16                  replicaId;
    int                      coalescingIdleMs = 250;
    QList<Session *>         sessions;  // filled by Task 23
};

}  // namespace Markoff
```

- [ ] **Step 5: Create the implementation (just the parts this task needs)**

Create `libs/markoff-core/src/MarkoffDocument.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/MarkoffDocument.h>

#include "MarkoffDocumentPrivate.h"

namespace Markoff {

MarkoffDocument::MarkoffDocument(quint16 replicaId, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(replicaId))
{
}

MarkoffDocument::~MarkoffDocument() = default;

QByteArray MarkoffDocument::toMarkdownUtf8() const
{
    const std::string s = d->buffer.text();
    return QByteArray::fromStdString(s);
}

QString MarkoffDocument::toMarkdown() const
{
    return QString::fromUtf8(toMarkdownUtf8());
}

quint32 MarkoffDocument::visibleLength() const
{
    return d->buffer.visible_length();
}

const Markoff::Document *MarkoffDocument::parsedDocument() const
{
    // Filled in Task 19 (ParsePool integration).
    return nullptr;
}

bool MarkoffDocument::parseIsPending() const
{
    // Filled in Task 19.
    return false;
}

quint16 MarkoffDocument::replicaId() const
{
    return d->replicaId;
}

CollabText::Crdt::Global MarkoffDocument::version() const
{
    return d->buffer.version();
}

// applyLocalEdit, undo/redo, applyRemoteOps, resetContent, anchorAt,
// resolveAnchor, sessions, collectGarbage, compact, coalescing setters
// are filled in subsequent tasks (10-23).

CollabText::Crdt::Operation
MarkoffDocument::applyLocalEdit(const QList<MarkoffEdit> &)
{
    // Stub — filled in Task 10.
    return CollabText::Crdt::EditOperation{};
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::undo()
{
    return std::nullopt;
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::redo()
{
    return std::nullopt;
}

int MarkoffDocument::undoDepth() const { return 0; }
bool MarkoffDocument::coalesceLastUndo() { return false; }

void MarkoffDocument::applyRemoteOps(
    const std::vector<CollabText::Crdt::Operation> &)
{
    // Filled in Task 13.
}

void MarkoffDocument::resetContent(const QByteArray &, Origin)
{
    // Filled in Task 16.
}

CollabText::Crdt::Anchor
MarkoffDocument::anchorAt(quint32 byteOffset, CollabText::Crdt::Bias bias) const
{
    return d->buffer.anchor_at(byteOffset, bias);
}

quint32 MarkoffDocument::resolveAnchor(const CollabText::Crdt::Anchor &a) const
{
    return d->buffer.resolve_anchor(a);
}

// Sessions — filled in Task 23.
Session *MarkoffDocument::createSession(const SessionParams &) { return nullptr; }
void MarkoffDocument::destroySession(Session *) {}
QList<Session *> MarkoffDocument::sessions() const { return {}; }
Session *MarkoffDocument::sessionForParticipant(const QString &) const
{
    return nullptr;
}

qsizetype MarkoffDocument::collectGarbage()
{
    return static_cast<qsizetype>(d->buffer.collect_garbage());
}

qsizetype MarkoffDocument::compact(const CollabText::Crdt::Global &watermark)
{
    return static_cast<qsizetype>(d->buffer.compact(watermark));
}

void MarkoffDocument::setCoalescingIdleMs(int ms) { d->coalescingIdleMs = ms; }
int  MarkoffDocument::coalescingIdleMs() const { return d->coalescingIdleMs; }

}  // namespace Markoff
```

- [ ] **Step 6: Add to library target**

Edit `libs/markoff-core/CMakeLists.txt`:

```cmake
add_library(markoff_core STATIC
    include/markoff-foundation/MarkoffFoundationExport.h
    include/markoff-foundation/Origin.h
    include/markoff-foundation/MarkoffEdit.h
    include/markoff-foundation/AnchorJson.h
    include/markoff-foundation/Selection.h
    include/markoff-foundation/FoldRef.h
    include/markoff-foundation/MarkoffDocument.h
    src/MarkoffDocumentPrivate.h
    src/MarkoffEdit.cpp
    src/AnchorJson.cpp
    src/Selection.cpp
    src/FoldRef.cpp
    src/MarkoffDocument.cpp
)
```

- [ ] **Step 7: Build + run**

```bash
cmake --build build-dev --target tst_markoff_document -j 2>&1 | tail -5
ctest --test-dir build-dev -R '^tst_markoff_document$' --output-on-failure
```

Expected: 3 test cases pass.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/src/MarkoffDocumentPrivate.h \
        libs/markoff-core/tests/tst_markoff_document.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): MarkoffDocument scaffold with replicaId + version + reads"
```

---

### Task 10: MarkoffDocument applyLocalEdit single-edit path

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-core/tests/tst_markoff_document.cpp`

- [ ] **Step 1: Add a failing test for applyLocalEdit**

Edit `libs/markoff-core/tests/tst_markoff_document.cpp`. Add these slots inside the class (before `QTEST_MAIN`):

```cpp
    void apply_local_edit_inserts_text() {
        MarkoffDocument doc(1);
        // Seed via direct buffer init through a single insert at offset 0.
        QList<MarkoffEdit> seed;
        MarkoffEdit ins;
        ins.oldStart = 0;
        ins.oldEnd = 0;
        ins.newText = "hello";
        seed << ins;
        doc.applyLocalEdit(seed);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello"));
        QCOMPARE(doc.visibleLength(), quint32(5));
    }

    void apply_local_edit_replaces_range() {
        MarkoffDocument doc(1);

        QList<MarkoffEdit> seed;
        MarkoffEdit ins;
        ins.oldStart = 0;
        ins.oldEnd = 0;
        ins.newText = "hello world";
        seed << ins;
        doc.applyLocalEdit(seed);

        QList<MarkoffEdit> edits;
        MarkoffEdit replace;
        replace.oldStart = 6;
        replace.oldEnd = 11;
        replace.newText = "there";
        edits << replace;
        doc.applyLocalEdit(edits);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello there"));
    }

    void apply_local_edit_deletes_range() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> seed;
        MarkoffEdit ins;
        ins.oldStart = 0;
        ins.oldEnd = 0;
        ins.newText = "abcdef";
        seed << ins;
        doc.applyLocalEdit(seed);

        QList<MarkoffEdit> del;
        MarkoffEdit d;
        d.oldStart = 2;
        d.oldEnd = 4;
        d.newText.clear();
        del << d;
        doc.applyLocalEdit(del);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("abef"));
    }
```

- [ ] **Step 2: Run, verify they fail**

```bash
ctest --test-dir build-dev -R '^tst_markoff_document$' --output-on-failure 2>&1 | tail -20
```

Expected: tests fail with text mismatch (the stub returns an empty Operation and doesn't mutate the buffer).

- [ ] **Step 3: Implement applyLocalEdit in MarkoffDocument.cpp**

Edit `libs/markoff-core/src/MarkoffDocument.cpp`. Replace the stub `applyLocalEdit`:

```cpp
CollabText::Crdt::Operation
MarkoffDocument::applyLocalEdit(const QList<MarkoffEdit> &edits)
{
    // Snapshot version so we can compute the resulting TextEdits afterwards.
    const CollabText::Crdt::Global oldVersion = d->buffer.version();

    // Translate QList<MarkoffEdit> to the std::vector<pair> + std::vector<string>
    // pair shape that Buffer::apply_local_edit expects.
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    std::vector<std::string> newTexts;
    ranges.reserve(static_cast<size_t>(edits.size()));
    newTexts.reserve(static_cast<size_t>(edits.size()));
    for (const MarkoffEdit &e : edits) {
        ranges.emplace_back(e.oldStart, e.oldEnd);
        newTexts.emplace_back(e.newText.constData(),
                              static_cast<size_t>(e.newText.size()));
    }

    const CollabText::Crdt::Operation op = d->buffer.apply_local_edit(ranges, newTexts);

    // Compute resulting visible-text edits from the buffer's diff API.
    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }

    if (!resultingEdits.isEmpty())
        Q_EMIT contentsChanged(resultingEdits);

    return op;
}
```

- [ ] **Step 4: Build + run**

```bash
cmake --build build-dev --target tst_markoff_document -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_markoff_document$' --output-on-failure
```

Expected: all 6 cases pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/tst_markoff_document.cpp
git commit -m "feat(foundation): MarkoffDocument::applyLocalEdit (insert/replace/delete)"
```

---

### Task 11: MarkoffDocument contentsChanged signal shape

**Files:**
- Modify: `libs/markoff-core/tests/tst_markoff_document.cpp`

- [ ] **Step 1: Write a failing test asserting the signal shape**

Add to `tst_markoff_document.cpp`:

```cpp
    void apply_local_edit_emits_contents_changed() {
        MarkoffDocument doc(1);
        // Seed.
        {
            QList<MarkoffEdit> seed;
            MarkoffEdit i;
            i.oldStart = 0;
            i.oldEnd = 0;
            i.newText = "abc";
            seed << i;
            doc.applyLocalEdit(seed);
        }

        QSignalSpy spy(&doc, &MarkoffDocument::contentsChanged);
        QList<MarkoffEdit> edits;
        MarkoffEdit ins;
        ins.oldStart = 1;
        ins.oldEnd = 1;
        ins.newText = "X";
        edits << ins;
        doc.applyLocalEdit(edits);

        QCOMPARE(spy.count(), 1);
        const QList<MarkoffEdit> received =
            spy.takeFirst().at(0).value<QList<MarkoffEdit>>();
        QVERIFY(!received.isEmpty());
        // The first received edit should describe the insertion at oldStart=1.
        QCOMPARE(received.first().oldStart, quint32(1));
    }
```

Add at the top of the file (before the class declaration), so QSignalSpy can serialize the type:

```cpp
#include <QSignalSpy>
Q_DECLARE_METATYPE(QList<Markoff::MarkoffEdit>)
```

Add in `main()` before `QTEST_MAIN` is invoked... actually `QTEST_MAIN` generates main; we can't add code to it. Use a constructor pattern instead. Replace:

```cpp
QTEST_MAIN(TstMarkoffDocument)
```

with:

```cpp
int main(int argc, char *argv[]) {
    qRegisterMetaType<QList<Markoff::MarkoffEdit>>("QList<Markoff::MarkoffEdit>");
    QApplication app(argc, argv);
    TstMarkoffDocument tc;
    return QTest::qExec(&tc, argc, argv);
}
```

And ensure includes at the top:

```cpp
#include <QApplication>
```

- [ ] **Step 2: Run + verify it fails or passes**

```bash
cmake --build build-dev --target tst_markoff_document -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_markoff_document$' --output-on-failure
```

Expected: should pass (the signal is already emitted in Task 10's impl). If the metatype registration is wrong, you'll see runtime warnings about unregistered types.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-core/tests/tst_markoff_document.cpp
git commit -m "test(foundation): assert contentsChanged signal shape"
```

---

### Task 12: MarkoffDocument applyLocalEdit batch (multiple edits)

**Files:**
- Modify: `libs/markoff-core/tests/tst_markoff_document.cpp`

The single-edit and batch paths both use `Buffer::apply_local_edit` which already accepts a vector of ranges. The implementation in Task 10 already supports batches. This task adds explicit test coverage.

- [ ] **Step 1: Add a batch test**

```cpp
    void apply_local_edit_batch() {
        MarkoffDocument doc(1);
        {
            QList<MarkoffEdit> seed;
            MarkoffEdit i;
            i.oldStart = 0;
            i.oldEnd = 0;
            i.newText = "aaaa bbbb cccc";
            seed << i;
            doc.applyLocalEdit(seed);
        }
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("aaaa bbbb cccc"));

        // Two non-overlapping replacements in one batch.
        QList<MarkoffEdit> edits;
        MarkoffEdit r1;
        r1.oldStart = 0;
        r1.oldEnd = 4;
        r1.newText = "AAAA";
        edits << r1;
        MarkoffEdit r2;
        r2.oldStart = 10;
        r2.oldEnd = 14;
        r2.newText = "CCCC";
        edits << r2;
        doc.applyLocalEdit(edits);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("AAAA bbbb CCCC"));
    }
```

- [ ] **Step 2: Build + run**

```bash
cmake --build build-dev --target tst_markoff_document -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_markoff_document$' --output-on-failure
```

Expected: pass.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-core/tests/tst_markoff_document.cpp
git commit -m "test(foundation): batch applyLocalEdit with non-overlapping ranges"
```

---

### Task 13: MarkoffDocument applyRemoteOps

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-core/tests/tst_markoff_document.cpp`

- [ ] **Step 1: Write the failing test (two MarkoffDocuments exchanging ops)**

Add to `tst_markoff_document.cpp`:

```cpp
    void apply_remote_ops_replicates() {
        MarkoffDocument alice(1);
        MarkoffDocument bob(2);

        // Alice types.
        QList<MarkoffEdit> ed;
        MarkoffEdit ins;
        ins.oldStart = 0;
        ins.oldEnd = 0;
        ins.newText = "hello";
        ed << ins;
        const auto op = alice.applyLocalEdit(ed);

        // Bob applies Alice's op.
        bob.applyRemoteOps({ op });
        QCOMPARE(bob.toMarkdownUtf8(), QByteArray("hello"));
    }

    void apply_remote_ops_emits_contents_changed() {
        MarkoffDocument alice(1);
        MarkoffDocument bob(2);

        QList<MarkoffEdit> ed;
        MarkoffEdit ins;
        ins.oldStart = 0;
        ins.oldEnd = 0;
        ins.newText = "x";
        ed << ins;
        const auto op = alice.applyLocalEdit(ed);

        QSignalSpy spy(&bob, &MarkoffDocument::contentsChanged);
        bob.applyRemoteOps({ op });
        QCOMPARE(spy.count(), 1);
    }
```

- [ ] **Step 2: Run, verify fails**

```bash
ctest --test-dir build-dev -R '^tst_markoff_document$' --output-on-failure 2>&1 | tail -10
```

Expected: bob's text is empty (the stub does nothing).

- [ ] **Step 3: Implement applyRemoteOps**

Edit `libs/markoff-core/src/MarkoffDocument.cpp`. Replace the stub:

```cpp
void MarkoffDocument::applyRemoteOps(
    const std::vector<CollabText::Crdt::Operation> &ops)
{
    if (ops.empty())
        return;

    const CollabText::Crdt::Global oldVersion = d->buffer.version();
    d->buffer.apply_ops(ops);

    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }

    if (!resultingEdits.isEmpty())
        Q_EMIT contentsChanged(resultingEdits);
}
```

- [ ] **Step 4: Build + run**

```bash
cmake --build build-dev --target tst_markoff_document -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_markoff_document$' --output-on-failure
```

Expected: all current cases pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/tst_markoff_document.cpp
git commit -m "feat(foundation): MarkoffDocument::applyRemoteOps with edits_since translation"
```

---

### Task 14: MarkoffDocument undo / redo

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-core/tests/tst_markoff_document.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    void undo_reverses_last_local_edit() {
        MarkoffDocument doc(1);
        {
            QList<MarkoffEdit> seed;
            MarkoffEdit i;
            i.oldStart = 0;
            i.oldEnd = 0;
            i.newText = "ab";
            seed << i;
            doc.applyLocalEdit(seed);
        }
        {
            QList<MarkoffEdit> ed;
            MarkoffEdit i;
            i.oldStart = 2;
            i.oldEnd = 2;
            i.newText = "c";
            ed << i;
            doc.applyLocalEdit(ed);
        }
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("abc"));
        QVERIFY(doc.undo().has_value());
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("ab"));
    }

    void redo_reapplies_undone_edit() {
        MarkoffDocument doc(1);
        {
            QList<MarkoffEdit> seed;
            MarkoffEdit i;
            i.oldStart = 0;
            i.oldEnd = 0;
            i.newText = "ab";
            seed << i;
            doc.applyLocalEdit(seed);
        }
        {
            QList<MarkoffEdit> ed;
            MarkoffEdit i;
            i.oldStart = 2;
            i.oldEnd = 2;
            i.newText = "c";
            ed << i;
            doc.applyLocalEdit(ed);
        }
        doc.undo();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("ab"));
        QVERIFY(doc.redo().has_value());
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("abc"));
    }

    void undo_with_no_history_returns_nullopt() {
        MarkoffDocument doc(1);
        QVERIFY(!doc.undo().has_value());
    }

    void undo_emits_contents_changed() {
        MarkoffDocument doc(1);
        {
            QList<MarkoffEdit> seed;
            MarkoffEdit i;
            i.oldStart = 0;
            i.oldEnd = 0;
            i.newText = "abc";
            seed << i;
            doc.applyLocalEdit(seed);
        }
        QSignalSpy spy(&doc, &MarkoffDocument::contentsChanged);
        doc.undo();
        QVERIFY(spy.count() >= 1);
    }
```

- [ ] **Step 2: Run, verify fail**

Expected: undo returns nullopt + text doesn't change.

- [ ] **Step 3: Implement undo / redo / undoDepth**

Edit `MarkoffDocument.cpp`. Replace the stubs:

```cpp
std::optional<CollabText::Crdt::Operation> MarkoffDocument::undo()
{
    const CollabText::Crdt::Global oldVersion = d->buffer.version();
    auto op = d->buffer.undo();
    if (!op.has_value())
        return std::nullopt;

    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }
    if (!resultingEdits.isEmpty())
        Q_EMIT contentsChanged(resultingEdits);

    return op;
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::redo()
{
    const CollabText::Crdt::Global oldVersion = d->buffer.version();
    auto op = d->buffer.redo();
    if (!op.has_value())
        return std::nullopt;

    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }
    if (!resultingEdits.isEmpty())
        Q_EMIT contentsChanged(resultingEdits);

    return op;
}

int MarkoffDocument::undoDepth() const
{
    return static_cast<int>(d->buffer.undo_depth());
}

bool MarkoffDocument::coalesceLastUndo()
{
    return d->buffer.coalesce_last_undo();
}
```

- [ ] **Step 4: Build + run**

```bash
cmake --build build-dev --target tst_markoff_document -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_markoff_document$' --output-on-failure
```

Expected: all undo/redo cases pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/tst_markoff_document.cpp
git commit -m "feat(foundation): MarkoffDocument::undo / redo / undoDepth / coalesceLastUndo"
```

---

### Task 15: MarkoffDocument coalesceLastUndo test

**Files:**
- Modify: `libs/markoff-core/tests/tst_markoff_document.cpp`

- [ ] **Step 1: Add a test asserting coalescing behavior**

```cpp
    void coalesce_last_undo_groups_two_edits() {
        MarkoffDocument doc(1);
        {
            QList<MarkoffEdit> seed;
            MarkoffEdit i;
            i.oldStart = 0;
            i.oldEnd = 0;
            i.newText = "ab";
            seed << i;
            doc.applyLocalEdit(seed);
        }
        {
            QList<MarkoffEdit> ed;
            MarkoffEdit i;
            i.oldStart = 2;
            i.oldEnd = 2;
            i.newText = "c";
            ed << i;
            doc.applyLocalEdit(ed);
        }
        // Two edits → undoDepth == 2.
        QCOMPARE(doc.undoDepth(), 2);

        // Coalesce the last two into one undo step.
        QVERIFY(doc.coalesceLastUndo());
        QCOMPARE(doc.undoDepth(), 1);

        // One undo should now revert both edits.
        doc.undo();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray());
    }
```

- [ ] **Step 2: Build + run**

```bash
cmake --build build-dev --target tst_markoff_document -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_markoff_document$' --output-on-failure
```

Expected: pass.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-core/tests/tst_markoff_document.cpp
git commit -m "test(foundation): coalesceLastUndo groups consecutive edits"
```

---

### Task 16: MarkoffDocument resetContent + Origin behavior

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-core/tests/tst_markoff_document.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    void reset_content_first_open_clears_undo() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i;
        i.oldStart = 0;
        i.oldEnd = 0;
        i.newText = "old";
        ed << i;
        doc.applyLocalEdit(ed);
        QVERIFY(doc.undoDepth() > 0);

        doc.resetContent(QByteArray("new content"), Origin::FirstOpen);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("new content"));
        QCOMPARE(doc.undoDepth(), 0);
    }

    void reset_content_emits_document_reloaded() {
        MarkoffDocument doc(1);
        QSignalSpy spy(&doc, &MarkoffDocument::documentReloaded);
        doc.resetContent(QByteArray("hello"), Origin::FirstOpen);
        QCOMPARE(spy.count(), 1);
    }

    void reset_content_user_revert_pushes_undo_entry() {
        MarkoffDocument doc(1);
        {
            QList<MarkoffEdit> ed;
            MarkoffEdit i;
            i.oldStart = 0;
            i.oldEnd = 0;
            i.newText = "draft";
            ed << i;
            doc.applyLocalEdit(ed);
        }
        const int beforeDepth = doc.undoDepth();

        doc.resetContent(QByteArray("saved"), Origin::UserRevertToSaved);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("saved"));
        // UserRevertToSaved pushes one mega-edit so undo reverses the revert.
        QVERIFY(doc.undoDepth() > beforeDepth);
        doc.undo();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("draft"));
    }
```

- [ ] **Step 2: Run, verify fail**

Expected: `resetContent` is a no-op stub; tests fail.

- [ ] **Step 3: Implement resetContent**

Replace the stub in `MarkoffDocument.cpp`:

```cpp
void MarkoffDocument::resetContent(const QByteArray &newContent, Origin origin)
{
    switch (origin) {
    case Origin::FirstOpen:
    case Origin::ExternalReloadClean:
    case Origin::ExternalReloadResolved:
    case Origin::TestFixture: {
        // Replace the entire buffer state by re-constructing it. CollabText
        // does not expose a "clear all state" call; the only safe path is
        // to construct a fresh Buffer with the same replica id.
        d->buffer = CollabText::Crdt::Buffer(d->replicaId);
        if (!newContent.isEmpty()) {
            std::vector<std::pair<uint32_t, uint32_t>> ranges{ {0, 0} };
            std::vector<std::string> texts{
                std::string(newContent.constData(),
                            static_cast<size_t>(newContent.size())) };
            d->buffer.apply_local_edit(ranges, texts);
            // First-open inserts shouldn't be on the undo stack; collapse the
            // freshly-pushed entry by undoing-then-clearing-via-coalesce.
            // Simpler: re-construct again post-content-set so the stack is
            // empty.
        }
        break;
    }
    case Origin::UserRevertToSaved: {
        // Push one mega-edit that replaces the entire buffer with newContent.
        // Undo reverses the revert.
        const auto curLen = d->buffer.visible_length();
        std::vector<std::pair<uint32_t, uint32_t>> ranges{ {0u, curLen} };
        std::vector<std::string> texts{
            std::string(newContent.constData(),
                        static_cast<size_t>(newContent.size())) };
        d->buffer.apply_local_edit(ranges, texts);
        break;
    }
    }
    Q_EMIT documentReloaded();
}
```

Note: the FirstOpen / ExternalReload / TestFixture path uses Buffer reconstruction to clear state. The "First-open inserts shouldn't be on the undo stack" comment is honest about a small wrinkle — to fully clear the undo stack after the seed insert, we should re-construct AGAIN after the seed. Let's tighten:

```cpp
    case Origin::FirstOpen:
    case Origin::ExternalReloadClean:
    case Origin::ExternalReloadResolved:
    case Origin::TestFixture: {
        d->buffer = CollabText::Crdt::Buffer(d->replicaId);
        if (!newContent.isEmpty()) {
            std::vector<std::pair<uint32_t, uint32_t>> ranges{ {0, 0} };
            std::vector<std::string> texts{
                std::string(newContent.constData(),
                            static_cast<size_t>(newContent.size())) };
            d->buffer.apply_local_edit(ranges, texts);
            // Clear the undo stack so this seed is not undoable.
            // Buffer doesn't expose a clear-undo API directly, but a fresh
            // Buffer would lose the inserted content. The cheapest correct
            // approach is to set max_undo_depth(0) then back to default.
            const auto savedDepth = d->buffer.max_undo_depth();
            d->buffer.set_max_undo_depth(0);
            d->buffer.set_max_undo_depth(savedDepth);
        }
        break;
    }
```

(`Buffer::set_max_undo_depth` trims the stack on shrink; reset back to default to permit subsequent undo on local edits.)

Replace the stub block accordingly.

- [ ] **Step 4: Build + run**

```bash
cmake --build build-dev --target tst_markoff_document -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_markoff_document$' --output-on-failure
```

Expected: all reset-related cases pass. `reset_content_first_open_clears_undo` should now show undoDepth == 0 after reset.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/tst_markoff_document.cpp
git commit -m "feat(foundation): MarkoffDocument::resetContent with Origin semantics"
```

---

### Task 17: MarkoffDocument anchor passthrough tests

**Files:**
- Modify: `libs/markoff-core/tests/tst_markoff_document.cpp`

The implementation already exists; this task adds explicit test coverage.

- [ ] **Step 1: Add tests**

```cpp
    void anchor_at_resolves_to_offset() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i;
        i.oldStart = 0;
        i.oldEnd = 0;
        i.newText = "abcdef";
        ed << i;
        doc.applyLocalEdit(ed);

        const auto a = doc.anchorAt(3, CollabText::Crdt::Bias::Left);
        QCOMPARE(doc.resolveAnchor(a), quint32(3));
    }

    void anchor_survives_left_insert() {
        MarkoffDocument doc(1);
        {
            QList<MarkoffEdit> ed;
            MarkoffEdit i;
            i.oldStart = 0;
            i.oldEnd = 0;
            i.newText = "ace";
            ed << i;
            doc.applyLocalEdit(ed);
        }
        // Anchor at offset 1 (between 'a' and 'c'), right-bias.
        const auto a = doc.anchorAt(1, CollabText::Crdt::Bias::Right);

        // Insert "b" at offset 1.
        {
            QList<MarkoffEdit> ed;
            MarkoffEdit i;
            i.oldStart = 1;
            i.oldEnd = 1;
            i.newText = "b";
            ed << i;
            doc.applyLocalEdit(ed);
        }
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("abce"));
        // Right-bias anchor moves past the inserted text.
        QCOMPARE(doc.resolveAnchor(a), quint32(2));
    }
```

- [ ] **Step 2: Build + run**

```bash
cmake --build build-dev --target tst_markoff_document -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_markoff_document$' --output-on-failure
```

Expected: pass.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-core/tests/tst_markoff_document.cpp
git commit -m "test(foundation): anchor_at + resolveAnchor through edits"
```

---

## Phase 4 — Sessions (Tasks 18–23)

Continued in next pass — phases 4-13 are written using the same pattern as above. To keep this plan reviewable, the remaining tasks are summarized below; the engineer expands each into TDD steps following the same template (test → fail → impl → pass → commit). If preferred, this plan can be split into a Part 1 (Phases 1–3, Tasks 1–17, fully expanded above) and a Part 2 (Phases 4–13) to be drafted as a separate doc once Part 1 lands.

### Task 18: SessionParams + Session header skeleton

Define `SessionParams` (participantId, participantLabel, presenceColor) and `Session` Q_OBJECT class declaration in `libs/markoff-core/include/markoff-foundation/Session.h`. Empty implementation in `Session.cpp`.

### Task 19: Session primary selection

`primarySelection() / setPrimarySelection() / primarySelectionChanged` signal. Test asserts setter changes value and signal fires once.

### Task 20: Session secondary selections

`secondarySelections() / setSecondarySelections() / addSecondarySelection() / clearSecondarySelectionsOfKind()`. Test covers add/clear-by-kind preserves other kinds.

### Task 21: Session scroll + folds

`topVisibleAnchor / topVisibleFraction / setTopVisible / scrollChanged`. `foldedRegions / setFoldedRegions / toggleFold / foldedRegionsChanged`.

### Task 22: Session copyStateFrom + JSON

`copyStateFrom(other)` copies primary, secondaries, scroll, folds. `toJson() / fromJson()` round-trip everything.

### Task 23: MarkoffDocument session lifecycle

Implement `createSession / destroySession / sessions / sessionForParticipant` plus `sessionCreated / sessionDestroyed` signals. Test creating two sessions, querying by participant, destroying.

---

## Phase 5 — ParsePool integration (Task 24)

### Task 24: MarkoffDocument ParsePool integration

Salvage `ParsePool` and `ParsePoolWorker` from existing `markoff-core` (copy verbatim, adjust for UTF-8 input). Wire into `MarkoffDocument` so `applyLocalEdit / applyRemoteOps / resetContent` schedule a parse, and `parseUpdated` fires on completion. Test asserts that `parsedDocument()` becomes non-null after a debounce.

---

## Phase 6 — Theme (Tasks 25–28)

### Task 25: Theme slot enum + color/setColor

`Q_GADGET` value type, `Slot` enum (per spec §7.5), `color() / setColor()`.

### Task 26: Theme fonts + bold/italic/sizeMultiplier

`FontRole` enum, `font() / setFont()`, `isBold() / isItalic() / fontSizeMultiplier()`.

### Task 27: Theme defaultLight / defaultDark + JSON roundtrip

Static factory methods producing reasonable default themes; `toJson() / fromJson()`.

### Task 28: Theme code-token color mapping

`colorForCodeToken(CodeTokenKind)` — map from the parallel CodeTokenKind enum to Slot::Code* entries; convenience over manual color() lookups.

---

## Phase 7 — LinkService (Tasks 29–30)

### Task 29: LinkKind + LinkActivation + abstract LinkService

`LinkKind` enum, `LinkActivation` struct, abstract `LinkService` Q_OBJECT base.

### Task 30: DefaultLinkService implementation

Classifies http/https/mailto as External; everything else as Unknown. `resolve()` returns the literal QUrl. Test asserts classification matrix.

---

## Phase 8 — Commands (Tasks 31–37)

### Task 31: Cmd::Edit (undo / redo wrappers)

`undo / redo` free functions wrapping `MarkoffDocument::undo()/redo()`.

### Task 32: Cmd inline format — toggleBold

Pure `editsForToggleBold(doc, sel)` returns the edit list to wrap/unwrap `**` around the selection; convenience `toggleBold(doc, sel)` calls applyLocalEdit. Test covers: wrap unstyled, unwrap styled, partial overlap, empty selection at cursor.

### Task 33: Cmd inline format — italic, strikethrough, inline code

Same shape as Task 32 but for `*..*`, `~~..~~`, `` `..` ``. Use a shared helper to avoid duplication.

### Task 34: Cmd::setHeading

`editsForSetHeading(doc, sel, level)`. Replace heading prefix on each affected block. Test covers: paragraph→H1, H2→H3, H4→paragraph (level 0), multi-block selection.

### Task 35: Cmd::toggleCheckbox + blockQuote

`toggleCheckbox` cycles `[ ]` ↔ `[x]` ↔ no-checkbox on a list item. `blockQuote` wraps/unwraps `>` prefix.

### Task 36: Cmd::insertTable / insertLink / insertImage / insertHorizontalRule

Inserts at cursor. `insertTable` builds a GFM pipe table with header. Tests cover trivial cases.

### Task 37: Cmd::applyToAllPrimaryAndSecondaries helper

Iterates `session->primarySelection()` + `session->secondarySelections()` of `Kind::Secondary`, applies the given edit fn to each, batches the results into a single `applyLocalEdit`. Test with multi-cursor.

---

## Phase 9 — CommandFacade (Task 38)

### Task 38: CommandFacade Q_OBJECT for QML

Implements per-method Q_INVOKABLE wrappers. Test instantiates Facade with a doc + session and calls `toggleBold()`; asserts doc state changes.

---

## Phase 10 — Search + Replace (Tasks 39–42)

### Task 39: SearchEngine FindFlags + findAll

`FindFlags` enum, `findAll(doc, session, needle, flags)` populates session secondary selections of `Kind::SearchMatch`.

### Task 40: SearchEngine findNext / findPrevious / clearMatches

Cycle through `SearchMatch` selections; set `primarySelection` to the next.

### Task 41: ReplaceController::replaceCurrent

Replace the active match (the one matching `primarySelection`); advance.

### Task 42: ReplaceController::replaceAll

Batched replace of all matches in document order; returns count + Operation.

---

## Phase 11 — Code blocks (Tasks 43–47)

### Task 43: CodeTokenKind + CodeSpan

Plain enum + value struct.

### Task 44: SyntaxHighlightService interface

Abstract base with `highlight() / availableLanguages() / supportsLanguage()`.

### Task 45: Kf6SyntaxHighlightService implementation

KF6::SyntaxHighlighting backend. Translates KF6 token classes to our `CodeTokenKind`.

### Task 46: CodeBlockProcessor + RenderedBlock

Abstract processor; value struct for output.

### Task 47: CodeBlockProcessorRegistry

Register / unregister / lookup.

---

## Phase 12 — Completion (Tasks 48–52)

### Task 48: CompletionTrigger enum + CompletionContext + CompletionCandidate

Plain types.

### Task 49: CompletionDetector::detect (basic triggers)

Wikilink (`[[`), tag (`#` in body), emoji (`:`).

### Task 50: CompletionDetector edge cases

Heading marker `#` (NOT a tag), code-block contexts (no triggers inside ```` ``` ````), escaped `[[`, footnotes `[^`.

### Task 51: CompletionProvider + CompletionRegistry

Abstract provider; registry aggregates synchronously; async candidates via `candidatesReady`.

### Task 52: EmojiCompletionProvider

Default provider with ~600-emoji table baked in (or a curated subset). Test asserts "smi" prefix returns ":smile:" candidate.

---

## Phase 13 — Services bundle + property tests + acceptance (Tasks 53–55)

### Task 53: MarkoffServices struct

Plain struct bundling pointers; tested transitively via integration.

### Task 54: tst_markoff_document_property

Random sequence of `applyLocalEdit` calls; assert `toMarkdownUtf8()` always equals an independently-computed reference text. ~100 sequences seeded by deterministic RNG.

### Task 55: Acceptance pass

Run full ctest suite, document baseline (`ctest --output-on-failure | tee tests-baseline.log`). Foundation is "viable" when all tests pass.

```bash
ctest --test-dir build-dev --output-on-failure -j 2>&1 | tee /tmp/foundation-tests.log
```

Commit the test log as a build artifact in the docs/ directory:

```bash
cp /tmp/foundation-tests.log docs/2026-04-28-foundation-tests-baseline.log
git add docs/2026-04-28-foundation-tests-baseline.log
git commit -m "docs: foundation library test-suite baseline"
```

---

## Self-Review Notes

Performed inline:

1. **Spec coverage:** Each spec §7 sub-section maps to a task or task group. Phase 4 (sessions) covers spec §7.2; Phase 6 (Theme) covers §7.5; Phase 7 (LinkService) covers §7.6; Phase 8/9 (commands + facade) cover §7.7; Phase 10 (search) covers §7.8; Phase 11 (code blocks) covers §7.9; Phase 12 (completion) covers §7.10. ParsePool integration (Phase 5) covers spec §6.1's salvage requirement.

2. **Scope decomposition:** This plan covers the foundation library only. The POC view (`markoff-view-qml`) and its integration tests are a separate plan to be drafted once Phase 13 (acceptance) passes. This is consistent with the writing-plans skill's "each plan should produce working, testable software on its own" — the foundation plan produces a tested static library.

3. **Detailed-vs-summarized split:** Tasks 1–17 (Phases 1–3, MarkoffDocument core) are fully expanded with TDD steps, code, and commits. Tasks 18–55 (Phases 4–13) are summarized. The reason: expanding all 55 tasks in TDD detail produces a ~6000-line plan that is hard to review as a unit. The expanded tasks establish the pattern; the summarized tasks follow the same template (write failing test → run → implement → run → commit). If preferred, the summarized tasks can be expanded into Part 2 of this plan as a separate document once Part 1 lands and the pattern is demonstrably sound.

4. **Type consistency:** Method names match across tasks (e.g., `applyLocalEdit` is consistent in Tasks 9–13; `editsForToggleBold` consistent in Phase 8). Names match the spec.

5. **Placeholder scan:** The summarized tasks (18–55) do contain "summarized" descriptions that aren't full TDD steps — flagging this explicitly in §3 above. The fully-expanded tasks (1–17) have no placeholders.

---

## Execution Handoff

**Plan complete and committed to `docs/plans/2026-04-28-foundation-library.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — Dispatch a fresh subagent per task. Two-stage review per task (code review, then plan-fidelity review). Best for a plan this large since context stays clean and review is focused.

**2. Inline Execution** — Execute tasks in this session via `executing-plans`. Batch execution with checkpoints for review. Faster iteration but consumes more context window.

**Decisions to make before execution:**

- **Expand the summarized tasks first?** Tasks 18–55 are summarized rather than fully TDD-expanded. Two options: (a) expand them now as Part 2 of this plan before any execution, or (b) execute Tasks 1–17, then expand 18–55 informed by what Tasks 1–17 surfaced. Recommend (b) — pattern is established, expansion can incorporate concrete lessons from the first 17 tasks.

- **Subagent-Driven or Inline Execution for Tasks 1–17?**

Which approach for execution, and do you want Tasks 18–55 expanded now or after Tasks 1–17 land?
