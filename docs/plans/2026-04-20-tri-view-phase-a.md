# Tri-View Unified API — Phase A Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Absorb Corbomite's `qutepart-corbomite` and `readingview`
libraries into `~/dev/Markoff/`, stand up a shared `markoff-core`
library with the `MarkdownView` polymorphic base + shared primitives
(document, search, ephemeral state), and wire all three leaf widgets
to inherit the contract. No Corbomite changes in this phase — Corbomite
keeps building against its pinned submodule SHA until Phase B.

**Architecture:** Four peer libraries (`markoff-parser`, `markoff-live`,
`markoff-source`, `markoff-reading`) plus one shared library
(`markoff-core`) under `~/dev/Markoff/libs/`. Core provides
`Markoff::MarkdownView` (abstract base), `Markoff::MarkoffDocument`
(canonical text + shared undo + cached parse), `SearchController` /
`ReplaceController` / `SearchAdapter`, and the existing `SearchBar`
UI moved out of live. Each leaf widget inherits `MarkdownView` with
mostly-forwarding implementations. The leaf widgets remain
content-authoritative in Phase A — `setDocument()` just stores the
pointer; actual shared-document binding happens in Phase C.

**Tech Stack:** Qt6.8+ (Core, Gui, Widgets, Test), C++20, CMake 3.19+,
KF6::SyntaxHighlighting, JKQTMathText, tree-sitter-markdown via
MarkoffParser. Tests use QtTest with `QT_QPA_PLATFORM=offscreen`.

**Reference spec:** `docs/specs/2026-04-20-tri-view-unified-api-design.md`.

---

## File Structure

### New files

- `libs/markoff-core/CMakeLists.txt` — library definition
- `libs/markoff-core/include/markoff/MarkdownView.h` — abstract base
- `libs/markoff-core/include/markoff/CursorPos.h` — value type
- `libs/markoff-core/include/markoff/TextSpan.h` — value type
- `libs/markoff-core/include/markoff/FoldSpec.h` — value type
- `libs/markoff-core/include/markoff/EphemeralState.h`
- `libs/markoff-core/src/EphemeralState.cpp`
- `libs/markoff-core/include/markoff/MarkoffDocument.h`
- `libs/markoff-core/src/MarkoffDocument.cpp`
- `libs/markoff-core/include/markoff/SearchAdapter.h`
- `libs/markoff-core/include/markoff/SearchController.h`
- `libs/markoff-core/src/SearchController.cpp`
- `libs/markoff-core/include/markoff/ReplaceController.h`
- `libs/markoff-core/src/ReplaceController.cpp`
- `libs/markoff-core/tests/CMakeLists.txt`
- `libs/markoff-core/tests/tst_ephemeral_state.cpp`
- `libs/markoff-core/tests/tst_markoff_document.cpp`
- `libs/markoff-core/tests/tst_search_controller.cpp`
- `libs/markoff-core/tests/tst_replace_controller.cpp`

### Moved files

- `libs/markoff/include/markoff/SearchBar.h` → `libs/markoff-core/include/markoff/SearchBar.h`
- `libs/markoff/src/SearchBar.cpp` → `libs/markoff-core/src/SearchBar.cpp`
- `libs/markoff/tests/tst_search_bar.cpp` → `libs/markoff-core/tests/tst_search_bar.cpp`

### Renamed dirs

- `libs/markoff/` → `libs/markoff-live/`
- `Corbomite/libs/qutepart-corbomite/` → `~/dev/Markoff/libs/markoff-source/` (subtree import)
- `Corbomite/libs/readingview/` → `~/dev/Markoff/libs/markoff-reading/` (subtree import)

### Modified files

- `CMakeLists.txt` (top level) — add the four new `add_subdirectory` lines
- `libs/markoff-live/CMakeLists.txt` — project name, target name, depend on `markoff-core`, drop SearchBar sources
- `libs/markoff-live/include/markoff/Editor.h` — inherit `MarkdownView`, add virtual overrides, rename class to `LivePreviewEditor` (keep `Editor` as deprecated alias)
- `libs/markoff-live/src/Editor.cpp` — implement MarkdownView virtuals as forwarding calls
- `libs/markoff-source/include/markoff/source/SourceEditor.h` — renamed from `Corbomite::SourceEditor`, inherits `MarkdownView`
- `libs/markoff-source/src/SourceEditor.cpp` — namespace + base-class wiring
- `libs/markoff-source/CMakeLists.txt` — target name, ALIAS `Markoff::Source`
- `libs/markoff-reading/include/markoff/reading/ReadingView.h` — namespace, inherits `MarkdownView`
- `libs/markoff-reading/src/ReadingView.cpp` — namespace + base-class wiring
- `libs/markoff-reading/CMakeLists.txt` — target name, ALIAS `Markoff::Reading`
- `tests/markoff/CMakeLists.txt` — add new cross-lib smoke test
- `tests/markoff/tst_tri_view_smoke.cpp` (new) — polymorphic dispatch through `MarkdownView*`

### Build directory

All tasks use `build-dev/` at the Markoff top level:
```bash
cmake -S /home/clinton/dev/Markoff -B /home/clinton/dev/Markoff/build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/Markoff/build-dev
```

Tests: `cd /home/clinton/dev/Markoff/build-dev && ctest --output-on-failure`.

---

## Task 1: Bootstrap `libs/markoff-core/` skeleton

**Files:**
- Create: `libs/markoff-core/CMakeLists.txt`
- Create: `libs/markoff-core/src/_placeholder.cpp`
- Create: `libs/markoff-core/include/markoff/MarkoffCoreExport.h`
- Create: `libs/markoff-core/CLAUDE.md`
- Modify: `CMakeLists.txt` (top level)

The empty library has to link, so one tiny placeholder source goes in
until Task 2 adds real content. This lets us verify the CMake wiring
without waiting on the full type surface.

- [ ] **Step 1: Create `libs/markoff-core/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.19)
project(markoff_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)

add_library(markoff_core STATIC
    src/_placeholder.cpp
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
        MarkoffParser::MarkoffParser
)

set_target_properties(markoff_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(Markoff::Core ALIAS markoff_core)

option(MARKOFF_CORE_BUILD_TESTS "Build markoff-core tests" ON)
if(MARKOFF_CORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Create the placeholder source**

`libs/markoff-core/src/_placeholder.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Placeholder TU so the static library has at least one object.
// Removed as soon as real sources land (Task 2 onward).
namespace Markoff::Internal {
[[maybe_unused]] inline void _markoff_core_placeholder() {}
}
```

- [ ] **Step 3: Create `libs/markoff-core/include/markoff/MarkoffCoreExport.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Reserved for future DLL-export macros if we ever ship shared
// builds. For now MARKOFF_CORE_EXPORT is empty — static lib.
#define MARKOFF_CORE_EXPORT
```

- [ ] **Step 4: Create `libs/markoff-core/CLAUDE.md`**

```markdown
# markoff-core

Shared primitives for the Markoff tri-view family (live preview,
source, reading).

Public contract:
- `Markoff::MarkdownView` — abstract QWidget base implemented by each
  leaf view widget.
- `Markoff::MarkoffDocument` — canonical markdown source + undo +
  cached parse. Views attach via `setDocument()`.
- `Markoff::SearchController` / `ReplaceController` / `SearchAdapter`
  — view-agnostic find/replace engine.
- `Markoff::Theme`, `Markoff::ResourceProvider`, `Markoff::LinkResolver`
  (future work in Phase C) — currently live in markoff-live and will
  migrate here as duplication is consolidated.

Depends on: Qt6 (Core, Gui, Widgets), KF6::SyntaxHighlighting,
MarkoffParser. Does NOT depend on any leaf widget library.

See `docs/specs/2026-04-20-tri-view-unified-api-design.md` at the
Markoff top level for the overall architecture.
```

- [ ] **Step 5: Update top-level `CMakeLists.txt`**

Modify `/home/clinton/dev/Markoff/CMakeLists.txt`:

Replace the block:

```cmake
add_subdirectory(libs/rapidyaml)
add_subdirectory(libs/markoff-parser)
add_subdirectory(libs/markoff)
```

With:

```cmake
add_subdirectory(libs/rapidyaml)
add_subdirectory(libs/markoff-parser)
add_subdirectory(libs/markoff-core)
add_subdirectory(libs/markoff)
```

And update the comment block above (lines that list libraries) to
include `libs/markoff-core   (Markoff::Core — shared primitives)`.

- [ ] **Step 6: Create empty tests subdir to satisfy the CMakeLists**

`libs/markoff-core/tests/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.19)
project(markoff_core_tests LANGUAGES CXX)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# Tests land here starting Task 2.
```

- [ ] **Step 7: Configure + build to verify**

```bash
cmake -S /home/clinton/dev/Markoff -B /home/clinton/dev/Markoff/build-dev \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/Markoff/build-dev --target markoff_core
```

Expected: builds clean. `libs/markoff_core.a` (or similar) exists in
`build-dev/libs/markoff-core/`.

- [ ] **Step 8: Commit**

```bash
cd /home/clinton/dev/Markoff
git add CMakeLists.txt libs/markoff-core/
git commit -m "Bootstrap libs/markoff-core/ skeleton

Empty static library that will host the shared MarkdownView contract
plus supporting primitives. Placeholder TU lets the lib link until
real sources land.

Per docs/specs/2026-04-20-tri-view-unified-api-design.md Phase A step 3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Value types — `CursorPos`, `TextSpan`, `FoldSpec`

**Files:**
- Create: `libs/markoff-core/include/markoff/CursorPos.h`
- Create: `libs/markoff-core/include/markoff/TextSpan.h`
- Create: `libs/markoff-core/include/markoff/FoldSpec.h`
- Create: `libs/markoff-core/tests/tst_value_types.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

Value types used across the `MarkdownView` contract. Header-only —
simple structs with equality operators and `qHash` so they can live
in `QSet`/`QHash`.

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/tst_value_types.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QHash>

#include <markoff/CursorPos.h>
#include <markoff/TextSpan.h>
#include <markoff/FoldSpec.h>

using namespace Markoff;

class TstValueTypes : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cursorPosEquality() {
        QCOMPARE(CursorPos{3, 7}, (CursorPos{3, 7}));
        QVERIFY(!((CursorPos{3, 7}) == (CursorPos{3, 8})));
    }

    void textSpanOrdering() {
        const TextSpan a{0, 5};
        const TextSpan b{0, 10};
        QVERIFY(a.length() == 5);
        QVERIFY(b.contains(a.offset));
        QVERIFY(!a.contains(8));
    }

    void foldSpecHashesByLine() {
        QSet<FoldSpec> s;
        s.insert({2, 3});
        s.insert({2, 3});
        s.insert({2, 5});  // different level: distinct entry
        QCOMPARE(s.size(), 2);
    }

    void textSpanQSetRoundTrips() {
        QSet<TextSpan> s;
        s.insert({0, 5});
        s.insert({0, 5});
        s.insert({6, 3});
        QCOMPARE(s.size(), 2);
    }
};

QTEST_MAIN(TstValueTypes)
#include "tst_value_types.moc"
```

- [ ] **Step 2: Register the test**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_value_types tst_value_types.cpp)
add_test(NAME tst_markoff_value_types COMMAND tst_markoff_value_types)
target_link_libraries(tst_markoff_value_types PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_value_types PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_value_types
```

Expected: FAIL with "markoff/CursorPos.h: No such file or directory"
(or equivalent — headers don't exist yet).

- [ ] **Step 4: Create `libs/markoff-core/include/markoff/CursorPos.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QHashFunctions>

namespace Markoff {

/// A 1-based (line, column) position used by widgets that expose a
/// cursor (Live Preview, Source). Reading view has no cursor; its
/// MarkdownView::cursorPosition() returns a default-constructed value.
struct CursorPos {
    int line = 0;
    int column = 0;

    friend bool operator==(const CursorPos &a, const CursorPos &b) {
        return a.line == b.line && a.column == b.column;
    }
    friend bool operator!=(const CursorPos &a, const CursorPos &b) {
        return !(a == b);
    }
};

inline size_t qHash(const CursorPos &p, size_t seed = 0) noexcept {
    return qHashMulti(seed, p.line, p.column);
}

}  // namespace Markoff
```

- [ ] **Step 5: Create `libs/markoff-core/include/markoff/TextSpan.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QHashFunctions>

namespace Markoff {

/// A half-open range `[offset, offset + length)` over the canonical
/// flat markdown source owned by MarkoffDocument. Every search match
/// and every replace target is expressed as a TextSpan so views can
/// translate to their local coordinates.
struct TextSpan {
    int offset = 0;
    int length = 0;

    int end() const { return offset + length; }
    bool isEmpty() const { return length == 0; }
    bool contains(int pos) const {
        return pos >= offset && pos < end();
    }

    friend bool operator==(const TextSpan &a, const TextSpan &b) {
        return a.offset == b.offset && a.length == b.length;
    }
    friend bool operator!=(const TextSpan &a, const TextSpan &b) {
        return !(a == b);
    }
    friend bool operator<(const TextSpan &a, const TextSpan &b) {
        if (a.offset != b.offset) return a.offset < b.offset;
        return a.length < b.length;
    }
};

inline size_t qHash(const TextSpan &s, size_t seed = 0) noexcept {
    return qHashMulti(seed, s.offset, s.length);
}

}  // namespace Markoff
```

- [ ] **Step 6: Create `libs/markoff-core/include/markoff/FoldSpec.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QHashFunctions>

namespace Markoff {

/// Identifies a collapsible heading by (starting line, heading level).
/// Used by MarkdownView::foldedHeadings() / setFoldedHeadings().
/// Reading, Source, and Live Preview each map FoldSpec to their
/// internal fold representation.
struct FoldSpec {
    int line = 0;
    int level = 0;

    friend bool operator==(const FoldSpec &a, const FoldSpec &b) {
        return a.line == b.line && a.level == b.level;
    }
    friend bool operator!=(const FoldSpec &a, const FoldSpec &b) {
        return !(a == b);
    }
};

inline size_t qHash(const FoldSpec &f, size_t seed = 0) noexcept {
    return qHashMulti(seed, f.line, f.level);
}

}  // namespace Markoff
```

- [ ] **Step 7: Run test to verify it passes**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_value_types && \
  QT_QPA_PLATFORM=offscreen /home/clinton/dev/Markoff/build-dev/bin/tst_markoff_value_types
```

Expected: all four slots PASS.

- [ ] **Step 8: Commit**

```bash
cd /home/clinton/dev/Markoff
git add libs/markoff-core/
git commit -m "markoff-core: add CursorPos, TextSpan, FoldSpec value types

Value types referenced by MarkdownView and MarkoffDocument. Each has
equality and qHash so it can live in QSet/QHash.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: `EphemeralState` + JSON round-trip

**Files:**
- Create: `libs/markoff-core/include/markoff/EphemeralState.h`
- Create: `libs/markoff-core/src/EphemeralState.cpp`
- Create: `libs/markoff-core/tests/tst_ephemeral_state.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

Transport struct for workspace.json round-trip. Three common fields
(scroll, cursor, viewMode as string, folded headings) plus an opaque
per-view `QJsonObject extras` that each widget round-trips freely.

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/tst_ephemeral_state.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>

#include <markoff/EphemeralState.h>

using namespace Markoff;

class TstEphemeralState : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void defaultStateSerialises() {
        EphemeralState s;
        const QJsonObject j = s.toJson();
        QVERIFY(j.contains("scroll"));
        QCOMPARE(j.value("scroll").toDouble(), 0.0);
        QVERIFY(j.contains("mode"));
    }

    void nonDefaultRoundTrips() {
        EphemeralState a;
        a.scroll = 42.73f;
        a.cursor = {17, 4};
        a.viewMode = QStringLiteral("live");
        a.foldedHeadings = {{3, 1}, {12, 2}};
        QJsonObject extras;
        extras.insert("liveScrollOffsets", QJsonArray{1, 2, 3});
        a.extras = extras;

        const QJsonObject j = a.toJson();
        const EphemeralState b = EphemeralState::fromJson(j);

        QCOMPARE(b.scroll, a.scroll);
        QCOMPARE(b.cursor, a.cursor);
        QCOMPARE(b.viewMode, a.viewMode);
        QCOMPARE(b.foldedHeadings.size(), a.foldedHeadings.size());
        QCOMPARE(b.foldedHeadings[0], a.foldedHeadings[0]);
        QCOMPARE(b.extras.value("liveScrollOffsets").toArray().size(), 3);
    }

    void missingKeysDefault() {
        const EphemeralState s = EphemeralState::fromJson({});
        QCOMPARE(s.scroll, 0.0f);
        QCOMPARE(s.cursor, (CursorPos{0, 0}));
        QVERIFY(s.foldedHeadings.isEmpty());
    }
};

