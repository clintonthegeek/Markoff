> **Status: completed.** `libs/markoff-bench/`, `apps/bench/` CLI frontends, and `tst_bench_smoke` are in tree (commit range `666dcea`…`6040dd2`). Do not execute.

# Parse / render pipeline benchmark — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a two-tier benchmark harness (parse-only + render) for the foundation-exploration parse/render pipeline, per `docs/specs/2026-04-29-parse-render-bench-design.md`. Output is per-phase wall-time + reuse counts + alloc bytes/count + p50/p95/p99/max for 9 synthetic profiles × 7 edit scenarios, emitted as JSON for trending.

**Architecture:** A new internal STATIC library `libs/markoff-bench/` hosts the corpus generator, fixture loader, scenario builders, ScenarioRunner, PhaseTimer, AllocCounter, PercentileReducer, and JsonReporter. Two CLI frontends (`apps/bench/markoff-bench-parse`, `apps/bench/markoff-bench-render`) consume the library plus one CTest-registered smoke target (`tst_bench_smoke`) that runs a small slice on every `ctest -L bench`. Tier 1 reaches `IncrementalParseSession` directly via `PRIVATE` include access into `libs/markoff-core/src/`; Tier 2 spins up the QML view under `QT_QPA_PLATFORM=offscreen`.

**Tech Stack:** C++20, Qt6 (Core, Gui, Widgets, Test, Qml, Quick), KF6::SyntaxHighlighting, tree-sitter, CMake 3.19+. Existing in-repo: `markoff-foundation`, `markoff-parser`, `markoff-view-qml`, `collabtext`.

---

## File structure

| Path | Purpose |
|---|---|
| `libs/markoff-bench/CMakeLists.txt` | STATIC library `markoff_bench` (alias `Markoff::Bench`). |
| `libs/markoff-bench/include/markoff-bench/PhaseTimer.h` | RAII timer recording labelled `steady_clock` durations to a `PhaseTable`. |
| `libs/markoff-bench/include/markoff-bench/PercentileReducer.h` | Fixed-point percentile reducer over `std::vector<uint64_t>`. |
| `libs/markoff-bench/include/markoff-bench/AllocCounter.h` | Thread-local enable flag + counters (bytes, count) populated by a global `operator new`/`delete` shim. |
| `libs/markoff-bench/include/markoff-bench/CorpusGen.h` | `CorpusProfile` enum + `generate(profile, seed)` returning a `QByteArray`. |
| `libs/markoff-bench/include/markoff-bench/FixtureLoader.h` | `loadFixture(name)` returning `QByteArray` from compile-time fixture path. |
| `libs/markoff-bench/include/markoff-bench/Scenario.h` | `ScenarioKind` enum + `buildSteps(kind, doc, rngSeed)` returning a `QList<MarkoffEdit>`. |
| `libs/markoff-bench/include/markoff-bench/ScenarioRunner.h` | Drives a scenario at Tier 1 (direct), Tier 1b (pool), or Tier 2 (render). Returns `RunResult`. |
| `libs/markoff-bench/include/markoff-bench/JsonReporter.h` | `RunResult` → `QJsonObject`; `QJsonArray` of results. |
| `libs/markoff-bench/src/AllocShim.cpp` | `operator new`/`delete` global override; thread-local enable flag from `AllocCounter`. |
| `libs/markoff-bench/src/PhaseTimer.cpp` | impl. |
| `libs/markoff-bench/src/PercentileReducer.cpp` | impl. |
| `libs/markoff-bench/src/AllocCounter.cpp` | impl (just the TLS getters/setters). |
| `libs/markoff-bench/src/CorpusGen.cpp` | impl. |
| `libs/markoff-bench/src/FixtureLoader.cpp` | impl. |
| `libs/markoff-bench/src/Scenario.cpp` | impl. |
| `libs/markoff-bench/src/ScenarioRunner.cpp` | impl. |
| `libs/markoff-bench/src/JsonReporter.cpp` | impl. |
| `libs/markoff-bench/fixtures/foundation-design.md` | Copy of `docs/specs/2026-04-28-foundation-design.md`. |
| `libs/markoff-bench/fixtures/typing-perf-plan.md` | Copy of `docs/plans/2026-04-28-typing-perf.md`. |
| `libs/markoff-bench/fixtures/commonmark-spec.md` | Public-domain CommonMark spec dump. |
| `libs/markoff-bench/tests/CMakeLists.txt` | Registers the unit tests below + `tst_bench_smoke` (label `bench`). |
| `libs/markoff-bench/tests/tst_bench_phase_timer.cpp` | Unit tests for PhaseTimer. |
| `libs/markoff-bench/tests/tst_bench_percentile.cpp` | Unit tests for PercentileReducer. |
| `libs/markoff-bench/tests/tst_bench_alloc_counter.cpp` | Unit tests for AllocCounter. |
| `libs/markoff-bench/tests/tst_bench_corpus_gen.cpp` | Unit tests for CorpusGen (determinism, profile sizes). |
| `libs/markoff-bench/tests/tst_bench_fixture_loader.cpp` | Unit tests for FixtureLoader. |
| `libs/markoff-bench/tests/tst_bench_scenario.cpp` | Unit tests for Scenario step builders. |
| `libs/markoff-bench/tests/tst_bench_smoke.cpp` | CTest smoke (one profile × one scenario × tiny iter count); label `bench`. |
| `libs/markoff-parser/include/markoff-parser/TreeSitterParser.h` | **Modify:** add `int blockChangedByteCount() const` accessor + private `m_lastBlockChangedBytes` member. |
| `libs/markoff-parser/src/TreeSitterParser.cpp` | **Modify:** populate `m_lastBlockChangedBytes` from `ts_tree_get_changed_ranges` in `parseIncremental`; reset to -1 in `parse()`. |
| `libs/markoff-parser/tests/tst_incremental_parse.cpp` | **Modify:** add a test that asserts `blockChangedByteCount()` is small for a localised edit and -1 after `parse()`. |
| `apps/bench/CMakeLists.txt` | Adds the two CLI executables. |
| `apps/bench/markoff-bench-parse.cpp` | Tier 1 + Tier 1b CLI. |
| `apps/bench/markoff-bench-render.cpp` | Tier 2 CLI (offscreen QPA + QML view + key dispatch + render latency). |
| `CMakeLists.txt` (root) | **Modify:** `add_subdirectory(libs/markoff-bench)` + `add_subdirectory(apps/bench)`. |
| `libs/markoff-bench/README.md` | Usage, output schema, caveats. |

Total: 15 new C++ files (excluding fixtures), 7 unit/smoke tests, 2 CLIs, 1 README, 3 modifications to existing files.

---

## Task 1: Add `blockChangedByteCount()` to `TreeSitterParser`

**Files:**
- Modify: `libs/markoff-parser/include/markoff-parser/TreeSitterParser.h`
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp`
- Test: `libs/markoff-parser/tests/tst_incremental_parse.cpp`

This is the only production-code change in the plan. It adds an observability hook mirroring the existing `inlineTreeReuseCount()`. Implementation uses `ts_tree_get_changed_ranges(prevTree, newTree)` which the parser already has access to right after `ts_parser_parse(prevTree, …)`.

- [ ] **Step 1.1: Read the existing inline-reuse counter to confirm the pattern**

Run: `grep -n "m_lastInlineReuseCount\|inlineTreeReuseCount" libs/markoff-parser/include/markoff-parser/TreeSitterParser.h libs/markoff-parser/src/TreeSitterParser.cpp`

Expected: header declares `int inlineTreeReuseCount() const { return m_lastInlineReuseCount; }` plus a private `int m_lastInlineReuseCount = 0;`. Cpp resets to 0 in `parse()` and at the top of the inline-reuse loop in `parseIncremental()`.

- [ ] **Step 1.2: Write a failing test**

Edit `libs/markoff-parser/tests/tst_incremental_parse.cpp`. Add a private slot `blockChangedByteCount_initialAndIncremental`:

```cpp
void TstIncrementalParse::blockChangedByteCount_initialAndIncremental()
{
    Markoff::TreeSitterParser parser;
    QVERIFY(parser.parse(QStringLiteral("# Heading\n\nA paragraph.\n")));
    // After a fresh parse there is no previous tree to compare against.
    QCOMPARE(parser.blockChangedByteCount(), -1);

    // Insert a single character at the end of the paragraph.
    Markoff::ByteEdit edit;
    edit.oldStart = 23;          // before final '\n'
    edit.oldEnd   = 23;
    edit.newLength = 1;
    QByteArray newBuf = QByteArrayLiteral("# Heading\n\nA paragraph!.\n");
    QVERIFY(parser.parseIncremental({edit}, newBuf));

    const int changed = parser.blockChangedByteCount();
    QVERIFY2(changed >= 0, "after parseIncremental the counter must be set");
    QVERIFY2(changed <= 64, "a one-char edit on a tiny doc must produce a small changed-bytes total");
}
```

Add the method declaration alongside other private slots near the top of the class.

- [ ] **Step 1.3: Run the test to verify it fails**

Run:
```
cmake --build build-dev --target tst_markoff_parser_incremental -j
ctest --test-dir build-dev -R tst_markoff_parser_incremental --output-on-failure
```
Expected: compile error — `blockChangedByteCount` is not a member of `TreeSitterParser`.

- [ ] **Step 1.4: Add the accessor + member to the header**

Edit `libs/markoff-parser/include/markoff-parser/TreeSitterParser.h`. Insert after the existing `inlineTreeReuseCount()` declaration:

```cpp
    /// Total bytes covered by ts_tree_get_changed_ranges(prevTree, newTree)
    /// on the most recent parseIncremental() call. Returns -1 after a fresh
    /// parse() (no previous tree to compare). Observability hook for benches.
    int blockChangedByteCount() const { return m_lastBlockChangedBytes; }
```

And add to the private members section, next to `m_lastInlineReuseCount`:

```cpp
    int m_lastBlockChangedBytes = -1;
```

- [ ] **Step 1.5: Reset the counter in `parse()`**

Edit `libs/markoff-parser/src/TreeSitterParser.cpp`. In `bool TreeSitterParser::parse(const QString &text)`, alongside `m_lastInlineReuseCount = 0;`, add:

```cpp
    m_lastBlockChangedBytes = -1;
```

- [ ] **Step 1.6: Populate the counter in `parseIncremental()`**

Locate the call site `m_blockTree = ts_parser_parse(m_blockParser, prevTree, …)` (or equivalent) in `parseIncremental`. Immediately after the new tree is produced and **before** `ts_tree_delete(prevTree)`, insert:

```cpp
    {
        uint32_t nRanges = 0;
        TSRange *ranges = ts_tree_get_changed_ranges(prevTree, m_blockTree, &nRanges);
        quint64 totalBytes = 0;
        for (uint32_t i = 0; i < nRanges; ++i) {
            totalBytes += static_cast<quint64>(ranges[i].end_byte) - ranges[i].start_byte;
        }
        if (ranges) free(ranges);
        m_lastBlockChangedBytes = static_cast<int>(qMin<quint64>(totalBytes, INT_MAX));
    }
```

(`ts_tree_get_changed_ranges` allocates with `malloc`; the matching `free` is correct.)

If the local variable is named differently in the actual code (e.g. `oldTree` instead of `prevTree`), use the existing name. Confirm by grepping for `ts_tree_get_changed_ranges` in the file before editing — if it is already called for the inline-reuse path, the new call goes alongside it, not duplicating it.

- [ ] **Step 1.7: Run the test to verify it passes**

Run:
```
cmake --build build-dev --target tst_markoff_parser_incremental -j
ctest --test-dir build-dev -R tst_markoff_parser_incremental --output-on-failure
```
Expected: all incremental-parse tests pass, including the new one.

- [ ] **Step 1.8: Run the full test suite to confirm no regression**

Run:
```
cmake --build build-dev -j
ctest --test-dir build-dev -j -E "tst_realistic|tst_benchmark" --output-on-failure
```
Expected: same green count as before the change (was 78/78 minus the realistic/benchmark exclusions; new test is +1).

- [ ] **Step 1.9: Commit**

```
git add libs/markoff-parser/include/markoff-parser/TreeSitterParser.h \
        libs/markoff-parser/src/TreeSitterParser.cpp \
        libs/markoff-parser/tests/tst_incremental_parse.cpp
git commit -m "feat(parser): expose blockChangedByteCount() observability hook

Mirrors inlineTreeReuseCount(); reports total bytes covered by
ts_tree_get_changed_ranges(prevTree, newTree) after parseIncremental(),
or -1 after a fresh parse(). Used by the upcoming markoff-bench harness
to attribute block-tree cost during steady-state typing.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Scaffold `libs/markoff-bench/` library

**Files:**
- Create: `libs/markoff-bench/CMakeLists.txt`
- Create: `libs/markoff-bench/include/markoff-bench/.gitkeep` (placeholder; deleted in Task 3)
- Create: `libs/markoff-bench/src/.gitkeep` (placeholder; deleted in Task 3)
- Modify: `CMakeLists.txt` (root)

- [ ] **Step 2.1: Create the directory structure**

```
mkdir -p libs/markoff-bench/include/markoff-bench \
         libs/markoff-bench/src \
         libs/markoff-bench/tests \
         libs/markoff-bench/fixtures
touch libs/markoff-bench/include/markoff-bench/.gitkeep \
      libs/markoff-bench/src/.gitkeep
```

- [ ] **Step 2.2: Write `libs/markoff-bench/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.19)
project(markoff_bench VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

# Bench is a STATIC library used only by the in-tree bench frontends and
# tests. It is not installed and is not part of the public Markoff API.
add_library(markoff_bench STATIC
    # Sources are added incrementally as tasks land.
)

target_include_directories(markoff_bench
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/markoff-core/src
)

target_link_libraries(markoff_bench
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        markoff_core
        MarkoffParser::MarkoffParser
)

# Compile-time path to fixture directory (used by FixtureLoader).
target_compile_definitions(markoff_bench
    PRIVATE
        MARKOFF_BENCH_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_library(Markoff::Bench ALIAS markoff_bench)

# Tests.
if(NOT DEFINED MARKOFF_BENCH_BUILD_TESTS)
    set(MARKOFF_BENCH_BUILD_TESTS ON)
endif()
if(MARKOFF_BENCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2.3: Write minimal `libs/markoff-bench/tests/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.19)
project(markoff_bench_tests LANGUAGES CXX)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Tests are added incrementally as tasks land.
```

- [ ] **Step 2.4: Hook up the root `CMakeLists.txt`**

Edit `CMakeLists.txt` (root). After the existing `add_subdirectory(libs/markoff-source)` line, add:

```cmake
add_subdirectory(libs/markoff-bench)
```

- [ ] **Step 2.5: Configure + build to confirm the skeleton compiles**

Run:
```
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-dev --target markoff_bench -j
```
Expected: builds with no errors. (The `STATIC` library is empty — CMake does emit a warning about that on some versions; ignore it for now, it will go away in Task 3 when we add the first source.)

- [ ] **Step 2.6: Commit**

```
git add libs/markoff-bench/CMakeLists.txt \
        libs/markoff-bench/tests/CMakeLists.txt \
        libs/markoff-bench/include/markoff-bench/.gitkeep \
        libs/markoff-bench/src/.gitkeep \
        libs/markoff-bench/fixtures \
        CMakeLists.txt
