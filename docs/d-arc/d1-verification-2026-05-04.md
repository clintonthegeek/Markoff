# D1 IdList Verification — 2026-05-04

## Header location

**Actual path:** `/home/clinton/dev/collabtext/libs/collabtext/src/crdt/IdList.h`

There is no public `include/collabtext/Crdt/` install prefix. The header is an
internal source file, included via the `collabtext` CMake target's
`target_include_directories`. The plan's path
(`~/dev/collabtext/include/collabtext/Crdt/IdList.h`) does not exist.

## CMake wiring

collabtext is brought in via `add_subdirectory(libs/collabtext)` in the root
`CMakeLists.txt`. `libs/collabtext` is a symlink to
`/home/clinton/dev/collabtext`. The `markoff-core` target links it:

```cmake
# libs/markoff-core/CMakeLists.txt
target_link_libraries(markoff-core ... collabtext)
```

CMake configure completes cleanly with no collabtext-related errors.

## Built artifact

```
build-dev/libs/collabtext/libs/collabtext/libcollabtext.a
```

Present and up to date. Link probe: **pass via CMake target** — the library
builds and links successfully as part of the `markoff-core` target.

## Link probe status

**N/A — internal header, included via CMake target.**

The header is not reachable through an installed include path; it is made
available to `markoff-core` via `collabtext`'s
`target_include_directories(collabtext PUBLIC ...)` in the collabtext
CMakeLists. A standalone compile of a translation unit that does
`#include "crdt/IdList.h"` would only work if linked against the collabtext
CMake target, which sets the include path automatically.

## Class synopsis — `CollabText::Crdt::IdList`

```cpp
namespace CollabText::Crdt {

class IdList {
public:
    explicit IdList(uint16_t replica_id);

    // --- Mutation ---
    IdListOperation insert_after(const Anchor& after, uint64_t id);
    IdListOperation remove_at(const Anchor& target);
    void apply_ops(const std::vector<IdListOperation>& ops);

    // --- Undo / redo ---
    std::optional<IdListOperation> undo();
    std::optional<IdListOperation> redo();
    size_t undo_depth() const;
    bool coalesce_last_undo();
    size_t max_undo_depth() const;
    void set_max_undo_depth(size_t depth);

    // --- Query ---
    std::vector<uint64_t> ids() const;
    uint32_t size() const;

    // --- Anchor resolution ---
    Anchor anchor_of(uint64_t id, Bias bias = Bias::Left) const;
    Anchor anchor_at_index(uint32_t index, Bias bias = Bias::Left) const;
    uint32_t resolve_anchor(const Anchor& a) const;
    int compare_anchors(const Anchor& a, const Anchor& b) const;

    // --- Version / identity ---
    const Global& version() const;
    uint16_t replica_id() const;

    // --- GC / compaction ---
    size_t collect_garbage();
    size_t compact(const Global& watermark);

    // --- Inspection ---
    std::vector<IdListEntry> entries() const;
    size_t tombstone_count() const;
    size_t entry_count() const;

    // --- Change notification ---
    using ChangeCallback = std::function<void()>;
    void set_on_change(ChangeCallback cb);
};

} // namespace CollabText::Crdt
```

Key supporting types (defined in the same header):

- `IdListSummary` — B-tree summary: visible/deleted counts, max locator,
  version range, insertion version range.
- `VisibleIndex` — cursor type for B-tree seek by visible position.
- `IdListEntry` — leaf node: `origin` (Lamport), `locator`, `id`, `deletions`,
  `visible`; MVCC helpers `compute_visible`, `was_visible_at`.
- `IdListTree` — alias `SumTree<IdListEntry, 6>`.

## Conclusion

D1 is available and integrated. All three verification criteria pass:

| Criterion | Result |
|-----------|--------|
| Header readable at actual path | PASS |
| CMake target present + links | PASS |
| Static library artifact built | PASS |
| Link probe | N/A — internal header via CMake target |