QTEST_MAIN(TstEphemeralState)
#include "tst_ephemeral_state.moc"
```

- [ ] **Step 2: Register the test**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_ephemeral_state tst_ephemeral_state.cpp)
add_test(NAME tst_markoff_ephemeral_state COMMAND tst_markoff_ephemeral_state)
target_link_libraries(tst_markoff_ephemeral_state PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_ephemeral_state PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_ephemeral_state
```

Expected: FAIL with "markoff/EphemeralState.h: No such file or directory".

- [ ] **Step 4: Create `libs/markoff-core/include/markoff/EphemeralState.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <markoff/CursorPos.h>
#include <markoff/FoldSpec.h>

namespace Markoff {

/// Per-leaf view state the host persists in workspace.json. Three
/// common fields plus an opaque per-view QJsonObject so each widget
/// can round-trip extras without polluting the base contract.
struct EphemeralState {
    float scroll = 0.0f;
    CursorPos cursor;
    QString viewMode;               // host-defined string; Markoff doesn't interpret
    QVector<FoldSpec> foldedHeadings;
    QJsonObject extras;             // opaque; each view owns its own shape

    QJsonObject toJson() const;
    static EphemeralState fromJson(const QJsonObject &);
};

}  // namespace Markoff
```

- [ ] **Step 5: Create `libs/markoff-core/src/EphemeralState.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/EphemeralState.h>

#include <QJsonArray>

namespace Markoff {

QJsonObject EphemeralState::toJson() const
{
    QJsonObject j;
    j.insert(QStringLiteral("scroll"), scroll);
    j.insert(QStringLiteral("cursorLine"), cursor.line);
    j.insert(QStringLiteral("cursorColumn"), cursor.column);
    j.insert(QStringLiteral("mode"), viewMode);

    QJsonArray folds;
    for (const FoldSpec &f : foldedHeadings) {
        folds.append(QJsonObject{
            {QStringLiteral("line"), f.line},
            {QStringLiteral("level"), f.level},
        });
    }
    j.insert(QStringLiteral("folded"), folds);

    j.insert(QStringLiteral("extras"), extras);
    return j;
}

EphemeralState EphemeralState::fromJson(const QJsonObject &j)
{
    EphemeralState s;
    s.scroll = static_cast<float>(j.value(QStringLiteral("scroll")).toDouble(0.0));
    s.cursor.line = j.value(QStringLiteral("cursorLine")).toInt(0);
    s.cursor.column = j.value(QStringLiteral("cursorColumn")).toInt(0);
    s.viewMode = j.value(QStringLiteral("mode")).toString();

    for (const QJsonValue &v : j.value(QStringLiteral("folded")).toArray()) {
        const QJsonObject o = v.toObject();
        s.foldedHeadings.append(FoldSpec{
            o.value(QStringLiteral("line")).toInt(0),
            o.value(QStringLiteral("level")).toInt(0),
        });
    }

    s.extras = j.value(QStringLiteral("extras")).toObject();
    return s;
}

}  // namespace Markoff
```

- [ ] **Step 6: Add the source to `libs/markoff-core/CMakeLists.txt`**

In the `add_library(markoff_core STATIC ...)` block, replace
`src/_placeholder.cpp` with:

```cmake
add_library(markoff_core STATIC
    src/EphemeralState.cpp
    include/markoff/CursorPos.h
    include/markoff/TextSpan.h
    include/markoff/FoldSpec.h
    include/markoff/EphemeralState.h
    include/markoff/MarkoffCoreExport.h
)
```

Delete `libs/markoff-core/src/_placeholder.cpp`.

- [ ] **Step 7: Run test to verify it passes**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_ephemeral_state && \
  QT_QPA_PLATFORM=offscreen /home/clinton/dev/Markoff/build-dev/bin/tst_markoff_ephemeral_state
```

Expected: all three slots PASS.

- [ ] **Step 8: Commit**

```bash
cd /home/clinton/dev/Markoff
git add libs/markoff-core/
git rm libs/markoff-core/src/_placeholder.cpp 2>/dev/null || true
git commit -m "markoff-core: add EphemeralState with JSON round-trip

Transport struct for per-leaf persistence in workspace.json. Common
fields (scroll, cursor, view-mode string, folded headings) plus an
opaque extras QJsonObject each widget owns.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: `MarkoffDocument` — text, undo, transactions, cached parse

**Files:**
- Create: `libs/markoff-core/include/markoff/MarkoffDocument.h`
- Create: `libs/markoff-core/src/MarkoffDocument.cpp`
- Create: `libs/markoff-core/tests/tst_markoff_document.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

The canonical source document + shared undo. Wraps a `QTextDocument`
internally so `QPlainTextEdit::setDocument()` (Source mode in later
tasks) can adopt it. Parse caching in this task is synchronous only;
the async worker moves here in Phase C.

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/tst_markoff_document.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTextDocument>

#include <markoff/MarkoffDocument.h>

using namespace Markoff;

class TstMarkoffDocument : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void setPlainTextEmitsContentsChanged() {
        MarkoffDocument doc;
        QSignalSpy spy(&doc, &MarkoffDocument::contentsChanged);
        doc.setPlainText(QStringLiteral("hello"));
        QCOMPARE(doc.plainText(), QStringLiteral("hello"));
        QVERIFY(spy.count() >= 1);
    }

    void replaceMutatesTextAndEmits() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("hello world"));
        QSignalSpy spy(&doc, &MarkoffDocument::contentsChanged);
        doc.replace(6, 5, QStringLiteral("there"));
        QCOMPARE(doc.plainText(), QStringLiteral("hello there"));
        QVERIFY(spy.count() >= 1);
    }

    void insertAndRemove() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("ac"));
        doc.insert(1, QStringLiteral("b"));
        QCOMPARE(doc.plainText(), QStringLiteral("abc"));
        doc.remove(0, 1);
        QCOMPARE(doc.plainText(), QStringLiteral("bc"));
    }

    void transactionBoundaryCoalescesUndo() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("hello"));
        QTextDocument *td = doc.textDocument();
        const QString beforeTxn = doc.plainText();

        doc.beginTransaction();
        doc.insert(5, QStringLiteral(" a"));
        doc.insert(doc.plainText().size(), QStringLiteral(" b"));
        doc.endTransaction();

        QCOMPARE(doc.plainText(), QStringLiteral("hello a b"));

        // One undo should revert the entire transaction.
        td->undo();
        QCOMPARE(doc.plainText(), beforeTxn);
    }

    void untransactedEditsAreIndividuallyUndoable() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a"));
        QTextDocument *td = doc.textDocument();

        doc.insert(1, QStringLiteral("b"));
        doc.insert(2, QStringLiteral("c"));
        QCOMPARE(doc.plainText(), QStringLiteral("abc"));

        td->undo();
        QCOMPARE(doc.plainText(), QStringLiteral("ab"));
        td->undo();
        QCOMPARE(doc.plainText(), QStringLiteral("a"));
    }

    void parseIsSyncForSmallDocs() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("# Heading\n\nbody\n"));
        QVERIFY(!doc.parseIsPending());
        QVERIFY(doc.parsed() != nullptr);
    }

    void parseIsInvalidatedOnEdit() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("# h\n"));
        const auto *first = doc.parsed();
        QVERIFY(first != nullptr);
        doc.insert(doc.plainText().size(), QStringLiteral("\nmore\n"));
        // After an edit the cached parse is either regenerated or
        // cleared. Either way the previous pointer is no longer the
        // authoritative result. We verify via parseIsPending() or a
        // non-null successor.
        if (doc.parseIsPending()) {
            QVERIFY(doc.parsed() == nullptr);
        } else {
            QVERIFY(doc.parsed() != nullptr);
        }
    }
};

QTEST_MAIN(TstMarkoffDocument)
#include "tst_markoff_document.moc"
```