git commit -m "feat(bench): scaffold markoff-bench library

Empty STATIC library with PRIVATE include access into
libs/markoff-core/src/ so the harness can reach
IncrementalParseSession directly. Sources land in subsequent tasks.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: PhaseTimer

**Files:**
- Create: `libs/markoff-bench/include/markoff-bench/PhaseTimer.h`
- Create: `libs/markoff-bench/src/PhaseTimer.cpp`
- Create: `libs/markoff-bench/tests/tst_bench_phase_timer.cpp`
- Modify: `libs/markoff-bench/CMakeLists.txt`
- Modify: `libs/markoff-bench/tests/CMakeLists.txt`

A `PhaseTable` is a flat `std::array<quint64, kPhaseCount>` of nanosecond counters. `PhaseTimer` is an RAII guard that records elapsed time into one slot on destruction. Phases are an enum so we don't allocate strings on the hot path.

- [ ] **Step 3.1: Write the failing test**

Create `libs/markoff-bench/tests/tst_bench_phase_timer.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/PhaseTimer.h>

#include <thread>
#include <chrono>

class TstBenchPhaseTimer : public QObject {
    Q_OBJECT
private slots:
    void records_into_correct_slot();
    void accumulates_across_calls();
    void zero_elapsed_when_unused();
};

using namespace Markoff::Bench;

void TstBenchPhaseTimer::records_into_correct_slot()
{
    PhaseTable t{};
    {
        PhaseTimer guard(t, Phase::Diff);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    QVERIFY(t[static_cast<int>(Phase::Diff)] >= 1'000'000ull);          // ≥1ms
    QCOMPARE(t[static_cast<int>(Phase::Extract)], 0ull);
    QCOMPARE(t[static_cast<int>(Phase::ParseBlock)], 0ull);
}

void TstBenchPhaseTimer::accumulates_across_calls()
{
    PhaseTable t{};
    for (int i = 0; i < 3; ++i) {
        PhaseTimer guard(t, Phase::Queries);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QVERIFY(t[static_cast<int>(Phase::Queries)] >= 2'500'000ull);       // ≥2.5ms total
}

void TstBenchPhaseTimer::zero_elapsed_when_unused()
{
    PhaseTable t{};
    QCOMPARE(t[static_cast<int>(Phase::Snapshot)], 0ull);
}

QTEST_GUILESS_MAIN(TstBenchPhaseTimer)
#include "tst_bench_phase_timer.moc"
```

- [ ] **Step 3.2: Register the test in `libs/markoff-bench/tests/CMakeLists.txt`**

Append:

```cmake
add_executable(tst_bench_phase_timer tst_bench_phase_timer.cpp)
add_test(NAME tst_bench_phase_timer COMMAND tst_bench_phase_timer)
target_link_libraries(tst_bench_phase_timer PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_phase_timer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3.3: Run to verify failure**

Run: `cmake --build build-dev --target tst_bench_phase_timer -j`
Expected: compile error — `PhaseTimer.h` does not exist.

- [ ] **Step 3.4: Write the header**

Create `libs/markoff-bench/include/markoff-bench/PhaseTimer.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>
#include <array>
#include <chrono>

namespace Markoff::Bench {

enum class Phase : int {
    Extract     = 0,
    Diff        = 1,
    ParseBlock  = 2,
    ParseInline = 3,
    Queries     = 4,
    Snapshot    = 5,
    PoolQueue   = 6,   // Tier 1b: applyLocalEdit return → worker pickup
    SignalHop   = 7,   // Tier 1b: worker emit → main-thread receipt
    RenderFrame = 8,   // Tier 2 only
    _Count      = 9,
};

constexpr int kPhaseCount = static_cast<int>(Phase::_Count);

/// Per-iteration phase totals in nanoseconds, indexed by Phase.
using PhaseTable = std::array<quint64, kPhaseCount>;

/// RAII guard that adds the elapsed wall time (steady_clock nanoseconds)
/// of its lifetime to `table[phase]`. Cheap; no heap traffic, no syscalls
/// beyond clock_gettime.
class PhaseTimer {
public:
    PhaseTimer(PhaseTable &table, Phase phase) noexcept
        : m_table(table), m_phase(phase),
          m_start(std::chrono::steady_clock::now()) {}

    ~PhaseTimer() {
        const auto end = std::chrono::steady_clock::now();
        const auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            end - m_start).count();
        m_table[static_cast<int>(m_phase)] += static_cast<quint64>(ns);
    }

    PhaseTimer(const PhaseTimer &) = delete;
    PhaseTimer &operator=(const PhaseTimer &) = delete;

private:
    PhaseTable &m_table;
    Phase       m_phase;
    std::chrono::steady_clock::time_point m_start;
};

}  // namespace Markoff::Bench
```

- [ ] **Step 3.5: Add a no-op `PhaseTimer.cpp` so AUTOMOC has a translation unit and the library has at least one source**

Create `libs/markoff-bench/src/PhaseTimer.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/PhaseTimer.h>
// All inline; this TU exists so markoff_bench has at least one source file
// (CMake STATIC libraries on Linux warn when entirely empty).
namespace Markoff::Bench { /* intentionally empty */ }
```

- [ ] **Step 3.6: Add the source to `libs/markoff-bench/CMakeLists.txt`**

Replace the empty `add_library(markoff_bench STATIC)` block with:

```cmake
add_library(markoff_bench STATIC
    src/PhaseTimer.cpp
)
```

- [ ] **Step 3.7: Delete the `src/.gitkeep` placeholder**

```
git rm libs/markoff-bench/src/.gitkeep
```

- [ ] **Step 3.8: Build + run the test**

Run:
```
cmake --build build-dev --target tst_bench_phase_timer -j
ctest --test-dir build-dev -R tst_bench_phase_timer --output-on-failure
```
Expected: PASS (3 tests).

- [ ] **Step 3.9: Commit**

```
git add libs/markoff-bench/include/markoff-bench/PhaseTimer.h \
        libs/markoff-bench/src/PhaseTimer.cpp \
        libs/markoff-bench/tests/tst_bench_phase_timer.cpp \
        libs/markoff-bench/CMakeLists.txt \
        libs/markoff-bench/tests/CMakeLists.txt
git commit -m "feat(bench): PhaseTimer + Phase enum

RAII timer that accumulates steady_clock nanoseconds into a flat
PhaseTable indexed by enum. No heap traffic. Slots cover the six
parse phases plus pool-queue / signal-hop / render-frame for the
Tier 1b and Tier 2 paths.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: PercentileReducer

**Files:**
- Create: `libs/markoff-bench/include/markoff-bench/PercentileReducer.h`
- Create: `libs/markoff-bench/src/PercentileReducer.cpp`
- Create: `libs/markoff-bench/tests/tst_bench_percentile.cpp`
- Modify: `libs/markoff-bench/CMakeLists.txt`
- Modify: `libs/markoff-bench/tests/CMakeLists.txt`

Computes p50, p95, p99, max, min, and mean over a `std::vector<quint64>` using `std::nth_element` (O(n) per percentile, three calls = ~3n). For the max/min we just `std::max_element`/`std::min_element`. Mean is included because it's free.

- [ ] **Step 4.1: Write the failing test**

Create `libs/markoff-bench/tests/tst_bench_percentile.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/PercentileReducer.h>

#include <vector>
#include <numeric>

class TstBenchPercentile : public QObject {
    Q_OBJECT
private slots:
    void empty_input_yields_zeros();
    void single_value();
    void monotonic_sequence();
    void unsorted_does_not_mutate();
};

using namespace Markoff::Bench;

void TstBenchPercentile::empty_input_yields_zeros()
{
    std::vector<quint64> v;
    Distribution d = reducePercentiles(v);
    QCOMPARE(d.count, 0u);
    QCOMPARE(d.p50, 0ull);
    QCOMPARE(d.p95, 0ull);
    QCOMPARE(d.p99, 0ull);
    QCOMPARE(d.max, 0ull);
    QCOMPARE(d.min, 0ull);
}

void TstBenchPercentile::single_value()
{
    std::vector<quint64> v{42};
    Distribution d = reducePercentiles(v);
    QCOMPARE(d.count, 1u);
    QCOMPARE(d.p50, 42ull);
    QCOMPARE(d.p95, 42ull);
    QCOMPARE(d.p99, 42ull);
    QCOMPARE(d.max, 42ull);
    QCOMPARE(d.min, 42ull);
}

void TstBenchPercentile::monotonic_sequence()
{
    std::vector<quint64> v(100);
    std::iota(v.begin(), v.end(), 1);   // 1..100
    Distribution d = reducePercentiles(v);
    QCOMPARE(d.count, 100u);
    QCOMPARE(d.min, 1ull);
    QCOMPARE(d.max, 100ull);
    // Nearest-rank: p50 = ceil(0.50 * 100) = 50th element (1-indexed) = 50
    QCOMPARE(d.p50, 50ull);
    QCOMPARE(d.p95, 95ull);
    QCOMPARE(d.p99, 99ull);
}

void TstBenchPercentile::unsorted_does_not_mutate()
{
    std::vector<quint64> v{3, 1, 4, 1, 5, 9, 2, 6, 5, 3};
    std::vector<quint64> copy = v;
    Distribution d = reducePercentiles(v);
    QCOMPARE(v, copy);                        // input unchanged
    QCOMPARE(d.min, 1ull);
    QCOMPARE(d.max, 9ull);
}

QTEST_GUILESS_MAIN(TstBenchPercentile)
#include "tst_bench_percentile.moc"
```

- [ ] **Step 4.2: Register in tests CMakeLists**

Append to `libs/markoff-bench/tests/CMakeLists.txt`:

```cmake
add_executable(tst_bench_percentile tst_bench_percentile.cpp)
add_test(NAME tst_bench_percentile COMMAND tst_bench_percentile)
target_link_libraries(tst_bench_percentile PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_percentile PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 4.3: Run to verify failure**

Run: `cmake --build build-dev --target tst_bench_percentile -j`
Expected: compile error — header missing.

- [ ] **Step 4.4: Write the header**

Create `libs/markoff-bench/include/markoff-bench/PercentileReducer.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>
#include <vector>

namespace Markoff::Bench {

struct Distribution {
    quint32 count = 0;
    quint64 min   = 0;
    quint64 max   = 0;
    quint64 mean  = 0;
    quint64 p50   = 0;
    quint64 p95   = 0;
    quint64 p99   = 0;
};

/// Compute nearest-rank percentiles, min, max, and arithmetic mean over
/// `samples`. Does NOT mutate the input — internally copies (or move-copies)
/// to a scratch buffer. Returns an all-zeroed Distribution for an empty input.
Distribution reducePercentiles(const std::vector<quint64> &samples);

}  // namespace Markoff::Bench
```

- [ ] **Step 4.5: Write the impl**

Create `libs/markoff-bench/src/PercentileReducer.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/PercentileReducer.h>

#include <algorithm>
#include <numeric>

namespace Markoff::Bench {

namespace {
quint64 nearestRank(std::vector<quint64> &scratch, double pct) {
    // 1-indexed rank, then clamp to [0, n-1] for 0-indexed access.
    const auto n = scratch.size();
    if (n == 0) return 0;
    auto rank = static_cast<size_t>((pct * static_cast<double>(n)) + 0.5);
    if (rank == 0) rank = 1;
    if (rank > n) rank = n;
    const size_t idx = rank - 1;
    std::nth_element(scratch.begin(), scratch.begin() + idx, scratch.end());
    return scratch[idx];
}
}  // namespace

Distribution reducePercentiles(const std::vector<quint64> &samples)
{
    Distribution d;
    if (samples.empty()) return d;

    d.count = static_cast<quint32>(samples.size());
    d.min   = *std::min_element(samples.begin(), samples.end());
    d.max   = *std::max_element(samples.begin(), samples.end());

    quint64 sum = 0;
    for (auto v : samples) sum += v;
    d.mean = sum / d.count;

    std::vector<quint64> scratch = samples;
    d.p50 = nearestRank(scratch, 0.50);
    scratch = samples;
    d.p95 = nearestRank(scratch, 0.95);
    scratch = samples;
    d.p99 = nearestRank(scratch, 0.99);
    return d;
}

}  // namespace Markoff::Bench
```

- [ ] **Step 4.6: Register the source in `libs/markoff-bench/CMakeLists.txt`**

Update the `add_library` block:

```cmake
add_library(markoff_bench STATIC
    src/PhaseTimer.cpp
    src/PercentileReducer.cpp
)
```

- [ ] **Step 4.7: Build + run**

Run:
```
cmake --build build-dev --target tst_bench_percentile -j
ctest --test-dir build-dev -R tst_bench_percentile --output-on-failure
```
Expected: PASS (4 tests).

- [ ] **Step 4.8: Commit**

```
git add libs/markoff-bench/include/markoff-bench/PercentileReducer.h \
        libs/markoff-bench/src/PercentileReducer.cpp \
        libs/markoff-bench/tests/tst_bench_percentile.cpp \
        libs/markoff-bench/CMakeLists.txt \
        libs/markoff-bench/tests/CMakeLists.txt
git commit -m "feat(bench): PercentileReducer (p50/p95/p99 + min/max/mean)

Nearest-rank percentile over std::vector<quint64> via std::nth_element.
Non-mutating; copies to scratch per percentile. Empty input returns a
zeroed Distribution.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: AllocCounter (global new/delete shim, thread-local enable)

**Files:**
- Create: `libs/markoff-bench/include/markoff-bench/AllocCounter.h`
- Create: `libs/markoff-bench/src/AllocCounter.cpp`
- Create: `libs/markoff-bench/src/AllocShim.cpp`
- Create: `libs/markoff-bench/tests/tst_bench_alloc_counter.cpp`
- Modify: `libs/markoff-bench/CMakeLists.txt`
- Modify: `libs/markoff-bench/tests/CMakeLists.txt`

The shim overrides `operator new`/`operator new[]`/`operator delete`/`operator delete[]` (sized + unsized variants) to bump thread-local counters when enabled. The shim is in the static library; it takes effect for any binary that links `markoff_bench`. We accept that test binaries that link `markoff_bench` also get the shim — that's fine because the counters only update when the per-thread enable flag is set.

- [ ] **Step 5.1: Write the failing test**

