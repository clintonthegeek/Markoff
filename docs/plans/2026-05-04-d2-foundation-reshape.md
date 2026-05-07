# D2 — Markoff foundation reshape Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reshape `Markoff::MarkoffDocument` in-place from single-CRDT-rope-with-async-parser to per-block-CRDT coordinated by structural CRDT (`CollabText::Crdt::IdList`); migrate all in-tree consumers; round-trip determinism preserved; live-editing dogfood passes against the new foundation.

**Architecture:** In-place evolution of `markoff-foundation`. Public type name `MarkoffDocument` preserved; internals swap entirely. Composition: one `IdList<uint64>` for block ordering + one `Buffer` per block + six Markoff-owned causal-LWW sibling maps + `UndoLog` + `WatermarkCoordinator` + `InlineParseCache`. Two-layer edit API (low-level `applyBlockEdit` / `applyStructural` + high-level `Cmd::*`). Touch-aware save (per-block load-time bytes). Save-triggered watermark GC. Per-block synchronous-on-read inline parse. `markoff-live-render` L4–L5 reshape; marker-paragraph machinery deletes.

**Tech Stack:** C++20, Qt 6.8 (Core, Quick, Qml, Test, Widgets), `CollabText::Crdt::IdList` (D1; shipped 2026-05-04), `CollabText::Crdt::Buffer` (existing), tree-sitter Markdown via `markoff-parser`, CMake 3.19+, KDE Frameworks (KF6) where existing.

**Reference spec:** [`docs/specs/2026-05-04-d2-foundation-reshape-design.md`](../specs/2026-05-04-d2-foundation-reshape-design.md) — binding. Read its TL;DR (§0), premises (§1), and the "Why this and not the alternatives" subsections before starting.

**API-placeholder note.** Code samples in this plan that use `/*after-anchor*/`, `/*right*/`, `/*new-id*/` style comments inside `m_idList.insertAfter(...)` calls are placeholders for API specifics resolved at Task 0.1 via inspection of the shipped `~/dev/collabtext/include/collabtext/Crdt/IdList.h` header. Use the actual symbol names the header exposes (e.g., `Bias::Right`, the actual `Anchor` constructor, the actual return type of `insertAfter`). The plan can't bake exact names without that inspection, which Task 0.1 performs as the gating step.