- [ ] **Step 2: Register the test**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_document tst_markoff_document.cpp)
add_test(NAME tst_markoff_document COMMAND tst_markoff_document)
target_link_libraries(tst_markoff_document PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_document
```

Expected: FAIL (`MarkoffDocument.h` not found).

- [ ] **Step 4: Create `libs/markoff-core/include/markoff/MarkoffDocument.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

class QTextDocument;

namespace Markoff {

class Document;                          // markoff-parser

/// Canonical markdown source + undo + cached parse. Views attach via
/// MarkdownView::setDocument(). In Phase A the leaf widgets still
/// own their own content; attaching a MarkoffDocument stores the
/// pointer but doesn't yet bind text. Phase C flips that.
class MarkoffDocument : public QObject {
    Q_OBJECT
public:
    explicit MarkoffDocument(QObject *parent = nullptr);
    ~MarkoffDocument() override;

    QString plainText() const;
    void setPlainText(const QString &text);

    QTextDocument *textDocument() const;

    void replace(int sourceOffset, int removeLen, const QString &insert);
    void insert(int sourceOffset, const QString &text);
    void remove(int sourceOffset, int len);

    void beginTransaction();
    void endTransaction();

    void setCoalescingIdleMs(int ms);
    int coalescingIdleMs() const;

    /// Synchronous parse cache. Async worker lands in Phase C.
    const Document *parsed() const;
    bool parseIsPending() const;

Q_SIGNALS:
    void contentsChanged();
    void parsed(const Document *);

private:
    struct Private;
    Private *d;
};

}  // namespace Markoff
```

- [ ] **Step 5: Create `libs/markoff-core/src/MarkoffDocument.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/MarkoffDocument.h>

#include <QTextCursor>
#include <QTextDocument>

#include <markoff/Document.h>

namespace Markoff {

struct MarkoffDocument::Private {
    QTextDocument *textDoc = nullptr;
    int coalescingIdleMs = 500;
    bool parseDirty = true;
    std::unique_ptr<Document> cachedParse;
    int transactionDepth = 0;
};

MarkoffDocument::MarkoffDocument(QObject *parent)
    : QObject(parent), d(new Private)
{
    d->textDoc = new QTextDocument(this);
    connect(d->textDoc, &QTextDocument::contentsChanged,
            this, [this] {
                d->parseDirty = true;
                d->cachedParse.reset();
                Q_EMIT contentsChanged();
            });
}

MarkoffDocument::~MarkoffDocument() { delete d; }

QString MarkoffDocument::plainText() const
{
    return d->textDoc->toPlainText();
}

void MarkoffDocument::setPlainText(const QString &text)
{
    d->textDoc->setPlainText(text);
    // setPlainText emits contentsChanged, which invalidates the cache.
}

QTextDocument *MarkoffDocument::textDocument() const
{
    return d->textDoc;
}

void MarkoffDocument::replace(int sourceOffset, int removeLen,
                              const QString &insert)
{
    QTextCursor c(d->textDoc);
    c.setPosition(sourceOffset);
    if (removeLen > 0) {
        c.setPosition(sourceOffset + removeLen, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    }
    if (!insert.isEmpty()) {
        c.insertText(insert);
    }
}

void MarkoffDocument::insert(int sourceOffset, const QString &text)
{
    if (text.isEmpty()) return;
    QTextCursor c(d->textDoc);
    c.setPosition(sourceOffset);
    c.insertText(text);
}

void MarkoffDocument::remove(int sourceOffset, int len)
{
    if (len <= 0) return;
    QTextCursor c(d->textDoc);
    c.setPosition(sourceOffset);
    c.setPosition(sourceOffset + len, QTextCursor::KeepAnchor);
    c.removeSelectedText();
}

void MarkoffDocument::beginTransaction()
{
    if (d->transactionDepth++ == 0) {
        QTextCursor c(d->textDoc);
        c.beginEditBlock();
        // The cursor goes out of scope here; endEditBlock() pairs
        // through the document's own tracking. We use a separate
        // cursor at endTransaction() to close the same block —
        // QTextDocument nests editBlock calls on any cursor tied to it.
    }
}

void MarkoffDocument::endTransaction()
{
    if (--d->transactionDepth == 0) {
        QTextCursor c(d->textDoc);
        c.endEditBlock();
    }
}

void MarkoffDocument::setCoalescingIdleMs(int ms)
{
    d->coalescingIdleMs = ms;
}

int MarkoffDocument::coalescingIdleMs() const
{
    return d->coalescingIdleMs;
}

const Document *MarkoffDocument::parsed() const
{
    if (!d->parseDirty && d->cachedParse) {
        return d->cachedParse.get();
    }
    // Sync parse for Phase A. Async worker arrives in Phase C.
    d->cachedParse = std::make_unique<Document>(
        Document::fromMarkdown(d->textDoc->toPlainText()));
    d->parseDirty = false;
    // Emit while the caller is inside parsed() — OK, Qt signals are
    // direct by default for same-thread receivers.
    Q_EMIT const_cast<MarkoffDocument *>(this)->parsed(
        d->cachedParse.get());
    return d->cachedParse.get();
}

bool MarkoffDocument::parseIsPending() const
{
    // Sync parse only in Phase A — never pending.
    Q_UNUSED(d);
    return false;
}

}  // namespace Markoff
```

- [ ] **Step 6: Add the source to `libs/markoff-core/CMakeLists.txt`**

Update the `add_library(markoff_core STATIC ...)` block to include:

```cmake
add_library(markoff_core STATIC
    src/EphemeralState.cpp
    src/MarkoffDocument.cpp
    include/markoff/CursorPos.h
    include/markoff/TextSpan.h
    include/markoff/FoldSpec.h
    include/markoff/EphemeralState.h
    include/markoff/MarkoffDocument.h
    include/markoff/MarkoffCoreExport.h
)
```

- [ ] **Step 7: Run test to verify it passes**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_document && \
  QT_QPA_PLATFORM=offscreen /home/clinton/dev/Markoff/build-dev/bin/tst_markoff_document
```

Expected: all seven slots PASS.

- [ ] **Step 8: Commit**

```bash
cd /home/clinton/dev/Markoff
git add libs/markoff-core/
git commit -m "markoff-core: add MarkoffDocument

Canonical text + shared undo + synchronous parse cache. Wraps a
QTextDocument so QPlainTextEdit::setDocument() can adopt it in later
tasks. Transaction API coalesces multi-edit bursts into a single
undo entry.

Async parse worker, coalescing idle timer, and AST signal wiring
arrive in Phase C per the spec.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `SearchAdapter` + `SearchController`

**Files:**
- Create: `libs/markoff-core/include/markoff/SearchAdapter.h`
- Create: `libs/markoff-core/include/markoff/SearchController.h`
- Create: `libs/markoff-core/src/SearchController.cpp`
- Create: `libs/markoff-core/tests/tst_search_controller.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

View-independent search engine. Walks `MarkoffDocument::plainText()`
producing `QVector<TextSpan>`; maintains current index; navigation
primitives are `next() / prev()` with wrap.

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/tst_search_controller.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/MarkoffDocument.h>
#include <markoff/SearchController.h>
#include <markoff/SearchAdapter.h>

using namespace Markoff;

namespace {
class StubAdapter : public SearchAdapter {
public:
    int cursorSourceOffset() const override { return cursor; }
    void highlightMatches(QVector<TextSpan> s) override { highlighted = s; }
    void clearMatchHighlight() override { highlighted.clear(); }
    void scrollMatchIntoView(TextSpan s) override { scrolled = s; }

    int cursor = 0;
    QVector<TextSpan> highlighted;
    TextSpan scrolled{-1, -1};
};
}  // namespace

class TstSearchController : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void findsAllLiteralMatches() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("ab ab AB"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        c.setQuery(QStringLiteral("ab"));
        QCOMPARE(c.matchCount(), 2);  // case-insensitive off by default? test below
    }

    void caseSensitiveFlag() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("ab ab AB"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        SearchController::Flags f;
        f.caseSensitive = true;
        c.setFlags(f);
        c.setQuery(QStringLiteral("ab"));
        QCOMPARE(c.matchCount(), 2);
        f.caseSensitive = false;
        c.setFlags(f);
        QCOMPARE(c.matchCount(), 3);
    }

    void nextPrevWraps() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("x y x y x"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        c.setQuery(QStringLiteral("x"));
        QCOMPARE(c.matchCount(), 3);
        QCOMPARE(c.currentIndex(), 0);
        c.next();
        QCOMPARE(c.currentIndex(), 1);
        c.next();
        QCOMPARE(c.currentIndex(), 2);
        c.next();
        QCOMPARE(c.currentIndex(), 0);  // wrap
        c.prev();
        QCOMPARE(c.currentIndex(), 2);  // wrap back
    }

    void highlightsMatchesOnQueryChange() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a bb a"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        QCOMPARE(adapter.highlighted.size(), 2);
    }

    void clearsOnEmptyQuery() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        QCOMPARE(adapter.highlighted.size(), 1);
        c.setQuery({});
        QVERIFY(adapter.highlighted.isEmpty());
    }

    void wholeWordFlag() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("cat catalog category"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        SearchController::Flags f;
        f.wholeWord = true;
        c.setFlags(f);
        c.setQuery(QStringLiteral("cat"));
        QCOMPARE(c.matchCount(), 1);
    }

    void regexFlag() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("foo123 bar456"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        SearchController::Flags f;
        f.regex = true;
        c.setFlags(f);
        c.setQuery(QStringLiteral("[a-z]+\\d+"));
        QCOMPARE(c.matchCount(), 2);
    }

    void nextStartsFromCursor() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("xxxxx"));
        StubAdapter adapter;
        adapter.cursor = 3;
        SearchController c(&doc, &adapter);
        c.setQuery(QStringLiteral("x"));
        // Adapter cursor at 3 → first match at or after 3 is index 3.
        QCOMPARE(c.currentIndex(), 3);
    }
};

QTEST_MAIN(TstSearchController)
#include "tst_search_controller.moc"
```

- [ ] **Step 2: Register the test**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_search_controller tst_search_controller.cpp)
add_test(NAME tst_markoff_search_controller COMMAND tst_markoff_search_controller)
target_link_libraries(tst_markoff_search_controller PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_search_controller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_search_controller
```

Expected: FAIL (`SearchController.h` not found).

- [ ] **Step 4: Create `libs/markoff-core/include/markoff/SearchAdapter.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QVector>

#include <markoff/TextSpan.h>

namespace Markoff {

/// Each MarkdownView subclass provides one of these to SearchController
/// so the engine can highlight and scroll matches without knowing
/// anything about the view internals.
class SearchAdapter {
public:
    virtual ~SearchAdapter() = default;

    /// Where should "find from cursor" start? Source offset into the
    /// owning MarkoffDocument's plainText(). Read-only views return
    /// a sensible default (e.g. their scroll-top source offset).
    virtual int cursorSourceOffset() const = 0;

    /// Paint highlights for every match. May be called with an empty
    /// vector to request all highlights cleared.
    virtual void highlightMatches(QVector<TextSpan>) = 0;

    /// Explicit clear hook. SearchController calls this when the
    /// query is empty, before deactivation.
    virtual void clearMatchHighlight() = 0;

    /// Scroll the view so the match is visible. Called on next()/prev()
    /// and on query change if there is at least one match.
    virtual void scrollMatchIntoView(TextSpan) = 0;

    /// False for Reading-like read-only views. ReplaceController
    /// checks this and refuses to mutate when false.
    virtual bool supportsReplace() const { return true; }
};

}  // namespace Markoff
```

- [ ] **Step 5: Create `libs/markoff-core/include/markoff/SearchController.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <markoff/TextSpan.h>

namespace Markoff {

class MarkoffDocument;
class SearchAdapter;

class SearchController : public QObject {
    Q_OBJECT
public:
    struct Flags {
        bool caseSensitive = false;
        bool wholeWord = false;
        bool regex = false;
        bool wrap = true;
    };

    SearchController(MarkoffDocument *doc, SearchAdapter *adapter,
                     QObject *parent = nullptr);
    ~SearchController() override;

    void setFlags(Flags);
    Flags flags() const;

    void setQuery(const QString &q);
    QString query() const;

    int matchCount() const;
    int currentIndex() const;
    const QVector<TextSpan> &matches() const;

    void next();
    void prev();

Q_SIGNALS:
    void matchesChanged();
    void currentMatchChanged(int index);

protected:
    void recomputeMatches();
    void notifyAdapterHighlight();
    void scrollToCurrent();

    MarkoffDocument *m_doc = nullptr;
    SearchAdapter *m_adapter = nullptr;
    Flags m_flags;
    QString m_query;
    QVector<TextSpan> m_matches;
    int m_current = -1;
};

}  // namespace Markoff
```

- [ ] **Step 6: Create `libs/markoff-core/src/SearchController.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/SearchController.h>

#include <QRegularExpression>

#include <markoff/MarkoffDocument.h>
#include <markoff/SearchAdapter.h>

namespace Markoff {

SearchController::SearchController(MarkoffDocument *doc,
                                   SearchAdapter *adapter,
                                   QObject *parent)
    : QObject(parent), m_doc(doc), m_adapter(adapter)
{
    connect(m_doc, &MarkoffDocument::contentsChanged,
            this, &SearchController::recomputeMatches);
}

SearchController::~SearchController() = default;

void SearchController::setFlags(Flags f)
{
    m_flags = f;
    recomputeMatches();
}

SearchController::Flags SearchController::flags() const { return m_flags; }

void SearchController::setQuery(const QString &q)
{
    m_query = q;
    recomputeMatches();
}

QString SearchController::query() const { return m_query; }
int SearchController::matchCount() const { return m_matches.size(); }
int SearchController::currentIndex() const { return m_current; }
const QVector<TextSpan> &SearchController::matches() const { return m_matches; }

void SearchController::recomputeMatches()
{
    m_matches.clear();

    if (m_query.isEmpty()) {
        m_current = -1;
        m_adapter->clearMatchHighlight();
        Q_EMIT matchesChanged();
        Q_EMIT currentMatchChanged(m_current);
        return;
    }

    const QString text = m_doc->plainText();

    if (m_flags.regex) {
        QRegularExpression::PatternOptions opts =
            QRegularExpression::NoPatternOption;
        if (!m_flags.caseSensitive)
            opts |= QRegularExpression::CaseInsensitiveOption;
        QRegularExpression re(m_query, opts);
        if (!re.isValid()) {
            m_current = -1;
            m_adapter->clearMatchHighlight();
            Q_EMIT matchesChanged();
            Q_EMIT currentMatchChanged(m_current);
            return;
        }
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            if (m.capturedLength() == 0) continue;  // avoid infinite hits
            m_matches.append({m.capturedStart(), m.capturedLength()});
        }
    } else {
        const Qt::CaseSensitivity cs = m_flags.caseSensitive
            ? Qt::CaseSensitive : Qt::CaseInsensitive;
        int from = 0;
        while (true) {
            int idx = text.indexOf(m_query, from, cs);
            if (idx < 0) break;

            if (m_flags.wholeWord) {
                const bool leftBoundary =
                    idx == 0 || !text.at(idx - 1).isLetterOrNumber();
                const int endIdx = idx + m_query.size();
                const bool rightBoundary =
                    endIdx == text.size() || !text.at(endIdx).isLetterOrNumber();
                if (!leftBoundary || !rightBoundary) {
                    from = idx + 1;
                    continue;
                }
            }

            m_matches.append({idx, m_query.size()});
            from = idx + m_query.size();
        }
    }

    if (m_matches.isEmpty()) {
        m_current = -1;
    } else {
        const int cursor = m_adapter->cursorSourceOffset();
        m_current = 0;
        for (int i = 0; i < m_matches.size(); ++i) {
            if (m_matches[i].offset >= cursor) {
                m_current = i;
                break;
            }
        }
    }

    notifyAdapterHighlight();
    scrollToCurrent();
    Q_EMIT matchesChanged();
    Q_EMIT currentMatchChanged(m_current);
}

void SearchController::notifyAdapterHighlight()
{
    m_adapter->highlightMatches(m_matches);
}

void SearchController::scrollToCurrent()
{
    if (m_current >= 0 && m_current < m_matches.size())
        m_adapter->scrollMatchIntoView(m_matches[m_current]);
}

void SearchController::next()
{
    if (m_matches.isEmpty()) return;
    const int last = m_matches.size() - 1;
    if (m_current < last) {
        ++m_current;
    } else if (m_flags.wrap) {
        m_current = 0;
    } else {
        return;
    }
    scrollToCurrent();
    Q_EMIT currentMatchChanged(m_current);
}

void SearchController::prev()
{
    if (m_matches.isEmpty()) return;
    if (m_current > 0) {
        --m_current;
    } else if (m_flags.wrap) {
        m_current = m_matches.size() - 1;
    } else {
        return;
    }
    scrollToCurrent();
    Q_EMIT currentMatchChanged(m_current);
}

}  // namespace Markoff
```

- [ ] **Step 7: Add the source to `libs/markoff-core/CMakeLists.txt`**

Update the `add_library(markoff_core STATIC ...)` block to include
`src/SearchController.cpp` and the new headers.

- [ ] **Step 8: Run test to verify it passes**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_search_controller && \
  QT_QPA_PLATFORM=offscreen /home/clinton/dev/Markoff/build-dev/bin/tst_markoff_search_controller
```

Expected: all eight slots PASS.

- [ ] **Step 9: Commit**

```bash
cd /home/clinton/dev/Markoff
git add libs/markoff-core/
git commit -m "markoff-core: add SearchAdapter + SearchController

View-independent find engine walking MarkoffDocument plaintext.
Supports case/whole-word/regex/wrap flags, next/prev with wrap,
find-from-cursor via SearchAdapter::cursorSourceOffset().

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `ReplaceController` writing through `MarkoffDocument`