Create `libs/markoff-bench/tests/tst_bench_alloc_counter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/AllocCounter.h>

#include <string>
#include <vector>

class TstBenchAllocCounter : public QObject {
    Q_OBJECT
private slots:
    void disabled_by_default();
    void counts_when_enabled();
    void scope_guard_resets();
};

using namespace Markoff::Bench;

void TstBenchAllocCounter::disabled_by_default()
{
    AllocSnapshot before = currentAllocSnapshot();
    auto *p = new int(7);
    delete p;
    AllocSnapshot after = currentAllocSnapshot();
    // Disabled → counters unchanged regardless of allocations.
    QCOMPARE(before.bytes, after.bytes);
    QCOMPARE(before.count, after.count);
}

void TstBenchAllocCounter::counts_when_enabled()
{
    {
        AllocCounterScope scope;     // enables + zeros TLS counters
        std::vector<int> v;
        v.reserve(1024);             // ≥ one heap allocation
        AllocSnapshot snap = currentAllocSnapshot();
        QVERIFY2(snap.count >= 1, "expected at least one allocation");
        QVERIFY2(snap.bytes >= sizeof(int) * 1024, "expected ≥4096 bytes counted");
    }
    // Out of scope: disabled again.
    AllocSnapshot snap = currentAllocSnapshot();
    QCOMPARE(snap.count, 0u);
    QCOMPARE(snap.bytes, 0ull);
}

void TstBenchAllocCounter::scope_guard_resets()
{
    {
        AllocCounterScope scope;
        auto *p = new int[64];
        delete[] p;
        AllocSnapshot s1 = currentAllocSnapshot();
        QVERIFY(s1.count >= 1);
    }
    {
        AllocCounterScope scope;     // fresh scope → counters back to zero
        AllocSnapshot s2 = currentAllocSnapshot();
        QCOMPARE(s2.count, 0u);
        QCOMPARE(s2.bytes, 0ull);
    }
}

QTEST_GUILESS_MAIN(TstBenchAllocCounter)
#include "tst_bench_alloc_counter.moc"
```

- [ ] **Step 5.2: Register the test**

Append to `libs/markoff-bench/tests/CMakeLists.txt`:

```cmake
add_executable(tst_bench_alloc_counter tst_bench_alloc_counter.cpp)
add_test(NAME tst_bench_alloc_counter COMMAND tst_bench_alloc_counter)
target_link_libraries(tst_bench_alloc_counter PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_alloc_counter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 5.3: Run to verify failure**

Run: `cmake --build build-dev --target tst_bench_alloc_counter -j`
Expected: compile error.

- [ ] **Step 5.4: Write the header**

Create `libs/markoff-bench/include/markoff-bench/AllocCounter.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

namespace Markoff::Bench {

struct AllocSnapshot {
    quint32 count = 0;
    quint64 bytes = 0;
};

/// Snapshot the current thread's allocation counters. Returns zeros when
/// the counter is not enabled on this thread.
AllocSnapshot currentAllocSnapshot();

/// RAII scope: zeros and enables the per-thread allocation counter on
/// construction; restores the previous enabled state on destruction.
/// Nesting is supported (inner scope sees only its own delta because
/// it zeros on entry; outer scope's count is lost — by design).
class AllocCounterScope {
public:
    AllocCounterScope();
    ~AllocCounterScope();
    AllocCounterScope(const AllocCounterScope &) = delete;
    AllocCounterScope &operator=(const AllocCounterScope &) = delete;
};

namespace Detail {
/// Internal: called from the operator new/delete shim. Linked into every
/// binary that depends on markoff_bench; shim is no-op when disabled.
void recordAlloc(std::size_t bytes) noexcept;
void recordDealloc(std::size_t bytes) noexcept;
}

}  // namespace Markoff::Bench
```

- [ ] **Step 5.5: Write the AllocCounter impl**

Create `libs/markoff-bench/src/AllocCounter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/AllocCounter.h>

namespace Markoff::Bench {

namespace {
thread_local bool    g_enabled = false;
thread_local quint32 g_count   = 0;
thread_local quint64 g_bytes   = 0;
}

namespace Detail {
void recordAlloc(std::size_t bytes) noexcept {
    if (!g_enabled) return;
    ++g_count;
    g_bytes += bytes;
}
void recordDealloc(std::size_t /*bytes*/) noexcept {
    // We track only outbound allocations (peak pressure), not net retained.
    // Deltas of paired alloc+free still show up in `count` and `bytes`.
}
}  // namespace Detail

AllocSnapshot currentAllocSnapshot() {
    AllocSnapshot s;
    s.count = g_count;
    s.bytes = g_bytes;
    return s;
}

AllocCounterScope::AllocCounterScope() {
    g_count = 0;
    g_bytes = 0;
    g_enabled = true;
}

AllocCounterScope::~AllocCounterScope() {
    g_enabled = false;
    g_count   = 0;
    g_bytes   = 0;
}

}  // namespace Markoff::Bench
```

- [ ] **Step 5.6: Write the shim**

Create `libs/markoff-bench/src/AllocShim.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Global operator new / delete override. Compiled into the markoff_bench
// static library. Counters are disabled by default per-thread; enable via
// AllocCounterScope. When disabled, this is one branch + one indirect call
// per allocation — measurable but small.
//
// We deliberately do NOT override the nothrow / aligned / placement variants
// — the foundation/parser don't use them on the hot path.

#include <markoff-bench/AllocCounter.h>

#include <cstdlib>
#include <new>

using Markoff::Bench::Detail::recordAlloc;
using Markoff::Bench::Detail::recordDealloc;

void *operator new(std::size_t n) {
    if (void *p = std::malloc(n)) {
        recordAlloc(n);
        return p;
    }
    throw std::bad_alloc();
}

void *operator new[](std::size_t n) {
    if (void *p = std::malloc(n)) {
        recordAlloc(n);
        return p;
    }
    throw std::bad_alloc();
}

void operator delete(void *p) noexcept {
    recordDealloc(0);
    std::free(p);
}

void operator delete[](void *p) noexcept {
    recordDealloc(0);
    std::free(p);
}

void operator delete(void *p, std::size_t n) noexcept {
    recordDealloc(n);
    std::free(p);
}

void operator delete[](void *p, std::size_t n) noexcept {
    recordDealloc(n);
    std::free(p);
}
```

- [ ] **Step 5.7: Add both sources to `libs/markoff-bench/CMakeLists.txt`**

```cmake
add_library(markoff_bench STATIC
    src/PhaseTimer.cpp
    src/PercentileReducer.cpp
    src/AllocCounter.cpp
    src/AllocShim.cpp
)
```

- [ ] **Step 5.8: Build + run**

Run:
```
cmake --build build-dev --target tst_bench_alloc_counter -j
ctest --test-dir build-dev -R tst_bench_alloc_counter --output-on-failure
```
Expected: PASS (3 tests).

Then run the full bench unit set + foundation tests to confirm no regressions from the global shim:
```
cmake --build build-dev -j
ctest --test-dir build-dev -j -E "tst_realistic|tst_benchmark" --output-on-failure
```
Expected: same green count as Task 1 plus the four bench tests added so far.

If a foundation test starts failing because of the shim, the most likely cause is that some test linked `markoff_bench` transitively (it shouldn't — only the bench tests link it). Confirm via `ldd build-dev/bin/<failing-test>`; the bench shim should not appear in foundation/parser/view-qml tests.

- [ ] **Step 5.9: Commit**

```
git add libs/markoff-bench/include/markoff-bench/AllocCounter.h \
        libs/markoff-bench/src/AllocCounter.cpp \
        libs/markoff-bench/src/AllocShim.cpp \
        libs/markoff-bench/tests/tst_bench_alloc_counter.cpp \
        libs/markoff-bench/CMakeLists.txt \
        libs/markoff-bench/tests/CMakeLists.txt
git commit -m "feat(bench): AllocCounter + global new/delete shim

Per-thread enable flag and counters bumped from a global operator new
override. Disabled by default — bench binaries opt in via
AllocCounterScope around the timed window. Shim lives in markoff_bench
static lib; only binaries that link it (the bench frontends + bench
unit tests) are instrumented.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: CorpusGen (synthetic profiles)

**Files:**
- Create: `libs/markoff-bench/include/markoff-bench/CorpusGen.h`
- Create: `libs/markoff-bench/src/CorpusGen.cpp`
- Create: `libs/markoff-bench/tests/tst_bench_corpus_gen.cpp`
- Modify: `libs/markoff-bench/CMakeLists.txt`
- Modify: `libs/markoff-bench/tests/CMakeLists.txt`

Generates a `QByteArray` of valid markdown matching one of nine named profiles, using a seeded `std::mt19937_64` so output is reproducible. The generator emits paragraphs, code blocks, tables, footnote refs, and list/blockquote nesting in proportions defined per profile.

The exact byte size won't match the requested target perfectly; the contract is "within ±10 % of the requested size, deterministic given the seed".

- [ ] **Step 6.1: Write the failing test**

Create `libs/markoff-bench/tests/tst_bench_corpus_gen.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/CorpusGen.h>

class TstBenchCorpusGen : public QObject {
    Q_OBJECT
private slots:
    void all_profiles_within_size_tolerance_data();
    void all_profiles_within_size_tolerance();

    void deterministic_for_same_seed();
    void differs_for_different_seed();

    void code_heavy_has_code_blocks();
    void table_heavy_has_tables();
    void footnote_heavy_has_footnotes();
};

using namespace Markoff::Bench;

void TstBenchCorpusGen::all_profiles_within_size_tolerance_data() {
    QTest::addColumn<int>("profile");
    QTest::addColumn<qsizetype>("targetMin");
    QTest::addColumn<qsizetype>("targetMax");
    QTest::newRow("tiny")               << int(CorpusProfile::Tiny)              <<       900LL <<       1200LL;
    QTest::newRow("mid_prose")          << int(CorpusProfile::MidProse)          <<     14000LL <<      18000LL;
    QTest::newRow("mid_mixed")          << int(CorpusProfile::MidMixed)          <<     14000LL <<      18000LL;
    QTest::newRow("big_prose")          << int(CorpusProfile::BigProse)          <<     90000LL <<     110000LL;
    QTest::newRow("big_code_heavy")     << int(CorpusProfile::BigCodeHeavy)      <<     90000LL <<     110000LL;
    QTest::newRow("big_table_heavy")    << int(CorpusProfile::BigTableHeavy)     <<     90000LL <<     110000LL;
    QTest::newRow("big_footnote_heavy") << int(CorpusProfile::BigFootnoteHeavy)  <<     90000LL <<     110000LL;
    QTest::newRow("huge")               << int(CorpusProfile::Huge)              <<    450000LL <<     550000LL;
    QTest::newRow("pathological")       << int(CorpusProfile::Pathological)      <<   1800000LL <<    2200000LL;
}

void TstBenchCorpusGen::all_profiles_within_size_tolerance() {
    QFETCH(int, profile);
    QFETCH(qsizetype, targetMin);
    QFETCH(qsizetype, targetMax);
    const QByteArray bytes = generate(static_cast<CorpusProfile>(profile), 0xBEEF);
    QVERIFY2(bytes.size() >= targetMin && bytes.size() <= targetMax,
             qPrintable(QString("size %1 not in [%2, %3]").arg(bytes.size()).arg(targetMin).arg(targetMax)));
}

void TstBenchCorpusGen::deterministic_for_same_seed() {
    auto a = generate(CorpusProfile::MidMixed, 42);
    auto b = generate(CorpusProfile::MidMixed, 42);
    QCOMPARE(a, b);
}

void TstBenchCorpusGen::differs_for_different_seed() {
    auto a = generate(CorpusProfile::MidMixed, 1);
    auto b = generate(CorpusProfile::MidMixed, 2);
    QVERIFY(a != b);
}

void TstBenchCorpusGen::code_heavy_has_code_blocks() {
    auto bytes = generate(CorpusProfile::BigCodeHeavy, 0xBEEF);
    QVERIFY2(bytes.contains("```"), "expected fenced code blocks");
    // Crude proxy: at least 30 fenced code blocks in a 60% code-share doc of ~100KB.
    QVERIFY(bytes.count("```") >= 60);
}

void TstBenchCorpusGen::table_heavy_has_tables() {
    auto bytes = generate(CorpusProfile::BigTableHeavy, 0xBEEF);
    // Pipe tables — header row has at least three pipes per table line.
    QVERIFY2(bytes.contains("\n| "), "expected pipe tables");
}

void TstBenchCorpusGen::footnote_heavy_has_footnotes() {
    auto bytes = generate(CorpusProfile::BigFootnoteHeavy, 0xBEEF);
    QVERIFY2(bytes.contains("[^"), "expected footnote refs");
}

QTEST_GUILESS_MAIN(TstBenchCorpusGen)
#include "tst_bench_corpus_gen.moc"
```

- [ ] **Step 6.2: Register the test**

Append to `libs/markoff-bench/tests/CMakeLists.txt`:

```cmake
add_executable(tst_bench_corpus_gen tst_bench_corpus_gen.cpp)
add_test(NAME tst_bench_corpus_gen COMMAND tst_bench_corpus_gen)
target_link_libraries(tst_bench_corpus_gen PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_corpus_gen PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 6.3: Run to verify failure**

Run: `cmake --build build-dev --target tst_bench_corpus_gen -j`
Expected: compile error.

- [ ] **Step 6.4: Write the header**

Create `libs/markoff-bench/include/markoff-bench/CorpusGen.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace Markoff::Bench {

enum class CorpusProfile : int {
    Tiny              = 0,
    MidProse          = 1,
    MidMixed          = 2,
    BigProse          = 3,
    BigCodeHeavy      = 4,
    BigTableHeavy     = 5,
    BigFootnoteHeavy  = 6,
    Huge              = 7,
    Pathological      = 8,
};

constexpr int kCorpusProfileCount = 9;

/// Stable name for a profile (e.g. "mid_prose"). Used as a JSON key.
const char *profileName(CorpusProfile p);

/// Generate a deterministic markdown document matching the named profile.
/// `seed` parameterises the RNG; same (profile, seed) → byte-identical
/// output. Size is within ±10 % of the profile's target.
QByteArray generate(CorpusProfile profile, quint64 seed);

}  // namespace Markoff::Bench
```

- [ ] **Step 6.5: Write the impl**

Create `libs/markoff-bench/src/CorpusGen.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/CorpusGen.h>

#include <QByteArray>
#include <QString>

#include <random>
#include <array>

namespace Markoff::Bench {

namespace {

struct ProfileSpec {
    const char *name;
    qsizetype   targetBytes;
    double      inlineDensity;     // 0..1: emphasis/links/code-spans per paragraph
    double      codeShare;         // fraction of blocks that are fenced code
    double      tableShare;        // fraction of blocks that are pipe tables
    int         footnoteCount;     // total [^N] references emitted
    int         maxNestingDepth;   // 1 = no nesting; 4 = blockquote+list to 4
};

constexpr std::array<ProfileSpec, kCorpusProfileCount> kProfiles{{
    {"tiny",               1024,        0.05, 0.00, 0.00,   0, 1},
    {"mid_prose",          16  * 1024,  0.30, 0.00, 0.00,   0, 1},
    {"mid_mixed",          16  * 1024,  0.30, 0.20, 0.10,  25, 4},
    {"big_prose",          100 * 1024,  0.30, 0.00, 0.00,   0, 1},
    {"big_code_heavy",     100 * 1024,  0.05, 0.60, 0.00,   0, 1},
    {"big_table_heavy",    100 * 1024,  0.05, 0.00, 0.40,   0, 1},
    {"big_footnote_heavy", 100 * 1024,  0.30, 0.10, 0.00, 200, 1},
    {"huge",               500 * 1024,  0.30, 0.20, 0.10,  50, 4},
    {"pathological",       2000 * 1024, 0.60, 0.30, 0.20, 200, 8},
}};

constexpr const char *kWords[] = {
    "alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel",
    "india", "juliet", "kilo", "lima", "mike", "november", "oscar", "papa",
    "quebec", "romeo", "sierra", "tango", "uniform", "victor", "whiskey",
    "xray", "yankee", "zulu", "tree", "stone", "river", "cloud", "ember",
    "lattice", "iron", "frost", "moss", "harbour", "thread", "ribbon",
};
constexpr int kWordCount = sizeof(kWords) / sizeof(kWords[0]);

class Rng {
public:
    explicit Rng(quint64 seed) : m_engine(seed) {}
    int     uniformInt(int lo, int hi) { std::uniform_int_distribution<int> d(lo, hi); return d(m_engine); }
    double  uniformDbl()               { std::uniform_real_distribution<double> d(0.0, 1.0); return d(m_engine); }
    const char *word()                 { return kWords[uniformInt(0, kWordCount - 1)]; }
private:
    std::mt19937_64 m_engine;
};

void emitWords(QByteArray &out, Rng &rng, int n) {
    for (int i = 0; i < n; ++i) {
        if (i) out.append(' ');
        out.append(rng.word());
    }
}

void emitParagraph(QByteArray &out, Rng &rng, double inlineDensity,
                   int footnoteRemaining, int &footnotesEmitted)
{
    const int wordCount = rng.uniformInt(40, 120);
    int wordsEmitted = 0;
    while (wordsEmitted < wordCount) {
        const int chunk = std::min(rng.uniformInt(6, 12), wordCount - wordsEmitted);
        if (rng.uniformDbl() < inlineDensity) {
            // Random inline decoration.
            const int kind = rng.uniformInt(0, 3);
            if (kind == 0) { out.append("**"); emitWords(out, rng, chunk); out.append("**"); }
            else if (kind == 1) { out.append('*'); emitWords(out, rng, chunk); out.append('*'); }
            else if (kind == 2) { out.append('`'); emitWords(out, rng, chunk); out.append('`'); }
            else {
                out.append('['); emitWords(out, rng, chunk);
                out.append("](https://example.invalid/");
                out.append(rng.word()); out.append(')');
            }
        } else {
            emitWords(out, rng, chunk);
        }
        wordsEmitted += chunk;
        if (wordsEmitted < wordCount) out.append(' ');
        if (footnotesEmitted < footnoteRemaining && rng.uniformDbl() < 0.05) {
            ++footnotesEmitted;
            out.append("[^"); out.append(QByteArray::number(footnotesEmitted)); out.append(']');
        }
    }
    out.append("\n\n");
}

void emitCodeBlock(QByteArray &out, Rng &rng) {
    out.append("```cpp\n");
    const int lines = rng.uniformInt(6, 20);
    for (int i = 0; i < lines; ++i) {
        const int n = rng.uniformInt(3, 8);
        emitWords(out, rng, n);
        out.append(";\n");
    }
    out.append("```\n\n");
}