**Cross-CRDT `OpId` convention.** The plan uses a single `OpId = uint64_t` type (defined in `UndoLog.h`) as the cross-CRDT op identifier. Each CRDT (collabtext `IdList`, `Buffer`, and Markoff's `CausalLwwMap`) returns an `OpId` from its write methods that the `UndoLog::Transaction::registerOp` consumes. For collabtext CRDTs, derive the `OpId` from the returned `Operation`'s causal stamp (`replicaId` shifted into the high 16 bits of the uint64 + `counter` in the low 48). For `CausalLwwMap`, the `OpId` is the same shape derived from its `CausalStamp`. The `UndoLog::Dispatcher` reverses this on undo: it splits the `OpId` into `(replicaId, counter)` and calls the CRDT's `undo(operation_with_that_stamp)` equivalent. Task 1.4 (CausalLwwMap undo/redo) and Task 4.4 (MarkoffDocument undo dispatch) both use this convention.

**Acceptance criterion (binary):** All foundation tests pass against the new internals; the round-trip corpus tests pass byte-identical for all corpus files; `markoff-live-render` L4 / L5 migration completes (marker-paragraph machinery deleted; freshness gate / cycle guards / previousText cache deleted); convergence tests against collabtext fixtures pass; user signs off on a dogfood pass per Task 15.2.

---

## Working environment

All work happens inside `.worktrees/foundation-exploration/`. The build dir is `build-dev`. The fast inner-loop test command is `ctest --test-dir build-dev -j 8 -E "tst_realistic|tst_benchmark"`. The full configure-then-build is `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build-dev -j 8`. **Cap parallelism at `-j 8`** (project policy).

Per-task workflow:

1. Run the build before starting (verify clean state).
2. Write the failing test.
3. Run it to verify failure.
4. Implement minimal code.
5. Run the test to verify pass.
6. Run the fast inner loop (`ctest -j 8 -E "tst_realistic|tst_benchmark"`) to verify no regression.
7. Commit with the prescribed message format.

Commit message prefixes: `d2(foundation): <task>` for new code, `d2(foundation): retire <thing>` for deletions, `d2(foundation): test <thing>` for test-only commits, `d2(view): <task>` for `markoff-live-render` migration tasks, `d2(parser): <task>` for parser-library boundary tasks, `d2(docs): <task>` for status / docs.

---

## Plan-time decisions (resolves spec §11)

| Q | Decision | Rationale |
|---|---|---|
| Q1 (inline parse cache eviction) | **Never-evict for first impl.** Cache holds one InlineSpanTree per BlockId; refreshes only when per-block edit-counter changes. LRU revisit deferred unless a real workload shows memory pressure. | "Build complex/general first" — simplest correct cache; revisit on evidence. |
| Q3 (round-trip corpus license) | **Start with GFM specification examples + project's own `docs/` files.** Both are unambiguous-license content. Obsidian vault excerpts deferred until license-cleared samples are curated. | Get the corpus useful immediately without license blocking. |
| Q5 (`Cmd::pasteMarkdown` parser threading) | **Synchronous on calling thread.** Paste is user-initiated, latency-tolerant; parsing the paste source is bounded by paste size which is small in normal use. | Matches spec §11 Q5's tentative answer; no async machinery needed. |

Plan-time-deferred to D3: Q2 (plugin block-kind registration), Q4 (`BlockAttrsMap::AttrValue` variant scope).

---

## File map

### New files

```
libs/markoff-core/
├─ include/markoff-foundation/
│  ├─ BlockId.h                              # Phase 1
│  ├─ BlockEdit.h                            # Phase 1
│  ├─ StructuralOp.h                         # Phase 1
│  ├─ BlockKind.h                            # Phase 1
│  ├─ CausalLwwMap.h                         # Phase 1
│  ├─ UndoLog.h                              # Phase 2
│  ├─ WatermarkCoordinator.h                 # Phase 9
│  ├─ InlineParseCache.h                     # Phase 10
│  ├─ KindTagMap.h                           # Phase 5 (typedef)
│  ├─ BlockAttrsMap.h                        # Phase 5
│  ├─ FrontmatterMap.h                       # Phase 5
│  ├─ LinkRefMap.h                           # Phase 5
│  ├─ FootnoteDefMap.h                       # Phase 5
│  └─ BlockSerializer.h                      # Phase 8
├─ src/
│  ├─ CausalLwwMap.cpp                       # Phase 1
│  ├─ UndoLog.cpp                            # Phase 2
│  ├─ WatermarkCoordinator.cpp               # Phase 9
│  ├─ InlineParseCache.cpp                   # Phase 10
│  ├─ Cmd_d2.cpp                             # Phase 6 (replaces Cmd.cpp content)
│  └─ BlockSerializers.cpp                   # Phase 8
└─ tests/
   ├─ tst_causal_lww_map.cpp                 # Phase 1
   ├─ tst_undo_log.cpp                       # Phase 2
   ├─ tst_block_id.cpp                       # Phase 1
   ├─ tst_d2_apply_block_edit.cpp            # Phase 4
   ├─ tst_d2_apply_structural.cpp            # Phase 4
   ├─ tst_d2_undo.cpp                        # Phase 4
   ├─ tst_d2_per_block_undo.cpp              # Phase 4
   ├─ tst_d2_signals.cpp                     # Phase 4
   ├─ tst_d2_sibling_maps.cpp                # Phase 5
   ├─ tst_d2_cmd_decomposition.cpp           # Phase 6
   ├─ tst_d2_load.cpp                        # Phase 7
   ├─ tst_d2_save.cpp                        # Phase 8
   ├─ tst_d2_touch_test.cpp                  # Phase 8
   ├─ tst_d2_roundtrip.cpp                   # Phase 8
   ├─ tst_d2_gc.cpp                          # Phase 9
   ├─ tst_d2_inline_parse_cache.cpp          # Phase 10
   ├─ tst_d2_convergence.cpp                 # Phase 13
   └─ roundtrip/corpus/...                   # Phase 8 (corpus files)
```

### Modified files

```
libs/markoff-core/
├─ include/markoff-foundation/
│  ├─ MarkoffDocument.h                      # Phase 4 (public surface reshape; type name preserved)
│  ├─ TextAnchor.h                           # Phase 3
│  ├─ BlockAnchor.h                          # Phase 3 (becomes typedef for BlockId)
│  └─ Cmd.h                                  # Phase 6
├─ src/
│  ├─ MarkoffDocument.cpp                    # Phase 4
│  └─ AnchorConversion.h                     # Phase 3
└─ CMakeLists.txt                            # Phase 1 onward (add new sources)

libs/markoff-live/
├─ src/
│  ├─ LiveEditBinding.cpp                    # Phase 11.1 (rewrite)
│  ├─ LiveStructuralKeyHandler.cpp           # Phase 11.2 (rewrite)
│  ├─ LiveBlockModel.cpp                     # Phase 11.5
│  └─ LiveCursorState.cpp                    # Phase 11.6
└─ (deletions)
   ├─ src/MarkerScrubber.{h,cpp}             # Phase 11.3 — delete
   ├─ src/UndoCoalescer.{h,cpp}              # Phase 11.4 — delete
   └─ include/markoff/Marker.h               # Phase 11.3 — delete

libs/markoff-core/src/
├─ Search/                                    # Phase 12.1 — adapt to per-block iteration
├─ Replace/                                   # Phase 12.2
├─ DefaultLinkService.cpp                     # Phase 12.3
├─ CompletionRegistry.cpp                     # Phase 12.4
└─ Kf6SyntaxHighlightService.cpp              # Phase 12.5

libs/markoff-parser/
└─ (D4-deletion-staging only; this plan marks unused but doesn't delete)
   ├─ ParsePool — marked unused in Phase 14.1
   └─ IncrementalParseSession — marked unused in Phase 14.1
```

### Spec coverage map

| Spec section | Plan task(s) |
|---|---|
| §2 Architecture: composition | Phase 1, 2, 5, 9, 10 (each component built; Phase 4 wires them together) |
| §3 Public types | Phase 1 (BlockId, BlockEdit, StructuralOp, BlockKind), Phase 3 (TextAnchor reshape) |
| §4 Edit + undo | Phase 2 (UndoLog), Phase 4 (apply* + undo dispatch), Phase 6 (Cmd::*) |
| §5.1 Load | Phase 7 |
| §5.2 Save | Phase 8 |
| §5.3 Touch test | Phase 8.4 |
| §5.4 Round-trip | Phase 8.7, 8.8 |
| §6 Parser scope contract | Phase 10 (consume the contract), Phase 14.1 (mark D4-staged deletions) |
| §7 GC | Phase 9 |
| §8 Signals/sequences | Phase 4.5, 4.6 |
| §9 Migration | Phase 11, 12 |
| §10 Test strategy | Phase 13 (convergence), Phase 8.8 (round-trip corpus); foundation unit tests woven through Phase 1–10 |

---

## Phase 0: Setup and verification

### Task 0.1: Verify D1 (collabtext IdList) is available

**Files:**
- Read: `~/dev/collabtext/include/collabtext/Crdt/IdList.h` (or wherever the shipped header lives)
- Verify: `~/dev/collabtext/build-*/lib*collabtext*` library exists

- [ ] **Step 1: Inspect the shipped IdList header**

```bash
ls ~/dev/collabtext/include/collabtext/Crdt/IdList.h && head -60 ~/dev/collabtext/include/collabtext/Crdt/IdList.h
```

Expected: file exists; header declares `class IdList` with `insertAfter`, `removeAt`, `ids`, `anchorOf`, `applyRemote`, `setOnChange`, `undo`, `redo`, `collect_garbage`, `compact` per the maintainer response API.

- [ ] **Step 2: Verify CollabText is buildable from this worktree**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | grep -iE "collabtext|crdt" | head -10
```

Expected: CMake finds `CollabText::collabtext` target. If not, the sibling-symlink at `libs/collabtext` may need refreshing; coordinate with the user before proceeding.

- [ ] **Step 3: Verify IdList is linkable by writing a one-line probe**

Create `/tmp/idlist_probe.cpp`:
```cpp
#include <collabtext/Crdt/IdList.h>
int main() { CollabText::Crdt::IdList list(1); list.insertAfter({}, 42); return 0; }
```

Build and run:
```bash
g++ -std=c++20 $(pkg-config --cflags Qt6Core 2>/dev/null) \
    -I libs/collabtext/include /tmp/idlist_probe.cpp \
    -L build-dev/libs/collabtext -lcollabtext -o /tmp/idlist_probe && /tmp/idlist_probe
```

Expected: compiles, links, runs (exits 0). If link fails, the IdList symbols aren't yet built into the local collabtext artifact — escalate before proceeding.

- [ ] **Step 4: Commit verification record (no code change; document only)**

Create `docs/d-arc/d1-verification-2026-05-04.md` recording the inspected API surface (paste the header signature) and the probe result. Commit:

```bash
git add docs/d-arc/d1-verification-2026-05-04.md
git commit -m "d2(docs): record D1 IdList verification — API + link probe pass"
```

### Task 0.2: Verify build is green at branch tip

- [ ] **Step 1: Configure and build clean**

```bash
rm -rf build-dev && cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build-dev -j 8
```

Expected: zero errors. Warnings are OK if they match `docs/2026-05-03-cmake-warnings-rationale.md`.

- [ ] **Step 2: Run fast-tier tests**

```bash
ctest --test-dir build-dev -j 8 -E "tst_realistic|tst_benchmark" --output-on-failure
```

Expected: all pass (78/78 per the worktree CLAUDE.md baseline).

- [ ] **Step 3: Refresh `compile_commands.json` symlink for clangd**

```bash
ln -sf build-dev/compile_commands.json compile_commands.json
```

- [ ] **Step 4: No commit (verification only)**

### Task 0.3: D2 test directory scaffolding

**Files:**
- Create: `libs/markoff-core/tests/d2/CMakeLists.txt`
- Modify: `libs/markoff-core/tests/CMakeLists.txt` (add `add_subdirectory(d2)`)

- [ ] **Step 1: Create the d2 subdirectory CMakeLists**

`libs/markoff-core/tests/d2/CMakeLists.txt`:
```cmake
# D2 test executables. Each tst_* target is added by its respective phase task.
# Keep this file minimal; tasks add their own targets.
```

- [ ] **Step 2: Wire it from the parent**

Append to `libs/markoff-core/tests/CMakeLists.txt`:
```cmake
add_subdirectory(d2)
```

- [ ] **Step 3: Re-configure to verify**

```bash
cmake -S . -B build-dev && cmake --build build-dev -j 8 --target markoff-foundation
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-core/tests/d2/CMakeLists.txt libs/markoff-core/tests/CMakeLists.txt
git commit -m "d2(foundation): test scaffolding — d2/ test subdirectory"
```

---

## Phase 1: Foundation primitives

### Task 1.1: `CausalLwwMap` header skeleton + first test

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/CausalLwwMap.h`
- Create: `libs/markoff-core/tests/d2/tst_causal_lww_map.cpp`
- Modify: `libs/markoff-core/tests/d2/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/d2/tst_causal_lww_map.cpp`:
```cpp
#include <QTest>
#include <markoff-foundation/CausalLwwMap.h>

class TstCausalLwwMap : public QObject {
    Q_OBJECT
private slots:
    void emptyMap_getReturnsNullopt();
};

void TstCausalLwwMap::emptyMap_getReturnsNullopt() {
    Markoff::CausalLwwMap<int, QString> map(/*replicaId=*/1);
    QCOMPARE(map.get(42).has_value(), false);
}

QTEST_GUILESS_MAIN(TstCausalLwwMap)
#include "tst_causal_lww_map.moc"
```

- [ ] **Step 2: Wire the test into CMake**

Append to `libs/markoff-core/tests/d2/CMakeLists.txt`:
```cmake
qt_add_executable(tst_causal_lww_map tst_causal_lww_map.cpp)
target_link_libraries(tst_causal_lww_map PRIVATE Qt6::Test markoff-foundation)
add_test(NAME tst_causal_lww_map COMMAND tst_causal_lww_map)
```

- [ ] **Step 3: Run test to verify failure**

```bash
cmake --build build-dev -j 8 --target tst_causal_lww_map 2>&1 | tail -5
```

Expected: FAIL (`CausalLwwMap.h` not found).

- [ ] **Step 4: Write minimal header**

`libs/markoff-core/include/markoff-foundation/CausalLwwMap.h`:
```cpp
#pragma once
#include <cstdint>
#include <optional>
#include <QHash>

namespace Markoff {

template <typename Key, typename Value>
class CausalLwwMap {
public:
    explicit CausalLwwMap(uint16_t replicaId) : m_replicaId(replicaId) {}

    std::optional<Value> get(const Key &k) const {
        auto it = m_entries.constFind(k);
        if (it == m_entries.cend()) return std::nullopt;
        return it->value;
    }

private:
    struct Entry { Value value; uint64_t stamp; };
    uint16_t m_replicaId;
    QHash<Key, Entry> m_entries;
};

}  // namespace Markoff
```

- [ ] **Step 5: Run test to verify pass**

```bash
cmake --build build-dev -j 8 --target tst_causal_lww_map && ctest --test-dir build-dev -R '^tst_causal_lww_map$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/CausalLwwMap.h libs/markoff-core/tests/d2/tst_causal_lww_map.cpp libs/markoff-core/tests/d2/CMakeLists.txt
git commit -m "d2(foundation): CausalLwwMap header skeleton + empty-get test"
```

### Task 1.2: `CausalLwwMap` set / get / remove

- [ ] **Step 1: Write the failing tests**

Append to `libs/markoff-core/tests/d2/tst_causal_lww_map.cpp` `private slots` block:
```cpp
    void set_then_getReturnsValue();
    void setOverwrites_higherStampWins();
    void remove_clearsEntry();
```

Implementations (append after class):
```cpp
void TstCausalLwwMap::set_then_getReturnsValue() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(42, QStringLiteral("hello"), Markoff::CausalStamp{1, 1});
    QCOMPARE(map.get(42).value(), QStringLiteral("hello"));
}

void TstCausalLwwMap::setOverwrites_higherStampWins() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(42, QStringLiteral("first"), Markoff::CausalStamp{1, 1});
    map.set(42, QStringLiteral("second"), Markoff::CausalStamp{1, 2});
    QCOMPARE(map.get(42).value(), QStringLiteral("second"));
    // Out-of-order arrival: lower stamp arrives later, must NOT win.
    map.set(42, QStringLiteral("stale"), Markoff::CausalStamp{1, 1});
    QCOMPARE(map.get(42).value(), QStringLiteral("second"));
}

void TstCausalLwwMap::remove_clearsEntry() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(42, QStringLiteral("x"), Markoff::CausalStamp{1, 1});
    map.remove(42, Markoff::CausalStamp{1, 2});
    QCOMPARE(map.get(42).has_value(), false);
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build-dev -j 8 --target tst_causal_lww_map 2>&1 | tail -5
```

Expected: FAIL (compile errors — `CausalStamp`, `set`, `remove` undefined).

- [ ] **Step 3: Add `CausalStamp` and methods to header**

In `CausalLwwMap.h`:
```cpp
namespace Markoff {

struct CausalStamp {
    uint16_t replicaId;
    uint64_t counter;

    // Lamport-style ordering: counter primary, replicaId tiebreak.
    bool operator<(const CausalStamp &o) const noexcept {
        return std::tie(counter, replicaId) < std::tie(o.counter, o.replicaId);
    }
    bool operator==(const CausalStamp &o) const noexcept {
        return counter == o.counter && replicaId == o.replicaId;
    }
};

template <typename Key, typename Value>
class CausalLwwMap {
public:
    explicit CausalLwwMap(uint16_t replicaId) : m_replicaId(replicaId) {}

    void set(const Key &k, Value v, CausalStamp s) {
        auto it = m_entries.find(k);
        if (it == m_entries.end() || it->stamp < s) {
            m_entries.insert(k, Entry{std::move(v), s, /*tombstone=*/false});
        }
    }

    void remove(const Key &k, CausalStamp s) {
        auto it = m_entries.find(k);
        if (it == m_entries.end() || it->stamp < s) {
            m_entries.insert(k, Entry{Value{}, s, /*tombstone=*/true});
        }
    }

    std::optional<Value> get(const Key &k) const {
        auto it = m_entries.constFind(k);
        if (it == m_entries.cend() || it->tombstone) return std::nullopt;
        return it->value;
    }

private:
    struct Entry { Value value; CausalStamp stamp; bool tombstone; };
    uint16_t m_replicaId;
    QHash<Key, Entry> m_entries;
};

}  // namespace Markoff
```

- [ ] **Step 4: Run to verify pass**

```bash
cmake --build build-dev -j 8 --target tst_causal_lww_map && ctest --test-dir build-dev -R '^tst_causal_lww_map$' --output-on-failure
```

Expected: 4 tests pass.

- [ ] **Step 5: Commit**

```bash
git add -u
git commit -m "d2(foundation): CausalLwwMap set/get/remove with stamp ordering"
```

### Task 1.2b: `CausalLwwMap` next-stamp convenience + `OpId` derivation

This convenience layer is what Phase 4–6 sites use; underlying primitive is the explicit-stamp form from Task 1.2.

- [ ] **Step 1: Test**

```cpp
void TstCausalLwwMap::setWithNextStamp_returnsMonotonicOpId();

void TstCausalLwwMap::setWithNextStamp_returnsMonotonicOpId() {
    Markoff::CausalLwwMap<int, QString> map(/*replicaId=*/1);
    auto op1 = map.setWithNextStamp(7, QStringLiteral("a"));
    auto op2 = map.setWithNextStamp(7, QStringLiteral("b"));
    QVERIFY(op2 > op1);
}
```

- [ ] **Step 2: Add to header**

```cpp
public:
    using OpId = uint64_t;  // (replicaId << 48) | counter — matches UndoLog::OpId

    OpId setWithNextStamp(const Key &k, Value v) {
        CausalStamp s = nextStamp();
        set(k, std::move(v), s);
        return stampToOpId(s);
    }
    OpId removeWithNextStamp(const Key &k) {
        CausalStamp s = nextStamp();
        remove(k, s);
        return stampToOpId(s);
    }
    CausalStamp nextStamp() { return CausalStamp{m_replicaId, ++m_localCounter}; }

    static OpId stampToOpId(CausalStamp s) noexcept {
        return (static_cast<uint64_t>(s.replicaId) << 48) | (s.counter & 0x0000FFFFFFFFFFFFull);
    }
    static CausalStamp opIdToStamp(OpId id) noexcept {
        return CausalStamp{static_cast<uint16_t>(id >> 48), id & 0x0000FFFFFFFFFFFFull};
    }

private:
    uint64_t m_localCounter = 0;
```

- [ ] **Step 3: Run, verify, commit**

```bash
git add -u
git commit -m "d2(foundation): CausalLwwMap nextStamp + OpId derivation convenience"
```

### Task 1.3: `CausalLwwMap` change callback

- [ ] **Step 1: Write the failing test**

Append:
```cpp
    void setOnChange_firesOnEachSet();

void TstCausalLwwMap::setOnChange_firesOnEachSet() {
    Markoff::CausalLwwMap<int, QString> map(1);
    int callCount = 0;
    int lastKey = -1;
    std::optional<QString> lastOld;
    std::optional<QString> lastNew;
    map.setOnChange([&](int k, std::optional<QString> oldV, std::optional<QString> newV) {
        ++callCount;
        lastKey = k;
        lastOld = oldV;
        lastNew = newV;
    });

    map.set(7, QStringLiteral("a"), {1, 1});
    QCOMPARE(callCount, 1);
    QCOMPARE(lastKey, 7);
    QCOMPARE(lastOld.has_value(), false);
    QCOMPARE(lastNew.value(), QStringLiteral("a"));

    map.set(7, QStringLiteral("b"), {1, 2});
    QCOMPARE(callCount, 2);
    QCOMPARE(lastOld.value(), QStringLiteral("a"));
    QCOMPARE(lastNew.value(), QStringLiteral("b"));

    // Stale write: no callback fire.
    map.set(7, QStringLiteral("c"), {1, 1});
    QCOMPARE(callCount, 2);
}
```

- [ ] **Step 2: Verify failure, add callback to header, verify pass**

In `CausalLwwMap.h` add to public:
```cpp
    using ChangeCallback = std::function<void(const Key &, std::optional<Value>, std::optional<Value>)>;
    void setOnChange(ChangeCallback cb) { m_onChange = std::move(cb); }
```

Modify `set` and `remove` to fire `m_onChange` only when the entry actually changes (compare old vs new value before/after the write). Also include `<functional>` at the top.

- [ ] **Step 3: Commit**

```bash
git add -u
git commit -m "d2(foundation): CausalLwwMap change callback fires on accepted writes only"
```

### Task 1.4: `CausalLwwMap` undo/redo

- [ ] **Step 1: Write the failing tests**

```cpp
    void undo_revertsLastWrite();
    void redo_replaysAfterUndo();
    void undo_acrossMultipleKeys();

void TstCausalLwwMap::undo_revertsLastWrite() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(1, QStringLiteral("a"), {1, 1});
    map.set(1, QStringLiteral("b"), {1, 2});
    map.undo();
    QCOMPARE(map.get(1).value(), QStringLiteral("a"));
}

void TstCausalLwwMap::redo_replaysAfterUndo() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(1, QStringLiteral("a"), {1, 1});
    map.set(1, QStringLiteral("b"), {1, 2});
    map.undo();
    map.redo();
    QCOMPARE(map.get(1).value(), QStringLiteral("b"));
}

void TstCausalLwwMap::undo_acrossMultipleKeys() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(1, QStringLiteral("a"), {1, 1});
    map.set(2, QStringLiteral("b"), {1, 2});
    map.undo();  // undoes set(2, b)
    QCOMPARE(map.get(2).has_value(), false);
    QCOMPARE(map.get(1).value(), QStringLiteral("a"));
}
```

- [ ] **Step 2: Add undo/redo machinery to header**

Add to `CausalLwwMap`:
```cpp
public:
    void undo() {
        if (m_undoStack.empty()) return;
        UndoOp op = m_undoStack.back();
        m_undoStack.pop_back();
        m_redoStack.push_back(op);
        applyUndoOp(op, /*forward=*/false);
    }

    void redo() {
        if (m_redoStack.empty()) return;
        UndoOp op = m_redoStack.back();
        m_redoStack.pop_back();
        m_undoStack.push_back(op);
        applyUndoOp(op, /*forward=*/true);
    }

private:
    struct UndoOp {
        Key key;
        std::optional<Entry> before;  // nullopt if key didn't exist
        Entry after;
    };
    std::vector<UndoOp> m_undoStack;
    std::vector<UndoOp> m_redoStack;

    void recordUndo(const Key &k, std::optional<Entry> before, Entry after) {
        m_undoStack.push_back({k, std::move(before), std::move(after)});
        m_redoStack.clear();  // new edit invalidates redo
    }

    void applyUndoOp(const UndoOp &op, bool forward) {
        const Entry &target = forward ? op.after : op.before.value_or(Entry{});
        if (forward || op.before.has_value()) {
            m_entries.insert(op.key, target);
        } else {
            m_entries.remove(op.key);
        }
        if (m_onChange) {
            // Notify with appropriate old/new values; details elided for brevity but the implementer
            // computes pre-state from m_entries before the in-place insert/remove and fires after.
        }
    }
```

Modify `set` and `remove` to call `recordUndo` after a successful write.

- [ ] **Step 3: Run, verify pass, commit**

```bash
git add -u
git commit -m "d2(foundation): CausalLwwMap undo/redo with per-op stacks"
```

### Task 1.5: `CausalLwwMap::compact(watermark)`

- [ ] **Step 1: Write the failing test**

```cpp
    void compact_dropsEntriesBelowWatermark();

void TstCausalLwwMap::compact_dropsEntriesBelowWatermark() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(1, QStringLiteral("a"), {1, 1});
    map.set(2, QStringLiteral("b"), {1, 5});
    map.set(3, QStringLiteral("c"), {1, 10});
    // Compact at watermark 5 — undo entries for stamps 1, 5 collapse; stamp 10 survives.
    map.compact(Markoff::CausalStamp{1, 5});

    // Get still returns current values (state isn't lost; only undo history collapses).
    QCOMPARE(map.get(1).value(), QStringLiteral("a"));
    QCOMPARE(map.get(2).value(), QStringLiteral("b"));
    QCOMPARE(map.get(3).value(), QStringLiteral("c"));

    // Undo can no longer reach the compacted ops.
    map.undo();
    QCOMPARE(map.get(3).has_value(), false);  // only the un-compacted stamp-10 op undid
    map.undo();
    QCOMPARE(map.get(2).value(), QStringLiteral("b"));  // can't undo further
}
```

- [ ] **Step 2: Add `compact()` to header**

```cpp
public:
    void compact(CausalStamp watermark) {
        // Drop undo entries whose `after.stamp` is at or below the watermark.
        m_undoStack.erase(
            std::remove_if(m_undoStack.begin(), m_undoStack.end(),
                [&](const UndoOp &op) { return !(watermark < op.after.stamp); }),
            m_undoStack.end()
        );
    }
```

- [ ] **Step 3: Run, verify pass, commit**

```bash
git add -u
git commit -m "d2(foundation): CausalLwwMap compact(watermark) drops collapsed undo entries"
```

### Task 1.6: `CausalLwwMap::applyRemote(RemoteOp)`

- [ ] **Step 1: Write the failing test**

```cpp
    void applyRemote_acceptsForeignWrite();
    void applyRemote_doesNotEnterLocalUndoStack();

void TstCausalLwwMap::applyRemote_acceptsForeignWrite() {
    Markoff::CausalLwwMap<int, QString> map(/*replicaId=*/1);
    Markoff::CausalLwwMap<int, QString>::RemoteOp op{
        /*key=*/7, /*value=*/QStringLiteral("from-replica-2"),
        /*stamp=*/{2, 1}, /*tombstone=*/false
    };
    map.applyRemote(op);
    QCOMPARE(map.get(7).value(), QStringLiteral("from-replica-2"));
}

void TstCausalLwwMap::applyRemote_doesNotEnterLocalUndoStack() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(1, QStringLiteral("local"), {1, 1});
    Markoff::CausalLwwMap<int, QString>::RemoteOp op{2, QStringLiteral("remote"), {2, 1}, false};
    map.applyRemote(op);
    map.undo();  // undoes only the local set(1, local)
    QCOMPARE(map.get(1).has_value(), false);
    QCOMPARE(map.get(2).value(), QStringLiteral("remote"));  // remote write survives
}
```

- [ ] **Step 2: Add `RemoteOp` and `applyRemote` to header**

```cpp
public:
    struct RemoteOp { Key key; Value value; CausalStamp stamp; bool tombstone; };

    void applyRemote(const RemoteOp &op) {
        // Apply identically to set/remove but DO NOT recordUndo — remote ops
        // are not part of the local undo history.
        auto it = m_entries.find(op.key);
        if (it == m_entries.end() || it->stamp < op.stamp) {
            m_entries.insert(op.key, Entry{op.value, op.stamp, op.tombstone});
            // Fire change callback per usual.
        }
    }
```

- [ ] **Step 3: Run, verify pass, commit**

```bash
git add -u
git commit -m "d2(foundation): CausalLwwMap applyRemote — foreign writes bypass local undo"
```

### Task 1.7: `BlockId` value type

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/BlockId.h`
- Create: `libs/markoff-core/tests/d2/tst_block_id.cpp`
- Modify: `libs/markoff-core/tests/d2/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tst_block_id.cpp`:
```cpp
#include <QTest>
#include <markoff-foundation/BlockId.h>

class TstBlockId : public QObject {
    Q_OBJECT
private slots:
    void defaultConstructed_isNull();
    void distinctRawIds_compareUnequal();
    void sameRawId_comparesEqual();
    void hashable_canKeyQHash();
};

void TstBlockId::defaultConstructed_isNull() {
    Markoff::BlockId id;
    QVERIFY(id.isNull());
}

void TstBlockId::distinctRawIds_compareUnequal() {
    Markoff::BlockId a = Markoff::BlockId::fromRaw(1);
    Markoff::BlockId b = Markoff::BlockId::fromRaw(2);
    QVERIFY(a != b);
}

void TstBlockId::sameRawId_comparesEqual() {
    Markoff::BlockId a = Markoff::BlockId::fromRaw(42);
    Markoff::BlockId b = Markoff::BlockId::fromRaw(42);
    QVERIFY(a == b);
}

void TstBlockId::hashable_canKeyQHash() {
    QHash<Markoff::BlockId, QString> map;
    map.insert(Markoff::BlockId::fromRaw(1), QStringLiteral("one"));
    QCOMPARE(map.value(Markoff::BlockId::fromRaw(1)), QStringLiteral("one"));
}

QTEST_GUILESS_MAIN(TstBlockId)
#include "tst_block_id.moc"
```

CMake:
```cmake
qt_add_executable(tst_block_id tst_block_id.cpp)
target_link_libraries(tst_block_id PRIVATE Qt6::Test markoff-foundation)
add_test(NAME tst_block_id COMMAND tst_block_id)
```

- [ ] **Step 2: Verify failure, write minimal header**

`BlockId.h`:
```cpp
#pragma once
#include <cstdint>
#include <QHashFunctions>

namespace Markoff {

class BlockId {
public:
    BlockId() noexcept = default;
    static BlockId fromRaw(uint64_t raw) noexcept { BlockId b; b.m_raw = raw; return b; }
    bool isNull() const noexcept { return m_raw == 0; }
    uint64_t raw() const noexcept { return m_raw; }
    bool operator==(const BlockId &o) const noexcept { return m_raw == o.m_raw; }
    bool operator!=(const BlockId &o) const noexcept { return m_raw != o.m_raw; }
private:
    uint64_t m_raw = 0;  // 0 = null
};

inline size_t qHash(const BlockId &id, size_t seed = 0) noexcept {
    return ::qHash(id.raw(), seed);
}

}  // namespace Markoff
```

- [ ] **Step 3: Run, verify pass, commit**

```bash
git add -u
git commit -m "d2(foundation): BlockId value type — null, equality, QHash key"
```

### Task 1.8: `BlockEdit`, `StructuralOp`, `BlockKind`

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/BlockEdit.h`
- Create: `libs/markoff-core/include/markoff-foundation/StructuralOp.h`
- Create: `libs/markoff-core/include/markoff-foundation/BlockKind.h`
- Test: extend `tst_block_id.cpp` (or new `tst_block_types.cpp`)

- [ ] **Step 1: Write tests for the three types**

```cpp
void TstBlockTypes::blockKind_paragraphAndHeadingDistinct() {
    QVERIFY(Markoff::BlockKind::Paragraph != Markoff::BlockKind::Heading);
}

void TstBlockTypes::blockEdit_constructionAndAccess() {
    Markoff::BlockEdit edit{
        Markoff::BlockId::fromRaw(1), /*offset=*/3, /*removed=*/0, /*inserted=*/QByteArray("x")
    };
    QCOMPARE(edit.blockId, Markoff::BlockId::fromRaw(1));
    QCOMPARE(edit.withinBlockByteOffset, 3u);
    QCOMPARE(edit.removedBytes, 0u);
    QCOMPARE(edit.insertedUtf8, QByteArray("x"));
}

void TstBlockTypes::structuralOp_insertEntryVariant() {
    Markoff::StructuralOp op = Markoff::StructuralOp::InsertEntry{
        Markoff::BlockId::fromRaw(1), Markoff::BlockKind::Paragraph
    };
    QVERIFY(std::holds_alternative<Markoff::StructuralOp::InsertEntry>(op.payload));
}
```

- [ ] **Step 2: Write the headers**

`BlockKind.h`:
```cpp
#pragma once
#include <cstdint>
namespace Markoff {
enum class BlockKind : uint8_t {
    Paragraph, Heading, CodeBlock, ListItem, BlockQuote,
    HorizontalRule, Image, Math, Mermaid, HtmlBlock, Table,
};
}
```

`BlockEdit.h`:
```cpp
#pragma once
#include <markoff-foundation/BlockId.h>
#include <QByteArray>
namespace Markoff {
struct BlockEdit {
    BlockId blockId;
    uint32_t withinBlockByteOffset;
    uint32_t removedBytes;
    QByteArray insertedUtf8;
};
}
```

`StructuralOp.h`:
```cpp
#pragma once
#include <markoff-foundation/BlockId.h>
#include <markoff-foundation/BlockKind.h>
#include <variant>
namespace Markoff {
struct StructuralOp {
    struct InsertEntry { BlockId afterBlockId; BlockKind kind; };
    struct RemoveEntry { BlockId blockId; };
    struct ChangeKind  { BlockId blockId; BlockKind newKind; };
    std::variant<InsertEntry, RemoveEntry, ChangeKind> payload;
};
}
```

- [ ] **Step 3: Run, verify pass, commit**

```bash
git add -u
git commit -m "d2(foundation): BlockEdit + StructuralOp + BlockKind value types"
```

---

## Phase 2: UndoLog

### Task 2.1: `UndoLog::Transaction` RAII skeleton

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/UndoLog.h`
- Create: `libs/markoff-core/src/UndoLog.cpp`
- Create: `libs/markoff-core/tests/d2/tst_undo_log.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <QTest>
#include <markoff-foundation/UndoLog.h>

class TstUndoLog : public QObject {
    Q_OBJECT
private slots:
    void singleTransaction_producesOneEntry();
    void emptyTransaction_producesNoEntry();
    void nestedTransaction_joinsOuter();
};

void TstUndoLog::singleTransaction_producesOneEntry() {
    Markoff::UndoLog log;
    {
        Markoff::UndoLog::Transaction t(log);
        t.registerOp(Markoff::CrdtTarget::idList(), 42);
    }
    QCOMPARE(log.entryCount(), 1);
}

void TstUndoLog::emptyTransaction_producesNoEntry() {
    Markoff::UndoLog log;
    { Markoff::UndoLog::Transaction t(log); }
    QCOMPARE(log.entryCount(), 0);
}

void TstUndoLog::nestedTransaction_joinsOuter() {
    Markoff::UndoLog log;
    {
        Markoff::UndoLog::Transaction outer(log);
        outer.registerOp(Markoff::CrdtTarget::idList(), 1);
        {
            Markoff::UndoLog::Transaction inner(log);
            inner.registerOp(Markoff::CrdtTarget::buffer(Markoff::BlockId::fromRaw(7)), 2);
        }
    }
    QCOMPARE(log.entryCount(), 1);
    QCOMPARE(log.lastEntry().targets.size(), 2);
}
```

- [ ] **Step 2: Write the header**

`UndoLog.h`:
```cpp
#pragma once
#include <markoff-foundation/BlockId.h>
#include <variant>
#include <vector>

namespace Markoff {

struct CrdtTarget {
    struct IdListT {};
    struct KindTagMapT {};
    struct BlockAttrsMapT {};
    struct FrontmatterMapT {};
    struct LinkRefMapT {};
    struct FootnoteDefMapT {};
    struct BufferT { BlockId blockId; };
    std::variant<IdListT, KindTagMapT, BlockAttrsMapT, FrontmatterMapT, LinkRefMapT, FootnoteDefMapT, BufferT> kind;

    static CrdtTarget idList() { return {IdListT{}}; }
    static CrdtTarget kindTagMap() { return {KindTagMapT{}}; }
    static CrdtTarget blockAttrsMap() { return {BlockAttrsMapT{}}; }
    static CrdtTarget frontmatterMap() { return {FrontmatterMapT{}}; }
    static CrdtTarget linkRefMap() { return {LinkRefMapT{}}; }
    static CrdtTarget footnoteDefMap() { return {FootnoteDefMapT{}}; }
    static CrdtTarget buffer(BlockId id) { return {BufferT{id}}; }
};

using OpId = uint64_t;
using ActionId = uint64_t;

struct UndoEntry {
    ActionId actionId;
    std::vector<std::pair<CrdtTarget, OpId>> targets;
};

class UndoLog {
public:
    class Transaction {
    public:
        explicit Transaction(UndoLog &log);
        ~Transaction();
        void registerOp(CrdtTarget target, OpId opId);
        void rollback() noexcept;
    private:
        UndoLog &m_log;
        bool m_isOutermost;
        bool m_rolledBack = false;
    };

    size_t entryCount() const noexcept { return m_entries.size(); }
    const UndoEntry &lastEntry() const { return m_entries.back(); }

private:
    friend class Transaction;
    std::vector<UndoEntry> m_entries;
    std::vector<UndoEntry> m_redoStack;
    UndoEntry *m_pendingEntry = nullptr;  // nullptr when no transaction open
    int m_nestingDepth = 0;
    ActionId m_nextActionId = 1;
};

}
```

- [ ] **Step 3: Write `UndoLog.cpp`**

```cpp
#include <markoff-foundation/UndoLog.h>
namespace Markoff {

UndoLog::Transaction::Transaction(UndoLog &log) : m_log(log) {
    m_isOutermost = (log.m_nestingDepth == 0);
    if (m_isOutermost) {
        log.m_entries.push_back(UndoEntry{log.m_nextActionId++, {}});
        log.m_pendingEntry = &log.m_entries.back();
    }
    ++log.m_nestingDepth;
}

UndoLog::Transaction::~Transaction() {
    --m_log.m_nestingDepth;
    if (m_isOutermost) {
        if (m_rolledBack || m_log.m_pendingEntry->targets.empty()) {
            m_log.m_entries.pop_back();
        }
        m_log.m_pendingEntry = nullptr;
    }
}

void UndoLog::Transaction::registerOp(CrdtTarget target, OpId opId) {
    if (!m_log.m_pendingEntry) return;  // no transaction open; defensive
    m_log.m_pendingEntry->targets.emplace_back(std::move(target), opId);
}

void UndoLog::Transaction::rollback() noexcept { m_rolledBack = true; }

}
```

- [ ] **Step 4: Run, verify pass, commit**

```bash
git add -u
git commit -m "d2(foundation): UndoLog::Transaction RAII — open/close/nest/empty"
```

### Task 2.2: `UndoLog::undo()` and `redo()` dispatch (mock targets)

- [ ] **Step 1: Write tests for dispatch order using a mock target dispatcher**

The real dispatcher needs handles to actual CRDTs (which don't exist yet). Use a callback for mockable testing:

```cpp
void TstUndoLog::undo_dispatchesTargetsInReverseOpOrder();

void TstUndoLog::undo_dispatchesTargetsInReverseOpOrder() {
    Markoff::UndoLog log;
    std::vector<Markoff::OpId> dispatched;
    log.setDispatcher([&](const Markoff::CrdtTarget &, Markoff::OpId opId, bool /*forward*/) {
        dispatched.push_back(opId);
    });

    {
        Markoff::UndoLog::Transaction t(log);
        t.registerOp(Markoff::CrdtTarget::idList(), 100);
        t.registerOp(Markoff::CrdtTarget::kindTagMap(), 101);
        t.registerOp(Markoff::CrdtTarget::buffer(Markoff::BlockId::fromRaw(1)), 102);
    }
    log.undo();
    QCOMPARE(dispatched.size(), 3u);
    QCOMPARE(dispatched[0], 102u);  // reverse op-order: buffer first (was last in)
    QCOMPARE(dispatched[1], 101u);
    QCOMPARE(dispatched[2], 100u);
}
```

- [ ] **Step 2: Add dispatcher to UndoLog**

In `UndoLog.h`:
```cpp
public:
    using Dispatcher = std::function<void(const CrdtTarget &, OpId, bool forward)>;
    void setDispatcher(Dispatcher d) { m_dispatcher = std::move(d); }

    void undo();
    void redo();

private:
    Dispatcher m_dispatcher;
```

In `UndoLog.cpp`:
```cpp
void UndoLog::undo() {
    if (m_entries.empty() || !m_dispatcher) return;
    UndoEntry e = std::move(m_entries.back());
    m_entries.pop_back();
    for (auto it = e.targets.rbegin(); it != e.targets.rend(); ++it) {
        m_dispatcher(it->first, it->second, /*forward=*/false);
    }
    m_redoStack.push_back(std::move(e));
}

void UndoLog::redo() {
    if (m_redoStack.empty() || !m_dispatcher) return;
    UndoEntry e = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    for (auto &[target, opId] : e.targets) {
        m_dispatcher(target, opId, /*forward=*/true);
    }
    m_entries.push_back(std::move(e));
}
```

- [ ] **Step 3: Run, verify pass, commit**

```bash
git add -u
git commit -m "d2(foundation): UndoLog undo/redo dispatch in reverse op-order"
```

### Task 2.3: `UndoLog::undoForBlock` permissive

- [ ] **Step 1: Write the test**

```cpp
void TstUndoLog::undoForBlock_picksMostRecentEntryThatTouchesThisBlock();

void TstUndoLog::undoForBlock_picksMostRecentEntryThatTouchesThisBlock() {
    Markoff::UndoLog log;
    std::vector<Markoff::OpId> dispatched;
    log.setDispatcher([&](const Markoff::CrdtTarget &, Markoff::OpId opId, bool) {
        dispatched.push_back(opId);
    });

    auto blkA = Markoff::BlockId::fromRaw(1);
    auto blkB = Markoff::BlockId::fromRaw(2);

    // Action 1: edit block A
    { Markoff::UndoLog::Transaction t(log); t.registerOp(Markoff::CrdtTarget::buffer(blkA), 10); }
    // Action 2: edit block B
    { Markoff::UndoLog::Transaction t(log); t.registerOp(Markoff::CrdtTarget::buffer(blkB), 20); }
    // Action 3: structural Enter that creates block C; touches IdList + kindTag + new buffer
    auto blkC = Markoff::BlockId::fromRaw(3);
    {
        Markoff::UndoLog::Transaction t(log);
        t.registerOp(Markoff::CrdtTarget::idList(), 30);
        t.registerOp(Markoff::CrdtTarget::kindTagMap(), 31);
        t.registerOp(Markoff::CrdtTarget::buffer(blkC), 32);
    }

    // undoForBlock(B) should pick Action 2 (most recent that touches B), not Action 3.
    log.undoForBlock(blkB);
    QCOMPARE(dispatched, std::vector<Markoff::OpId>{20});

    // undoForBlock(C) should pick Action 3 and dispatch all three of its ops in reverse.
    dispatched.clear();
    log.undoForBlock(blkC);
    QCOMPARE(dispatched, (std::vector<Markoff::OpId>{32, 31, 30}));
}
```

- [ ] **Step 2: Add `undoForBlock` to header + impl**

```cpp
void UndoLog::undoForBlock(BlockId block) {
    if (!m_dispatcher) return;
    auto it = std::find_if(m_entries.rbegin(), m_entries.rend(), [&](const UndoEntry &e) {
        return std::any_of(e.targets.begin(), e.targets.end(), [&](const auto &p) {
            if (auto *b = std::get_if<CrdtTarget::BufferT>(&p.first.kind)) {
                return b->blockId == block;
            }
            return false;
        });
    });
    if (it == m_entries.rend()) return;
    UndoEntry e = std::move(*it);
    m_entries.erase(std::next(it).base());
    for (auto rit = e.targets.rbegin(); rit != e.targets.rend(); ++rit) {
        m_dispatcher(rit->first, rit->second, /*forward=*/false);
    }
    m_redoStack.push_back(std::move(e));
}
```

- [ ] **Step 3: Run, verify pass, commit**

```bash
git add -u
git commit -m "d2(foundation): UndoLog::undoForBlock permissive — picks most recent entry touching block"
```

### Task 2.4: `UndoLog` coalescing rule

- [ ] **Step 1: Write the test**

```cpp
void TstUndoLog::coalescing_extendsPreviousEntry();
void TstUndoLog::coalescing_breaksOnFocusChange();
void TstUndoLog::coalescing_breaksOnIdleThreshold();
void TstUndoLog::coalescing_breaksOnStructuralOp();

void TstUndoLog::coalescing_extendsPreviousEntry() {
    Markoff::UndoLog log;
    auto blk = Markoff::BlockId::fromRaw(1);
    Markoff::CoalesceContext ctx{blk, /*isPrintable=*/true, /*timestampMs=*/0};
    log.maybeCoalesceOrTransaction(ctx, [&](Markoff::UndoLog::Transaction &t) {
        t.registerOp(Markoff::CrdtTarget::buffer(blk), 1);
    });
    ctx.timestampMs = 100;  // <1000ms; same block; printable
    log.maybeCoalesceOrTransaction(ctx, [&](Markoff::UndoLog::Transaction &t) {
        t.registerOp(Markoff::CrdtTarget::buffer(blk), 2);
    });
    QCOMPARE(log.entryCount(), 1u);
    QCOMPARE(log.lastEntry().targets.size(), 2u);
}
```

(The other three coalesce-break tests follow the same shape; vary the `ctx` field that should break the chain and assert two entries result.)

- [ ] **Step 2: Add coalesce machinery**

In `UndoLog.h`:
```cpp
struct CoalesceContext {
    BlockId block;
    bool isPrintable;
    qint64 timestampMs;
    int focusGeneration = 0;  // bumps when focus changes
};

template <typename Body>
void maybeCoalesceOrTransaction(const CoalesceContext &ctx, Body &&body) {
    bool canExtend = !m_entries.empty() && m_lastCoalesceCtx.has_value()
        && ctx.isPrintable && m_lastCoalesceCtx->isPrintable
        && ctx.block == m_lastCoalesceCtx->block
        && ctx.focusGeneration == m_lastCoalesceCtx->focusGeneration
        && (ctx.timestampMs - m_lastCoalesceCtx->timestampMs) < 1000;
    if (canExtend) {
        m_pendingEntry = &m_entries.back();
        ++m_nestingDepth;
        Transaction t(*this, /*pre-attached=*/true);  // overload that doesn't push a new entry
        body(t);
    } else {
        Transaction t(*this);
        body(t);
    }
    m_lastCoalesceCtx = ctx;
}

private:
    std::optional<CoalesceContext> m_lastCoalesceCtx;
```

(The implementation needs care; see `UndoLog.cpp` for the conditional logic. The "pre-attached" Transaction overload skips entry push and uses the existing `m_pendingEntry`.)

- [ ] **Step 3: Run, verify all four coalesce tests pass, commit**

```bash
git add -u
git commit -m "d2(foundation): UndoLog coalescing — extend on printable+sameblock+focus+idle"
```

### Task 2.5: `UndoLog::compact(watermark)` — trim collapsed entries

- [ ] **Step 1: Test**

```cpp
void TstUndoLog::compact_dropsEntriesAllOfWhoseOpsAreCollapsed();
```

(Test creates entries, simulates each target reporting "this op is past my watermark", asserts entry is dropped.)

- [ ] **Step 2: Add `compact` to UndoLog**

```cpp
public:
    using IsCollapsedQuery = std::function<bool(const CrdtTarget &, OpId)>;
    void compact(IsCollapsedQuery query) {
        m_entries.erase(
            std::remove_if(m_entries.begin(), m_entries.end(), [&](const UndoEntry &e) {
                return std::all_of(e.targets.begin(), e.targets.end(),
                    [&](const auto &p) { return query(p.first, p.second); });
            }),
            m_entries.end()
        );
    }
```

- [ ] **Step 3: Commit**

```bash
git add -u
git commit -m "d2(foundation): UndoLog compact — drops entries whose ops are all collapsed"
```

---

## Phase 3: TextAnchor + BlockAnchor reshape

### Task 3.1: Reshape `TextAnchor` to wrap `(BlockId, Crdt::Anchor)`

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/TextAnchor.h`
- Modify: `libs/markoff-core/src/AnchorConversion.h`
- Test: existing `tst_text_anchor.cpp` (rewrite tests for new shape)

- [ ] **Step 1: Read current TextAnchor**

```bash
cat libs/markoff-core/include/markoff-foundation/TextAnchor.h
```

Capture the current public surface; the new `TextAnchor` must expose comparable methods.

- [ ] **Step 2: Write failing tests for new shape**

```cpp
void TstTextAnchor::carriesBlockIdAndPerBlockAnchor();
void TstTextAnchor::nullAnchor_isDistinctFromAnyBlock();
```

- [ ] **Step 3: Reshape the type**

`TextAnchor.h`:
```cpp
#pragma once
#include <markoff-foundation/BlockId.h>
#include <cstdint>
namespace Markoff {

class TextAnchor {
public:
    TextAnchor() noexcept = default;
    static TextAnchor make(BlockId b, uint16_t replicaId, uint64_t charValue, bool rightBias) noexcept {
        TextAnchor a; a.m_block = b; a.m_replicaId = replicaId; a.m_charValue = charValue; a.m_rightBias = rightBias;
        return a;
    }

    bool isNull() const noexcept { return m_block.isNull(); }
    BlockId block() const noexcept { return m_block; }
    uint16_t replicaId() const noexcept { return m_replicaId; }
    uint64_t charValue() const noexcept { return m_charValue; }
    bool rightBias() const noexcept { return m_rightBias; }

    bool operator==(const TextAnchor &o) const noexcept {
        return m_block == o.m_block && m_replicaId == o.m_replicaId
            && m_charValue == o.m_charValue && m_rightBias == o.m_rightBias;
    }
private:
    BlockId m_block;
    uint16_t m_replicaId = 0;
    uint64_t m_charValue = 0;
    bool m_rightBias = false;
};

}
```

- [ ] **Step 4: Update `AnchorConversion.h` (foundation-internal)**

The `Detail::toCrdt(TextAnchor) → Crdt::Anchor` and `Detail::fromCrdt(BlockId, Crdt::Anchor) → TextAnchor` helpers.

- [ ] **Step 5: Run all foundation tests; expect compile errors in callers; fix call sites task-by-task**

```bash
cmake --build build-dev -j 8 --target markoff-foundation 2>&1 | grep error | head -20
```

- [ ] **Step 6: Fix all call sites in `markoff-foundation` (Selection, MarkoffDocument current accessors). Compile passes.**

- [ ] **Step 7: Run, verify pass, commit**

```bash
git add -u
git commit -m "d2(foundation): TextAnchor reshape — wraps (BlockId, per-block CRDT anchor)"
```

### Task 3.2: `BlockAnchor.h` becomes typedef for `BlockId`

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/BlockAnchor.h`

- [ ] **Step 1: Replace contents with typedef**

`BlockAnchor.h`:
```cpp
#pragma once
#include <markoff-foundation/BlockId.h>
namespace Markoff {
// Compatibility alias; new code should use BlockId directly.
using BlockAnchor = BlockId;
}
```

- [ ] **Step 2: Build; expect zero new errors (alias preserves all uses)**

```bash
cmake --build build-dev -j 8 2>&1 | grep error | head -20
```

- [ ] **Step 3: Commit**

```bash
git add -u
git commit -m "d2(foundation): BlockAnchor.h becomes typedef for BlockId"
```

---

## Phase 4: MarkoffDocument new internals

This is the largest phase. Each task touches `MarkoffDocument.h` / `.cpp` and may break existing callers; fix them inline.

### Task 4.1: `MarkoffDocument` private members swap

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`

- [ ] **Step 1: Add new private members alongside old; old still wired**

In `MarkoffDocument.h` private section:
```cpp
// New D2 internals (Phase 4 onwards). Old members coexist until Phase 14.
CollabText::Crdt::IdList m_idList;
QHash<BlockId, std::unique_ptr<CollabText::Crdt::Buffer>> m_blockBuffers;
KindTagMap m_kindTagMap;          // Phase 5 — typedef placeholder for now
BlockAttrsMap m_blockAttrsMap;
FrontmatterMap m_frontmatterMap;
LinkRefMap m_linkRefMap;
FootnoteDefMap m_footnoteDefMap;
UndoLog m_undoLog;
WatermarkCoordinator m_watermark;  // Phase 9
InlineParseCache m_inlineCache;    // Phase 10
```

(Stub the unwritten classes with `class WatermarkCoordinator { public: WatermarkCoordinator(MarkoffDocument&); };` etc., to satisfy the compiler; real implementations come in their phases.)

- [ ] **Step 2: Initialize them in constructor**

In `MarkoffDocument.cpp` ctor init list:
```cpp
m_idList(replicaId),
m_kindTagMap(replicaId),
m_blockAttrsMap(replicaId),
m_frontmatterMap(replicaId),
m_linkRefMap(replicaId),
m_footnoteDefMap(replicaId),
m_watermark(*this),
```

- [ ] **Step 3: Build; verify clean**

- [ ] **Step 4: Commit**

```bash
git add -u
git commit -m "d2(foundation): MarkoffDocument new D2 internals declared (old still wired)"
```

### Task 4.2: `applyBlockEdit(BlockEdit)` implementation

- [ ] **Step 1: Write the failing test**

`tst_d2_apply_block_edit.cpp`:
```cpp
#include <QTest>
#include <markoff-foundation/MarkoffDocument.h>

class TstD2ApplyBlockEdit : public QObject {
    Q_OBJECT
private slots:
    void emptyDoc_applyEditCreatesBufferIfBlockExists();
    void existingBlock_insertChar_appearsInBlockText();
    void existingBlock_removeBytes_dropsThem();
};

void TstD2ApplyBlockEdit::existingBlock_insertChar_appearsInBlockText() {
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown("hello\n");  // single paragraph "hello"
    Markoff::BlockId blk = doc.iterateBlocks().front();
    doc.applyBlockEdit(Markoff::BlockEdit{blk, /*offset=*/5, /*removed=*/0, "!"});
    QCOMPARE(doc.blockText(blk), QByteArray("hello!"));
}
// (other tests similar)
```

(Note: `loadFromMarkdown` doesn't exist yet — it's Phase 7. For this task, manually construct the doc by directly inserting a BlockId + Buffer. Add a `MarkoffDocument::testInsertBlock(kind, content)` test helper guarded by `#ifdef MARKOFF_TESTING` that disappears in Phase 7.)

- [ ] **Step 2: Implement `applyBlockEdit`**

In `MarkoffDocument.cpp`:
```cpp
void MarkoffDocument::applyBlockEdit(const BlockEdit &edit) {
    auto it = m_blockBuffers.find(edit.blockId);
    if (it == m_blockBuffers.end()) return;  // unknown block; defensive
    UndoLog::Transaction t(m_undoLog);
    auto opId = it.value()->applyLocal(edit.withinBlockByteOffset, edit.removedBytes, edit.insertedUtf8);
    t.registerOp(CrdtTarget::buffer(edit.blockId), opId);
}
```

- [ ] **Step 3: Run, verify pass, commit**

```bash
git add -u
git commit -m "d2(foundation): MarkoffDocument::applyBlockEdit — per-block edit + undo registration"
```

### Task 4.3: `applyStructural(StructuralOp)` implementation

- [ ] **Step 1: Write tests covering all three variants**

```cpp
void TstD2ApplyStructural::insertEntry_appendsBlock();
void TstD2ApplyStructural::removeEntry_dropsBlockFromIteration();
void TstD2ApplyStructural::changeKind_updatesKindTagMap();
```

- [ ] **Step 2: Implement**

```cpp
void MarkoffDocument::applyStructural(const StructuralOp &op) {
    UndoLog::Transaction t(m_undoLog);
    std::visit([&](auto &&payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, StructuralOp::InsertEntry>) {
            auto newAnchor = m_idList.insertAfter(/*after-anchor*/, /*new-id*/);
            BlockId newBlock = BlockId::fromRaw(/*new-id*/);
            m_blockBuffers[newBlock] = std::make_unique<CollabText::Crdt::Buffer>(replicaId());
            m_kindTagMap.set(newBlock, payload.kind, m_kindTagMap.nextStamp());
            t.registerOp(CrdtTarget::idList(), /*opId from idList*/);
            t.registerOp(CrdtTarget::kindTagMap(), /*opId from kindTag*/);
            t.registerOp(CrdtTarget::buffer(newBlock), 0 /*allocation isn't a CRDT op; use sentinel*/);
        } else if constexpr (std::is_same_v<T, StructuralOp::RemoveEntry>) {
            // ... similar pattern
        } else if constexpr (std::is_same_v<T, StructuralOp::ChangeKind>) {
            // ... similar pattern
        }
    }, op.payload);
}
```

- [ ] **Step 3: Run, verify, commit**

```bash
git add -u
git commit -m "d2(foundation): MarkoffDocument::applyStructural — Insert/Remove/ChangeKind"
```

### Task 4.4: `undo()` / `redo()` / `undoForBlock()` dispatch wiring

- [ ] **Step 1: Write tests**

```cpp
void TstD2Undo::undoUndoesLastEdit();
void TstD2Undo::undoOfEnterAtEnd_removesNewBlock();
void TstD2Undo::undoForBlock_pickRecentBlockEntry();
```

- [ ] **Step 2: Wire `UndoLog::Dispatcher` to actual CRDT undo calls**

In `MarkoffDocument.cpp` ctor:
```cpp
m_undoLog.setDispatcher([this](const CrdtTarget &target, OpId opId, bool forward) {
    std::visit(/* dispatch to the right CRDT */, target.kind);
});
```

Each branch calls the corresponding CRDT's `undo(opId)` (collabtext) or `applyUndoOpById(opId)` (sibling maps).

- [ ] **Step 3: Add public `undo()`, `redo()`, `undoForBlock(BlockId)` to `MarkoffDocument`**

```cpp
public:
    void undo() { m_undoLog.undo(); }
    void redo() { m_undoLog.redo(); }
    void undoForBlock(BlockId b) { m_undoLog.undoForBlock(b); }
```

- [ ] **Step 4: Run tests, verify, commit**

```bash
git add -u
git commit -m "d2(foundation): MarkoffDocument undo/redo/undoForBlock wired through UndoLog"
```

### Task 4.5: Per-CRDT signals exposed

- [ ] **Step 1: Write tests for each signal shape**

```cpp
void TstD2Signals::bufferEditSignal_firesOnApplyBlockEdit();
void TstD2Signals::idListStructureChanged_firesOnInsertEntry();
void TstD2Signals::kindTagMapChanged_firesOnChangeKind();
```

- [ ] **Step 2: Add Qt-signal-emitting accessors**

For each per-CRDT, expose a `QObject` interface (`Markoff::BufferProxy`, `Markoff::IdListProxy`, `Markoff::SiblingMapProxy<K,V>`) that forwards CRDT change callbacks to Qt signals.

```cpp
class BufferProxy : public QObject {
    Q_OBJECT
public:
    explicit BufferProxy(CollabText::Crdt::Buffer &b);
    quint64 editSequence() const;
signals:
    void inlineSpansChanged();
};
```

`MarkoffDocument` exposes per-block proxies via `bufferProxy(BlockId)` etc.

- [ ] **Step 3: Run, commit**

```bash
git add -u
git commit -m "d2(foundation): per-CRDT Qt signal proxies + accessors"
```

### Task 4.6: `documentEditSequence()` + `documentChanged` derived

- [ ] **Step 1: Test**

```cpp
void TstD2Signals::documentEditSequence_sumsAcrossCrdts();
void TstD2Signals::documentChanged_debouncesFanOut();
```

- [ ] **Step 2: Implement**

In `MarkoffDocument`:
```cpp
quint64 documentEditSequence() const {
    quint64 sum = m_idList.editSequence() + m_kindTagMap.editSequence() + /* ... */;
    for (const auto &[id, buf] : m_blockBuffers) sum += buf->editSequence();
    return sum;
}
```

`documentChanged` debouncing: connect every per-CRDT signal to a `QTimer::singleShot(0, ...)` that emits `documentChanged` once per event-loop spin (collapses fan-out).

- [ ] **Step 3: Commit**

```bash
git add -u
git commit -m "d2(foundation): documentEditSequence (sum) + documentChanged (debounced)"
```

### Task 4.7: `iterateBlocks()` and per-block accessors

- [ ] **Step 1: Test**

```cpp
void TstD2BlockAccessors::iterateBlocks_returnsIdListOrder();
void TstD2BlockAccessors::blockKind_returnsKindTagValue();
void TstD2BlockAccessors::blockAttrs_returnsAttrsMapValues();
void TstD2BlockAccessors::blockText_returnsBufferContent();
```

- [ ] **Step 2: Implement**

```cpp
std::vector<BlockId> MarkoffDocument::iterateBlocks() const {
    auto rawIds = m_idList.ids();
    std::vector<BlockId> out;
    for (auto raw : rawIds) out.push_back(BlockId::fromRaw(raw));
    return out;
}
BlockKind MarkoffDocument::blockKind(BlockId id) const {
    return m_kindTagMap.get(id).value_or(BlockKind::Paragraph);
}
QByteArray MarkoffDocument::blockText(BlockId id) const {
    auto it = m_blockBuffers.find(id);
    return it != m_blockBuffers.end() ? it.value()->text() : QByteArray{};
}
```

- [ ] **Step 3: Commit**

```bash
git add -u
git commit -m "d2(foundation): iterateBlocks + blockKind + blockText + blockAttrs accessors"
```

### Task 4.8: Mark old `applyLocalEdit(MarkoffEdit)` and `parseUpdated` deprecated

- [ ] **Step 1: Add `[[deprecated("D2: use applyBlockEdit; will be removed in Phase 14")]]` to old declarations**

- [ ] **Step 2: Build; expect deprecation warnings flagging all current call sites**

```bash
cmake --build build-dev -j 8 2>&1 | grep deprecated | head -20
```

(These warnings become the migration checklist for Phase 11–12.)

- [ ] **Step 3: Commit**

```bash
git add -u
git commit -m "d2(foundation): deprecate document-level applyLocalEdit + parseUpdated (removed in Phase 14)"
```

---

## Phase 5: Sibling map instantiations

### Task 5.1–5.5: KindTagMap, BlockAttrsMap, FrontmatterMap, LinkRefMap, FootnoteDefMap

Each is a thin typedef + a Qt-signal proxy. Pattern is identical for all five; following Task 5.1 in detail.

### Task 5.1: `KindTagMap`

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/KindTagMap.h`
- Create test: `libs/markoff-core/tests/d2/tst_d2_sibling_maps.cpp`

- [ ] **Step 1: Write test**

```cpp
void TstD2SiblingMaps::kindTagMap_setAndGet();
void TstD2SiblingMaps::kindTagMap_concurrentSetByCausalStamp();
```

- [ ] **Step 2: Header**

```cpp
#pragma once
#include <markoff-foundation/CausalLwwMap.h>
#include <markoff-foundation/BlockId.h>
#include <markoff-foundation/BlockKind.h>
namespace Markoff {
using KindTagMap = CausalLwwMap<BlockId, BlockKind>;
}
```

- [ ] **Step 3: Verify pass, commit**

```bash
git add -u
git commit -m "d2(foundation): KindTagMap = CausalLwwMap<BlockId, BlockKind>"
```

### Task 5.2: `BlockAttrsMap`

Same pattern. Keys are `(BlockId, AttrName)` where `AttrName` is a string. Value is a `std::variant<int, QString, bool>` (per plan-time decision; D3 may extend).

```cpp
namespace Markoff {
using AttrName = QByteArray;  // small ascii constants like "level", "info", "marker"
using AttrValue = std::variant<int, QString, bool>;
struct BlockAttrKey { BlockId block; AttrName name; bool operator==(const BlockAttrKey&) const = default; };
inline size_t qHash(const BlockAttrKey &k, size_t seed = 0) noexcept { /* combine */ }
using BlockAttrsMap = CausalLwwMap<BlockAttrKey, AttrValue>;
}
```

Commit:
```bash
git commit -m "d2(foundation): BlockAttrsMap = CausalLwwMap<BlockAttrKey, AttrValue>"
```

### Task 5.3: `FrontmatterMap`

```cpp
using FrontmatterKey = QByteArray;  // YAML key
using FrontmatterValue = QByteArray;  // YAML serialized value
using FrontmatterMap = CausalLwwMap<FrontmatterKey, FrontmatterValue>;
```

Commit:
```bash
git commit -m "d2(foundation): FrontmatterMap = CausalLwwMap<key, serialized-yaml>"
```

### Task 5.4: `LinkRefMap`

```cpp
struct LinkRefValue { QString url; QString title; bool operator==(const LinkRefValue&) const = default; };
using LinkRefId = QByteArray;
using LinkRefMap = CausalLwwMap<LinkRefId, LinkRefValue>;
```

Commit:
```bash
git commit -m "d2(foundation): LinkRefMap = CausalLwwMap<LinkRefId, (url, title)>"
```

### Task 5.5: `FootnoteDefMap`

```cpp
using FootnoteId = QByteArray;
using FootnoteDefMap = CausalLwwMap<FootnoteId, QByteArray>;  // value = footnote content as markdown
```

Commit:
```bash
git commit -m "d2(foundation): FootnoteDefMap = CausalLwwMap<FootnoteId, content>"
```

---

## Phase 6: `Cmd::*` re-implementation

### Task 6.1: `Cmd::insertCharacter` with coalescing

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/Cmd.h`
- Create: `libs/markoff-core/src/Cmd_d2.cpp` (will replace old `Cmd.cpp` content in Phase 14.3)
- Test: `libs/markoff-core/tests/d2/tst_d2_cmd_decomposition.cpp`

- [ ] **Step 1: Write test**

```cpp
void TstD2Cmd::insertCharacter_oneCharProducesOneEntry();
void TstD2Cmd::insertCharacter_consecutiveCharsCoalesce();

void TstD2Cmd::insertCharacter_consecutiveCharsCoalesce() {
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("hello\n");
    auto blk = doc.iterateBlocks().front();
    auto undoStartCount = doc.undoLog().entryCount();
    Markoff::Cmd::insertCharacter(doc, blk, /*qtPos=*/5, 'a');
    Markoff::Cmd::insertCharacter(doc, blk, /*qtPos=*/6, 'b');
    QCOMPARE(doc.undoLog().entryCount(), undoStartCount + 1);  // coalesced
    QCOMPARE(doc.blockText(blk), QByteArray("helloab"));
}
```

- [ ] **Step 2: Implement in `Cmd_d2.cpp`**

```cpp
namespace Markoff::Cmd {

void insertCharacter(MarkoffDocument &doc, BlockId block, uint32_t qtPos, QChar ch) {
    UndoLog::CoalesceContext ctx{block, /*isPrintable=*/!ch.isControl(), QDateTime::currentMSecsSinceEpoch()};
    doc.undoLog().maybeCoalesceOrTransaction(ctx, [&](UndoLog::Transaction &t) {
        // qtPos to byte offset within the block
        auto byteOff = qtPosToByteOffset(doc.blockText(block), qtPos);
        QByteArray utf8 = QString(ch).toUtf8();
        auto opId = doc.bufferFor(block).applyLocal(byteOff, 0, utf8);
        t.registerOp(CrdtTarget::buffer(block), opId);
    });
}

}
```

- [ ] **Step 3: Run, commit**

```bash
git add -u
git commit -m "d2(foundation): Cmd::insertCharacter with coalescing"
```

### Task 6.2: `Cmd::enterAtEnd`

- [ ] **Step 1: Test**

```cpp
void TstD2Cmd::enterAtEnd_createsNewParagraphAfterCurrent();
void TstD2Cmd::enterAtEnd_oneTransactionTouchesIdListKindTagAndNewBuffer();
```

- [ ] **Step 2: Implement**

```cpp
BlockId enterAtEnd(MarkoffDocument &doc, BlockId currentBlock) {
    UndoLog::Transaction t(doc.undoLog());
    auto newId = doc.allocateNewBlockId();  // foundation-internal helper
    auto opId = doc.idList().insertAfter(doc.idList().anchorOf(currentBlock.raw(), /*right*/), newId.raw());
    t.registerOp(CrdtTarget::idList(), opId);
    auto kindOpId = doc.kindTagMap().setWithNextStamp(newId, BlockKind::Paragraph);
    t.registerOp(CrdtTarget::kindTagMap(), kindOpId);
    doc.allocateBufferFor(newId);
    t.registerOp(CrdtTarget::buffer(newId), /*sentinel; allocation has no CRDT op*/ 0);
    return newId;
}
```

- [ ] **Step 3: Commit**

```bash
git add -u
git commit -m "d2(foundation): Cmd::enterAtEnd — IdList insert + kind tag + new Buffer in one transaction"
```

### Task 6.3: `Cmd::backspaceMerge`

- [ ] **Step 1: Test**

```cpp
void TstD2Cmd::backspaceMerge_appendsCurrentBlockContentToPrevious();
void TstD2Cmd::backspaceMerge_oneTransactionTouchesPreviousBufferKindTagAndIdList();
void TstD2Cmd::backspaceMerge_returnsCursorPositionAtOldEndOfPreviousBlock();
```

- [ ] **Step 2: Implement** (following spec §4.6 row "Backspace at start of block N")

```cpp
struct BackspaceMergeResult { BlockId mergedInto; uint32_t cursorByteOffset; };

BackspaceMergeResult backspaceMerge(MarkoffDocument &doc, BlockId currentBlock) {
    auto blocks = doc.iterateBlocks();
    auto curIt = std::find(blocks.begin(), blocks.end(), currentBlock);
    if (curIt == blocks.begin()) return {currentBlock, 0};  // can't merge before first
    BlockId prev = *(curIt - 1);

    UndoLog::Transaction t(doc.undoLog());
    auto curText = doc.blockText(currentBlock);
    auto prevEndOffset = doc.bufferFor(prev).text().size();
    auto bufOpId = doc.bufferFor(prev).applyLocal(prevEndOffset, 0, curText);
    t.registerOp(CrdtTarget::buffer(prev), bufOpId);

    auto kindOpId = doc.kindTagMap().removeWithNextStamp(currentBlock);
    t.registerOp(CrdtTarget::kindTagMap(), kindOpId);

    auto idListOpId = doc.idList().removeAt(doc.idList().anchorOf(currentBlock.raw(), /*right*/));
    t.registerOp(CrdtTarget::idList(), idListOpId);

    // Buffer for currentBlock NOT disposed here — kept until next save's GC (per spec §7.3).
    doc.markBlockMergedInto(prev);  // for touch-aware save

    return {prev, static_cast<uint32_t>(prevEndOffset)};
}
```

- [ ] **Step 3: Commit**

```bash
git add -u
git commit -m "d2(foundation): Cmd::backspaceMerge — append content + remove block + return cursor"
```

### Task 6.4: `Cmd::deleteMerge`

(Same shape as backspaceMerge but in the other direction: appends *next* block's content to current, removes next.)

- [ ] **Step 1: Test, implement, commit**

```bash
git commit -m "d2(foundation): Cmd::deleteMerge — append next block's content + remove next"
```

### Task 6.5: `Cmd::insertSoftBreak` (Shift-Enter)

- [ ] **Step 1: Test**

```cpp
void TstD2Cmd::insertSoftBreak_insertsLiteralNewlineWithinBlock();
```

- [ ] **Step 2: Implement**

```cpp
void insertSoftBreak(MarkoffDocument &doc, BlockId block, uint32_t qtPos) {
    UndoLog::Transaction t(doc.undoLog());
    auto byteOff = qtPosToByteOffset(doc.blockText(block), qtPos);
    auto opId = doc.bufferFor(block).applyLocal(byteOff, 0, "\n");
    t.registerOp(CrdtTarget::buffer(block), opId);
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): Cmd::insertSoftBreak — literal newline within block"
```

### Task 6.6: `Cmd::changeKind`

- [ ] **Step 1: Test**

```cpp
void TstD2Cmd::changeKind_paragraphToHeading_setsKindAndStripsLeadingMarkup();
```

(Per spec §4.6 row "Promote paragraph to heading via leading `# `": the `# ` markup is stripped from the Buffer content, kind tag flips, attrs `level` becomes 1.)

- [ ] **Step 2: Implement** (handles the bytes-stripping for kinds where source markup must move out of the Buffer)

```cpp
void changeKind(MarkoffDocument &doc, BlockId block, BlockKind newKind, const BlockAttrs &newAttrs) {
    UndoLog::Transaction t(doc.undoLog());
    auto kindOpId = doc.kindTagMap().setWithNextStamp(block, newKind);
    t.registerOp(CrdtTarget::kindTagMap(), kindOpId);
    for (auto &[name, val] : newAttrs) {
        auto attrOpId = doc.blockAttrsMap().setWithNextStamp({block, name}, val);
        t.registerOp(CrdtTarget::blockAttrsMap(), attrOpId);
    }
    // If the source markdown markup for the new kind requires content adjustment
    // (e.g., paragraph → heading: strip leading "# "), apply that here.
    auto stripBytes = sourceMarkupStripForKindChange(doc.blockKind(block), newKind, doc.blockText(block));
    if (stripBytes > 0) {
        auto bufOpId = doc.bufferFor(block).applyLocal(0, stripBytes, {});
        t.registerOp(CrdtTarget::buffer(block), bufOpId);
    }
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): Cmd::changeKind — flip kind + adjust attrs + strip source markup"
```

### Task 6.7: `Cmd::pasteMarkdown`

- [ ] **Step 1: Test**

```cpp
void TstD2Cmd::pasteMarkdown_singleParagraphIntoMidBlock_splitsAndInserts();
void TstD2Cmd::pasteMarkdown_multiBlockIntoMidBlock_splitsCurrentAndInsertsAll();
```

- [ ] **Step 2: Implement** (spec §4.6 row "Paste a 3-block markdown chunk into mid-paragraph")

Synchronously parse the paste source through the load-time parser (per Q5 plan-time decision). For each parsed block, emit insertEntry + kindTag + buffer in one transaction. Split the current block at the paste position to make room.

```cpp
void pasteMarkdown(MarkoffDocument &doc, BlockId targetBlock, uint32_t qtPos, QByteArray source) {
    UndoLog::Transaction t(doc.undoLog());
    // 1. Parse the paste source synchronously
    auto pastedDoc = Markoff::Document::fromMarkdown(source);
    auto pastedBlocks = pastedDoc->topLevelBlocks();
    if (pastedBlocks.empty()) return;

    // 2. Split current block at qtPos
    auto byteOff = qtPosToByteOffset(doc.blockText(targetBlock), qtPos);
    auto tail = doc.blockText(targetBlock).mid(byteOff);
    auto bufOpId = doc.bufferFor(targetBlock).applyLocal(byteOff, tail.size(), {});
    t.registerOp(CrdtTarget::buffer(targetBlock), bufOpId);

    // 3. For each pasted block, insertAfter targetBlock (then update targetBlock to the new one for chaining)
    BlockId after = targetBlock;
    for (const auto &pb : pastedBlocks) {
        auto newId = doc.allocateNewBlockId();
        auto idOpId = doc.idList().insertAfter(doc.idList().anchorOf(after.raw(), /*right*/), newId.raw());
        t.registerOp(CrdtTarget::idList(), idOpId);
        auto kindOpId = doc.kindTagMap().setWithNextStamp(newId, /*map parser kind to BlockKind*/);
        t.registerOp(CrdtTarget::kindTagMap(), kindOpId);
        doc.allocateBufferFor(newId);
        doc.bufferFor(newId).applyLocal(0, 0, /*pb content bytes*/);
        // attrs as needed
        after = newId;
    }

    // 4. Append the original tail to the last pasted block (so cross-paste merges naturally)
    if (!tail.isEmpty()) {
        doc.bufferFor(after).applyLocal(doc.bufferFor(after).text().size(), 0, tail);
    }
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): Cmd::pasteMarkdown — sync parse + split target + insert chain"
```

### Task 6.8: Delete old `Cmd::*` functions that took `MarkoffEdit`

- [ ] **Step 1: List old Cmd functions, remove them, fix call sites**

```bash
grep -rn "MarkoffEdit" libs/markoff-core/src/Cmd*.cpp libs/markoff-core/include/markoff-foundation/Cmd.h
```

Each remaining MarkoffEdit-shaped Cmd gets removed; callers (which all flagged as deprecated in Task 4.8) are updated to the new shapes.

- [ ] **Step 2: Build, run all tests, commit**

```bash
git commit -m "d2(foundation): retire Cmd::* functions taking MarkoffEdit"
```

---

## Phase 7: Load

### Task 7.1: `loadFromMarkdown` skeleton + frontmatter extraction

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Test: `libs/markoff-core/tests/d2/tst_d2_load.cpp`

- [ ] **Step 1: Test**

```cpp
void TstD2Load::emptyDoc_loadEmpty_zeroBlocks();
void TstD2Load::singleParagraph_loadProducesOneBlock();
void TstD2Load::frontmatterPresent_populatesMap();
```

- [ ] **Step 2: Implement skeleton**

```cpp
void MarkoffDocument::loadFromMarkdown(const QByteArray &src) {
    // 1. Frontmatter extraction (existing)
    auto extracted = Markoff::Document::extract(src);
    for (const auto &[key, value] : extracted.frontmatter) {
        m_frontmatterMap.set(key, value, m_frontmatterMap.nextStamp());
    }
    // 2. Top-level parse
    auto parsedDoc = Markoff::Document::fromMarkdown(extracted.body);
    materializeBlocksFromParsedDoc(*parsedDoc);
    // 3. Set load baselines (Task 7.6)
    setLoadBaselines();
    emit documentLoaded();
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): loadFromMarkdown skeleton — frontmatter + top-level parse + signal"
```

### Task 7.2: Top-level block walk + per-block Buffer materialization

- [ ] **Step 1: Test**

```cpp
void TstD2Load::heading_paragraph_codeBlock_threeBlocksWithKinds();
void TstD2Load::eachBlock_hasLoadTimeBytesSet();
```

- [ ] **Step 2: Implement `materializeBlocksFromParsedDoc`** (excludes list unwrapping, link-ref defs, footnote defs — those are subsequent tasks)

```cpp
void MarkoffDocument::materializeBlocksFromParsedDoc(const Markoff::Document &parsed) {
    for (const auto &tb : parsed.topLevelBlocks()) {
        if (tb.kind == ParserKind::ListTight || tb.kind == ParserKind::ListLoose) continue;  // 7.3
        if (tb.kind == ParserKind::LinkReferenceDefinition) continue;  // 7.4
        // Frontmatter and footnote defs handled separately
        auto blockId = allocateNewBlockId();
        auto raw = blockId.raw();
        m_idList.insertAfter(m_idList.endAnchor(), raw);
        m_kindTagMap.set(blockId, mapParserKindToD2Kind(tb.kind), m_kindTagMap.nextStamp());
        // Per-kind attrs
        populateAttrsForBlock(blockId, tb);
        // Allocate Buffer with block content
        auto buf = std::make_unique<CollabText::Crdt::Buffer>(replicaId());
        auto contentBytes = parsed.body().mid(tb.byteStart, tb.byteEnd - tb.byteStart);
        buf->setInitialContent(contentBytes);
        buf->setLoadTimeBytes(contentBytes);  // Task 8.4 will use this
        m_blockBuffers.insert(blockId, std::move(buf));
    }
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): top-level block walk — kind, attrs, Buffer with load-time bytes"
```

### Task 7.3: List unwrapping

- [ ] **Step 1: Test**

```cpp
void TstD2Load::tightList_threeItems_threeListItemBlocks();
void TstD2Load::looseList_marker_inAttrs();
void TstD2Load::orderedList_startNumber_inAttrs();
```

- [ ] **Step 2: Extend `materializeBlocksFromParsedDoc`**

```cpp
if (tb.kind == ParserKind::ListTight || tb.kind == ParserKind::ListLoose) {
    bool isTight = (tb.kind == ParserKind::ListTight);
    auto markerStyle = parseListMarkerStyle(tb);  // bullet-asterisk, ordered-period, etc.
    auto startNumber = parseListStartNumber(tb);  // for ordered lists
    for (const auto &item : tb.children) {
        auto blockId = allocateNewBlockId();
        m_idList.insertAfter(m_idList.endAnchor(), blockId.raw());
        m_kindTagMap.set(blockId, BlockKind::ListItem, m_kindTagMap.nextStamp());
        m_blockAttrsMap.set({blockId, "tight"}, AttrValue{isTight}, m_blockAttrsMap.nextStamp());
        m_blockAttrsMap.set({blockId, "marker"}, AttrValue{markerStyle.toString()}, m_blockAttrsMap.nextStamp());
        if (startNumber) m_blockAttrsMap.set({blockId, "start"}, AttrValue{*startNumber}, m_blockAttrsMap.nextStamp());
        // Buffer content = item content (parser-stripped of marker)
        auto buf = std::make_unique<CollabText::Crdt::Buffer>(replicaId());
        auto itemContent = parsed.body().mid(item.byteStart, item.byteEnd - item.byteStart);
        buf->setInitialContent(itemContent);
        buf->setLoadTimeBytes(itemContent);
        m_blockBuffers.insert(blockId, std::move(buf));
    }
    continue;
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): list unwrapping — items as first-class blocks, marker/tight in attrs"
```

### Task 7.4: Link reference def routing into `LinkRefMap`

- [ ] **Step 1: Test**

```cpp
void TstD2Load::linkRefDef_populatesLinkRefMap_notIdList();
```

- [ ] **Step 2: Extend materialization**

```cpp
if (tb.kind == ParserKind::LinkReferenceDefinition) {
    auto [id, url, title] = parseLinkReferenceDefinition(tb, parsed.body());
    m_linkRefMap.set(id, LinkRefValue{url, title}, m_linkRefMap.nextStamp());
    continue;  // does not become a BlockId
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): link reference defs route to LinkRefMap, not IdList"
```

### Task 7.5: Footnote def routing into `FootnoteDefMap`

(Today's `Markoff::Document::extract` already harvests footnote metadata. Wire it into `FootnoteDefMap`.)

- [ ] **Step 1: Test, implement, commit**

```bash
git commit -m "d2(foundation): footnote defs populate FootnoteDefMap from extract output"
```

### Task 7.6: Edit-counter baselines + `documentLoaded` signal

- [ ] **Step 1: Test**

```cpp
void TstD2Load::afterLoad_eachCrdtRecordsLoadBaseline();
void TstD2Load::documentLoaded_signalEmittedOnce();
```

- [ ] **Step 2: Implement**

```cpp
void MarkoffDocument::setLoadBaselines() {
    m_idListLoadBaseline = m_idList.editSequence();
    m_kindTagMapLoadBaseline = m_kindTagMap.editSequence();
    // ... other sibling maps
    for (auto &[id, buf] : m_blockBuffers) {
        m_perBlockLoadBaseline[id] = buf->editSequence();
    }
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): load baselines per CRDT + documentLoaded signal"
```

---

## Phase 8: Save

### Task 8.1: `BlockSerializer` interface

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/BlockSerializer.h`
- Create: `libs/markoff-core/src/BlockSerializers.cpp`

- [ ] **Step 1: Define the interface**

```cpp
namespace Markoff {
using BlockSerializer = std::function<QByteArray(BlockKind, const QHash<AttrName, AttrValue> &, const QByteArray &content)>;
class BlockSerializerRegistry {
public:
    static BlockSerializerRegistry &instance();
    void registerSerializer(BlockKind, BlockSerializer);
    BlockSerializer get(BlockKind) const;
};
}
```

- [ ] **Step 2: Test the registry**

```cpp
void TstD2Save::registry_registerAndGet();
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): BlockSerializer interface + registry"
```

### Task 8.2: Per-kind serializers — paragraph, heading, code-block, list-item

- [ ] **Step 1: Test each shape**

```cpp
void TstD2Save::paragraphSerializer_returnsContentAsIs();
void TstD2Save::headingSerializer_prependsHashes();
void TstD2Save::codeBlockSerializer_wrapsInFences();
void TstD2Save::listItemSerializer_prependsMarker();
```

- [ ] **Step 2: Implement in `BlockSerializers.cpp`**

```cpp
QByteArray serializeParagraph(BlockKind, const Attrs &, const QByteArray &content) {
    return content;
}
QByteArray serializeHeading(BlockKind, const Attrs &attrs, const QByteArray &content) {
    int level = std::get<int>(attrs.value("level", AttrValue{1}));
    return QByteArray(level, '#') + " " + content;
}
QByteArray serializeCodeBlock(BlockKind, const Attrs &attrs, const QByteArray &content) {
    QByteArray info;
    if (auto it = attrs.find("info"); it != attrs.end()) info = std::get<QString>(*it).toUtf8();
    return "```" + info + "\n" + content + "\n```";
}
QByteArray serializeListItem(BlockKind, const Attrs &attrs, const QByteArray &content) {
    auto marker = std::get<QString>(attrs.value("marker", AttrValue{QString{"-"}})).toUtf8();
    return marker + " " + content;
}
```

(Register all four with the registry at static-init via `BlockSerializerRegistry::registerBuiltins()`.)

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): per-kind serializers — paragraph, heading, code-block, list-item"
```

### Task 8.3: Per-kind serializers — blockquote, hr, image, math, mermaid, html-block, table

- [ ] **Step 1–3: Test, implement, commit each**

```bash
git commit -m "d2(foundation): per-kind serializers — blockquote/hr/image/math/mermaid/html/table"
```

### Task 8.4: Touch test

- [ ] **Step 1: Test**

```cpp
void TstD2Save::untouchedBlock_savesLoadTimeBytes();
void TstD2Save::editedBlock_savesCanonical();
void TstD2Save::kindChangedBlock_savesCanonical();
void TstD2Save::neighborMergedBlock_savesCanonical();
void TstD2Save::bornAfterLoadBlock_alwaysCanonical();
```

- [ ] **Step 2: Implement `isBlockTouched(BlockId)`**

```cpp
bool MarkoffDocument::isBlockTouched(BlockId id) const {
    auto bufIt = m_blockBuffers.find(id);
    if (bufIt == m_blockBuffers.end()) return true;  // born after load
    if (!bufIt.value()->hasLoadTimeBytes()) return true;
    if (bufIt.value()->editSequence() != m_perBlockLoadBaseline.value(id)) return true;
    if (auto stamp = m_kindTagMap.stampOf(id); stamp > m_kindTagMapLoadBaseline) return true;
    if (m_blockAttrsMap.anyStampPastBaselineForBlock(id, m_blockAttrsMapLoadBaseline)) return true;
    if (m_mergedIntoSet.contains(id)) return true;
    return false;
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): touch test — content/kind/attrs/merge/born-after-load all flag canonical"
```

### Task 8.5: `save()` walk + serialize

- [ ] **Step 1: Test**

```cpp
void TstD2Save::save_writesFrontmatter_thenBlocks_thenLinkRefsAndFootnotes();
```

- [ ] **Step 2: Implement**

```cpp
QByteArray MarkoffDocument::serializeForSave() const {
    QByteArray out;
    out += serializeFrontmatter(m_frontmatterMap);
    auto blocks = iterateBlocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        BlockId id = blocks[i];
        QByteArray bytes;
        if (isBlockTouched(id)) {
            auto serializer = BlockSerializerRegistry::instance().get(blockKind(id));
            bytes = serializer(blockKind(id), blockAttrs(id), blockText(id));
        } else {
            bytes = m_blockBuffers.value(id)->loadTimeBytes();
        }
        out += bytes;
        if (i + 1 < blocks.size()) {
            out += interBlockSeparator(blockKind(id), blockKind(blocks[i + 1]),
                                        blockAttrs(id), blockAttrs(blocks[i + 1]));
        }
    }
    out += serializeLinkRefs(m_linkRefMap);
    out += serializeFootnoteDefs(m_footnoteDefMap);
    return out;
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): save walk — frontmatter + blocks + link refs + footnote defs"
```

### Task 8.6: Atomic write + fsync + GC trigger

- [ ] **Step 1: Test**

```cpp
void TstD2Save::saveToFile_atomicWrite_fsyncReturns();
```

- [ ] **Step 2: Implement**

```cpp
bool MarkoffDocument::save(const QString &path) {
    auto bytes = serializeForSave();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(bytes);
    if (!f.commit()) return false;  // commits + fsyncs + atomic-renames

    bool gcOk = m_watermark.onSaveSucceeded();  // Phase 9
    Q_UNUSED(gcOk);
    return true;
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): MarkoffDocument::save — atomic write + fsync + GC trigger"
```

### Task 8.7: Round-trip corpus infrastructure

**Files:**
- Create: `libs/markoff-core/tests/d2/roundtrip/corpus/` directory + sample files
- Create: `libs/markoff-core/tests/d2/tst_d2_roundtrip.cpp`

- [ ] **Step 1: Curate initial corpus** (Q3 plan-time decision)

```bash
mkdir -p libs/markoff-core/tests/d2/roundtrip/corpus
# Copy GFM examples
cp /path/to/gfm-spec-examples/*.md libs/markoff-core/tests/d2/roundtrip/corpus/
# Copy project's own docs (representative, no license issue)
cp docs/2026-05-02-live-view-architectural-audit.md libs/markoff-core/tests/d2/roundtrip/corpus/
cp docs/specs/2026-05-04-d2-foundation-reshape-design.md libs/markoff-core/tests/d2/roundtrip/corpus/
# Add a CommonMark-spec edge case selection (manually, ~10 small files)
```

- [ ] **Step 2: Write corpus test driver**

```cpp
class TstD2Roundtrip : public QObject {
    Q_OBJECT
private slots:
    void corpus_data();
    void corpus();
    void corpus_mutateOneBlock_data();
    void corpus_mutateOneBlock();
};

void TstD2Roundtrip::corpus_data() {
    QTest::addColumn<QString>("path");
    QDir dir(CORPUS_PATH);
    for (const auto &fi : dir.entryInfoList(QStringList{"*.md"}, QDir::Files)) {
        QTest::newRow(qPrintable(fi.fileName())) << fi.absoluteFilePath();
    }
}

void TstD2Roundtrip::corpus() {
    QFETCH(QString, path);
    QFile in(path);
    QVERIFY(in.open(QIODevice::ReadOnly));
    QByteArray original = in.readAll();
    in.close();

    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(original);
    QByteArray serialized = doc.serializeForSave();
    QCOMPARE(serialized, original);
}
```

- [ ] **Step 3: Wire into CMake (skip on early phases until enough impl is in place to pass)**

In `tests/d2/CMakeLists.txt`:
```cmake
qt_add_executable(tst_d2_roundtrip tst_d2_roundtrip.cpp)
target_link_libraries(tst_d2_roundtrip PRIVATE Qt6::Test markoff-foundation)
target_compile_definitions(tst_d2_roundtrip PRIVATE
    CORPUS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/roundtrip/corpus")
add_test(NAME tst_d2_roundtrip COMMAND tst_d2_roundtrip)
```

- [ ] **Step 4: Commit corpus and infrastructure (test will fail until Phase 8.5 lands; that's expected)**

```bash
git add libs/markoff-core/tests/d2/roundtrip/ libs/markoff-core/tests/d2/tst_d2_roundtrip.cpp libs/markoff-core/tests/d2/CMakeLists.txt
git commit -m "d2(foundation): round-trip corpus + test driver (passes once 8.5 lands)"
```

### Task 8.8: Round-trip corpus tests pass

- [ ] **Step 1: Run the corpus test, identify byte-diff failures**

```bash
ctest --test-dir build-dev -R '^tst_d2_roundtrip$' --output-on-failure
```

For each failure, the diff reveals a serializer / load bug (load lost info, save canonicalized when it shouldn't, separator mismatch, etc.). Fix iteratively; each fix is its own commit.

- [ ] **Step 2: Add the mutate-one-block test variant**

```cpp
void TstD2Roundtrip::corpus_mutateOneBlock() {
    QFETCH(QString, path);
    // ... load ...
    auto blocks = doc.iterateBlocks();
    if (blocks.empty()) QSKIP("no blocks");
    auto target = blocks.front();
    Markoff::Cmd::insertCharacter(doc, target, 0, 'X');
    QByteArray serialized = doc.serializeForSave();
    // Diff: only target's bytes (and maybe its inter-block separator) should change.
    auto diff = computeBlockBoundaryDiff(original, serialized);
    QVERIFY2(diff.changedBlocks.size() == 1 && diff.changedBlocks[0] == 0,
             qPrintable(QString("more than one block changed: %1").arg(diff.changedBlocks.size())));
}
```

- [ ] **Step 3: Commit when all corpus tests pass**

```bash
git commit -m "d2(foundation): round-trip corpus — all files byte-identical, single-block-mutate isolated"
```

---

## Phase 9: GC

### Task 9.1: `WatermarkCoordinator` skeleton + `onSaveSucceeded`

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/WatermarkCoordinator.h`
- Create: `libs/markoff-core/src/WatermarkCoordinator.cpp`
- Test: `libs/markoff-core/tests/d2/tst_d2_gc.cpp`

- [ ] **Step 1: Test**

```cpp
void TstD2Gc::onSaveSucceeded_advancesWatermark();
```

- [ ] **Step 2: Implement skeleton**

(See spec §7.1 for the type sketch. The first version implements `onSaveSucceeded` returning `true` and updating an internal `Watermark` snapshot; later tasks add quiesce/dispatch/trim/orphan-disposal.)

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): WatermarkCoordinator skeleton + onSaveSucceeded snapshot"
```

### Task 9.2: Quiesce check (refuse if transaction open)

- [ ] **Step 1: Test**

```cpp
void TstD2Gc::onSaveSucceeded_refusesIfTransactionOpen();
```

- [ ] **Step 2: Implement** — check `m_doc.undoLog().isTransactionOpen()`; return `false` if so.

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): WatermarkCoordinator quiesce — refuses if transaction open"
```

### Task 9.3: Per-CRDT compact dispatch

- [ ] **Step 1: Test**

```cpp
void TstD2Gc::compact_dispatchedToAllCrdts();
```

- [ ] **Step 2: Implement** — after snapshot, walk each CRDT and call `compact(seq)`.

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): WatermarkCoordinator dispatches compact to all CRDTs"
```

### Task 9.4: `UndoLog` trim

- [ ] **Step 1: Test**

```cpp
void TstD2Gc::undoLog_dropsEntriesWhoseOpsAreCollapsed();
```

- [ ] **Step 2: Implement** — `m_doc.undoLog().compact([&](target, opId) { return /* targetSeqMap[target] >= opId */; });`

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): WatermarkCoordinator trims UndoLog entries past watermark"
```

### Task 9.5: Orphaned `Buffer` disposal

- [ ] **Step 1: Test**

```cpp
void TstD2Gc::orphanedBuffer_disposedAfterDeleteOpCompacted();
void TstD2Gc::orphanedBuffer_keptIfDeleteOpStillUndoable();
```

- [ ] **Step 2: Implement**

```cpp
void WatermarkCoordinator::disposeOrphans() {
    auto liveIds = m_doc.iterateBlocks();
    QSet<BlockId> liveSet(liveIds.begin(), liveIds.end());
    for (auto it = m_doc.m_blockBuffers.begin(); it != m_doc.m_blockBuffers.end(); ) {
        if (!liveSet.contains(it.key())) {
            // Check whether the IdList::removeAt op for this block has been compacted
            if (m_doc.idList().isOpCompacted(/*op id of the remove for this block*/)) {
                it = m_doc.m_blockBuffers.erase(it);
                continue;
            }
        }
        ++it;
    }
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): orphan Buffers disposed when remove op crosses watermark"
```

---

## Phase 10: Per-block inline parse

### Task 10.1: Extract `inlineSpansFor` from parser library

**Files:**
- Modify: `libs/markoff-parser/include/markoff-parser/TreeSitterParser.h`
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp`

- [ ] **Step 1: Test**

```cpp
void TstParserInline::inlineSpansFor_paragraph_returnsBoldItalicSpans();
```

- [ ] **Step 2: Implement**

```cpp
namespace Markoff {
InlineSpanTree inlineSpansFor(const QByteArray &content, BlockKind kind) {
    // Reuse the existing inline-tree logic from IncrementalParseSession's
    // per-block inline pass, but without the document-context machinery.
    TreeSitterInlineParser parser;
    return parser.parseInline(content, kind);
}
}
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(parser): extract inlineSpansFor as a clean entry point"
```

### Task 10.2: `InlineParseCache` (sync on read, never-evict)

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/InlineParseCache.h`
- Create: `libs/markoff-core/src/InlineParseCache.cpp`

- [ ] **Step 1: Test**

```cpp
void TstD2InlineParseCache::firstRead_parsesAndCaches();
void TstD2InlineParseCache::secondRead_returnsFromCache();
void TstD2InlineParseCache::editIncrementsCounter_invalidatesCache();
```

- [ ] **Step 2: Implement**

```cpp
class InlineParseCache {
public:
    explicit InlineParseCache(MarkoffDocument &doc);
    InlineSpanTree spansFor(BlockId id) {
        auto curSeq = m_doc.bufferEditSequence(id);
        auto it = m_cache.find(id);
        if (it != m_cache.end() && it->cachedAtSeq == curSeq) {
            return it->tree;
        }
        auto tree = Markoff::inlineSpansFor(m_doc.blockText(id), m_doc.blockKind(id));
        m_cache.insert(id, Entry{tree, curSeq});
        return tree;
    }
private:
    struct Entry { InlineSpanTree tree; quint64 cachedAtSeq; };
    MarkoffDocument &m_doc;
    QHash<BlockId, Entry> m_cache;
};
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): InlineParseCache — sync on read, edit-counter keyed, never-evict"
```

### Task 10.3: `Buffer::inlineSpansChanged` signal

- [ ] **Step 1: Test**

```cpp
void TstD2InlineParseCache::onApplyBlockEdit_inlineSpansChangedSignalFires();
```

- [ ] **Step 2: Wire signal** — `BufferProxy::inlineSpansChanged` fires from `applyBlockEdit` when the cached entry's `cachedAtSeq` no longer matches the buffer's `editSequence()`.

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): inlineSpansChanged signal fires on cache invalidation"
```

### Task 10.4: View-side consumer adapt (compile against new API; full migration in Phase 11)

- [ ] **Step 1: Update all sites in `markoff-live-render` that read inline spans to call `doc.inlineSpansFor(blockId)` instead of pulling from R1B's pre-baked `TopLevelBlock::inlineSpans`**

```bash
grep -rn "inlineSpans" libs/markoff-live/src/
```

(For each site, swap to the new accessor. Compile passes; behavior may need adjustment in Phase 11.)

- [ ] **Step 2: Commit**

```bash
git commit -m "d2(view): consume InlineParseCache via doc.inlineSpansFor(blockId)"
```

---

## Phase 11: View layer migration

### Task 11.1: `LiveEditBinding` rewrite

**Files:**
- Modify: `libs/markoff-live/src/LiveEditBinding.cpp`
- Modify: `libs/markoff-live/include/markoff/live-render/LiveEditBinding.h`

- [ ] **Step 1: Read current LiveEditBinding to understand the shape being replaced**

```bash
cat libs/markoff-live/src/LiveEditBinding.cpp | head -100
```

Note what's being deleted: `m_applyingModelUpdate` cycle guard, `previousText` cache, freshness gate, `parseInputEditSeq`-based staleness check.

- [ ] **Step 2: Rewrite `onContentsChange` to call `applyBlockEdit`**

```cpp
void LiveEditBinding::onContentsChange(int qtPos, int charsRemoved, int charsAdded) {
    auto &block = m_currentBlockId;
    auto byteOff = qtPosToByteOffset(m_doc->blockText(block), qtPos);
    QByteArray inserted = m_textEdit->toPlainText().mid(qtPos, charsAdded).toUtf8();
    m_doc->applyBlockEdit(BlockEdit{block, byteOff, /*removedBytes=*/charsRemoved /*needs-utf8-conversion*/, inserted});
}
```

(Drop the freshness gate and the cycle guards; per the spec, none of them are needed in D2.)

- [ ] **Step 3: Run live-render tests**

```bash
ctest --test-dir build-dev -R '^tst_live_render_paragraph_edit$' --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
git commit -m "d2(view): LiveEditBinding rewrite — direct applyBlockEdit, drop freshness gate / cycle guards"
```

### Task 11.2: `LiveStructuralKeyHandler` rewrite to call `Cmd::*`

- [ ] **Step 1: For each existing handler, swap to the corresponding `Cmd::*`**

| Handler | Now calls |
|---|---|
| paragraph EOB-Enter | `Cmd::enterAtEnd(doc, currentBlock)` |
| paragraph mid-Enter | `Cmd::splitParagraphAt(doc, currentBlock, qtPos)` (new in Phase 6 if not yet) |
| Backspace at row-start | `Cmd::backspaceMerge(doc, currentBlock)` |
| Delete at row-end | `Cmd::deleteMerge(doc, currentBlock)` |
| Shift-Enter | `Cmd::insertSoftBreak(doc, currentBlock, qtPos)` |
| heading EOB-Enter | (paragraph behavior) |
| code-block Enter | NotHandled (lets default `\n` insert flow) |

- [ ] **Step 2: Verify each handler with the existing live-render structural test**

```bash
ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(view): LiveStructuralKeyHandler dispatches to Cmd::* instead of MarkoffEdit"
```

### Task 11.3: Delete marker-paragraph machinery

**Files:**
- Delete: `libs/markoff-live/src/MarkerScrubber.{h,cpp}`
- Delete: `libs/markoff-live/include/markoff/Marker.h`
- Modify: `libs/markoff-live/CMakeLists.txt` (remove deleted sources)
- Delete: `libs/markoff-live/tests/tst_marker_*.cpp` (the marker-only tests; structural tests stay)

- [ ] **Step 1: Identify all marker-paragraph references**

```bash
grep -rn -E "MarkerScrubber|markerParagraph|ZWSP|0x200B|atomicBundledEdit" libs/markoff-live/
```

- [ ] **Step 2: Delete the files and remove from CMake**

```bash
git rm libs/markoff-live/src/MarkerScrubber.h libs/markoff-live/src/MarkerScrubber.cpp
git rm libs/markoff-live/include/markoff/Marker.h
git rm libs/markoff-live/tests/tst_marker_*.cpp
# Update CMakeLists.txt
```

- [ ] **Step 3: Fix any callers that referenced the deleted code (likely zero after Tasks 11.1–11.2)**

- [ ] **Step 4: Build, run, commit**

```bash
git commit -m "d2(view): retire marker-paragraph machinery — D2 makes it unnecessary"
```

### Task 11.4: Delete `UndoCoalescer` (logic relocated to `UndoLog`)

- [ ] **Step 1: Delete `UndoCoalescer.{h,cpp}` and its test**

- [ ] **Step 2: Remove call sites — they're no longer needed because `Cmd::insertCharacter` handles coalescing internally**

- [ ] **Step 3: Build, run, commit**

```bash
git commit -m "d2(view): retire UndoCoalescer — coalescing relocated into UndoLog"
```

### Task 11.5: `LiveBlockModel` diff source change

- [ ] **Step 1: Identify the diff source**

The Myers-diff input today is `BlockKey(kind, BlockAnchor)` from parser output. Now the source is `(kind, BlockId)` from `IdList::ids()`.

- [ ] **Step 2: Rewrite `LiveBlockModel::onSourceUpdated` to read from `MarkoffDocument` directly**

```cpp
void LiveBlockModel::onSourceUpdated() {
    auto blocks = m_doc->iterateBlocks();
    std::vector<BlockKey> newKeys;
    for (auto id : blocks) newKeys.push_back({id, m_doc->blockKind(id)});
    auto ops = computeMyersDiff(m_currentKeys, newKeys);
    applyOps(ops);
    m_currentKeys = newKeys;
}
```

(Subscribe to `IdList::structureChanged` and `KindTagMap::changed` instead of the old `parseUpdated`.)

- [ ] **Step 3: Verify tests, commit**

```bash
git commit -m "d2(view): LiveBlockModel diff against (BlockId, kind) from IdList directly"
```

### Task 11.6: `LiveCursorState` `BlockId` rebind

- [ ] **Step 1: Update `BlockId` references — should be no-op since BlockAnchor is now a typedef for BlockId, but verify the cursor-delivery code (`requestTextCaretAtNewBlock`) reads from `IdList::structureChanged.rowsInserted` instead of the parser-row-pipeline**

- [ ] **Step 2: Build, run live-render tests, commit**

```bash
git commit -m "d2(view): LiveCursorState — cursor delivery via IdList::structureChanged"
```

---

## Phase 12: Other in-tree consumers

### Task 12.1: `SearchEngine` per-block iteration

**Files:**
- Modify: `libs/markoff-core/src/SearchEngine.cpp`

- [ ] **Step 1: Test**

```cpp
void TstSearchEngine_d2::search_findsMatchAcrossBlocks();
```

- [ ] **Step 2: Rewrite `SearchEngine::find` to iterate per-block**

```cpp
QVector<SearchHit> SearchEngine::find(const QString &needle) {
    QVector<SearchHit> hits;
    for (auto blockId : m_doc->iterateBlocks()) {
        auto text = m_doc->blockText(blockId);
        // ... find matches within block text ...
        for (each match) hits.append(SearchHit{blockId, matchStart, matchLen});
    }
    return hits;
}
```

- [ ] **Step 3: Verify, commit**

```bash
git commit -m "d2(foundation): SearchEngine adapts to per-block iteration"
```

### Task 12.2: `ReplaceController` adapt

(Same pattern; per-block replacement.)

```bash
git commit -m "d2(foundation): ReplaceController adapts to per-block edits"
```

### Task 12.3: `LinkService` adapt to `LinkRefMap`

- [ ] **Step 1: `DefaultLinkService::resolve(linkRefId)` reads from `m_doc->linkRefMap().get(id)` instead of the parsed-document side table**

- [ ] **Step 2: Commit**

```bash
git commit -m "d2(foundation): LinkService consumes LinkRefMap directly"
```

### Task 12.4: `CompletionRegistry` adapt

- [ ] **Step 1: `CompletionContext` carries `BlockId` + within-block byte offset instead of document byte offset**

- [ ] **Step 2: Commit**

```bash
git commit -m "d2(foundation): CompletionContext uses (BlockId, withinBlockOffset)"
```

### Task 12.5: `SyntaxHighlightService` adapt

- [ ] **Step 1: Already reads inline spans per-block; just verify call sites use the new `inlineSpansFor` accessor**

- [ ] **Step 2: Commit**

```bash
git commit -m "d2(foundation): SyntaxHighlightService verified against new InlineParseCache"
```

---

## Phase 13: Convergence tests

### Task 13.1: Two-replica structural ops

**Files:**
- Create: `libs/markoff-core/tests/d2/tst_d2_convergence.cpp`

- [ ] **Step 1: Test**

```cpp
void TstD2Convergence::twoReplicas_crossingInsertsAtSameAnchor_bothSurvive();
```

(Use the public IdList convergence fixtures from collabtext per the maintainer commitment; spec §10.2.)

- [ ] **Step 2: Implement** — set up two `MarkoffDocument` replicas; cross-apply structural ops; assert both survive in deterministic order.

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): convergence — two-replica crossing structural inserts"
```

### Task 13.2: Two-replica per-block ops

```bash
git commit -m "d2(foundation): convergence — two-replica per-block content edits"
```

### Task 13.3: Mixed structural + content

```bash
git commit -m "d2(foundation): convergence — mixed structural + per-block ops causal"
```

### Task 13.4: Remove-vs-edit race

```bash
git commit -m "d2(foundation): convergence — remove-vs-edit race orphans content correctly"
```

### Task 13.5: Cross-CRDT undo

```bash
git commit -m "d2(foundation): convergence — local undo of structural-ins falls back per spec §3.5"
```

---

## Phase 14: Cleanup (D4-staged deletions)

### Task 14.1: Mark `ParsePool` unused (D4 deletes)

**Files:**
- Modify: `libs/markoff-parser/include/markoff-parser/ParsePool.h` (add `[[deprecated("D4 will delete")]]`)

- [ ] **Step 1: Verify no caller remains**

```bash
grep -rn "ParsePool" libs/markoff-core/ libs/markoff-live/
```

Expected: zero hits (post-Phase 11).

- [ ] **Step 2: Add deprecation marker**

```cpp
class [[deprecated("D2: no callers; D4 will delete entirely")]] ParsePool { ... };
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(parser): mark ParsePool deprecated — D4 will delete"
```

### Task 14.2: Delete old `parseUpdated` / `parseSequence` accessors

- [ ] **Step 1: Delete from `MarkoffDocument.h`/.cpp**

- [ ] **Step 2: Verify build**

```bash
cmake --build build-dev -j 8 2>&1 | grep error | head
```

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): retire parseUpdated signal + parseSequence accessor"
```