**Files:**
- Create: `libs/markoff-core/include/markoff/ReplaceController.h`
- Create: `libs/markoff-core/src/ReplaceController.cpp`
- Create: `libs/markoff-core/tests/tst_replace_controller.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

Extends `SearchController`. Writes through
`MarkoffDocument::replace()`. `replaceAll` wraps a single
`beginTransaction()/endTransaction()` pair so one undo reverts the lot.
Refuses to mutate when `adapter->supportsReplace()` is false and logs
a diagnostic.

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/tst_replace_controller.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>

#include <markoff/MarkoffDocument.h>
#include <markoff/ReplaceController.h>
#include <markoff/SearchAdapter.h>

using namespace Markoff;

namespace {
class StubAdapter : public SearchAdapter {
public:
    int cursorSourceOffset() const override { return 0; }
    void highlightMatches(QVector<TextSpan> s) override { highlighted = s; }
    void clearMatchHighlight() override { highlighted.clear(); }
    void scrollMatchIntoView(TextSpan) override {}
    bool supportsReplace() const override { return replace; }

    QVector<TextSpan> highlighted;
    bool replace = true;
};
}  // namespace

class TstReplaceController : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void replaceCurrentMutates() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("foo bar foo"));
        StubAdapter adapter;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("foo"));
        QCOMPARE(c.matchCount(), 2);
        c.replaceCurrent(QStringLiteral("baz"));
        QCOMPARE(doc.plainText(), QStringLiteral("baz bar foo"));
    }

    void replaceAllIsAtomic() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a a a"));
        StubAdapter adapter;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        QCOMPARE(c.replaceAll(QStringLiteral("zz")), 3);
        QCOMPARE(doc.plainText(), QStringLiteral("zz zz zz"));
        // One undo reverts all three.
        doc.textDocument()->undo();
        QCOMPARE(doc.plainText(), QStringLiteral("a a a"));
    }

    void refusesWhenAdapterRejects() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a"));
        StubAdapter adapter;
        adapter.replace = false;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        const QString before = doc.plainText();
        c.replaceCurrent(QStringLiteral("b"));
        QCOMPARE(doc.plainText(), before);
        QCOMPARE(c.replaceAll(QStringLiteral("b")), 0);
        QCOMPARE(doc.plainText(), before);
    }

    void replaceAllHandlesOverlappingGrowth() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a a"));
        StubAdapter adapter;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        QCOMPARE(c.replaceAll(QStringLiteral("aa")), 2);
        QCOMPARE(doc.plainText(), QStringLiteral("aa aa"));
    }
};

QTEST_MAIN(TstReplaceController)
#include "tst_replace_controller.moc"
```

- [ ] **Step 2: Register the test**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_replace_controller tst_replace_controller.cpp)
add_test(NAME tst_markoff_replace_controller COMMAND tst_markoff_replace_controller)
target_link_libraries(tst_markoff_replace_controller PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_markoff_replace_controller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_replace_controller
```

Expected: FAIL (`ReplaceController.h` not found).

- [ ] **Step 4: Create `libs/markoff-core/include/markoff/ReplaceController.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/SearchController.h>

namespace Markoff {

class ReplaceController : public SearchController {
    Q_OBJECT
public:
    ReplaceController(MarkoffDocument *doc, SearchAdapter *adapter,
                      QObject *parent = nullptr);

    /// Replace the current match. No-op if adapter->supportsReplace()
    /// is false or if there is no current match. Logs a diagnostic
    /// when refused.
    void replaceCurrent(const QString &with);

    /// Replace every match in one atomic undo step. Returns the
    /// number replaced. Zero if the adapter rejects replacement.
    int replaceAll(const QString &with);
};

}  // namespace Markoff
```

- [ ] **Step 5: Create `libs/markoff-core/src/ReplaceController.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/ReplaceController.h>

#include <QLoggingCategory>

#include <markoff/MarkoffDocument.h>
#include <markoff/SearchAdapter.h>

namespace {
Q_LOGGING_CATEGORY(lc, "markoff.core.replace")
}

namespace Markoff {

ReplaceController::ReplaceController(MarkoffDocument *doc,
                                     SearchAdapter *adapter,
                                     QObject *parent)
    : SearchController(doc, adapter, parent)
{}

void ReplaceController::replaceCurrent(const QString &with)
{
    if (!m_adapter->supportsReplace()) {
        qCWarning(lc) << "replaceCurrent refused: adapter is read-only";
        return;
    }
    if (m_current < 0 || m_current >= m_matches.size()) return;
    const TextSpan s = m_matches[m_current];
    m_doc->replace(s.offset, s.length, with);
    // recomputeMatches fires via contentsChanged -> SearchController.
}

int ReplaceController::replaceAll(const QString &with)
{
    if (!m_adapter->supportsReplace()) {
        qCWarning(lc) << "replaceAll refused: adapter is read-only";
        return 0;
    }
    if (m_matches.isEmpty()) return 0;

    // Snapshot spans and apply right-to-left so each replace does not
    // invalidate the offsets of spans still pending.
    QVector<TextSpan> spans = m_matches;
    const int count = spans.size();

    m_doc->beginTransaction();
    for (int i = count - 1; i >= 0; --i) {
        const TextSpan s = spans[i];
        m_doc->replace(s.offset, s.length, with);
    }
    m_doc->endTransaction();
    return count;
}

}  // namespace Markoff
```

- [ ] **Step 6: Add the source to `libs/markoff-core/CMakeLists.txt`**

Update `add_library(markoff_core STATIC ...)` with the new .cpp and .h.

- [ ] **Step 7: Run test to verify it passes**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_replace_controller && \
  QT_QPA_PLATFORM=offscreen /home/clinton/dev/Markoff/build-dev/bin/tst_markoff_replace_controller
```

Expected: all four slots PASS.

- [ ] **Step 8: Commit**

```bash
cd /home/clinton/dev/Markoff
git add libs/markoff-core/
git commit -m "markoff-core: add ReplaceController with atomic replaceAll

Writes through MarkoffDocument::replace inside a single
begin/endTransaction pair so one undo reverts replaceAll.
Honours adapter->supportsReplace(); logs a diagnostic and no-ops
when false (e.g. ReadingView).

Retires the cross-item-undo TODO flagged in markoff-live.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Move `SearchBar` from `libs/markoff/` into core

**Files:**
- Move: `libs/markoff/include/markoff/SearchBar.h` → `libs/markoff-core/include/markoff/SearchBar.h`
- Move: `libs/markoff/src/SearchBar.cpp` → `libs/markoff-core/src/SearchBar.cpp`
- Move: `libs/markoff/tests/tst_search_bar.cpp` → `libs/markoff-core/tests/tst_search_bar.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`
- Modify: `libs/markoff/CMakeLists.txt` (drop SearchBar sources; add `markoff_core` dep)
- Modify: `libs/markoff/tests/CMakeLists.txt` (drop tst_search_bar registration)

`SearchBar` is already view-agnostic in `libs/markoff/` — it just
emits text-entered + next/prev/flags. It becomes the default UI in
core, driven by `SearchController`.

- [ ] **Step 1: Inspect SearchBar before moving**

```bash
head -60 /home/clinton/dev/Markoff/libs/markoff/include/markoff/SearchBar.h
grep -n "Markoff::" /home/clinton/dev/Markoff/libs/markoff/src/SearchBar.cpp | head
```

Confirm `SearchBar` lives in `Markoff::` and has no references to
`Markoff::Editor` or other markoff-live internals. If it does, note
them — they need to be severed before the move (likely just signal/
slot wiring done by the Editor; that wiring moves to the Editor's
side). This should already be the case per the existing find-replace
spec.

- [ ] **Step 2: `git mv` the files**

```bash
cd /home/clinton/dev/Markoff
git mv libs/markoff/include/markoff/SearchBar.h \
       libs/markoff-core/include/markoff/SearchBar.h
git mv libs/markoff/src/SearchBar.cpp \
       libs/markoff-core/src/SearchBar.cpp
git mv libs/markoff/tests/tst_search_bar.cpp \
       libs/markoff-core/tests/tst_search_bar.cpp
```

- [ ] **Step 3: Update `libs/markoff-core/CMakeLists.txt`**

Add `src/SearchBar.cpp` and `include/markoff/SearchBar.h` to the
`add_library(markoff_core STATIC ...)` block.

- [ ] **Step 4: Register the moved test**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_search_bar tst_search_bar.cpp)
add_test(NAME tst_markoff_search_bar COMMAND tst_markoff_search_bar)
target_link_libraries(tst_markoff_search_bar PRIVATE Qt6::Test Qt6::Widgets markoff_core)
set_tests_properties(tst_markoff_search_bar PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 5: Drop SearchBar from `libs/markoff/CMakeLists.txt`**

Remove the two lines that list `include/markoff/SearchBar.h` and
`src/SearchBar.cpp` from the `add_library(markoff STATIC ...)` block.
Ensure the `target_link_libraries(markoff PUBLIC ...)` list contains
`markoff_core` (add it if missing) so `markoff` can still see the
`SearchBar` header for its existing includes.

Also, if the `markoff` target does not already depend on
`markoff_core`, add it:

```cmake
target_link_libraries(markoff PUBLIC
    ...
    markoff_core
)
```

- [ ] **Step 6: Drop SearchBar from `libs/markoff/tests/CMakeLists.txt`**

Remove the five-line block registering `tst_markoff_search_bar`.

- [ ] **Step 7: Build all + run the moved test**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev && \
  cd /home/clinton/dev/Markoff/build-dev && \
  ctest -R markoff_search_bar --output-on-failure
```

Expected: `tst_markoff_search_bar` passes from its new home. All
other `markoff*` tests also still pass (run a broader ctest to
confirm).

- [ ] **Step 8: Commit**

```bash
cd /home/clinton/dev/Markoff
git add libs/markoff-core/ libs/markoff/
git commit -m "Move SearchBar from markoff-live into markoff-core

SearchBar is view-agnostic — it emits text/next/prev/flags signals
and carries no markoff-live internals. Promoting it to core makes
it the default find UI for all three view libraries.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: `MarkdownView` abstract base + header-only compile test

**Files:**
- Create: `libs/markoff-core/include/markoff/MarkdownView.h`
- Create: `libs/markoff-core/tests/tst_markdown_view_compile.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

The polymorphic contract itself. Abstract. No implementation. The
test is a tiny concrete subclass that overrides every pure virtual,
constructs it, and casts back to `MarkdownView *` — which proves the
header is consistent, all virtuals are reachable, and capability
defaults work.

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/tst_markdown_view_compile.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/MarkdownView.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/SearchAdapter.h>

using namespace Markoff;

namespace {
class DummyAdapter : public SearchAdapter {
public:
    int cursorSourceOffset() const override { return 0; }
    void highlightMatches(QVector<TextSpan>) override {}
    void clearMatchHighlight() override {}
    void scrollMatchIntoView(TextSpan) override {}
};

class MinimalView : public MarkdownView {
    Q_OBJECT
public:
    void setDocument(MarkoffDocument *d) override { m_doc = d; }
    MarkoffDocument *document() const override { return m_doc; }
    void setViewTheme(const Theme &) override {}
    void setViewResourceProvider(ResourceProvider *) override {}
    void setViewLinkResolver(LinkResolver *) override {}
    float scrollPosition() const override { return 0.f; }
    void setScrollPosition(float) override {}
    void zoomIn() override {}
    void zoomOut() override {}
    void resetZoom() override {}
    QJsonObject ephemeralState() const override { return {}; }
    void setEphemeralState(const QJsonObject &) override {}
    SearchAdapter *searchAdapter() override { return &m_adapter; }
private:
    MarkoffDocument *m_doc = nullptr;
    DummyAdapter m_adapter;
};
}  // namespace

class TstMarkdownViewCompile : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void constructsAndCasts() {
        MinimalView v;
        MarkdownView *base = &v;
        MarkoffDocument d;
        base->setDocument(&d);
        QCOMPARE(base->document(), &d);
        QVERIFY(!base->hasCursor());
        QVERIFY(!base->hasEditing());
        QVERIFY(!base->hasFold());
        QVERIFY(base->isReadOnly());            // !hasEditing() → true
        QVERIFY(!base->setReadOnly(false));     // capability-denied default
        QVERIFY(base->searchAdapter() != nullptr);
    }
};

QTEST_MAIN(TstMarkdownViewCompile)
#include "tst_markdown_view_compile.moc"
```

- [ ] **Step 2: Register the test**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_markdown_view tst_markdown_view_compile.cpp)
add_test(NAME tst_markoff_markdown_view COMMAND tst_markoff_markdown_view)
target_link_libraries(tst_markoff_markdown_view PRIVATE Qt6::Test Qt6::Widgets markoff_core)
set_tests_properties(tst_markoff_markdown_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_markdown_view
```

Expected: FAIL (`MarkdownView.h` not found).