void emitTable(QByteArray &out, Rng &rng) {
    const int cols = rng.uniformInt(3, 5);
    const int rows = rng.uniformInt(4, 12);
    out.append("|");
    for (int c = 0; c < cols; ++c) { out.append(' '); out.append(rng.word()); out.append(" |"); }
    out.append("\n|");
    for (int c = 0; c < cols; ++c) out.append("---|");
    out.append('\n');
    for (int r = 0; r < rows; ++r) {
        out.append("|");
        for (int c = 0; c < cols; ++c) { out.append(' '); out.append(rng.word()); out.append(" |"); }
        out.append('\n');
    }
    out.append('\n');
}

void emitFootnoteDefinitions(QByteArray &out, Rng &rng, int count) {
    for (int i = 1; i <= count; ++i) {
        out.append("[^"); out.append(QByteArray::number(i)); out.append("]: ");
        emitWords(out, rng, rng.uniformInt(8, 16));
        out.append('\n');
    }
    if (count > 0) out.append('\n');
}

void emitNestedBlockquote(QByteArray &out, Rng &rng, int depth, double inlineDensity) {
    QByteArray prefix;
    for (int i = 0; i < depth; ++i) prefix.append("> ");
    const int lines = rng.uniformInt(2, 5);
    for (int i = 0; i < lines; ++i) {
        out.append(prefix);
        emitWords(out, rng, rng.uniformInt(6, 12));
        if (rng.uniformDbl() < inlineDensity) {
            out.append(" *"); out.append(rng.word()); out.append('*');
        }
        out.append('\n');
    }
    out.append('\n');
}

}  // namespace

const char *profileName(CorpusProfile p) {
    return kProfiles[static_cast<int>(p)].name;
}

QByteArray generate(CorpusProfile profile, quint64 seed) {
    const ProfileSpec &spec = kProfiles[static_cast<int>(profile)];
    Rng rng(seed);
    QByteArray out;
    out.reserve(static_cast<int>(spec.targetBytes + 4096));

    out.append("---\ntitle: bench corpus ");
    out.append(spec.name);
    out.append("\n---\n\n# Synthetic Markdown — ");
    out.append(spec.name);
    out.append("\n\n");

    int footnotesEmitted = 0;
    while (out.size() < spec.targetBytes) {
        const double r = rng.uniformDbl();
        if (r < spec.codeShare) {
            emitCodeBlock(out, rng);
        } else if (r < spec.codeShare + spec.tableShare) {
            emitTable(out, rng);
        } else if (spec.maxNestingDepth > 1 && rng.uniformDbl() < 0.10) {
            const int depth = rng.uniformInt(1, spec.maxNestingDepth);
            emitNestedBlockquote(out, rng, depth, spec.inlineDensity);
        } else {
            emitParagraph(out, rng, spec.inlineDensity, spec.footnoteCount, footnotesEmitted);
        }
    }
    emitFootnoteDefinitions(out, rng, footnotesEmitted);
    return out;
}

}  // namespace Markoff::Bench
```

- [ ] **Step 6.6: Add the source to library CMakeLists**

```cmake
add_library(markoff_bench STATIC
    src/PhaseTimer.cpp
    src/PercentileReducer.cpp
    src/AllocCounter.cpp
    src/AllocShim.cpp
    src/CorpusGen.cpp
)
```

- [ ] **Step 6.7: Build + run**

```
cmake --build build-dev --target tst_bench_corpus_gen -j
ctest --test-dir build-dev -R tst_bench_corpus_gen --output-on-failure
```
Expected: PASS. If a profile size falls outside the tolerance, adjust the per-profile target downward (the generator overshoots slightly because the loop emits one full block past the target). The tolerance bands in the test are intentionally lax (±10 %); deeper tuning is a follow-up.

If `pathological` (2 MB) is too slow under RelWithDebInfo (>2 s for the test), increase the test scenario timeout or split it into its own test with `set_tests_properties(... PROPERTIES TIMEOUT 60)`.

- [ ] **Step 6.8: Commit**

```
git add libs/markoff-bench/include/markoff-bench/CorpusGen.h \
        libs/markoff-bench/src/CorpusGen.cpp \
        libs/markoff-bench/tests/tst_bench_corpus_gen.cpp \
        libs/markoff-bench/CMakeLists.txt \
        libs/markoff-bench/tests/CMakeLists.txt
git commit -m "feat(bench): CorpusGen — 9 named synthetic profiles

Deterministic generator producing valid markdown matching one of nine
named profiles (tiny → pathological), parameterised on inline density,
code/table share, footnote count, and nesting depth. Same (profile,
seed) yields byte-identical output. Within ±10 % of target size.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: FixtureLoader + real-doc fixtures

**Files:**
- Create: `libs/markoff-bench/include/markoff-bench/FixtureLoader.h`
- Create: `libs/markoff-bench/src/FixtureLoader.cpp`
- Create: `libs/markoff-bench/fixtures/foundation-design.md` (copy of spec)
- Create: `libs/markoff-bench/fixtures/typing-perf-plan.md` (copy of plan)
- Create: `libs/markoff-bench/tests/tst_bench_fixture_loader.cpp`
- Modify: `libs/markoff-bench/CMakeLists.txt`
- Modify: `libs/markoff-bench/tests/CMakeLists.txt`

The loader uses `MARKOFF_BENCH_FIXTURE_DIR` (compile-time define from CMake) to find the fixtures at runtime. Fixtures are committed copies, not symlinks, so the bench is portable across hosts.

- [ ] **Step 7.1: Copy fixtures into place**

```
cp docs/specs/2026-04-28-foundation-design.md libs/markoff-bench/fixtures/foundation-design.md
cp docs/plans/2026-04-28-typing-perf.md libs/markoff-bench/fixtures/typing-perf-plan.md
```

(The CommonMark spec fixture, called for in the design's "future work", is deliberately omitted in this plan to keep the repo footprint smaller. Add later if needed.)

- [ ] **Step 7.2: Write the failing test**

Create `libs/markoff-bench/tests/tst_bench_fixture_loader.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/FixtureLoader.h>

class TstBenchFixtureLoader : public QObject {
    Q_OBJECT
private slots:
    void loads_foundation_design();
    void loads_typing_perf_plan();
    void unknown_name_returns_empty();
};

using namespace Markoff::Bench;

void TstBenchFixtureLoader::loads_foundation_design() {
    const QByteArray bytes = loadFixture("foundation-design");
    QVERIFY2(!bytes.isEmpty(), "fixture should not be empty");
    QVERIFY(bytes.contains("foundation"));
}

void TstBenchFixtureLoader::loads_typing_perf_plan() {
    const QByteArray bytes = loadFixture("typing-perf-plan");
    QVERIFY(!bytes.isEmpty());
    QVERIFY(bytes.contains("perf"));
}

void TstBenchFixtureLoader::unknown_name_returns_empty() {
    const QByteArray bytes = loadFixture("does-not-exist");
    QVERIFY(bytes.isEmpty());
}

QTEST_GUILESS_MAIN(TstBenchFixtureLoader)
#include "tst_bench_fixture_loader.moc"
```

- [ ] **Step 7.3: Register the test**

```cmake
add_executable(tst_bench_fixture_loader tst_bench_fixture_loader.cpp)
add_test(NAME tst_bench_fixture_loader COMMAND tst_bench_fixture_loader)
target_link_libraries(tst_bench_fixture_loader PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_fixture_loader PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 7.4: Run to verify failure**

Run: `cmake --build build-dev --target tst_bench_fixture_loader -j`
Expected: compile error.

- [ ] **Step 7.5: Write the header**

Create `libs/markoff-bench/include/markoff-bench/FixtureLoader.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QStringList>

namespace Markoff::Bench {

/// Load a real-doc fixture from libs/markoff-bench/fixtures/<name>.md.
/// Returns the file's bytes, or an empty QByteArray if the file does
/// not exist or cannot be read. Logs a warning to qWarning() on failure.
QByteArray loadFixture(const QString &name);

/// Names of all available fixtures (without the .md suffix).
QStringList availableFixtures();

}  // namespace Markoff::Bench
```

- [ ] **Step 7.6: Write the impl**

Create `libs/markoff-bench/src/FixtureLoader.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/FixtureLoader.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

namespace Markoff::Bench {

#ifndef MARKOFF_BENCH_FIXTURE_DIR
#  error "MARKOFF_BENCH_FIXTURE_DIR not defined; set via CMake target_compile_definitions"
#endif

namespace {
Q_LOGGING_CATEGORY(lcFixture, "markoff.bench.fixture")
}

QByteArray loadFixture(const QString &name) {
    const QString path = QStringLiteral("%1/%2.md").arg(QStringLiteral(MARKOFF_BENCH_FIXTURE_DIR), name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcFixture).noquote() << "fixture not found:" << path;
        return {};
    }
    return f.readAll();
}

QStringList availableFixtures() {
    QDir d(QStringLiteral(MARKOFF_BENCH_FIXTURE_DIR));
    QStringList out;
    const auto entries = d.entryInfoList({QStringLiteral("*.md")}, QDir::Files);
    for (const auto &e : entries) out.append(e.completeBaseName());
    return out;
}

}  // namespace Markoff::Bench
```

- [ ] **Step 7.7: Add the source**

Update `libs/markoff-bench/CMakeLists.txt`:

```cmake
add_library(markoff_bench STATIC
    src/PhaseTimer.cpp
    src/PercentileReducer.cpp
    src/AllocCounter.cpp
    src/AllocShim.cpp
    src/CorpusGen.cpp
    src/FixtureLoader.cpp
)
```

- [ ] **Step 7.8: Build + run**

```
cmake --build build-dev --target tst_bench_fixture_loader -j
ctest --test-dir build-dev -R tst_bench_fixture_loader --output-on-failure
```
Expected: PASS (3 tests).

- [ ] **Step 7.9: Commit**

```
git add libs/markoff-bench/include/markoff-bench/FixtureLoader.h \
        libs/markoff-bench/src/FixtureLoader.cpp \
        libs/markoff-bench/fixtures/foundation-design.md \
        libs/markoff-bench/fixtures/typing-perf-plan.md \
        libs/markoff-bench/tests/tst_bench_fixture_loader.cpp \
        libs/markoff-bench/CMakeLists.txt \
        libs/markoff-bench/tests/CMakeLists.txt
git commit -m "feat(bench): FixtureLoader + real-doc fixtures

Loads foundation-design.md and typing-perf-plan.md from a compile-time
fixture path. Two real-doc fixtures committed (copies of in-repo
specs/plans), giving a 'would the user notice?' check alongside the
synthetic corpus.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Scenario step builders

**Files:**
- Create: `libs/markoff-bench/include/markoff-bench/Scenario.h`
- Create: `libs/markoff-bench/src/Scenario.cpp`
- Create: `libs/markoff-bench/tests/tst_bench_scenario.cpp`
- Modify: `libs/markoff-bench/CMakeLists.txt`
- Modify: `libs/markoff-bench/tests/CMakeLists.txt`

A `Scenario` is a recipe for producing a sequence of `MarkoffEdit` objects against a starting `QByteArray` document. Each scenario knows its iteration count, warmup count, and step generator. The runner asks the scenario for the i-th edit; the scenario is **stateless** beyond what the runner gives back (the runner tracks the current document state). Each step's edit must be valid against the runner's current document state — for `type_middle` we therefore can't precompute random offsets relative to the original doc; the scenario instead exposes a strategy that the runner queries per-iter with the current doc length.

- [ ] **Step 8.1: Write the failing test**