### Task 14.3: Delete `MarkoffEdit` type

- [ ] **Step 1: `git rm libs/markoff-core/include/markoff-foundation/MarkoffEdit.h`**

- [ ] **Step 2: Build**

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): retire MarkoffEdit — replaced by BlockEdit + StructuralOp"
```

### Task 14.4: Delete old `MarkoffDocument` internals

- [ ] **Step 1: Remove the old single-rope `Buffer` member, the old `applyLocalEdit(MarkoffEdit)` impl, the old document-byte-offset accessors that have no D2 equivalent**

- [ ] **Step 2: Run the full test suite**

```bash
ctest --test-dir build-dev -j 8 --output-on-failure
```

Expected: all pass.

- [ ] **Step 3: Commit**

```bash
git commit -m "d2(foundation): delete old MarkoffDocument internals — D2 internals now sole impl"
```

---

## Phase 15: D-arc status update + dogfood

### Task 15.1: Update D-arc status board

**Files:**
- Modify: `docs/d-arc/d-arc-status.md`

- [ ] **Step 1: Mark D2 phase as `in-progress` (when implementation starts) → `dogfood` (when Phase 14 completes) → `complete` (post-15.2)**

- [ ] **Step 2: Append entries to the recent-changes log for each phase milestone**

- [ ] **Step 3: Commit at each phase boundary**

```bash
git commit -m "d2(docs): D-arc status — Phase N complete; Phase N+1 in-progress"
```

### Task 15.2: User dogfood pass

- [ ] **Step 1: User opens the live-render test app against a representative document**

```bash
./build-dev/bin/markoff-live-app docs/specs/2026-05-04-d2-foundation-reshape-design.md
```

- [ ] **Step 2: User exercises:**
  - Typing into multiple paragraphs (no parse-vs-CRDT race; characters appear instantly)
  - Enter at end of paragraph (new paragraph created via structural CRDT; cursor lands)
  - Backspace at start of block (merges with previous; cursor at old end-of-prev)
  - Mid-block Enter (splits; cursor in new block)
  - Undo across all of the above (one Ctrl-Z = one user action)
  - Per-block undo via right-click menu (D2 exposes API; D3 wires UI optionally)
  - Promote paragraph to heading by typing `# `
  - Save and reopen — touch-aware (only edited blocks should diff)
  - Stress: stack ten Enters in a row (no marker leakage; clean creation each time)