- [ ] **Step 4: Create `libs/markoff-core/include/markoff/MarkdownView.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QUrl>
#include <QVector>
#include <QWidget>

#include <markoff/CursorPos.h>
#include <markoff/FoldSpec.h>

namespace Markoff {

class MarkoffDocument;
class SearchAdapter;
class Theme;
class ResourceProvider;
class LinkResolver;

/// Abstract polymorphic base for the three Markoff view widgets
/// (Live Preview, Source, Reading). Host applications hold a
/// MarkdownView* and dispatch through the contract, which keeps
/// mode-swap logic (QStackedWidget + flush/restore) at the host
/// layer rather than inside this library.
///
/// See docs/specs/2026-04-20-tri-view-unified-api-design.md.
class MarkdownView : public QWidget {
    Q_OBJECT
public:
    explicit MarkdownView(QWidget *parent = nullptr) : QWidget(parent) {}

    // Content lives on MarkoffDocument; views attach to it.
    virtual void setDocument(MarkoffDocument *doc) = 0;
    virtual MarkoffDocument *document() const = 0;

    // Appearance / resources (types currently stubbed; real definitions
    // arrive in Phase C when the three Theme/ResourceProvider/LinkResolver
    // implementations consolidate). Named setViewTheme/Resource/Link
    // rather than setTheme/… so they don't collide with leaf widgets'
    // existing same-named setters that take differently-typed concrete
    // arguments.
    virtual void setViewTheme(const Theme &theme) = 0;
    virtual void setViewResourceProvider(ResourceProvider *rp) = 0;
    virtual void setViewLinkResolver(LinkResolver *lr) = 0;

    // Scroll — visual-line float, every mode.
    virtual float scrollPosition() const = 0;
    virtual void setScrollPosition(float visualLine) = 0;

    // Zoom — every mode.
    virtual void zoomIn() = 0;
    virtual void zoomOut() = 0;
    virtual void resetZoom() = 0;

    // Ephemeral state — opaque per-view JSON blob.
    virtual QJsonObject ephemeralState() const = 0;
    virtual void setEphemeralState(const QJsonObject &) = 0;

    // Search — every view supplies one so a SearchController can
    // drive it without knowing the view internals.
    virtual SearchAdapter *searchAdapter() = 0;

    // Capability probes — callers dispatch through these instead of
    // RTTI. Defaults return false; views that support the capability
    // override.
    virtual bool hasCursor() const { return false; }
    virtual bool hasEditing() const { return false; }
    virtual bool hasFold() const { return false; }

    // Optional — default no-ops / capability-denied returns.
    virtual CursorPos cursorPosition() const { return {}; }
    virtual bool setCursorPosition(CursorPos) { return false; }
    virtual bool setReadOnly(bool) { return false; }
    virtual bool isReadOnly() const { return !hasEditing(); }
    virtual QVector<FoldSpec> foldedHeadings() const { return {}; }
    virtual void setFoldedHeadings(const QVector<FoldSpec> &) {}

Q_SIGNALS:
    void scrollPositionChanged(float visualLine);
    void cursorPositionChanged(CursorPos pos);  // emitted only when hasCursor()
    void linkActivated(const QUrl &url);
};

// Theme / ResourceProvider / LinkResolver are forward-declared only.
// Real definitions live in markoff-live today (Theme, ResourceProvider,
// LinkRenderer). Consumers who call setViewTheme() etc. include the
// real headers from wherever they currently live; Phase C consolidates
// them into markoff-core. MarkdownView only needs reference types.

}  // namespace Markoff
```

- [ ] **Step 5: Register `MarkdownView.h` in `libs/markoff-core/CMakeLists.txt`**

Add `include/markoff/MarkdownView.h` to the `add_library(markoff_core STATIC ...)` block.

- [ ] **Step 6: Run test to verify it passes**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_markdown_view && \
  QT_QPA_PLATFORM=offscreen /home/clinton/dev/Markoff/build-dev/bin/tst_markoff_markdown_view
```

Expected: `constructsAndCasts` PASS.

- [ ] **Step 7: Commit**

```bash
cd /home/clinton/dev/Markoff
git add libs/markoff-core/
git commit -m "markoff-core: add MarkdownView abstract polymorphic base

Contract for the three view widgets — content via MarkoffDocument,
shared Theme/ResourceProvider/LinkResolver (stub types for now;
real definitions consolidate in Phase C), scroll/zoom/ephemeral
state, SearchAdapter hook, capability probes.

Compile-smoke test via a minimal in-test concrete subclass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Rename `libs/markoff/` → `libs/markoff-live/`

**Files:**
- Rename: `libs/markoff/` → `libs/markoff-live/`
- Modify: `libs/markoff-live/CMakeLists.txt` (project + target name)
- Modify: `libs/markoff-live/tests/CMakeLists.txt` (link-library references if needed)
- Modify: `CMakeLists.txt` (top level — `add_subdirectory`)
- Modify: `tests/markoff/CMakeLists.txt` (integration tests reference the target)
- Modify: `.clangd` (build-dir path — optional; Markoff convention is `build-dev/`)

Directory and target rename only. The library's public include path
stays `<markoff/...>` so consumers' includes don't change. The
target alias `Markoff::Markoff` becomes `Markoff::Live` (and the
short target name becomes `markoff_live`).

The widget class `Markoff::Editor` keeps its name for now; it gets
inherited from `MarkdownView` in Task 10.

- [ ] **Step 1: `git mv` the directory**

```bash
cd /home/clinton/dev/Markoff
git mv libs/markoff libs/markoff-live
```

- [ ] **Step 2: Update `libs/markoff-live/CMakeLists.txt`**

Change:

```cmake
project(markoff VERSION 0.1.0 LANGUAGES C CXX)
```

to:

```cmake
project(markoff_live VERSION 0.1.0 LANGUAGES C CXX)
```

Change `add_library(markoff STATIC ...)` to
`add_library(markoff_live STATIC ...)`.

Change `set_target_properties(markoff PROPERTIES ...)` to reference
`markoff_live`.

Change `add_library(Markoff::Markoff ALIAS markoff)` to
`add_library(Markoff::Live ALIAS markoff_live)`.

Update every `target_link_libraries(markoff ...)`,
`target_include_directories(markoff ...)`,
`target_compile_definitions(markoff ...)`, and any other
target-oriented call to reference `markoff_live`.

- [ ] **Step 3: Update `libs/markoff-live/tests/CMakeLists.txt`**

Replace every `target_link_libraries(tst_markoff_* PRIVATE ... markoff)`
with `target_link_libraries(tst_markoff_* PRIVATE ... markoff_live)`.
Use a global rewrite:

```bash
sed -i 's/\bmarkoff\b$/markoff_live/; s/\bmarkoff)/markoff_live)/g; s/\bmarkoff /markoff_live /g' \
  /home/clinton/dev/Markoff/libs/markoff-live/tests/CMakeLists.txt
```

Then visually inspect the diff and fix any over-matches by hand. The
substitutions affect the short target name only; alias usages
(`Markoff::Markoff`) stay as-is until Step 5.

- [ ] **Step 4: Update top-level `CMakeLists.txt`**

In `/home/clinton/dev/Markoff/CMakeLists.txt`, change:

```cmake
add_subdirectory(libs/markoff)
```

to:

```cmake
add_subdirectory(libs/markoff-live)
```

Also update the library comment block at the top of the file to say
`libs/markoff-live (Markoff::Live — live-preview editor)`.

- [ ] **Step 5: Update `tests/markoff/CMakeLists.txt`**

The integration tests at `tests/markoff/CMakeLists.txt` reference the
`Markoff::Markoff` alias (or `markoff` directly). Replace every
occurrence:

```bash
sed -i 's/Markoff::Markoff/Markoff::Live/g; s/\bmarkoff\b/markoff_live/g' \
  /home/clinton/dev/Markoff/tests/markoff/CMakeLists.txt
```

Review the diff and fix any over-matches. (If the test dir is very
small, do this edit by hand in the editor instead.)

- [ ] **Step 6: Update `.clangd` compilation-database path**

`.clangd` should point at `build-dev` already, not `build`. Open
`/home/clinton/dev/Markoff/.clangd` and confirm its
`CompilationDatabase:` line reads `build-dev`. If not, update it.
Then recreate the symlink:

```bash
cd /home/clinton/dev/Markoff
rm -f compile_commands.json
ln -sf build-dev/compile_commands.json compile_commands.json
```

- [ ] **Step 7: Clean the stale build and reconfigure**

The previous CMake cache has the old target names in it. Drop it:

```bash
rm -rf /home/clinton/dev/Markoff/build-dev
cmake -S /home/clinton/dev/Markoff -B /home/clinton/dev/Markoff/build-dev \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/Markoff/build-dev
```

Expected: clean build. The library artifact is now
`build-dev/libs/markoff-live/libmarkoff_live.a` (or the platform
equivalent).

- [ ] **Step 8: Run every test to confirm nothing regressed**

```bash
cd /home/clinton/dev/Markoff/build-dev && ctest --output-on-failure
```

Expected: all pre-existing markoff tests pass, plus the core tests
added in Tasks 2–8.

- [ ] **Step 9: Commit**

```bash
cd /home/clinton/dev/Markoff
git add -A
git commit -m "Rename libs/markoff/ to libs/markoff-live/

Part of the tri-view absorption: markoff-live is the live-preview
leaf widget, peer to markoff-source and markoff-reading (coming
next) and markoff-core (the shared primitives).

Target renamed markoff → markoff_live; alias Markoff::Markoff →
Markoff::Live. Public include path <markoff/...> is unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: Live-preview `Editor` inherits `MarkdownView`

**Files:**
- Modify: `libs/markoff-live/include/markoff/Editor.h`
- Modify: `libs/markoff-live/src/Editor.cpp`
- Create: `libs/markoff-live/src/LiveSearchAdapter.h`
- Create: `libs/markoff-live/src/LiveSearchAdapter.cpp`
- Create: `libs/markoff-live/tests/tst_editor_markdown_view.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

`Markoff::Editor` becomes `MarkdownView`-derived. Phase A wiring is
forwarding-only: `setDocument` stores the pointer; content still
lives in the editor's existing text pipeline. Virtuals map to the
Editor's existing scroll/zoom/cursor/fold APIs. Capability probes
return `true` for cursor, editing, and fold. A small
`LiveSearchAdapter` bridges the Editor's existing search-highlight
code to the new `SearchAdapter` interface.

- [ ] **Step 1: Write the failing test**

`libs/markoff-live/tests/tst_editor_markdown_view.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>

#include <markoff/Editor.h>
#include <markoff/MarkdownView.h>
#include <markoff/MarkoffDocument.h>

using namespace Markoff;

class TstEditorMarkdownView : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void editorIsAMarkdownView() {
        Editor ed;
        MarkdownView *v = &ed;        // upcast compiles → inheritance holds
        QVERIFY(v != nullptr);
    }

    void capabilityProbes() {
        Editor ed;
        MarkdownView *v = &ed;
        QVERIFY(v->hasCursor());
        QVERIFY(v->hasEditing());
        QVERIFY(v->hasFold());
        QVERIFY(!v->isReadOnly());
    }

    void setDocumentStoresPointer() {
        Editor ed;
        MarkdownView *v = &ed;
        MarkoffDocument doc;
        v->setDocument(&doc);
        QCOMPARE(v->document(), &doc);
    }

    void scrollRoundTrips() {
        Editor ed;
        MarkdownView *v = &ed;
        ed.setPlainText(QStringLiteral("a\nb\nc\n"));
        v->setScrollPosition(1.0f);
        QVERIFY(v->scrollPosition() >= 0.0f);
    }

    void searchAdapterIsNonNull() {
        Editor ed;
        MarkdownView *v = &ed;
        QVERIFY(v->searchAdapter() != nullptr);
    }
};

QTEST_MAIN(TstEditorMarkdownView)
#include "tst_editor_markdown_view.moc"
```

- [ ] **Step 2: Register the test**

Append to `libs/markoff-live/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_editor_markdown_view tst_editor_markdown_view.cpp)
add_test(NAME tst_markoff_editor_markdown_view COMMAND tst_markoff_editor_markdown_view)
target_link_libraries(tst_markoff_editor_markdown_view PRIVATE Qt6::Test Qt6::Widgets markoff_live)
target_include_directories(tst_markoff_editor_markdown_view PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_editor_markdown_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_editor_markdown_view
```

Expected: FAIL — `Editor` is a `QGraphicsView`, not a `MarkdownView`;
the upcast in `editorIsAMarkdownView` won't compile.

- [ ] **Step 4: Make `Editor` inherit `MarkdownView`**

In `libs/markoff-live/include/markoff/Editor.h`, change the class
declaration from:

```cpp
class Editor : public QGraphicsView {
```

to:

```cpp
class Editor : public MarkdownView {
```

Add near the top:

```cpp
#include <markoff/MarkdownView.h>
```

`MarkdownView` inherits from `QWidget`, not `QGraphicsView`. To keep
the existing `QGraphicsView` behavior, the Editor now composes a
private `QGraphicsView *m_view` child widget rather than inheriting
QGraphicsView. Apply the minimal layout change:

1. Remove the `QGraphicsView` base class.
2. Add a `QGraphicsView *m_view` member (private).
3. In the constructor, create `m_view`, set its viewport options
   identically to what the class previously set on itself, and put
   `m_view` into a single-child `QVBoxLayout(this)` with zero margins
   so it fills the widget.
4. Replace every `this->` call that was a `QGraphicsView` method
   (e.g. `setScene()`, `viewport()`, `horizontalScrollBar()`) with
   `m_view->` calls.

This is a mechanical refactor across `Editor.cpp`. Grep first so
you see the scope:

```bash
grep -n 'setScene\|viewport\|horizontalScrollBar\|verticalScrollBar\|setRenderHint\|setSceneRect' \
  /home/clinton/dev/Markoff/libs/markoff-live/src/Editor.cpp | wc -l
```

Expected: ~40-80 occurrences. Each one becomes `m_view->...`.

- [ ] **Step 5: Declare the MarkdownView overrides in `Editor.h`**

**Signature reconciliation required:** the existing
`Editor::setReadOnly(bool)` returns `void`. `MarkdownView::setReadOnly`
returns `bool` (capability feedback). Change the existing declaration
AND implementation to return `bool` (and `return true;` at the end of
the body). This is source-compatible — existing callers that ignore
the return value keep compiling.

In the `public:` section of `Editor`, add the overrides:

```cpp
// MarkdownView — Phase A forwarding implementations.
void setDocument(MarkoffDocument *doc) override;
MarkoffDocument *document() const override;
void setViewTheme(const Theme &theme) override;
void setViewResourceProvider(ResourceProvider *rp) override;
void setViewLinkResolver(LinkResolver *lr) override;
float scrollPosition() const override;
void setScrollPosition(float visualLine) override;
void zoomIn() override;
void zoomOut() override;
void resetZoom() override;
QJsonObject ephemeralState() const override;
void setEphemeralState(const QJsonObject &) override;
SearchAdapter *searchAdapter() override;
bool hasCursor() const override { return true; }
bool hasEditing() const override { return true; }
bool hasFold() const override { return true; }
CursorPos cursorPosition() const override;
bool setCursorPosition(CursorPos) override;
bool setReadOnly(bool) override;
bool isReadOnly() const override;
QVector<FoldSpec> foldedHeadings() const override;
void setFoldedHeadings(const QVector<FoldSpec> &) override;
```

Add private members:

```cpp
MarkoffDocument *m_markoffDoc = nullptr;
std::unique_ptr<LiveSearchAdapter> m_searchAdapter;
```