Create `libs/markoff-bench/tests/tst_bench_scenario.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/Scenario.h>

class TstBenchScenario : public QObject {
    Q_OBJECT
private slots:
    void scenario_meta_table();
    void type_end_appends_one_byte_per_iter();
    void type_start_inserts_at_offset_zero();
    void block_boundary_inserts_blank_line();
    void paste_4kb_makes_4096_byte_insert();
    void replace_1kb_deletes_and_inserts();
    void cold_parse_emits_no_steps();
};

using namespace Markoff;
using namespace Markoff::Bench;

void TstBenchScenario::scenario_meta_table() {
    const ScenarioMeta m = scenarioMeta(ScenarioKind::TypeEnd);
    QCOMPARE(QString(m.name), QString("type_end"));
    QCOMPARE(m.warmupIters, 20);
    QCOMPARE(m.measuredIters, 180);
}

void TstBenchScenario::type_end_appends_one_byte_per_iter() {
    QByteArray doc("hello\n");
    const MarkoffEdit e = nextStep(ScenarioKind::TypeEnd, doc, 0, /*seed*/ 1);
    QCOMPARE(e.oldStart, quint32(doc.size()));
    QCOMPARE(e.oldEnd,   quint32(doc.size()));
    QCOMPARE(e.newText.size(), 1);
}

void TstBenchScenario::type_start_inserts_at_offset_zero() {
    QByteArray doc("hello\n");
    const MarkoffEdit e = nextStep(ScenarioKind::TypeStart, doc, 0, /*seed*/ 1);
    QCOMPARE(e.oldStart, 0u);
    QCOMPARE(e.oldEnd,   0u);
    QCOMPARE(e.newText.size(), 1);
}

void TstBenchScenario::block_boundary_inserts_blank_line() {
    QByteArray doc("Para A.\n\nPara B.\n");
    const MarkoffEdit e = nextStep(ScenarioKind::BlockBoundary, doc, 0, /*seed*/ 1);
    QCOMPARE(e.oldStart, e.oldEnd);                    // pure insertion
    QVERIFY(e.newText == QByteArray("\n") || e.newText == QByteArray("\n\n"));
}

void TstBenchScenario::paste_4kb_makes_4096_byte_insert() {
    QByteArray doc("hello\n");
    const MarkoffEdit e = nextStep(ScenarioKind::Paste4Kb, doc, 0, /*seed*/ 1);
    QCOMPARE(e.oldStart, e.oldEnd);
    QCOMPARE(e.newText.size(), 4096);
}

void TstBenchScenario::replace_1kb_deletes_and_inserts() {
    QByteArray doc(2048, 'x');
    doc.append('\n');
    const MarkoffEdit e = nextStep(ScenarioKind::Replace1Kb, doc, 0, /*seed*/ 1);
    QCOMPARE(e.oldEnd - e.oldStart, 1024u);
    QCOMPARE(e.newText.size(), 1024);
}

void TstBenchScenario::cold_parse_emits_no_steps() {
    const ScenarioMeta m = scenarioMeta(ScenarioKind::ColdParse);
    QCOMPARE(m.measuredIters, 1);
    QCOMPARE(m.warmupIters, 0);
}

QTEST_GUILESS_MAIN(TstBenchScenario)
#include "tst_bench_scenario.moc"
```

- [ ] **Step 8.2: Register the test**

```cmake
add_executable(tst_bench_scenario tst_bench_scenario.cpp)
add_test(NAME tst_bench_scenario COMMAND tst_bench_scenario)
target_link_libraries(tst_bench_scenario PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_scenario PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 8.3: Run to verify failure**

Run: `cmake --build build-dev --target tst_bench_scenario -j`
Expected: compile error.

- [ ] **Step 8.4: Write the header**

Create `libs/markoff-bench/include/markoff-bench/Scenario.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <markoff/core/MarkoffEdit.h>

namespace Markoff::Bench {

enum class ScenarioKind : int {
    ColdParse       = 0,
    TypeEnd         = 1,
    TypeStart       = 2,
    TypeMiddle      = 3,
    BlockBoundary   = 4,
    Paste4Kb        = 5,
    Replace1Kb      = 6,
};

constexpr int kScenarioCount = 7;

struct ScenarioMeta {
    const char *name;
    int         warmupIters;
    int         measuredIters;
};

ScenarioMeta scenarioMeta(ScenarioKind kind);

/// Produce the i-th edit for this scenario against `currentDoc`.
/// `iterIndex` is 0-based and counts both warmup and measured iters.
/// `seed` is mixed into the iteration so two different bench runs with
/// different seeds produce different per-iter byte payloads.
///
/// For ColdParse the function returns an empty (no-op) edit; the runner
/// is expected to skip nextStep() entirely for that scenario.
Markoff::MarkoffEdit
nextStep(ScenarioKind kind, const QByteArray &currentDoc, int iterIndex, quint64 seed);

}  // namespace Markoff::Bench
```

- [ ] **Step 8.5: Write the impl**

Create `libs/markoff-bench/src/Scenario.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/Scenario.h>

#include <array>
#include <random>

namespace Markoff::Bench {

namespace {
constexpr std::array<ScenarioMeta, kScenarioCount> kMeta{{
    {"cold_parse",     0,   1},
    {"type_end",      20, 180},
    {"type_start",    20, 180},
    {"type_middle",   20, 180},
    {"block_boundary", 5,  50},
    {"paste_4kb",      5,  20},
    {"replace_1kb",    5,  20},
}};

quint64 mixSeed(quint64 seed, int iterIndex) {
    return seed ^ (static_cast<quint64>(iterIndex) * 0x9E3779B97F4A7C15ull);
}

char pickPrintableChar(std::mt19937_64 &eng) {
    std::uniform_int_distribution<int> d(static_cast<int>('a'), static_cast<int>('z'));
    return static_cast<char>(d(eng));
}

QByteArray makeBlob(std::mt19937_64 &eng, int n) {
    QByteArray out;
    out.resize(n);
    for (int i = 0; i < n; ++i) out[i] = pickPrintableChar(eng);
    return out;
}

}  // namespace

ScenarioMeta scenarioMeta(ScenarioKind kind) {
    return kMeta[static_cast<int>(kind)];
}

Markoff::MarkoffEdit
nextStep(ScenarioKind kind, const QByteArray &doc, int iterIndex, quint64 seed)
{
    Markoff::MarkoffEdit e;
    std::mt19937_64 eng(mixSeed(seed, iterIndex));
    const quint32 docSize = static_cast<quint32>(doc.size());

    switch (kind) {
    case ScenarioKind::ColdParse:
        return e;       // no-op — runner does not call this for ColdParse

    case ScenarioKind::TypeEnd: {
        e.oldStart = docSize;
        e.oldEnd   = docSize;
        e.newText  = QByteArray(1, pickPrintableChar(eng));
        return e;
    }
    case ScenarioKind::TypeStart: {
        e.oldStart = 0;
        e.oldEnd   = 0;
        e.newText  = QByteArray(1, pickPrintableChar(eng));
        return e;
    }
    case ScenarioKind::TypeMiddle: {
        std::uniform_int_distribution<quint32> d(0, docSize);
        const quint32 off = d(eng);
        e.oldStart = off;
        e.oldEnd   = off;
        e.newText  = QByteArray(1, pickPrintableChar(eng));
        return e;
    }
    case ScenarioKind::BlockBoundary: {
        // Find a "\n\n" boundary near a random offset; insert one '\n'
        // after it (turns a paragraph break into a blank-line-then-break).
        std::uniform_int_distribution<int> d(0, std::max(0, doc.size() - 16));
        const int target = d(eng);
        const int idx = doc.indexOf("\n\n", target);
        const quint32 at = (idx >= 0) ? static_cast<quint32>(idx + 1) : docSize;
        e.oldStart = at;
        e.oldEnd   = at;
        e.newText  = QByteArray("\n");
        return e;
    }
    case ScenarioKind::Paste4Kb: {
        e.oldStart = docSize;
        e.oldEnd   = docSize;
        e.newText  = makeBlob(eng, 4096);
        return e;
    }
    case ScenarioKind::Replace1Kb: {
        const int span = 1024;
        const int maxStart = std::max(0, doc.size() - span);
        std::uniform_int_distribution<int> d(0, maxStart);
        const quint32 start = static_cast<quint32>(d(eng));
        e.oldStart = start;
        e.oldEnd   = start + span;
        e.newText  = makeBlob(eng, span);
        return e;
    }
    }
    return e;
}

}  // namespace Markoff::Bench
```

- [ ] **Step 8.6: Add the source**

```cmake
add_library(markoff_bench STATIC
    src/PhaseTimer.cpp
    src/PercentileReducer.cpp
    src/AllocCounter.cpp
    src/AllocShim.cpp
    src/CorpusGen.cpp
    src/FixtureLoader.cpp
    src/Scenario.cpp
)
```

- [ ] **Step 8.7: Build + run**

```
cmake --build build-dev --target tst_bench_scenario -j
ctest --test-dir build-dev -R tst_bench_scenario --output-on-failure
```
Expected: PASS (7 tests).

- [ ] **Step 8.8: Commit**

```
git add libs/markoff-bench/include/markoff-bench/Scenario.h \
        libs/markoff-bench/src/Scenario.cpp \
        libs/markoff-bench/tests/tst_bench_scenario.cpp \
        libs/markoff-bench/CMakeLists.txt \
        libs/markoff-bench/tests/CMakeLists.txt
git commit -m "feat(bench): Scenario step builders (7 scenario kinds)

Stateless per-iter MarkoffEdit producer for cold_parse, type_end,
type_start, type_middle, block_boundary, paste_4kb, replace_1kb.
Iteration index + seed determine the per-iter byte payload. Scenarios
are stateless beyond what the runner provides (current doc bytes);
runner is responsible for tracking doc state across iters.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: ScenarioRunner — Tier 1 (direct parser path)

**Files:**
- Create: `libs/markoff-bench/include/markoff-bench/ScenarioRunner.h`
- Create: `libs/markoff-bench/src/ScenarioRunner.cpp`
- Modify: `libs/markoff-bench/CMakeLists.txt`

This task adds the direct-parse path. Tier 1b (pool path) is a separate task. The runner exposes a `RunResult` struct holding distributions per phase + reuse counts + alloc snapshot; consumers reduce/print externally.

The runner drives `Markoff::Parse::Detail::IncrementalParseSession` which lives in `libs/markoff-core/src/IncrementalParseSession.h`. Access is via the `PRIVATE` include path we set up in Task 2 — only this library can reach it.

There's no separate test for ScenarioRunner; the smoke target in Task 12 exercises it end-to-end.

- [ ] **Step 9.1: Write the header**

Create `libs/markoff-bench/include/markoff-bench/ScenarioRunner.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <vector>

#include <markoff-bench/AllocCounter.h>
#include <markoff-bench/CorpusGen.h>
#include <markoff-bench/PercentileReducer.h>
#include <markoff-bench/PhaseTimer.h>
#include <markoff-bench/Scenario.h>

namespace Markoff::Bench {

enum class Tier : int {
    DirectParse = 0,    // Tier 1
    PoolParse   = 1,    // Tier 1b — added in Task 10
    Render      = 2,    // Tier 2  — added in Task 14
};

struct RunResult {
    // Identification
    const char *profileName    = "";
    const char *fixtureName    = "";   // mutually exclusive with profileName
    const char *scenarioName   = "";
    Tier        tier           = Tier::DirectParse;

    // Iteration counts
    int         iterations     = 0;
    int         warmupIters    = 0;

    // Per-phase wall-time (ns) — one Distribution per Phase slot.
    std::array<Distribution, kPhaseCount> phases{};

    // Wall-time per iteration overall (sum across all phases).
    Distribution totalNs{};

    // Reuse counts (Tier 1/1b only). Block: bytes covered by changed-ranges
    // (lower = more reuse). Inline: count of inline trees reused.
    Distribution blockChangedBytes{};
    Distribution inlineReuseCount{};

    // Allocations during the timed window (Tier 1/1b only).
    Distribution allocBytes{};
    Distribution allocCount{};
};

/// Run a scenario at Tier 1 (direct parser). `corpus` is the starting
/// document bytes; the scenario advances it edit-by-edit. Returns
/// percentile-reduced metrics across the measured iters (warmup excluded).
RunResult runDirectParse(const QByteArray &corpus,
                         ScenarioKind scenario,
                         quint64 seed);

}  // namespace Markoff::Bench
```

- [ ] **Step 9.2: Write the impl**

Create `libs/markoff-bench/src/ScenarioRunner.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/ScenarioRunner.h>

#include <markoff-parser/Document.h>
#include <markoff-parser/TreeSitterParser.h>

// Foundation-internal — accessible because markoff_bench has PRIVATE
// include access into libs/markoff-core/src/.
#include <IncrementalParseSession.h>

namespace Markoff::Bench {

namespace {

QByteArray applyEditToBuffer(const QByteArray &doc, const Markoff::MarkoffEdit &e) {
    QByteArray out;
    out.reserve(doc.size() - (int(e.oldEnd) - int(e.oldStart)) + e.newText.size());
    out.append(doc.constData(), e.oldStart);
    out.append(e.newText);
    out.append(doc.constData() + e.oldEnd, doc.size() - int(e.oldEnd));
    return out;
}

struct PerIter {
    PhaseTable     phases{};
    quint64        totalNs            = 0;
    int            blockChangedBytes  = -1;
    int            inlineReuseCount   = 0;
    AllocSnapshot  alloc{};
};

PerIter timeOneIter(Markoff::Parse::Detail::IncrementalParseSession &session,
                    const QByteArray &newDoc)
{
    PerIter iter;

    AllocCounterScope allocScope;

    const auto t0 = std::chrono::steady_clock::now();

    // We can't sub-time inside applyEdit() without modifying the
    // IncrementalParseSession source — instead we capture the total
    // wall-time of the call here and emit phase splits via an
    // instrumented internal API. For Phase-0 of this plan, total-only
    // is acceptable; per-phase splits come in a follow-up if a profile
    // shows phase-attribution is needed.
    {
        PhaseTimer guard(iter.phases, Phase::ParseBlock);  // single bucket for total parse cost
        session.applyEdit(QString::fromUtf8(newDoc));
    }

    const auto t1 = std::chrono::steady_clock::now();
    iter.totalNs = static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

    // Reuse counters — pull from the snapshot's parser. The session
    // doesn't expose its parser directly; we add the {Block,Inline}Reuse
    // accessors in a follow-up if needed. For now we read what the
    // Document snapshot exposes.
    auto snap = session.snapshot();
    Q_UNUSED(snap);

    iter.alloc = currentAllocSnapshot();
    return iter;
}

}  // namespace

RunResult runDirectParse(const QByteArray &corpus, ScenarioKind scenario, quint64 seed) {
    const ScenarioMeta meta = scenarioMeta(scenario);

    Markoff::Parse::Detail::IncrementalParseSession session;
    session.reset(QString::fromUtf8(corpus));

    QByteArray currentDoc = corpus;

    std::vector<quint64> totalNs;
    std::array<std::vector<quint64>, kPhaseCount> phaseSamples;
    std::vector<quint64> blockChangedBytes;
    std::vector<quint64> inlineReuse;
    std::vector<quint64> allocBytes;
    std::vector<quint64> allocCount;

    const int totalIters = meta.warmupIters + meta.measuredIters;

    if (scenario == ScenarioKind::ColdParse) {
        // Single-shot: time a fresh reset() against the corpus.
        Markoff::Parse::Detail::IncrementalParseSession s;
        AllocCounterScope allocScope;
        const auto t0 = std::chrono::steady_clock::now();
        s.reset(QString::fromUtf8(corpus));
        const auto t1 = std::chrono::steady_clock::now();
        const auto ns = static_cast<quint64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        totalNs.push_back(ns);
        const auto alloc = currentAllocSnapshot();
        allocBytes.push_back(alloc.bytes);
        allocCount.push_back(alloc.count);
    } else {
        for (int i = 0; i < totalIters; ++i) {
            const Markoff::MarkoffEdit edit = nextStep(scenario, currentDoc, i, seed);
            currentDoc = applyEditToBuffer(currentDoc, edit);

            const PerIter iter = timeOneIter(session, currentDoc);

            if (i >= meta.warmupIters) {
                totalNs.push_back(iter.totalNs);
                for (int p = 0; p < kPhaseCount; ++p) phaseSamples[p].push_back(iter.phases[p]);
                allocBytes.push_back(iter.alloc.bytes);
                allocCount.push_back(iter.alloc.count);
                // blockChangedBytes / inlineReuseCount: see follow-up note
                // in Step 9.1 — the session does not yet expose these. We
                // emit zeros for now; instrumentation lands in Task 11
                // when the JsonReporter wires the fields.
                blockChangedBytes.push_back(0);
                inlineReuse.push_back(0);
            }
        }
    }

    RunResult r;
    r.profileName    = "";
    r.fixtureName    = "";
    r.scenarioName   = meta.name;
    r.tier           = Tier::DirectParse;
    r.iterations     = static_cast<int>(totalNs.size());
    r.warmupIters    = (scenario == ScenarioKind::ColdParse) ? 0 : meta.warmupIters;
    for (int p = 0; p < kPhaseCount; ++p) r.phases[p] = reducePercentiles(phaseSamples[p]);
    r.totalNs            = reducePercentiles(totalNs);
    r.blockChangedBytes  = reducePercentiles(blockChangedBytes);
    r.inlineReuseCount   = reducePercentiles(inlineReuse);
    r.allocBytes         = reducePercentiles(allocBytes);
    r.allocCount         = reducePercentiles(allocCount);
    return r;
}

}  // namespace Markoff::Bench
```