- [ ] **Step 3: User reports any bug, escalation, or sign-off**

### Task 15.3: D2 marked complete

- [ ] **Step 1: Update `docs/d-arc/d-arc-status.md`** — D2 status `complete`, append final commit to recent-changes log

- [ ] **Step 2: Update `docs/d-arc/2026-05-04-d-arc-roadmap.md`** — D2 row status from 🟢 to ✅; D3 ready to begin (its stub is the next brainstorm input)

- [ ] **Step 3: Update worktree `CLAUDE.md` banner** — flip "ACTIVE WORK" from D2 to D3 (or to "D-arc on D2 complete; D3 next" if D3 hasn't been brainstormed yet)

- [ ] **Step 4: Commit**

```bash
git commit -m "d2(docs): D2 complete — foundation reshape lands; D3 next"
```

---

## Self-review checklist (engineer running this plan)

Before declaring D2 done:

- [ ] All foundation unit tests pass (`ctest -R '^tst_d2_' --output-on-failure`).
- [ ] All round-trip corpus tests pass byte-identical.
- [ ] All convergence tests pass.
- [ ] `markoff-live-render`'s existing tests pass (drift verification).
- [ ] No `parseUpdated`, `parseSequence`, `MarkoffEdit`, `MarkerScrubber`, `UndoCoalescer`, `LiveHoleLayer`, `LiveProxyBlockModel`, `BlockHole` references remain in tree (`grep -rn`).
- [ ] `ParsePool` and `IncrementalParseSession` are marked deprecated but not yet deleted (D4 deletes them).
- [ ] User has signed off on Task 15.2 dogfood.
- [ ] D-arc status board reflects D2 complete.

---

*End of D2 implementation plan. ~85 tasks across 16 phases. Estimated 6–10 weeks of focused work; smaller if subagent-driven execution parallelizes the layer-independent tasks.*