- [ ] **Step 6: Create `libs/markoff-live/src/LiveSearchAdapter.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/SearchAdapter.h>

namespace Markoff {

class Editor;

/// Bridges the live-preview Editor's existing search-highlight
/// machinery to the SearchAdapter interface owned by SearchController.
class LiveSearchAdapter : public SearchAdapter {
public:
    explicit LiveSearchAdapter(Editor *owner);

    int cursorSourceOffset() const override;
    void highlightMatches(QVector<TextSpan>) override;
    void clearMatchHighlight() override;
    void scrollMatchIntoView(TextSpan) override;

private:
    Editor *m_editor;
};

}  // namespace Markoff
```

- [ ] **Step 7: Create `libs/markoff-live/src/LiveSearchAdapter.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveSearchAdapter.h"

#include <markoff/Editor.h>

namespace Markoff {

LiveSearchAdapter::LiveSearchAdapter(Editor *owner) : m_editor(owner) {}

int LiveSearchAdapter::cursorSourceOffset() const
{
    // Phase A: approximate by asking the Editor for its current
    // source-line offset. Full source-offset fidelity lands in
    // Phase C when block items expose their source ranges through
    // the shared document.
    return m_editor ? m_editor->sourceOffsetAtCursor() : 0;
}

void LiveSearchAdapter::highlightMatches(QVector<TextSpan> spans)
{
    if (!m_editor) return;
    m_editor->highlightSearchSpans(spans);
}

void LiveSearchAdapter::clearMatchHighlight()
{
    if (!m_editor) return;
    m_editor->clearSearchHighlights();
}

void LiveSearchAdapter::scrollMatchIntoView(TextSpan span)
{
    if (!m_editor) return;
    m_editor->scrollSourceSpanIntoView(span);
}

}  // namespace Markoff
```

If `Editor` doesn't already expose `sourceOffsetAtCursor()`,
`highlightSearchSpans()`, `clearSearchHighlights()`, and
`scrollSourceSpanIntoView()`, add thin forwarders in `Editor.cpp`
that wrap existing internals (the existing find/replace pipeline has
equivalent internal methods — look at `Editor::highlightAllMatches`,
`Editor::textItemsInSearchOrder`, and `Editor::goToLine`). Use
exact existing method names where they match; otherwise add a new
public method that calls the existing code.

- [ ] **Step 8: Implement the `MarkdownView` overrides in `Editor.cpp`**

Add a `#include <markoff/MarkdownView.h>`, `#include <markoff/MarkoffDocument.h>`,
`#include "LiveSearchAdapter.h"`, and `#include <markoff/Theme.h>` at the top.
In the constructor, construct the adapter:

```cpp
m_searchAdapter = std::make_unique<LiveSearchAdapter>(this);
```

Implement each override as forwarding to existing APIs:

```cpp
void Editor::setDocument(MarkoffDocument *doc) { m_markoffDoc = doc; }
MarkoffDocument *Editor::document() const { return m_markoffDoc; }

void Editor::setViewTheme(const Markoff::Theme &) {}
void Editor::setViewResourceProvider(Markoff::ResourceProvider *) {}
void Editor::setViewLinkResolver(Markoff::LinkResolver *) {}

float Editor::scrollPosition() const
{
    // Existing pixel scroll → visual-line float approximation in Phase A.
    // ScrollPosition.h in core adds a precise conversion in Phase C.
    const int px = m_view->verticalScrollBar()->value();
    return static_cast<float>(px) / lineHeightPx();
}

void Editor::setScrollPosition(float visualLine)
{
    m_view->verticalScrollBar()->setValue(
        static_cast<int>(visualLine * lineHeightPx()));
}

void Editor::zoomIn() { setFontSize(m_fontSize + 1); }
void Editor::zoomOut() { setFontSize(m_fontSize - 1); }
// resetZoom is already implemented in master (4e68c25).

QJsonObject Editor::ephemeralState() const
{
    QJsonObject j;
    j.insert(QStringLiteral("scroll"), scrollPosition());
    // Extend with per-block scroll offsets in Phase C.
    return j;
}

void Editor::setEphemeralState(const QJsonObject &j)
{
    setScrollPosition(static_cast<float>(
        j.value(QStringLiteral("scroll")).toDouble(0.0)));
}

Markoff::SearchAdapter *Editor::searchAdapter()
{
    return m_searchAdapter.get();
}

Markoff::CursorPos Editor::cursorPosition() const
{
    return {currentLine(), currentColumn()};
}

bool Editor::setCursorPosition(Markoff::CursorPos p)
{
    goToLine(p.line);  // existing API; column is best-effort in Phase A
    return true;
}

bool Editor::setReadOnly(bool ro)
{
    // Route to the existing QGraphicsView-flavored read-only setter.
    // Full implementation already present in master; just wire it.
    m_readOnly = ro;
    // Propagate to text items — existing code does this elsewhere.
    propagateReadOnlyToItems();
    return true;
}

bool Editor::isReadOnly() const { return m_readOnly; }

QVector<Markoff::FoldSpec> Editor::foldedHeadings() const
{
    QVector<Markoff::FoldSpec> out;
    for (const auto &f : currentFoldedHeadings()) {
        out.append({f.line, f.level});
    }
    return out;
}

void Editor::setFoldedHeadings(const QVector<Markoff::FoldSpec> &v)
{
    QVector<LegacyFoldSpec> legacy;
    for (const auto &f : v) legacy.append({f.line, f.level});
    applyFoldedHeadings(legacy);
}
```

Where `currentLine()`, `currentColumn()`, `goToLine()`,
`propagateReadOnlyToItems()`, `currentFoldedHeadings()`,
`applyFoldedHeadings()`, `lineHeightPx()`, `m_readOnly`,
`LegacyFoldSpec` are either existing internals or thin helpers you
add in `Editor.cpp`/`Editor.h` that wrap existing code. Grep first
for the closest match and use real existing names — these sketched
names are a shape, not a literal contract.

- [ ] **Step 9: Wire new sources into `libs/markoff-live/CMakeLists.txt`**

Add `src/LiveSearchAdapter.h` and `src/LiveSearchAdapter.cpp` to the
`add_library(markoff_live STATIC ...)` block. Confirm the library
already declares `markoff_core` in its `target_link_libraries PUBLIC`
(added in Task 7 Step 5).

- [ ] **Step 10: Run the new test**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_editor_markdown_view && \
  QT_QPA_PLATFORM=offscreen /home/clinton/dev/Markoff/build-dev/bin/tst_markoff_editor_markdown_view
```

Expected: all five slots PASS.

- [ ] **Step 11: Run the full test suite to confirm no regression**

```bash
cd /home/clinton/dev/Markoff/build-dev && ctest --output-on-failure
```

Expected: every previously-passing markoff-live test still passes.
Any QGraphicsView-behaviour tests that access the editor's viewport
directly via Qt's metaobject or `qobject_cast<QGraphicsView*>` need
updating to `editor->viewport()` on the composed child — the test
CLAUDE note ("Tests define expected behavior — when a test fails,
fix the code, not the test") applies only when the test describes
user-observable behavior. A test that probed "Editor IS-A
QGraphicsView" was probing the previous internal representation,
not behavior, and may need rewriting to match the new composition.

- [ ] **Step 12: Commit**

```bash
cd /home/clinton/dev/Markoff
git add libs/markoff-live/ libs/markoff-core/
git commit -m "markoff-live: Editor inherits MarkdownView (forwarding)

Editor is now a MarkdownView subclass with capability probes
(hasCursor/hasEditing/hasFold = true), a LiveSearchAdapter bridging
the existing search highlight machinery, and forwarding overrides
for scroll/zoom/ephemeral-state/cursor/fold/readonly.

Internally Editor composes its QGraphicsView child rather than
inheriting from it — MarkdownView's base is QWidget.

Phase A forwarding only: setDocument stores the pointer; the Editor
remains content-authoritative until Phase C.

Renamed MarkdownView::setTheme/setResourceProvider/setLinkResolver
to setViewTheme/setViewResourceProvider/setViewLinkResolver to
avoid name clash with leaf widgets' existing same-named setters.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: Absorb `qutepart-corbomite` as `libs/markoff-source/`

**Files:**
- New directory: `libs/markoff-source/` (populated via git subtree)
- Modify: `CMakeLists.txt` (top level)

Pull the directory's history out of Corbomite via `git subtree split`,
then subtree-add into Markoff preserving commit history. Rename the
target `qutepart_corbomite` → `markoff_source`, alias → `Markoff::Source`.
Namespace the public API from `Corbomite::SourceEditor` → `Markoff::Source::SourceEditor`.
The vendored `Qutepart::` code stays untouched.

- [ ] **Step 1: Split the subdirectory history out of Corbomite**

```bash
cd /home/clinton/dev/Corbomite
git subtree split --prefix libs/qutepart-corbomite -b markoff-source-split
```

Expected: new branch `markoff-source-split` whose history is the
`libs/qutepart-corbomite/` subtree rooted at the repo root.

- [ ] **Step 2: Add the subtree to Markoff**

```bash
cd /home/clinton/dev/Markoff
git remote add --no-tags corbomite-src /home/clinton/dev/Corbomite
git fetch corbomite-src markoff-source-split
git subtree add --prefix libs/markoff-source corbomite-src/markoff-source-split
git remote remove corbomite-src
```

Expected: `libs/markoff-source/` now exists in Markoff with its full
commit history merged in. Git log at the repo root shows a merge
commit plus every commit that touched `libs/qutepart-corbomite/` in
Corbomite.

- [ ] **Step 3: Rename CMake target**

In `/home/clinton/dev/Markoff/libs/markoff-source/CMakeLists.txt`,
rename the project, library, and alias:

```bash
sed -i \
  -e 's/project(qutepart_corbomite/project(markoff_source/' \
  -e 's/add_library(qutepart_corbomite/add_library(markoff_source/' \
  -e 's/add_library(Qutepart::Corbomite ALIAS qutepart_corbomite)/add_library(Markoff::Source ALIAS markoff_source)/' \
  /home/clinton/dev/Markoff/libs/markoff-source/CMakeLists.txt
```

If the actual target / alias names differ, grep first and adjust the
sed invocations to match literally. Same for any other references in
the CMakeLists (set_target_properties, target_link_libraries).

- [ ] **Step 4: Namespace the public API**

Move the public header:

```bash
cd /home/clinton/dev/Markoff/libs/markoff-source
mkdir -p include/markoff/source
if [ -f src/SourceEditor.h ]; then
  git mv src/SourceEditor.h include/markoff/source/SourceEditor.h
fi
if [ -f include/corbomite/SourceEditor.h ]; then
  git mv include/corbomite/SourceEditor.h include/markoff/source/SourceEditor.h
fi
# Remove now-empty include/corbomite if it existed.
rmdir include/corbomite 2>/dev/null || true
```

Update the namespace in the SourceEditor header and source:

```bash
cd /home/clinton/dev/Markoff/libs/markoff-source
grep -rl 'namespace Corbomite' include src | while read -r f; do
  sed -i 's/namespace Corbomite {/namespace Markoff::Source {/' "$f"
  sed -i 's|^}  // namespace Corbomite|}  // namespace Markoff::Source|' "$f"
done
# Update existing consumers within the lib (tests, demos).
grep -rl 'Corbomite::SourceEditor' . | while read -r f; do
  sed -i 's/Corbomite::SourceEditor/Markoff::Source::SourceEditor/g' "$f"
done
```

Review the diff. Any file that was meant to *keep* its `Corbomite::`
namespace (e.g. a stray vendored shim) stays unchanged — revert by
hand if necessary.

- [ ] **Step 5: Update target_include_directories**

In `libs/markoff-source/CMakeLists.txt`, the PUBLIC include directory
was `include/` (corbomite layout). Confirm it's still `include/` —
the new header lives at `include/markoff/source/SourceEditor.h`, so
consumers include `<markoff/source/SourceEditor.h>`. No change needed
unless the CMakeLists referenced `include/corbomite` directly.

- [ ] **Step 6: Wire into the top-level CMakeLists**

In `/home/clinton/dev/Markoff/CMakeLists.txt`, add:

```cmake
add_subdirectory(libs/markoff-source)
```

Place it after `add_subdirectory(libs/markoff-core)` and before
`add_subdirectory(libs/markoff-live)` (or anywhere consistent; the
subdirs don't depend on each other yet).

Update the top-of-file comment block to list `libs/markoff-source
(Markoff::Source — plain-text markdown editor based on qutepart-cpp)`.

- [ ] **Step 7: Clean build, reconfigure, build**

```bash
rm -rf /home/clinton/dev/Markoff/build-dev
cmake -S /home/clinton/dev/Markoff -B /home/clinton/dev/Markoff/build-dev \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/Markoff/build-dev
```

Expected: clean build. `libmarkoff_source.a` exists.

- [ ] **Step 8: Run markoff-source's own tests**

```bash
cd /home/clinton/dev/Markoff/build-dev && ctest -R markoff_source --output-on-failure
```

Expected: all markoff-source tests (formerly qutepart-corbomite tests)
pass. If any test references `Corbomite::SourceEditor` and was missed
by Step 4's sed, fix the reference and rebuild.

- [ ] **Step 9: Commit**

The `git subtree add` in Step 2 already produced its own merge
commit. This task's follow-up changes go in a separate commit:

```bash
cd /home/clinton/dev/Markoff
git add -A
git commit -m "Absorb libs/markoff-source from Corbomite

Rename CMake target qutepart_corbomite → markoff_source; alias
Qutepart::Corbomite → Markoff::Source. Public header namespace
Corbomite::SourceEditor → Markoff::Source::SourceEditor. Public
header path <markoff/source/SourceEditor.h>. Vendored Qutepart::
code untouched.

Wired into top-level CMakeLists via add_subdirectory. History was
preserved via git subtree from Corbomite's libs/qutepart-corbomite/
subtree; see the merge commit immediately below this one.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 12: `Markoff::Source::SourceEditor` inherits `MarkdownView`

**Files:**
- Modify: `libs/markoff-source/include/markoff/source/SourceEditor.h`
- Modify: `libs/markoff-source/src/SourceEditor.cpp`
- Create: `libs/markoff-source/src/SourceSearchAdapter.h`
- Create: `libs/markoff-source/src/SourceSearchAdapter.cpp`
- Create: `libs/markoff-source/tests/tst_source_markdown_view.cpp`
- Modify: `libs/markoff-source/CMakeLists.txt`
- Modify: `libs/markoff-source/tests/CMakeLists.txt`

Mirror of Task 10 for the Source widget. Capability probes:
hasCursor = true, hasEditing = true, hasFold = true. The search
adapter wraps Qutepart's `QPlainTextEdit::find` or the equivalent
existing search path.

- [ ] **Step 1: Write the failing test**

`libs/markoff-source/tests/tst_source_markdown_view.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>

#include <markoff/MarkdownView.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/source/SourceEditor.h>

using namespace Markoff;

class TstSourceMarkdownView : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void sourceIsAMarkdownView() {
        Source::SourceEditor ed;
        MarkdownView *v = &ed;
        QVERIFY(v != nullptr);
    }

    void capabilityProbes() {
        Source::SourceEditor ed;
        MarkdownView *v = &ed;
        QVERIFY(v->hasCursor());
        QVERIFY(v->hasEditing());
        QVERIFY(v->hasFold());
        QVERIFY(!v->isReadOnly());
    }

    void setDocumentStoresPointer() {
        Source::SourceEditor ed;
        MarkdownView *v = &ed;
        MarkoffDocument doc;
        v->setDocument(&doc);
        QCOMPARE(v->document(), &doc);
    }

    void searchAdapterIsNonNull() {
        Source::SourceEditor ed;
        MarkdownView *v = &ed;
        QVERIFY(v->searchAdapter() != nullptr);
    }

    void setReadOnlyRoundTrips() {
        Source::SourceEditor ed;
        MarkdownView *v = &ed;
        QVERIFY(v->setReadOnly(true));
        QVERIFY(v->isReadOnly());
        QVERIFY(v->setReadOnly(false));
        QVERIFY(!v->isReadOnly());
    }
};