**Note (recorded for Task 11):** `IncrementalParseSession` does not currently expose its `TreeSitterParser` member, so the runner can't read `blockChangedByteCount()` / `inlineTreeReuseCount()` directly. Three options for Task 11:
  - Add `const TreeSitterParser &parser() const` to `IncrementalParseSession.h`.
  - Refactor the runner to drive `TreeSitterParser` directly (skipping the session) for these specific counters.
  - Leave the counters as zeros and document the gap.

Task 11 picks option 1 (one-line accessor). Recorded here so the implementer doesn't lose the thread.

- [ ] **Step 9.3: Add the source**

```cmake
add_library(markoff_bench STATIC
    src/PhaseTimer.cpp
    src/PercentileReducer.cpp
    src/AllocCounter.cpp
    src/AllocShim.cpp
    src/CorpusGen.cpp
    src/FixtureLoader.cpp
    src/Scenario.cpp
    src/ScenarioRunner.cpp
)
```

- [ ] **Step 9.4: Build to confirm it compiles**

Run: `cmake --build build-dev --target markoff_bench -j`
Expected: clean compile. (No new test target yet — runner is exercised via Task 12's smoke.)

- [ ] **Step 9.5: Commit**

```
git add libs/markoff-bench/include/markoff-bench/ScenarioRunner.h \
        libs/markoff-bench/src/ScenarioRunner.cpp \
        libs/markoff-bench/CMakeLists.txt
git commit -m "feat(bench): ScenarioRunner — Tier 1 direct-parse path

Drives IncrementalParseSession edit-by-edit, captures per-iter wall
time + alloc bytes/count, reduces to p50/p95/p99/max via
PercentileReducer. ColdParse is one-shot; typing scenarios run
warmup+measured iters and discard the warmup window. Reuse counters
emit zeros for now — accessor added in a follow-up task.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: ScenarioRunner — Tier 1b (pool / signal path)

**Files:**
- Modify: `libs/markoff-bench/include/markoff-bench/ScenarioRunner.h`
- Modify: `libs/markoff-bench/src/ScenarioRunner.cpp`

This task adds `runPoolParse()` which drives `MarkoffDocument::applyLocalEdit` against a real `MarkoffDocument` instance, waits synchronously for the `parseUpdated` signal, and times the round-trip including pool queue + signal hop. We use `QSignalSpy` with a deadline and a `QEventLoop`.

- [ ] **Step 10.1: Add the function declaration to the header**

In `libs/markoff-bench/include/markoff-bench/ScenarioRunner.h`, after `runDirectParse`:

```cpp
/// Run a scenario at Tier 1b (MarkoffDocument + ParsePool + signal hop).
/// Each iteration: applyLocalEdit, then run the event loop until
/// parseUpdated fires (or 5s deadline). Captures the same metrics as
/// runDirectParse plus PoolQueue + SignalHop phase splits.
RunResult runPoolParse(const QByteArray &corpus,
                       ScenarioKind scenario,
                       quint64 seed);
```

- [ ] **Step 10.2: Write the impl**

Append to `libs/markoff-bench/src/ScenarioRunner.cpp`:

```cpp
#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffEdit.h>

namespace Markoff::Bench {

namespace {
quint64 waitForParseUpdated(Markoff::MarkoffDocument *doc, int timeoutMs) {
    QSignalSpy spy(doc, &Markoff::MarkoffDocument::parseUpdated);
    const auto t0 = std::chrono::steady_clock::now();
    if (!spy.wait(timeoutMs)) {
        qWarning("bench: parseUpdated did not fire within %d ms", timeoutMs);
        return 0;
    }
    const auto t1 = std::chrono::steady_clock::now();
    return static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}
}  // namespace

RunResult runPoolParse(const QByteArray &corpus, ScenarioKind scenario, quint64 seed) {
    const ScenarioMeta meta = scenarioMeta(scenario);

    // Caller is responsible for ensuring a QCoreApplication exists (the
    // CLI frontend constructs one).
    Q_ASSERT(QCoreApplication::instance() != nullptr);

    Markoff::MarkoffDocument doc(/*replicaId*/ 1);
    doc.resetContent(corpus, Markoff::Origin::Local);
    waitForParseUpdated(&doc, 30'000);   // initial parse can be slow on huge corpora

    QByteArray currentDoc = corpus;
    std::vector<quint64> totalNs;
    std::vector<quint64> waitNs;             // PoolQueue + SignalHop combined
    std::vector<quint64> allocBytes;
    std::vector<quint64> allocCount;

    const int totalIters = meta.warmupIters + meta.measuredIters;

    if (scenario == ScenarioKind::ColdParse) {
        // For ColdParse at Tier 1b: rebuild the doc and wait for parseUpdated.
        Markoff::MarkoffDocument fresh(/*replicaId*/ 2);
        AllocCounterScope allocScope;
        const auto t0 = std::chrono::steady_clock::now();
        fresh.resetContent(corpus, Markoff::Origin::Local);
        const quint64 wait = waitForParseUpdated(&fresh, 30'000);
        const auto t1 = std::chrono::steady_clock::now();
        const auto ns = static_cast<quint64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        totalNs.push_back(ns);
        waitNs.push_back(wait);
        const auto alloc = currentAllocSnapshot();
        allocBytes.push_back(alloc.bytes);
        allocCount.push_back(alloc.count);
    } else {
        for (int i = 0; i < totalIters; ++i) {
            const Markoff::MarkoffEdit edit = nextStep(scenario, currentDoc, i, seed);
            currentDoc = applyEditToBuffer(currentDoc, edit);

            AllocCounterScope allocScope;
            const auto t0 = std::chrono::steady_clock::now();
            doc.applyLocalEdit({edit});
            const quint64 wait = waitForParseUpdated(&doc, 5'000);
            const auto t1 = std::chrono::steady_clock::now();

            if (i >= meta.warmupIters) {
                totalNs.push_back(static_cast<quint64>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
                waitNs.push_back(wait);
                const auto alloc = currentAllocSnapshot();
                allocBytes.push_back(alloc.bytes);
                allocCount.push_back(alloc.count);
            }
        }
    }

    RunResult r;
    r.scenarioName = meta.name;
    r.tier         = Tier::PoolParse;
    r.iterations   = static_cast<int>(totalNs.size());
    r.warmupIters  = (scenario == ScenarioKind::ColdParse) ? 0 : meta.warmupIters;
    r.totalNs      = reducePercentiles(totalNs);
    // Lump PoolQueue + SignalHop into the SignalHop slot; finer-grained
    // split would require instrumentation in MarkoffDocument itself.
    r.phases[static_cast<int>(Phase::SignalHop)] = reducePercentiles(waitNs);
    r.allocBytes   = reducePercentiles(allocBytes);
    r.allocCount   = reducePercentiles(allocCount);
    return r;
}

}  // namespace Markoff::Bench
```

- [ ] **Step 10.3: Build**

Run: `cmake --build build-dev --target markoff_bench -j`
Expected: clean compile.

- [ ] **Step 10.4: Commit**

```
git add libs/markoff-bench/include/markoff-bench/ScenarioRunner.h \
        libs/markoff-bench/src/ScenarioRunner.cpp
git commit -m "feat(bench): ScenarioRunner — Tier 1b pool/signal path

runPoolParse drives MarkoffDocument::applyLocalEdit and waits
synchronously on the parseUpdated signal via QSignalSpy. Captures
per-iter total wall time (which includes pool queue + worker parse +
signal hop) and emits the wait portion into the SignalHop phase slot.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: Wire reuse counters; JsonReporter

**Files:**
- Modify: `libs/markoff-core/src/IncrementalParseSession.h`
- Modify: `libs/markoff-bench/src/ScenarioRunner.cpp`
- Create: `libs/markoff-bench/include/markoff-bench/JsonReporter.h`
- Create: `libs/markoff-bench/src/JsonReporter.cpp`
- Modify: `libs/markoff-bench/CMakeLists.txt`

Two sub-changes: (a) expose `IncrementalParseSession::parser()` so the runner can read reuse counters, (b) add `JsonReporter` that converts `RunResult` → `QJsonObject`.

- [ ] **Step 11.1: Expose `parser()` on `IncrementalParseSession`**

In `libs/markoff-core/src/IncrementalParseSession.h`, add to the public section:

```cpp
    /// Read-only access to the underlying TreeSitterParser. Used by the
    /// in-tree benchmark to read inline-tree-reuse and block-changed-bytes
    /// counters. Not part of the public foundation API.
    const Markoff::TreeSitterParser &parser() const { return m_parser; }
```

- [ ] **Step 11.2: Read the counters from the runner**

In `libs/markoff-bench/src/ScenarioRunner.cpp`, replace the `blockChangedBytes.push_back(0)` and `inlineReuse.push_back(0)` lines in `runDirectParse` with:

```cpp
            const int changed = session.parser().blockChangedByteCount();
            const int inlineR = session.parser().inlineTreeReuseCount();
            blockChangedBytes.push_back(changed < 0 ? 0u : static_cast<quint64>(changed));
            inlineReuse.push_back(static_cast<quint64>(inlineR));
```

- [ ] **Step 11.3: Build to confirm**

Run: `cmake --build build-dev --target markoff_bench -j`
Expected: clean.

- [ ] **Step 11.4: Write JsonReporter header**

Create `libs/markoff-bench/include/markoff-bench/JsonReporter.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QString>

#include <markoff-bench/ScenarioRunner.h>

namespace Markoff::Bench {

/// Convert one RunResult to a JSON object matching schema_version=1.
QJsonObject toJson(const RunResult &r);

/// Wrap a list of RunResult objects into a top-level JSON object with
/// metadata (schema_version, git_sha, build_type, host) at the root.
QJsonObject toJsonReport(const QList<RunResult> &results,
                         const QString &gitSha,
                         const QString &buildType);

}  // namespace Markoff::Bench
```

- [ ] **Step 11.5: Write the impl**

Create `libs/markoff-bench/src/JsonReporter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/JsonReporter.h>

#include <QJsonValue>
#include <QSysInfo>

namespace Markoff::Bench {

namespace {
QJsonObject distToJson(const Distribution &d) {
    QJsonObject o;
    o["count"] = static_cast<qint64>(d.count);
    o["min"]   = static_cast<qint64>(d.min);
    o["mean"]  = static_cast<qint64>(d.mean);
    o["p50"]   = static_cast<qint64>(d.p50);
    o["p95"]   = static_cast<qint64>(d.p95);
    o["p99"]   = static_cast<qint64>(d.p99);
    o["max"]   = static_cast<qint64>(d.max);
    return o;
}

const char *tierName(Tier t) {
    switch (t) {
    case Tier::DirectParse: return "direct_parse";
    case Tier::PoolParse:   return "pool_parse";
    case Tier::Render:      return "render";
    }
    return "unknown";
}

const char *phaseJsonKey(int p) {
    switch (static_cast<Phase>(p)) {
    case Phase::Extract:     return "phase_extract";
    case Phase::Diff:        return "phase_diff";
    case Phase::ParseBlock:  return "phase_parse_block";
    case Phase::ParseInline: return "phase_parse_inline";
    case Phase::Queries:     return "phase_queries";
    case Phase::Snapshot:    return "phase_snapshot";
    case Phase::PoolQueue:   return "phase_pool_queue";
    case Phase::SignalHop:   return "phase_signal_hop";
    case Phase::RenderFrame: return "phase_render_frame";
    case Phase::_Count:      return "_count";
    }
    return "phase_unknown";
}
}  // namespace

QJsonObject toJson(const RunResult &r) {
    QJsonObject o;
    o["tier"]            = tierName(r.tier);
    o["scenario"]        = r.scenarioName;
    if (r.profileName && r.profileName[0] != 0)  o["corpus_profile"] = r.profileName;
    if (r.fixtureName && r.fixtureName[0] != 0)  o["corpus_fixture"] = r.fixtureName;
    o["iterations"]      = r.iterations;
    o["warmup_iterations"] = r.warmupIters;

    QJsonObject metrics;
    metrics["total_ns"] = distToJson(r.totalNs);
    for (int p = 0; p < kPhaseCount; ++p) {
        metrics[phaseJsonKey(p)] = distToJson(r.phases[p]);
    }
    metrics["block_changed_bytes"] = distToJson(r.blockChangedBytes);
    metrics["inline_reuse_count"]  = distToJson(r.inlineReuseCount);
    metrics["alloc_bytes"]         = distToJson(r.allocBytes);
    metrics["alloc_count"]         = distToJson(r.allocCount);
    o["metrics"] = metrics;
    return o;
}

QJsonObject toJsonReport(const QList<RunResult> &results,
                         const QString &gitSha,
                         const QString &buildType) {
    QJsonObject root;
    root["schema_version"] = 1;
    root["git_sha"]        = gitSha;
    root["build_type"]     = buildType;

    QJsonObject host;
    host["cpu"]  = QSysInfo::currentCpuArchitecture();
    host["kernel"] = QSysInfo::kernelVersion();
    host["qt"]   = QString(qVersion());
    root["host"] = host;

    QJsonArray arr;
    for (const auto &r : results) arr.append(toJson(r));
    root["results"] = arr;
    return root;
}

}  // namespace Markoff::Bench
```

- [ ] **Step 11.6: Add the source**

```cmake
add_library(markoff_bench STATIC
    src/PhaseTimer.cpp
    src/PercentileReducer.cpp
    src/AllocCounter.cpp
    src/AllocShim.cpp
    src/CorpusGen.cpp
    src/FixtureLoader.cpp
    src/Scenario.cpp
    src/ScenarioRunner.cpp
    src/JsonReporter.cpp
)
```

- [ ] **Step 11.7: Build + run the existing bench tests to confirm no regression**

```
cmake --build build-dev -j
ctest --test-dir build-dev -j -R "tst_bench_" --output-on-failure
```
Expected: all green.

- [ ] **Step 11.8: Commit**

```
git add libs/markoff-core/src/IncrementalParseSession.h \
        libs/markoff-bench/include/markoff-bench/JsonReporter.h \
        libs/markoff-bench/src/JsonReporter.cpp \
        libs/markoff-bench/src/ScenarioRunner.cpp \
        libs/markoff-bench/CMakeLists.txt
git commit -m "feat(bench): JsonReporter + reuse-counter wiring

IncrementalParseSession exposes parser() (foundation-internal); runner
reads blockChangedByteCount + inlineTreeReuseCount per iter. JsonReporter
serialises RunResult into schema_version=1 JSON with phase-keyed metrics.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 12: CTest smoke target

**Files:**
- Create: `libs/markoff-bench/tests/tst_bench_smoke.cpp`
- Modify: `libs/markoff-bench/tests/CMakeLists.txt`

The smoke test runs **one profile (mid_prose) × one scenario (type_end) × ~50 iters** at Tier 1, asserts the result has plausible numbers (positive total, count == iterations), and dumps a one-line summary to stdout. Runs as part of `ctest -L bench`. Designed to catch breakage in the harness itself (compile / link / ABI), not perf regressions.

- [ ] **Step 12.1: Write the smoke test**

Create `libs/markoff-bench/tests/tst_bench_smoke.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include <markoff-bench/CorpusGen.h>
#include <markoff-bench/JsonReporter.h>
#include <markoff-bench/Scenario.h>
#include <markoff-bench/ScenarioRunner.h>

#include <QJsonDocument>

class TstBenchSmoke : public QObject {
    Q_OBJECT
private slots:
    void direct_parse_mid_prose_type_end_runs();
};

using namespace Markoff::Bench;

void TstBenchSmoke::direct_parse_mid_prose_type_end_runs() {
    const QByteArray corpus = generate(CorpusProfile::MidProse, 0xBEEF);
    QVERIFY(!corpus.isEmpty());

    RunResult r = runDirectParse(corpus, ScenarioKind::TypeEnd, /*seed*/ 0xBEEF);
    r.profileName = profileName(CorpusProfile::MidProse);

    // Sanity: warmup excluded, measured count matches the scenario meta.
    QCOMPARE(r.warmupIters, 20);
    QCOMPARE(r.iterations, 180);
    QCOMPARE(static_cast<int>(r.totalNs.count), 180);
    QVERIFY2(r.totalNs.p50 > 0, "p50 must be positive");
    QVERIFY2(r.totalNs.p99 >= r.totalNs.p50, "p99 must be ≥ p50");

    // Dump JSON to stderr for human inspection.
    const QJsonDocument doc(toJson(r));
    qInfo().noquote() << doc.toJson(QJsonDocument::Indented);
}

QTEST_GUILESS_MAIN(TstBenchSmoke)
#include "tst_bench_smoke.moc"
```

- [ ] **Step 12.2: Register the test with the `bench` label**

Append to `libs/markoff-bench/tests/CMakeLists.txt`:

```cmake
add_executable(tst_bench_smoke tst_bench_smoke.cpp)
add_test(NAME tst_bench_smoke COMMAND tst_bench_smoke)
target_link_libraries(tst_bench_smoke PRIVATE Qt6::Test markoff_bench)
set_tests_properties(tst_bench_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "bench"
    TIMEOUT 30)
```

- [ ] **Step 12.3: Build + run**

```
cmake --build build-dev --target tst_bench_smoke -j
ctest --test-dir build-dev -R tst_bench_smoke --output-on-failure
```
Expected: PASS with JSON dump on stderr showing positive p50/p95/p99/max.

- [ ] **Step 12.4: Confirm `ctest -L bench` selects only this test**

Run: `ctest --test-dir build-dev -L bench -N`
Expected: lists `tst_bench_smoke` and only `tst_bench_smoke`.

- [ ] **Step 12.5: Confirm `ctest` (no label) excludes it**

Run: `ctest --test-dir build-dev -j -E "tst_realistic|tst_benchmark|tst_bench_smoke" --output-on-failure`
Expected: full green; no `tst_bench_smoke` entry. (The default ctest run still includes it because we did not exclude `bench` by label by default — the user runs the fast loop with `-LE bench` if desired. Note this in Task 15 README.)

- [ ] **Step 12.6: Commit**

```
git add libs/markoff-bench/tests/tst_bench_smoke.cpp \
        libs/markoff-bench/tests/CMakeLists.txt
git commit -m "feat(bench): tst_bench_smoke — CTest harness self-test

One profile (mid_prose) × one scenario (type_end) × 180 measured iters
at Tier 1. Asserts the harness wires together cleanly and produces
plausible numbers; dumps JSON to stderr for human review. Labelled
'bench'; run via 'ctest -L bench'.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 13: `markoff-bench-parse` CLI

**Files:**
- Create: `apps/bench/CMakeLists.txt`
- Create: `apps/bench/markoff-bench-parse.cpp`
- Modify: `CMakeLists.txt` (root)

Standalone CLI that runs the full parse-tier matrix and emits JSON. Flags per the design.

- [ ] **Step 13.1: Create `apps/bench/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.19)
project(markoff_bench_apps LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

add_executable(markoff-bench-parse markoff-bench-parse.cpp)
target_link_libraries(markoff-bench-parse PRIVATE
    Qt6::Core
    markoff_bench
)

# Tier 2 binary added in Task 14.
```

- [ ] **Step 13.2: Hook up the root CMakeLists**

Edit `CMakeLists.txt` (root). After `add_subdirectory(libs/markoff-bench)`, add:

```cmake
option(MARKOFF_BUILD_BENCH_APPS "Build standalone bench CLIs" ON)
if(MARKOFF_BUILD_BENCH_APPS)
    add_subdirectory(apps/bench)
endif()
```

- [ ] **Step 13.3: Write the CLI**

Create `apps/bench/markoff-bench-parse.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QStringList>
#include <QTextStream>

#include <markoff-bench/CorpusGen.h>
#include <markoff-bench/FixtureLoader.h>
#include <markoff-bench/JsonReporter.h>
#include <markoff-bench/Scenario.h>
#include <markoff-bench/ScenarioRunner.h>

namespace {
QString gitShaOrUnknown() {
    // Hard-coded "unknown" here; the wrapper script (or CI) is expected
    // to inject the SHA via --git-sha. Avoids a popen() in the binary.
    return QStringLiteral("unknown");
}

QList<Markoff::Bench::CorpusProfile> profilesFromFlags(const QStringList &names) {
    using Markoff::Bench::CorpusProfile;
    QList<CorpusProfile> out;
    auto match = [&](const QString &n) -> int {
        for (int i = 0; i < Markoff::Bench::kCorpusProfileCount; ++i) {
            if (n == Markoff::Bench::profileName(static_cast<CorpusProfile>(i)))
                return i;
        }
        return -1;
    };
    for (const auto &n : names) {
        const int i = match(n);
        if (i < 0) qWarning("unknown profile '%s'", qPrintable(n));
        else out.append(static_cast<CorpusProfile>(i));
    }
    return out;
}

QList<Markoff::Bench::ScenarioKind> scenariosFromFlags(const QStringList &names) {
    using Markoff::Bench::ScenarioKind;
    QList<ScenarioKind> out;
    static const QStringList all = {
        "cold_parse", "type_end", "type_start", "type_middle",
        "block_boundary", "paste_4kb", "replace_1kb"};
    for (const auto &n : names) {
        const int i = all.indexOf(n);
        if (i < 0) qWarning("unknown scenario '%s'", qPrintable(n));
        else out.append(static_cast<ScenarioKind>(i));
    }
    return out;
}

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("markoff-bench-parse");

    QCommandLineParser p;
    p.setApplicationDescription("Parse-tier benchmark for the foundation pipeline");
    p.addHelpOption();

    p.addOption({{"profile"},  "Corpus profile (repeatable). Default: all.",  "name"});
    p.addOption({{"fixture"},  "Real-doc fixture (repeatable).",              "name"});
    p.addOption({{"scenario"}, "Scenario (repeatable). Default: all.",         "name"});
    p.addOption({{"seed"},     "RNG seed (default 0xBEEF).",                  "int", "48879"});
    p.addOption({{"out"},      "Output JSON path (default: stdout).",         "path"});
    p.addOption({{"tier"},     "tier1 | tier1b | both (default: both).",      "tier", "both"});
    p.addOption({{"git-sha"},  "Git SHA to embed in the report.",             "sha", "unknown"});
    p.addOption({{"build-type"}, "Build type to embed.",                      "type", "RelWithDebInfo"});
    p.process(app);

    using namespace Markoff::Bench;

    QStringList profileNames = p.values("profile");
    if (profileNames.isEmpty()) {
        for (int i = 0; i < kCorpusProfileCount; ++i)
            profileNames << profileName(static_cast<CorpusProfile>(i));
    }
    const QList<CorpusProfile> profiles = profilesFromFlags(profileNames);

    const QStringList fixtureNames = p.values("fixture");

    QStringList scenarioNames = p.values("scenario");
    if (scenarioNames.isEmpty()) {
        scenarioNames << "cold_parse" << "type_end" << "type_start" << "type_middle"
                      << "block_boundary" << "paste_4kb" << "replace_1kb";
    }
    const QList<ScenarioKind> scenarios = scenariosFromFlags(scenarioNames);

    const quint64 seed = p.value("seed").toULongLong();
    const QString tierFlag = p.value("tier");
    const bool runDirect = (tierFlag == "tier1" || tierFlag == "both");
    const bool runPool   = (tierFlag == "tier1b" || tierFlag == "both");

    QList<RunResult> results;
    auto pushResult = [&](const QByteArray &corpus,
                          const char *profileLabel,
                          const char *fixtureLabel,
                          ScenarioKind sc) {
        if (runDirect) {
            RunResult r = runDirectParse(corpus, sc, seed);
            r.profileName = profileLabel ? profileLabel : "";
            r.fixtureName = fixtureLabel ? fixtureLabel : "";
            results.append(r);
        }
        if (runPool) {
            RunResult r = runPoolParse(corpus, sc, seed);
            r.profileName = profileLabel ? profileLabel : "";
            r.fixtureName = fixtureLabel ? fixtureLabel : "";
            results.append(r);
        }
    };

    for (auto pr : profiles) {
        const QByteArray corpus = generate(pr, seed);
        const char *label = profileName(pr);
        for (auto sc : scenarios) pushResult(corpus, label, nullptr, sc);
    }
    for (const QString &fname : fixtureNames) {
        const QByteArray corpus = loadFixture(fname);
        if (corpus.isEmpty()) continue;
        // We need a stable C-string for the label; the QByteArray below
        // owns the bytes for the duration of the run.
        const QByteArray fbytes = fname.toUtf8();
        for (auto sc : scenarios) pushResult(corpus, nullptr, fbytes.constData(), sc);
        // NOTE: fbytes goes out of scope at end of loop iter — but
        // pushResult() copies the C string into RunResult.fixtureName
        // (which is a const char*). Fix: store labels in a Q-list of
        // QByteArray that outlives the result list.
        // For now the implementer should refactor to keep labels alive;
        // the simplest fix is a local QList<QByteArray> labels;
        // labels.append(fname.toUtf8()); pushResult(..., labels.last().constData(), ...);
    }

    const QJsonObject report = toJsonReport(
        results,
        p.value("git-sha"),
        p.value("build-type"));
    const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);

    const QString outPath = p.value("out");
    if (outPath.isEmpty()) {
        QTextStream(stdout) << bytes;
    } else {
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning("cannot open %s", qPrintable(outPath));
            return 1;
        }
        f.write(bytes);
    }
    return 0;
}
```

- [ ] **Step 13.4: Resolve the lifetime caveat**

The CLI as written has a label-lifetime bug (commented in the code). Fix it in the same step before building: hoist a `QList<QByteArray>` `fixtureLabels` outside the loop, push each fixture's label into it, and pass `fixtureLabels.last().constData()` to `pushResult`. Apply the same pattern for profiles by using `profileName()` (already returns a static `const char *`, no lifetime issue there).

Concretely, replace the fixtures loop with:

```cpp
    QList<QByteArray> fixtureLabels;
    for (const QString &fname : fixtureNames) {
        const QByteArray corpus = loadFixture(fname);
        if (corpus.isEmpty()) continue;
        fixtureLabels.append(fname.toUtf8());
        const char *label = fixtureLabels.last().constData();
        for (auto sc : scenarios) pushResult(corpus, nullptr, label, sc);
    }
```

And delete the multi-line NOTE comment. (`fixtureLabels` outlives the JSON report build, so the `const char *` references stay valid.)

- [ ] **Step 13.5: Build**

```
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff-bench-parse -j
```
Expected: clean.

- [ ] **Step 13.6: Smoke-run with a small slice**

```
./build-dev/bin/markoff-bench-parse \
    --profile mid_prose --scenario type_end --tier tier1
```
Expected: prints a JSON document to stdout with one result block, all fields populated, p99 ≥ p50 > 0.

- [ ] **Step 13.7: Smoke-run the pool tier**

```
./build-dev/bin/markoff-bench-parse \
    --profile mid_prose --scenario type_end --tier tier1b
```
Expected: prints JSON; total_ns is meaningfully larger than tier1 (because it includes pool queue + signal hop).

- [ ] **Step 13.8: Commit**

```
git add apps/bench/CMakeLists.txt \
        apps/bench/markoff-bench-parse.cpp \
        CMakeLists.txt
git commit -m "feat(bench): markoff-bench-parse CLI

Standalone parse-tier benchmark binary. Flags: --profile, --fixture,
--scenario, --tier (tier1/tier1b/both), --seed, --out, --git-sha,
--build-type. Defaults to running every profile × every scenario at
both tiers, emitting a JSON report to stdout. Built behind
MARKOFF_BUILD_BENCH_APPS (default ON).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 14: `markoff-bench-render` CLI (Tier 2)

**Files:**
- Create: `apps/bench/markoff-bench-render.cpp`
- Modify: `apps/bench/CMakeLists.txt`

Tier 2 spins up a `QGuiApplication` under `QT_QPA_PLATFORM=offscreen`, loads the QML root (the same one `markoff-view-qml-app` uses, in source mode), drives keystrokes via `QTest::keyClick` into the focused TextArea, and times each keystroke→`afterRendering` round trip.

We deliberately keep this tier minimal: only `cold_parse`, `type_end`, and `paste_4kb` from the design's narrowed Tier-2 list.

- [ ] **Step 14.1: Find the QML entry-point used by the existing app**

```
grep -rn "loadFromModule\|setSource\|markoff-view-qml" libs/markoff-view-qml/app/ 2>/dev/null
```
Expected: identifies the `QQmlApplicationEngine` setup pattern and the QML module name. Mirror it in the bench binary.

- [ ] **Step 14.2: Add the executable to `apps/bench/CMakeLists.txt`**

Append:

```cmake
find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Quick QuickTest Test)

add_executable(markoff-bench-render markoff-bench-render.cpp)
target_link_libraries(markoff-bench-render PRIVATE
    Qt6::Gui
    Qt6::Quick
    Qt6::Test
    markoff_bench
    markoff_view_qml         # provides the QML module + types
)
```

(The exact name of the view-qml CMake target may differ — confirm by `grep -n "add_library" libs/markoff-view-qml/CMakeLists.txt` and use whatever the file declares.)

- [ ] **Step 14.3: Write the CLI**

Create `apps/bench/markoff-bench-render.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tier 2 — render benchmark. Drives the markoff-view-qml source-mode
// editor under QT_QPA_PLATFORM=offscreen and times keystroke→render.
//
// Caveats:
//   - Offscreen QPA is not a real GPU — numbers are useful for
//     RELATIVE comparisons across commits, not absolute UX latency.
//   - Frame coalescing in Qt Quick means measured "frames per keystroke"
//     can be 0 if a render is in flight.

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest/QTest>

#include <markoff-bench/CorpusGen.h>
#include <markoff-bench/JsonReporter.h>
#include <markoff-bench/PercentileReducer.h>
#include <markoff-bench/Scenario.h>

namespace {

// Walk the Quick item tree to find the first focused TextEdit/TextArea.
// We assume the source-mode view focuses the editor on load.
QQuickItem *findFocusedTextEdit(QQuickWindow *win) {
    QQuickItem *focused = win->activeFocusItem();
    return focused;
}

}  // namespace

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    QCommandLineParser p;
    p.addHelpOption();
    p.addOption({{"profile"},  "Corpus profile (default mid_prose).", "name", "mid_prose"});
    p.addOption({{"scenario"}, "Scenario (default type_end).",       "name", "type_end"});
    p.addOption({{"out"},      "Output JSON path (default stdout).",  "path"});
    p.addOption({{"git-sha"},  "Git SHA.",                           "sha", "unknown"});
    p.process(app);

    using namespace Markoff::Bench;

    // 1) Find the requested profile.
    int profileIdx = -1;
    for (int i = 0; i < kCorpusProfileCount; ++i) {
        if (p.value("profile") == profileName(static_cast<CorpusProfile>(i))) profileIdx = i;
    }
    if (profileIdx < 0) { qWarning("unknown profile"); return 2; }
    const QByteArray corpus = generate(static_cast<CorpusProfile>(profileIdx), 0xBEEF);

    // 2) Load the QML root. The exact import path / object name comes
    //    from the view-qml app discovered in Step 14.1.
    QQmlApplicationEngine engine;
    engine.loadFromModule("markoff.view.qml.app", "Main");   // PLACEHOLDER — replace with discovered module
    if (engine.rootObjects().isEmpty()) { qWarning("QML load failed"); return 3; }

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!win) { qWarning("root is not QQuickWindow"); return 4; }

    // 3) Set the document content via a known property/slot. The view-qml
    //    app exposes a Q_INVOKABLE on a 'controller' context property in
    //    its source-mode entry — check libs/markoff-view-qml/app/ for the
    //    exact name. PLACEHOLDER below: assumes the controller has a
    //    setText(QString) Q_INVOKABLE.
    auto *root = win->contentItem();
    QMetaObject::invokeMethod(root, "setDocumentText",
                              Q_ARG(QVariant, QString::fromUtf8(corpus)));

    // 4) Wait for the initial render.
    bool initialRendered = false;
    QObject::connect(win, &QQuickWindow::frameSwapped,
                     [&]() { initialRendered = true; });
    while (!initialRendered) QGuiApplication::processEvents();

    // 5) Run the scenario.
    int scenarioIdx = -1;
    static const QStringList scenarioNames = {
        "cold_parse", "type_end", "type_start", "type_middle",
        "block_boundary", "paste_4kb", "replace_1kb"};
    scenarioIdx = scenarioNames.indexOf(p.value("scenario"));
    if (scenarioIdx < 0) { qWarning("unknown scenario"); return 5; }
    const auto kind = static_cast<ScenarioKind>(scenarioIdx);

    QQuickItem *editor = findFocusedTextEdit(win);
    if (!editor) { qWarning("no focused text edit"); return 6; }

    const ScenarioMeta meta = scenarioMeta(kind);
    std::vector<quint64> latencies;
    QByteArray currentDoc = corpus;

    for (int i = 0; i < meta.warmupIters + meta.measuredIters; ++i) {
        const Markoff::MarkoffEdit e = nextStep(kind, currentDoc, i, /*seed*/ 0xBEEF);
        currentDoc = currentDoc.left(e.oldStart) + e.newText
                   + currentDoc.mid(e.oldEnd);

        QElapsedTimer t;
        t.start();
        // For the bench we approximate keystrokes by typing the new bytes.
        // For multi-byte payloads (paste_4kb) we set the whole text in one go.
        if (e.newText.size() == 1 && e.oldStart == e.oldEnd) {
            QTest::keyClick(editor, e.newText.at(0));
        } else {
            QMetaObject::invokeMethod(root, "setDocumentText",
                                      Q_ARG(QVariant, QString::fromUtf8(currentDoc)));
        }
        // Wait one render frame.
        bool framed = false;
        QMetaObject::Connection c = QObject::connect(
            win, &QQuickWindow::frameSwapped,
            [&]() { framed = true; });
        while (!framed) QGuiApplication::processEvents();
        QObject::disconnect(c);

        if (i >= meta.warmupIters)
            latencies.push_back(static_cast<quint64>(t.nsecsElapsed()));
    }

    RunResult r;
    r.profileName    = profileName(static_cast<CorpusProfile>(profileIdx));
    r.scenarioName   = meta.name;
    r.tier           = Tier::Render;
    r.iterations     = static_cast<int>(latencies.size());
    r.warmupIters    = meta.warmupIters;
    r.totalNs        = reducePercentiles(latencies);

    const QJsonObject report = toJsonReport({r}, p.value("git-sha"), QStringLiteral("RelWithDebInfo"));
    const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    const QString outPath = p.value("out");
    if (outPath.isEmpty()) {
        fputs(bytes.constData(), stdout);
    } else {
        QFile f(outPath); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write(bytes);
    }
    return 0;
}
```

The two `PLACEHOLDER` markers (QML module import path and `setDocumentText` invocable name) MUST be replaced before the binary will run. The Step 14.1 grep tells the implementer what to substitute. If the view-qml app does not expose a setter, add one in the same task (`apps/bench/markoff-bench-render.cpp` may need a small companion change in `libs/markoff-view-qml/`).

- [ ] **Step 14.4: Resolve the placeholders**

Run `grep -n "setDocumentText\|loadFromModule" libs/markoff-view-qml/app/` and substitute the actual module path + invocable name. If a `setDocumentText` Q_INVOKABLE doesn't exist, add one to whatever class the existing app uses for its controller, with a single-line implementation that delegates to `MarkoffDocument::resetContent`. Commit that small foundation/view-qml change separately first.

- [ ] **Step 14.5: Build**

```
cmake --build build-dev --target markoff-bench-render -j
```
Expected: clean.

- [ ] **Step 14.6: Smoke-run**

```
QT_QPA_PLATFORM=offscreen ./build-dev/bin/markoff-bench-render \
    --profile mid_prose --scenario type_end
```
Expected: JSON output with `tier: "render"`, p50/p99 positive, count = 180.

- [ ] **Step 14.7: Commit**

```
git add apps/bench/markoff-bench-render.cpp \
        apps/bench/CMakeLists.txt
git commit -m "feat(bench): markoff-bench-render CLI (Tier 2)

Spins up the view-qml source-mode editor under QT_QPA_PLATFORM=offscreen,
drives keystrokes via QTest::keyClick, and times each keystroke→
frameSwapped round trip. Caveat documented: offscreen QPA ≠ real GPU,
so numbers are useful for relative comparisons not absolute UX claims.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 15: README + final full test run

**Files:**
- Create: `libs/markoff-bench/README.md`

- [ ] **Step 15.1: Write the README**

Create `libs/markoff-bench/README.md`:

```markdown
# markoff-bench

In-tree benchmark harness for the foundation-exploration parse / render
pipeline. See `docs/specs/2026-04-29-parse-render-bench-design.md` for
design rationale.

## Layout

- `include/markoff-bench/`, `src/` — library sources (STATIC, internal,
  not installed). Public-to-bench-frontends only.
- `tests/` — unit tests for the harness primitives + CTest smoke target.
- `fixtures/` — committed real-doc fixtures (markdown).

Frontends live under `apps/bench/`:

- `markoff-bench-parse` — Tier 1 + Tier 1b (parse direct + via ParsePool).
- `markoff-bench-render` — Tier 2 (offscreen QPA, QML view, keystroke→render).

## Build + run

```bash
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff-bench-parse markoff-bench-render -j

# Full parse-tier matrix:
./build-dev/bin/markoff-bench-parse --out /tmp/bench.json

# One profile / one scenario:
./build-dev/bin/markoff-bench-parse \
    --profile mid_prose --scenario type_end --tier tier1

# Render tier:
./build-dev/bin/markoff-bench-render \
    --profile mid_prose --scenario type_end --out /tmp/bench-render.json
```

## CTest integration

The harness includes one CTest-registered smoke target:

```bash
ctest --test-dir build-dev -L bench --output-on-failure
```

This runs `tst_bench_smoke` (mid_prose × type_end × 180 iters at Tier 1)
in ~5 seconds. It verifies the harness builds and produces plausible
numbers; it does NOT enforce perf thresholds. To skip it on the fast
inner loop:

```bash
ctest --test-dir build-dev -j -LE bench -E "tst_realistic|tst_benchmark" \
    --output-on-failure
```

## Output schema (v1)

```json
{
  "schema_version": 1,
  "git_sha": "…",
  "build_type": "RelWithDebInfo",
  "host": {"cpu": "…", "kernel": "…", "qt": "6.8.x"},
  "results": [
    {
      "tier": "direct_parse",
      "scenario": "type_end",
      "corpus_profile": "mid_prose",
      "iterations": 180,
      "warmup_iterations": 20,
      "metrics": {
        "total_ns":            {"count": …, "min": …, "mean": …, "p50": …, "p95": …, "p99": …, "max": …},
        "phase_extract":       {"…"},
        "phase_diff":          {"…"},
        "phase_parse_block":   {"…"},
        "phase_parse_inline":  {"…"},
        "phase_queries":       {"…"},
        "phase_snapshot":      {"…"},
        "phase_pool_queue":    {"…"},
        "phase_signal_hop":    {"…"},
        "phase_render_frame":  {"…"},
        "block_changed_bytes": {"…"},
        "inline_reuse_count":  {"…"},
        "alloc_bytes":         {"…"},
        "alloc_count":         {"…"}
      }
    },
    …
  ]
}
```

All times are nanoseconds. `block_changed_bytes` and `inline_reuse_count`
are populated on Tier 1 only (we read the parser directly); Tier 1b/2
emit zeros for those fields.

## Caveats

1. **Phase splits are coarse.** Tier 1 currently buckets the entire
   per-iter parse cost into `phase_parse_block`. Finer splits require
   instrumenting `IncrementalParseSession::applyEdit` with PhaseTimer
   guards inside the foundation source — a follow-up task once a
   profile motivates it.
2. **Allocation counter is approximate.** The shim sees `operator new`
   from C++ code in markoff_bench-linked binaries. Calls that bypass
   the global operator (mmap, jemalloc, etc.) are not counted.
3. **Offscreen QPA ≠ real GPU.** Tier 2 numbers are useful for
   relative comparisons, not absolute UX claims.
4. **Cross-host comparisons are not meaningful.** The harness is
   designed for trend-on-same-machine; comparing numbers across CPUs
   or kernels will produce noise.
5. **Bench tests link the global new/delete shim.** Other test
   binaries in this repo do NOT link `markoff_bench`, so they are
   uninstrumented — but if you add a new test that links bench, be
   aware its allocator path is shimmed.

## Updating the synthetic corpus

Profile definitions live in `src/CorpusGen.cpp`'s `kProfiles` table.
Bumping a target size or changing a share is a stable change as long
as the size-tolerance test still passes. To add a new profile:

1. Append to the `CorpusProfile` enum in `include/markoff-bench/CorpusGen.h`.
2. Append a `ProfileSpec` to `kProfiles` in the same order.
3. Bump `kCorpusProfileCount`.
4. Add a row to `tst_bench_corpus_gen.cpp::all_profiles_within_size_tolerance_data`.
```

- [ ] **Step 15.2: Run the full test suite to confirm everything is green**

```
cmake --build build-dev -j
ctest --test-dir build-dev -j -E "tst_realistic|tst_benchmark" --output-on-failure
```
Expected: pre-existing 78 + 1 (parser test from Task 1) + 6 bench unit tests + tst_bench_smoke = ~86 tests green.

- [ ] **Step 15.3: Run a full bench matrix end-to-end (sanity check)**

```
./build-dev/bin/markoff-bench-parse \
    --profile tiny --profile mid_prose --profile mid_mixed \
    --scenario cold_parse --scenario type_end \
    --tier both --out /tmp/markoff-bench-sanity.json
cat /tmp/markoff-bench-sanity.json | head -60
```
Expected: a JSON document with 3 profiles × 2 scenarios × 2 tiers = 12 result blocks. Skim for sanity: every block has positive p50/p95/p99/max; pool tier numbers are larger than direct-parse numbers for the same (profile, scenario).

- [ ] **Step 15.4: Commit the README + close out**

```
git add libs/markoff-bench/README.md
git commit -m "docs(bench): README — usage, schema, caveats

Documents the build, CLI flags, CTest integration, JSON v1 schema,
and the five known caveats (coarse phase splits, approximate alloc
counter, offscreen QPA ≠ GPU, no cross-host comparison, shim links
into bench-using binaries).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Self-review notes

- **Spec coverage.** Every section of `2026-04-29-parse-render-bench-design.md` maps to at least one task: Tier 1 (Task 9), Tier 1b (Task 10), Tier 2 (Task 14), 9 corpus profiles (Task 6), 3-5 fixtures (Task 7), 7 scenarios (Task 8), per-phase metrics (Task 3 + Task 11), reuse counts (Tasks 1 + 11), allocation counter (Task 5), JSON schema (Task 11), CTest smoke (Task 12), CLI frontends (Tasks 13/14), README (Task 15). Open questions in the spec: real-doc fixtures (handled — copies, see Task 7), CTest smoke slice (Task 12: mid_prose × type_end × 180 iters), allocation counter approach (Task 5: shim, per design preference for accuracy), block-tree reuse counter (Task 1: added).
- **Coarse phase splits.** Task 9 documents that the initial implementation buckets all parse cost into `phase_parse_block`; finer splits land in a follow-up. This is a deliberate scope cut to keep the plan deliverable in one pass — the JSON schema reserves the slots so the follow-up is purely additive.
- **Type consistency.** `RunResult`, `Distribution`, `Phase`, `ScenarioKind`, `CorpusProfile` are defined once and used uniformly across tasks. The reuse-counter accessor names (`blockChangedByteCount`, `inlineTreeReuseCount`) match the existing parser convention.
- **Tier 2 placeholders.** Tasks 14 has two PLACEHOLDER markers that the implementer must resolve from the existing `libs/markoff-view-qml/app/` code (Step 14.1 + 14.4). This is the only spot where the plan can't be 100% concrete without reading code that wasn't sampled during plan-writing — the resolution path is fully specified.