QTEST_MAIN(TstSourceMarkdownView)
#include "tst_source_markdown_view.moc"
```

- [ ] **Step 2: Register the test**

Append to `libs/markoff-source/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_source_markdown_view tst_source_markdown_view.cpp)
add_test(NAME tst_markoff_source_markdown_view COMMAND tst_markoff_source_markdown_view)
target_link_libraries(tst_markoff_source_markdown_view PRIVATE Qt6::Test Qt6::Widgets markoff_source)
set_tests_properties(tst_markoff_source_markdown_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_source_markdown_view
```

Expected: FAIL — `Source::SourceEditor` is a `QWidget` but not a
`MarkdownView`; upcast won't compile.

- [ ] **Step 4: Make `Source::SourceEditor` inherit `MarkdownView`**

In `libs/markoff-source/include/markoff/source/SourceEditor.h`, change
the class declaration from:

```cpp
class SourceEditor : public QWidget {
```

to:

```cpp
class SourceEditor : public Markoff::MarkdownView {
```

(inside `namespace Markoff::Source`). Add
`#include <markoff/MarkdownView.h>` at the top.

`MarkdownView` is-a `QWidget`, so no layout change is needed — the
existing `m_qutepart` child + layout still fits.

- [ ] **Step 5: Declare the MarkdownView overrides**

**Signature reconciliation required** (same as Task 10 Step 5): the
existing `SourceEditor::setReadOnly(bool)` returns `void`; the
MarkdownView virtual returns `bool`. Change the existing declaration
and implementation to return `bool` (append `return true;`). Source-
compatible for call sites that ignored the return value.

Add to the `public:` section of `SourceEditor`:

```cpp
// MarkdownView — Phase A forwarding implementations.
void setDocument(MarkoffDocument *doc) override;
MarkoffDocument *document() const override;
void setViewTheme(const Markoff::Theme &) override;
void setViewResourceProvider(Markoff::ResourceProvider *) override;
void setViewLinkResolver(Markoff::LinkResolver *) override;
float scrollPosition() const override;
void setScrollPosition(float visualLine) override;
void zoomIn() override;                     // already exists; mark override
void zoomOut() override;                    // already exists; mark override
void resetZoom() override;                  // already exists; mark override
QJsonObject ephemeralState() const override;
void setEphemeralState(const QJsonObject &) override;
SearchAdapter *searchAdapter() override;
bool hasCursor() const override { return true; }
bool hasEditing() const override { return true; }
bool hasFold() const override { return true; }
CursorPos cursorPosition() const override;
bool setCursorPosition(CursorPos) override;
bool setReadOnly(bool ro) override;
bool isReadOnly() const override;
QVector<FoldSpec> foldedHeadings() const override;
void setFoldedHeadings(const QVector<FoldSpec> &) override;
```

The existing `setCursorPosition(CursorPos)` method on `SourceEditor`
(inherited from the Corbomite shim — see the header in Task's pre-read)
is *already the same signature* as the `MarkdownView` virtual. Mark
it `override` and the compiler will check the match.

Add private members:

```cpp
MarkoffDocument *m_markoffDoc = nullptr;
std::unique_ptr<SourceSearchAdapter> m_searchAdapter;
```

- [ ] **Step 6: Create `libs/markoff-source/src/SourceSearchAdapter.h` and `.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/SearchAdapter.h>

namespace Markoff::Source {

class SourceEditor;

class SourceSearchAdapter : public SearchAdapter {
public:
    explicit SourceSearchAdapter(SourceEditor *owner);

    int cursorSourceOffset() const override;
    void highlightMatches(QVector<TextSpan>) override;
    void clearMatchHighlight() override;
    void scrollMatchIntoView(TextSpan) override;

private:
    SourceEditor *m_editor;
};

}  // namespace Markoff::Source
```

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceSearchAdapter.h"

#include <QTextCursor>

#include <markoff/source/SourceEditor.h>

namespace Markoff::Source {

SourceSearchAdapter::SourceSearchAdapter(SourceEditor *owner)
    : m_editor(owner) {}

int SourceSearchAdapter::cursorSourceOffset() const
{
    return m_editor ? m_editor->textCursor().position() : 0;
}

void SourceSearchAdapter::highlightMatches(QVector<TextSpan> spans)
{
    if (m_editor) m_editor->setSearchHighlights(spans);
}

void SourceSearchAdapter::clearMatchHighlight()
{
    if (m_editor) m_editor->setSearchHighlights({});
}

void SourceSearchAdapter::scrollMatchIntoView(TextSpan span)
{
    if (!m_editor) return;
    QTextCursor c(m_editor->document()->textDocument());
    c.setPosition(span.offset);
    m_editor->setTextCursor(c);
    m_editor->ensureCursorVisible();
}

}  // namespace Markoff::Source
```

`SourceEditor::textCursor()`, `setTextCursor()`, and
`ensureCursorVisible()` forward to the underlying `m_qutepart`
QPlainTextEdit — they likely already exist in the shim. If not, add
thin wrappers in `SourceEditor.cpp`:

```cpp
QTextCursor SourceEditor::textCursor() const { return m_qutepart->textCursor(); }
void SourceEditor::setTextCursor(QTextCursor c) { m_qutepart->setTextCursor(c); }
void SourceEditor::ensureCursorVisible() { m_qutepart->ensureCursorVisible(); }
void SourceEditor::setSearchHighlights(const QVector<Markoff::TextSpan> &spans)
{
    QList<QTextEdit::ExtraSelection> sel;
    QTextCharFormat fmt;
    fmt.setBackground(Qt::yellow);
    for (const auto &s : spans) {
        QTextEdit::ExtraSelection e;
        e.format = fmt;
        e.cursor = QTextCursor(m_qutepart->document());
        e.cursor.setPosition(s.offset);
        e.cursor.setPosition(s.offset + s.length, QTextCursor::KeepAnchor);
        sel.append(e);
    }
    m_qutepart->setExtraSelections(sel);
}
```

- [ ] **Step 7: Implement the MarkdownView overrides in `SourceEditor.cpp`**

Add the overrides as forwarders. Include `<markoff/MarkdownView.h>`
and `<markoff/MarkoffDocument.h>` and `"SourceSearchAdapter.h"` at
the top. Construct the adapter in the constructor:

```cpp
m_searchAdapter = std::make_unique<SourceSearchAdapter>(this);
```

Forwarding bodies — adapt each to whatever the existing shim exposes:

```cpp
void SourceEditor::setDocument(Markoff::MarkoffDocument *doc)
{
    m_markoffDoc = doc;
    // Phase A: store only. Phase C: m_qutepart->setDocument(doc->textDocument()).
}

Markoff::MarkoffDocument *SourceEditor::document() const { return m_markoffDoc; }

void SourceEditor::setViewTheme(const Markoff::Theme &) {}
void SourceEditor::setViewResourceProvider(Markoff::ResourceProvider *) {}
void SourceEditor::setViewLinkResolver(Markoff::LinkResolver *) {}

float SourceEditor::scrollPosition() const
{
    // Existing API returns visual-line float already.
    return scrollPositionVisualLine();  // existing shim method
}

void SourceEditor::setScrollPosition(float v) { setScrollPositionVisualLine(v); }

// zoomIn/Out/resetZoom already exist — just mark them override in the header.

QJsonObject SourceEditor::ephemeralState() const
{
    QJsonObject j;
    j.insert(QStringLiteral("scroll"), scrollPosition());
    // Fold list round-trip.
    QJsonArray folds;
    for (int line : foldedLines()) folds.append(line);
    j.insert(QStringLiteral("foldedLines"), folds);
    return j;
}

void SourceEditor::setEphemeralState(const QJsonObject &j)
{
    setScrollPosition(static_cast<float>(
        j.value(QStringLiteral("scroll")).toDouble(0.0)));
    QVector<int> fl;
    for (const auto &v : j.value(QStringLiteral("foldedLines")).toArray())
        fl.append(v.toInt());
    setFoldedLinesLegacy(fl);  // existing shim method
}

Markoff::SearchAdapter *SourceEditor::searchAdapter() { return m_searchAdapter.get(); }

Markoff::CursorPos SourceEditor::cursorPosition() const
{
    const auto cp = /* existing shim CursorPos */;
    return {cp.line, cp.column};
}

bool SourceEditor::setCursorPosition(Markoff::CursorPos p)
{
    setLegacyCursorPos(/*CursorPos from shim*/{p.line, p.column});
    return true;
}

bool SourceEditor::setReadOnly(bool ro) { m_qutepart->setReadOnly(ro); return true; }
bool SourceEditor::isReadOnly() const   { return m_qutepart->isReadOnly(); }

QVector<Markoff::FoldSpec> SourceEditor::foldedHeadings() const
{
    QVector<Markoff::FoldSpec> out;
    for (int line : foldedLines()) out.append({line, 0});  // level unknown in Phase A
    return out;
}

void SourceEditor::setFoldedHeadings(const QVector<Markoff::FoldSpec> &v)
{
    QVector<int> lines;
    for (const auto &f : v) lines.append(f.line);
    setFoldedLinesLegacy(lines);
}
```

If `foldedLines()` / `setFoldedLinesLegacy()` aren't the existing
method names, grep for `QVector<int>` and `fold` in
`libs/markoff-source/` and use the real names. The existing
`SourceEditor::foldedHeadings()` / `setFoldedHeadings()` already
returned/took `QVector<int>` — that's the legacy signature we wrap.

- [ ] **Step 8: Wire sources into CMakeLists**

Add `src/SourceSearchAdapter.h` and `src/SourceSearchAdapter.cpp` to
the `add_library(markoff_source STATIC ...)` block.

Confirm `target_link_libraries(markoff_source PUBLIC ...)` includes
`markoff_core`. Add it if missing.

- [ ] **Step 9: Run the new test + full suite**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev && \
  cd /home/clinton/dev/Markoff/build-dev && \
  ctest --output-on-failure
```

Expected: `tst_markoff_source_markdown_view` passes, all existing
`markoff-source` tests still pass.

- [ ] **Step 10: Commit**

```bash
cd /home/clinton/dev/Markoff
git add libs/markoff-source/
git commit -m "markoff-source: SourceEditor inherits MarkdownView

Capability probes hasCursor/hasEditing/hasFold = true. SourceSearchAdapter
bridges existing Qutepart find/highlight machinery to the MarkdownView
SearchAdapter contract. Forwarding overrides for scroll/zoom/
ephemeral-state/cursor/fold/readonly. Phase A: setDocument() stores
the pointer; the Qutepart QPlainTextEdit remains content-authoritative
until Phase C.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 13: Absorb `readingview` as `libs/markoff-reading/`

**Files:**
- New directory: `libs/markoff-reading/` (populated via git subtree)
- Modify: `CMakeLists.txt` (top level)

Same pattern as Task 11: subtree split + subtree add, rename target,
namespace public API.

- [ ] **Step 1: Subtree-split out of Corbomite**

```bash
cd /home/clinton/dev/Corbomite
git subtree split --prefix libs/readingview -b markoff-reading-split
```

- [ ] **Step 2: Subtree-add into Markoff**

```bash
cd /home/clinton/dev/Markoff
git remote add --no-tags corbomite-src /home/clinton/dev/Corbomite
git fetch corbomite-src markoff-reading-split
git subtree add --prefix libs/markoff-reading corbomite-src/markoff-reading-split
git remote remove corbomite-src
```

- [ ] **Step 3: Rename CMake target**

In `libs/markoff-reading/CMakeLists.txt`:

```bash
sed -i \
  -e 's/project(readingview/project(markoff_reading/' \
  -e 's/add_library(readingview/add_library(markoff_reading/g' \
  /home/clinton/dev/Markoff/libs/markoff-reading/CMakeLists.txt
```

Add or change the alias to `Markoff::Reading`:

```cmake
add_library(Markoff::Reading ALIAS markoff_reading)
```

(delete any existing `Corbomite::ReadingView` alias if present).

Rewrite every `target_link_libraries(readingview ...)` and similar
to `markoff_reading`.

- [ ] **Step 4: Namespace the public API**

```bash
cd /home/clinton/dev/Markoff/libs/markoff-reading
mkdir -p include/markoff/reading
# Move public headers into the new prefix.
git mv include/corbomite/readingview/*.h include/markoff/reading/
git mv include/corbomite/readingview/styling include/markoff/reading/styling 2>/dev/null || true
rmdir include/corbomite/readingview 2>/dev/null || true
rmdir include/corbomite 2>/dev/null || true
```

Update namespaces:

```bash
grep -rl 'namespace Corbomite::ReadingView' include src tests | while read -r f; do
  sed -i 's/namespace Corbomite::ReadingView/namespace Markoff::Reading/g' "$f"
  sed -i 's|^}  // namespace Corbomite::ReadingView|}  // namespace Markoff::Reading|' "$f"
done
grep -rl 'Corbomite::ReadingView' . | while read -r f; do
  sed -i 's|Corbomite::ReadingView|Markoff::Reading|g' "$f"
done
grep -rl 'corbomite/readingview/' . | while read -r f; do
  sed -i 's|corbomite/readingview/|markoff/reading/|g' "$f"
done
```

Review the diff for any file that legitimately needed to keep its
`Corbomite::` namespace (unlikely inside this lib; possible for a
vendored shim). Revert any bad over-matches.

- [ ] **Step 5: Wire into top-level CMakeLists**

Add to `/home/clinton/dev/Markoff/CMakeLists.txt`:

```cmake
add_subdirectory(libs/markoff-reading)
```

Update the library comment block.

- [ ] **Step 6: Clean build + test**

```bash
rm -rf /home/clinton/dev/Markoff/build-dev
cmake -S /home/clinton/dev/Markoff -B /home/clinton/dev/Markoff/build-dev \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/Markoff/build-dev
cd /home/clinton/dev/Markoff/build-dev && ctest --output-on-failure
```

Expected: all tests pass, including markoff-reading's own suite.

- [ ] **Step 7: Commit**

```bash
cd /home/clinton/dev/Markoff
git add -A
git commit -m "Absorb libs/markoff-reading from Corbomite

Rename CMake target readingview → markoff_reading; alias
Corbomite::ReadingView → Markoff::Reading. Public header namespace
Corbomite::ReadingView → Markoff::Reading. Public header path
<corbomite/readingview/X.h> → <markoff/reading/X.h>. Wired into
top-level CMakeLists.

History was preserved via git subtree from Corbomite's libs/readingview/
subtree.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 14: `Markoff::Reading::ReadingView` inherits `MarkdownView`

**Files:**
- Modify: `libs/markoff-reading/include/markoff/reading/ReadingView.h`
- Modify: `libs/markoff-reading/src/ReadingView.cpp`
- Create: `libs/markoff-reading/src/ReadingSearchAdapter.h`
- Create: `libs/markoff-reading/src/ReadingSearchAdapter.cpp`
- Create: `libs/markoff-reading/tests/tst_reading_markdown_view.cpp`
- Modify: `libs/markoff-reading/CMakeLists.txt`
- Modify: `libs/markoff-reading/tests/CMakeLists.txt`

Reading's capability probes: hasCursor = false, hasEditing = false,
hasFold = true (per-section heading collapse). `setReadOnly(false)`
returns false and logs. The search adapter's `supportsReplace()`
returns false.

- [ ] **Step 1: Write the failing test**

`libs/markoff-reading/tests/tst_reading_markdown_view.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>

#include <markoff/MarkdownView.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/reading/ReadingView.h>

using namespace Markoff;

class TstReadingMarkdownView : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void readingIsAMarkdownView() {
        Reading::ReadingView rv;
        MarkdownView *v = &rv;
        QVERIFY(v != nullptr);
    }

    void capabilityProbes() {
        Reading::ReadingView rv;
        MarkdownView *v = &rv;
        QVERIFY(!v->hasCursor());
        QVERIFY(!v->hasEditing());
        QVERIFY(v->hasFold());
        QVERIFY(v->isReadOnly());
    }

    void setReadOnlyFalseIsRefused() {
        Reading::ReadingView rv;
        MarkdownView *v = &rv;
        QVERIFY(!v->setReadOnly(false));
        QVERIFY(v->isReadOnly());
    }

    void searchAdapterRefusesReplace() {
        Reading::ReadingView rv;
        MarkdownView *v = &rv;
        QVERIFY(v->searchAdapter() != nullptr);
        QVERIFY(!v->searchAdapter()->supportsReplace());
    }

    void setDocumentStoresPointer() {
        Reading::ReadingView rv;
        MarkdownView *v = &rv;
        MarkoffDocument doc;
        v->setDocument(&doc);
        QCOMPARE(v->document(), &doc);
    }
};

QTEST_MAIN(TstReadingMarkdownView)
#include "tst_reading_markdown_view.moc"
```

- [ ] **Step 2: Register the test**

Append to `libs/markoff-reading/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_reading_markdown_view tst_reading_markdown_view.cpp)
add_test(NAME tst_markoff_reading_markdown_view COMMAND tst_markoff_reading_markdown_view)
target_link_libraries(tst_markoff_reading_markdown_view PRIVATE Qt6::Test Qt6::Widgets markoff_reading)
set_tests_properties(tst_markoff_reading_markdown_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_markoff_reading_markdown_view
```

Expected: FAIL — `Reading::ReadingView` is not a `MarkdownView`.

- [ ] **Step 4: Make `Reading::ReadingView` inherit `MarkdownView`**

In `libs/markoff-reading/include/markoff/reading/ReadingView.h`,
change the class declaration to inherit `Markoff::MarkdownView`:

```cpp
class ReadingView : public Markoff::MarkdownView {
```

(inside `namespace Markoff::Reading`). Include
`<markoff/MarkdownView.h>` at the top. If the existing class
inherited `QWidget` or `QGraphicsView`, the same composition
treatment as Task 10 applies — but `MarkdownView` is-a QWidget and
ReadingView likely already is too, so composition is unnecessary.
If ReadingView was a QGraphicsView, apply the Task 10 composition
pattern (add a `QGraphicsView *m_graphicsView` child).

- [ ] **Step 5: Declare and implement the overrides**

Mirror Task 12 Step 5 + 7, adjusting capability defaults to
`hasCursor = false`, `hasEditing = false`, `hasFold = true`.
`setReadOnly(true)` returns true (it was already read-only);
`setReadOnly(false)` does the log-and-refuse:

```cpp
bool ReadingView::setReadOnly(bool ro)
{
    if (!ro) {
        qCWarning(lcReading) << "setReadOnly(false) refused on ReadingView — reading mode is read-only by design";
        return false;
    }
    return true;
}

bool ReadingView::isReadOnly() const { return true; }
```

`CursorPos cursorPosition() const` returns `{}` (the default); do not
override it — the `MarkdownView` default is correct.

`foldedHeadings()` returns the current per-section heading collapse
state (Reading's `ReadingSection::headingCollapsed`). Map each
collapsed section's `{from-line, level}` to `FoldSpec`.

- [ ] **Step 6: Create `ReadingSearchAdapter`**

Same pattern as Tasks 10/12 for live and source. Implement
`supportsReplace() { return false; }` per the capability contract.
`highlightMatches` drives the existing ReadingView highlight pass (or,
if none exists yet, a Phase A minimal implementation that walks
mounted sections and paints background rectangles for intersecting
source-ranges). `cursorSourceOffset()` returns the source-offset of
the top-of-viewport section (there's no cursor).

- [ ] **Step 7: Wire sources into CMakeLists**

Add `src/ReadingSearchAdapter.h` and `src/ReadingSearchAdapter.cpp`.
Confirm `markoff_core` is in `target_link_libraries(markoff_reading PUBLIC ...)`.

- [ ] **Step 8: Run the new test + full suite**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev && \
  cd /home/clinton/dev/Markoff/build-dev && \
  ctest --output-on-failure
```

Expected: new test + all pre-existing markoff-reading tests PASS.

- [ ] **Step 9: Commit**

```bash
cd /home/clinton/dev/Markoff
git add libs/markoff-reading/
git commit -m "markoff-reading: ReadingView inherits MarkdownView

Capability probes hasCursor = false, hasEditing = false,
hasFold = true. setReadOnly(false) refused with diagnostic log.
ReadingSearchAdapter::supportsReplace() returns false. Forwarding
overrides for scroll/zoom/ephemeral-state/fold.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 15: Cross-leaf smoke test — polymorphic dispatch

**Files:**
- Create: `tests/markoff/tst_tri_view_smoke.cpp`
- Modify: `tests/markoff/CMakeLists.txt`

The top-level `tests/markoff/` directory already exists and is
registered from the top-level CMakeLists. Add one integration test
that constructs all three leaf widgets, attaches them to a single
`MarkoffDocument`, and verifies they round-trip through the
`MarkdownView *` base. This proves the whole polymorphic contract is
real across libraries.

- [ ] **Step 1: Write the failing test**

`tests/markoff/tst_tri_view_smoke.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>

#include <markoff/MarkdownView.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/Editor.h>                        // live
#include <markoff/source/SourceEditor.h>
#include <markoff/reading/ReadingView.h>

using namespace Markoff;

class TstTriViewSmoke : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void allThreeAreMarkdownViews() {
        Editor live;
        Source::SourceEditor source;
        Reading::ReadingView reading;

        const QVector<MarkdownView *> views = {&live, &source, &reading};
        for (MarkdownView *v : views) QVERIFY(v != nullptr);
    }

    void capabilityMatrix() {
        Editor live;
        Source::SourceEditor source;
        Reading::ReadingView reading;

        QVERIFY(live.hasCursor());
        QVERIFY(live.hasEditing());
        QVERIFY(live.hasFold());

        QVERIFY(source.hasCursor());
        QVERIFY(source.hasEditing());
        QVERIFY(source.hasFold());

        QVERIFY(!reading.hasCursor());
        QVERIFY(!reading.hasEditing());
        QVERIFY(reading.hasFold());
    }

    void allAttachToOneDocument() {
        MarkoffDocument doc;
        Editor live;
        Source::SourceEditor source;
        Reading::ReadingView reading;
        live.setDocument(&doc);
        source.setDocument(&doc);
        reading.setDocument(&doc);

        QCOMPARE(live.document(), &doc);
        QCOMPARE(source.document(), &doc);
        QCOMPARE(reading.document(), &doc);
    }

    void searchAdapterDispatchIsPolymorphic() {
        Editor live;
        Source::SourceEditor source;
        Reading::ReadingView reading;

        const QVector<MarkdownView *> views = {&live, &source, &reading};
        for (MarkdownView *v : views) QVERIFY(v->searchAdapter() != nullptr);

        // Only Reading refuses replace.
        QVERIFY(live.searchAdapter()->supportsReplace());
        QVERIFY(source.searchAdapter()->supportsReplace());
        QVERIFY(!reading.searchAdapter()->supportsReplace());
    }
};

QTEST_MAIN(TstTriViewSmoke)
#include "tst_tri_view_smoke.moc"
```

- [ ] **Step 2: Register the test**

Open `/home/clinton/dev/Markoff/tests/markoff/CMakeLists.txt`. Add:

```cmake
add_executable(tst_tri_view_smoke tst_tri_view_smoke.cpp)
add_test(NAME tst_tri_view_smoke COMMAND tst_tri_view_smoke)
target_link_libraries(tst_tri_view_smoke PRIVATE
    Qt6::Test Qt6::Widgets
    markoff_core
    markoff_live
    markoff_source
    markoff_reading
)
set_tests_properties(tst_tri_view_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build and run**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev --target tst_tri_view_smoke && \
  QT_QPA_PLATFORM=offscreen /home/clinton/dev/Markoff/build-dev/bin/tst_tri_view_smoke
```

Expected: all four slots PASS.

- [ ] **Step 4: Full test run**

```bash
cd /home/clinton/dev/Markoff/build-dev && ctest --output-on-failure
```

Expected: every test across all four leaf libraries + core passes.

- [ ] **Step 5: Commit**

```bash
cd /home/clinton/dev/Markoff
git add tests/
git commit -m "Tri-view smoke test: polymorphic MarkdownView dispatch

Constructs live/source/reading widgets, attaches all three to one
MarkoffDocument, verifies capability-matrix and searchAdapter
polymorphism through the MarkdownView base.

Closes Phase A of the tri-view absorption
(docs/specs/2026-04-20-tri-view-unified-api-design.md).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Phase A — Acceptance

The phase is complete when:

- `~/dev/Markoff/build-dev` builds clean from a cold cache.
- `ctest --output-on-failure` in `build-dev` passes every test: the
  core suite (7 tests added in Tasks 2–8), every markoff-live test
  (pre-existing), every markoff-source test (newly absorbed), every
  markoff-reading test (newly absorbed), and `tst_tri_view_smoke` at
  the top level.
- `libs/markoff-core/`, `libs/markoff-live/`, `libs/markoff-source/`,
  `libs/markoff-reading/` all exist and build as independent targets.
- `Markoff::Editor`, `Markoff::Source::SourceEditor`, and
  `Markoff::Reading::ReadingView` are all `Markoff::MarkdownView`
  subclasses with correct capability probes.
- `MarkoffDocument` exists in core with undo, transactions, and a
  synchronous parse cache; `SearchController` and `ReplaceController`
  drive it.
- Corbomite is untouched by this phase; its submodule pin continues
  to reference a pre-rename Markoff SHA. Phase B (separate plan) is
  where Corbomite flips to external consumption.

## Deferred to later phases

- Async parse worker (`ReadingParseWorker` → `MarkoffDocument`). Phase C.
- Theme / ResourceProvider / LinkResolver consolidation into real
  types. Phase C.
- CodeHighlighter / MathRenderer / MermaidRenderer unification.
  Phase C.
- `MarkoffDocument::parsed()` emits the `parsed(const Document*)`
  signal on a worker thread. Phase C.
- Precise pixel ↔ visual-line float scroll conversion
  (`ScrollPosition.h`). Phase C.
- Source-offset ↔ per-block cursor translation for undo across mode
  switch. Phase C (the translation exists as read-only; undo stress
  tests arrive with the binding).
- Corbomite: submodule removal, include-path migration,
  `SourceEditor` shim deletion, `NoteEditorWidget` polymorphic
  refactor. Phase B.
- Consumer integration guide (`Markoff/docs/integration-guide.md`). Phase D.
