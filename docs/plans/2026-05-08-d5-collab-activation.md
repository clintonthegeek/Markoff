# D5 — Collab Activation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Markoff's collab boundary on `MarkoffDocument` — outbound op stream, inbound apply, sibling-map sync, watermark/ack gate, remote presence rendering — plus a reference test harness, per the D5 design spec.

**Architecture:** The boundary lives on `MarkoffDocument` (Qt-idiomatic: signals + methods, no abstract adapter type). Single-user mode is the default; an explicit `quint16` replica ID at construction activates collab mode. Local user-actions emit `localOpsProduced(ops, bundleMeta)` after each `UndoLog::Transaction` commits; remote ops arrive via `applyRemoteOps(ops, meta)` and dispatch by `CrdtTarget` to per-CRDT `applyRemote(...)`. Sibling-map ops use a Markoff-defined wire envelope; Buffer/IdList ops wrap collabtext's existing serialisation byte-for-byte. Tier-0 confidence verification is two-replica in-process convergence tests routed via direct method call (no transport required). A reference testapp at `apps/markoff-collab-testapp/` adds an in-memory `ITransport` mock, two-window dogfood, and convergence-through-router tests.

**Tech Stack:** C++20, Qt 6.8+ (Core, Test, Widgets, Qml, Quick, QuickWidgets), CMake 3.19+, KF6::SyntaxHighlighting (transitive), QTest, the existing collabtext primitives (`Buffer`, `IdList`, `Anchor`, `Lamport`).

**Spec:** `docs/specs/2026-05-07-d5-collab-activation-design.md`.
**Authoritative posture:** `docs/handoff/2026-05-07-pivot-to-d5-first.md` §4.4.

---

## 0. Read first

Before starting any phase:

1. The D5 spec (`docs/specs/2026-05-07-d5-collab-activation-design.md`) — full document.
2. The pivot doc (`docs/handoff/2026-05-07-pivot-to-d5-first.md`) — operating principles, especially principle 1 ("one arc at a time") and principle 5 ("no side work").
3. The developmental history (`docs/handoff/2026-05-07-live-binding-developmental-history.md`) — relevant when phase 7 touches the live pipeline; cited under operating principle 4.
4. `libs/markoff-core/CLAUDE.md` and `libs/markoff-live/CLAUDE.md` — per-library conventions.
5. The d-arc status board (`docs/d-arc/d-arc-status.md`) — update its recent-changes log after each phase commit.

Before any phase commit, the full test suite must pass:

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
cd build-dev && ctest -j 8 --output-on-failure
```

**Build cap:** `cmake --build … -j 8`. Never bare `-j` or higher.

---

## 1. Conventions

- **C++ files:** SPDX header `// SPDX-License-Identifier: GPL-3.0-or-later` on every new file.
- **Test files:** new tests under `libs/markoff-core/tests/d5/` or `libs/markoff-live/tests/d5/`; prefix `tst_d5_<topic>.cpp`. Test class names `TstD5<Topic>`. Use `QTEST_MAIN`.
- **Test wiring:** add to the relevant `tests/CMakeLists.txt` via the existing `qt_add_executable` + `target_link_libraries` + `add_test` triplet pattern (see `libs/markoff-core/tests/d2/CMakeLists.txt:20-22` for the exact form).
- **Commit messages:** format `markoff-core: <subject>` or `markoff-live: <subject>` or `markoff-testapp: <subject>`. Short imperative. Body when motivation isn't obvious from the diff.
- **Granularity:** one task = one TDD cycle (write failing test → run → fail → implement minimal → run → pass → commit). Each task ends with a green tree.
- **D-arc status board:** after each phase's final commit, append a line to `docs/d-arc/d-arc-status.md`'s recent-changes log noting the phase completion and the commit SHA.

---

## 2. File structure

### Files to create (markoff-core)

| Path | Purpose |
|---|---|
| `libs/markoff-core/include/markoff/core/MarkoffOp.h` | `CrdtTarget` enum, `MarkoffOp`, `MarkoffBundleMeta` types + `Q_DECLARE_METATYPE` |
| `libs/markoff-core/include/markoff/core/MarkoffSerializer.h` | `MarkoffSerializer::encode` / `decode` public API |
| `libs/markoff-core/src/MarkoffSerializer.cpp` | Serialisation implementation |
| `libs/markoff-core/include/markoff/core/SiblingMapOpHeader.h` | Sibling-map wire envelope type |
| `libs/markoff-core/src/SiblingMapOpHeader.cpp` | Codec for the envelope |

### Files to modify (markoff-core)

| Path | Change |
|---|---|
| `libs/markoff-core/include/markoff/core/MarkoffDocument.h` | Add replica-ID constructor, `replicaId()`, `isCollabConfigured()`, `applyRemoteOps()`, `notifyAcksAtWatermark()`, `setRemoteCursor()`, `clearRemoteCursor()`, `clearAllRemoteCursors()`, signals `localOpsProduced`, `localWatermarkAdvanced`, `wantsAcksAtWatermark` |
| `libs/markoff-core/src/MarkoffDocument.cpp` | Implement |
| `libs/markoff-core/src/MarkoffDocumentPrivate.h` | Add private state for collab mode + bundle ID counter |
| `libs/markoff-core/include/markoff/core/UndoLog.h` | Add `Transaction::onCommit` callback hook (used to emit `localOpsProduced`) |
| `libs/markoff-core/src/UndoLog.cpp` | Wire the hook |
| `libs/markoff-core/src/CausalLwwMap.cpp` (or `.h`-only template) | Emit sibling-map ops on `setWithNextStamp` / `removeWithNextStamp` |
| `libs/markoff-core/src/WatermarkCoordinator.cpp` | Emit `wantsAcksAtWatermark`, accept `notifyAcksAtWatermark`, gate compaction |
| `libs/markoff-core/CMakeLists.txt` | Add new sources |

### Files to modify (markoff-live)

| Path | Change |
|---|---|
| `libs/markoff-live/include/markoff/live/LiveListModelBinding.h` | Add `setRemoteCursor` / `clearRemoteCursor` plumbing forwarded from doc |
| `libs/markoff-live/src/LiveListModelBinding.cpp` | Implement remote-cursor forwarding to QML |
| `libs/markoff-live/qml/LiveView.qml` | Render remote cursors as overlay |
| `libs/markoff-live/qml/delegates/RemoteCursorOverlay.qml` (new) | The overlay item (caret bar + label tag) |

### Files to create (testapp)

| Path | Purpose |
|---|---|
| `apps/CMakeLists.txt` | New top-level apps directory |
| `apps/markoff-collab-testapp/CMakeLists.txt` | App build wiring |
| `apps/markoff-collab-testapp/main.cpp` | Entry point: two windows side-by-side |
| `apps/markoff-collab-testapp/InMemoryTransport.h` / `.cpp` | The `ITransport` mock |
| `apps/markoff-collab-testapp/CollabConsumer.h` / `.cpp` | Wires a `MarkoffDocument` to a transport |
| `apps/markoff-collab-testapp/MainWindow.h` / `.cpp` | Two-pane window (each pane an editor + status) |
| `apps/markoff-collab-testapp/tests/CMakeLists.txt` | Test wiring |
| `apps/markoff-collab-testapp/tests/tst_d5_testapp_convergence.cpp` | Convergence-through-router tests |

### Test files

All new D5 tests in `libs/<lib>/tests/d5/`. Each task creates its own test file (or appends to one). Files listed inline in tasks.

### Root CMakeLists.txt

Add `add_subdirectory(apps)` after `add_subdirectory(libs/markoff-source)` (Phase 8 only).

---

## 3. Phase map

| Phase | Lib | Surface | Tier |
|---|---|---|---|
| **0** | (collabtext) | **Prerequisite — public op API + public serialisation land in collabtext** | — |
| 1 | markoff-core | Boundary types + serialisation | 1 |
| 2 | markoff-core | `MarkoffDocument` constructor with replica ID | 1 |
| 3 | markoff-core | `localOpsProduced` emission from transaction commit | 1 |
| 4 | markoff-core | `applyRemoteOps` + **tier-0 convergence tests** | 0/1 |
| 5 | markoff-core | Sibling-map ops (encode/decode/emission/application/LWW) | 0/1 |
| 6 | markoff-core | Watermark/ack gate | 1 |
| 7 | markoff-live | Remote cursor rendering | 2 |
| 8 | apps/markoff-collab-testapp | In-memory transport + two-window testapp + router-routed convergence tests | 3 |
| 9 | (dogfood) | Manual two-window acceptance pass | — |

**Critical-path note:** Phase 4 is where two-doc convergence becomes verifiable. Phases 5–8 ride on top of that confidence. If Phase 4 reveals an architectural flaw, fix at Phase 4 — much cheaper than at Phase 9.

---

## Phase 0 — Prerequisite: collabtext public API landing

**Phase 1 begins after this prerequisite is met.** D5 implementation is paused until then, per pivot-doc principle 5 ("no side work").

The collabtext maintainers committed (`~/dev/collabtext/docs/specs/2026-05-08-d5-negotiation-response.md`) to landing two pieces in ~2 weeks from 2026-05-08:

1. **Public op API on `CrdtEngine` and `IdList`.** `setOnLocalOp(callback<Operation>)` and `applyRemoteOp(Operation)` on each, surfaced through the public engine pImpl. This is plumbing: the methods already exist inside `Buffer` internally.
2. **`Serialization.h` promoted to public include path** as `include/collabtext/Serialization.h`. `encode_operation` / `decode_operation` / `encode_idlist_operation` / `decode_idlist_operation` become public functions. The argument types (`Operation`, `IdListOperation`, plus support types `Lamport`, `Anchor`, `Global`, `Fragment`) get publicly-visible declarations.

### What to watch for

- A collabtext release tag or dev-branch signal advertising items 1–2 ready.
- `include/collabtext/Serialization.h` appearing in the public tree.
- Public methods on `CrdtEngine` matching the `Operation` type signatures.

### Verification before starting Phase 1

```bash
# Check the public surface exists.
ls /home/clinton/dev/collabtext/include/collabtext/Serialization.h
grep -n "setOnLocalOp\|applyRemoteOp" /home/clinton/dev/collabtext/include/collabtext/CrdtEngine.h \
     /home/clinton/dev/collabtext/include/collabtext/IdList.h
```

Both should return matches. If not, items 1–2 haven't landed yet — wait.

### Items 3–6 land later (~8–10 weeks)

The full `OpStream` interface, `StreamSync`'s adoption of it, per-peer ack-frontier publication, and convergence/ack tests on the collabtext side land in the longer timeframe. **Phase 8** of this plan (the testapp) initially uses our own `InMemoryTransport`; once `OpStream` is public, the testapp can also wire `StreamSync` directly as a real-transport mode behind a feature flag. That's a small amendment to Phase 8 to add when the time comes; it doesn't gate any earlier phase.

### Joint-design pass

See "Joint-design coordination" at the end of this plan for the calendar commitment with the collabtext maintainers.

---

## Phase 1 — Boundary types and serialisation

**Goal:** Define `CrdtTarget`, `MarkoffOp`, `MarkoffBundleMeta`, the sibling-map envelope, and `MarkoffSerializer::encode/decode`. No `MarkoffDocument` changes yet. Round-trip tests prove the wire format.

### Task 1.1: Define `CrdtTarget` enum and `MarkoffOp`

**Files:**
- Create: `libs/markoff-core/include/markoff/core/MarkoffOp.h`
- Test: `libs/markoff-core/tests/d5/tst_d5_op_types.cpp`

- [ ] **Step 1: Create the test directory and write the failing test**

```bash
mkdir -p libs/markoff-core/tests/d5
```

Create `libs/markoff-core/tests/d5/tst_d5_op_types.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QMetaType>
#include <markoff/core/MarkoffOp.h>

class TstD5OpTypes : public QObject {
    Q_OBJECT
private slots:
    void crdtTarget_hasExpectedValues() {
        using T = Markoff::CrdtTarget;
        QCOMPARE(static_cast<quint8>(T::IdList),         quint8(0));
        QCOMPARE(static_cast<quint8>(T::Buffer),         quint8(1));
        QCOMPARE(static_cast<quint8>(T::KindTagMap),     quint8(2));
        QCOMPARE(static_cast<quint8>(T::BlockAttrsMap),  quint8(3));
        QCOMPARE(static_cast<quint8>(T::FrontmatterMap), quint8(4));
        QCOMPARE(static_cast<quint8>(T::LinkRefMap),     quint8(5));
        QCOMPARE(static_cast<quint8>(T::FootnoteDefMap), quint8(6));
    }
    void markoffOp_isDefaultConstructible() {
        Markoff::MarkoffOp op;
        QCOMPARE(op.target, Markoff::CrdtTarget::IdList);
        QCOMPARE(op.blockId, quint64(0));
        QVERIFY(op.payload.isEmpty());
        QCOMPARE(op.producerReplicaId, quint16(0));
    }
    void markoffOp_metatypeRegistered() {
        const int id = qMetaTypeId<Markoff::MarkoffOp>();
        QVERIFY(id > 0);
    }
    void markoffOpList_metatypeRegistered() {
        const int id = qMetaTypeId<QList<Markoff::MarkoffOp>>();
        QVERIFY(id > 0);
    }
};
QTEST_MAIN(TstD5OpTypes)
#include "tst_d5_op_types.moc"
```

- [ ] **Step 2: Create `libs/markoff-core/tests/d5/CMakeLists.txt`**

```cmake
qt_add_executable(tst_d5_op_types tst_d5_op_types.cpp)
target_link_libraries(tst_d5_op_types PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d5_op_types COMMAND tst_d5_op_types)
```

- [ ] **Step 3: Hook the d5 test directory into the parent CMake**

In `libs/markoff-core/tests/CMakeLists.txt`, add (alongside the existing `add_subdirectory(d2)` line):

```cmake
add_subdirectory(d5)
```

- [ ] **Step 4: Run — expect FAIL (no header)**

```bash
cmake --build build-dev -j 8 --target tst_d5_op_types 2>&1 | tail -3
```

Expected: a `fatal error: 'markoff/core/MarkoffOp.h' file not found` line.

- [ ] **Step 5: Create the header**

`libs/markoff-core/include/markoff/core/MarkoffOp.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/ActionId.h>
#include <markoff/core/MarkoffCoreExport.h>

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QtGlobal>

namespace Markoff {

/// What CRDT a MarkoffOp targets. The consumer routes by this tag.
/// See spec §2.3.
enum class CrdtTarget : quint8 {
    IdList         = 0,
    Buffer         = 1,
    KindTagMap     = 2,
    BlockAttrsMap  = 3,
    FrontmatterMap = 4,
    LinkRefMap     = 5,
    FootnoteDefMap = 6,
};

/// A single op crossing the boundary. Opaque payload.
struct MARKOFF_CORE_EXPORT MarkoffOp {
    CrdtTarget  target            = CrdtTarget::IdList;
    quint64     blockId           = 0;     // valid iff target==Buffer
    QByteArray  payload;
    quint16     producerReplicaId = 0;
};

/// Metadata identifying the user-action this set of ops belongs to.
/// One bundle = one transaction = one user-action.
struct MARKOFF_CORE_EXPORT MarkoffBundleMeta {
    quint16    producerReplicaId = 0;
    quint64    bundleId          = 0;     // monotonic per producer
    quint16    opCountInBundle   = 0;
    ActionId   actionId          = ActionId::None;
    quint64    producerLamport   = 0;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::MarkoffOp)
Q_DECLARE_METATYPE(Markoff::MarkoffBundleMeta)
Q_DECLARE_METATYPE(QList<Markoff::MarkoffOp>)
```

- [ ] **Step 6: Confirm `Markoff::ActionId::None` exists**

```bash
grep -n "None\|enum class ActionId" libs/markoff-core/include/markoff/core/ActionId.h | head -5
```

If `None` is not a member, add it as the first enum value with explicit value `0`. Otherwise no change needed.

- [ ] **Step 7: Run — expect PASS**

```bash
cmake --build build-dev -j 8 --target tst_d5_op_types
cd build-dev && ctest -j 8 -R tst_d5_op_types --output-on-failure
cd ..
```

Expected: 1 test passes (4 sub-cases).

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffOp.h \
        libs/markoff-core/include/markoff/core/ActionId.h \
        libs/markoff-core/tests/d5/CMakeLists.txt \
        libs/markoff-core/tests/d5/tst_d5_op_types.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "markoff-core: add MarkoffOp / MarkoffBundleMeta / CrdtTarget boundary types (D5 phase 1)"
```

### Task 1.2: Sibling-map op envelope

**Files:**
- Create: `libs/markoff-core/include/markoff/core/SiblingMapOpHeader.h`
- Create: `libs/markoff-core/src/SiblingMapOpHeader.cpp`
- Test: `libs/markoff-core/tests/d5/tst_d5_sibling_map_envelope.cpp`

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/d5/tst_d5_sibling_map_envelope.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/SiblingMapOpHeader.h>

class TstD5SiblingMapEnvelope : public QObject {
    Q_OBJECT
private slots:
    void encodeDecode_simpleEntry() {
        Markoff::SiblingMapOpHeader h;
        h.key             = QByteArray("key-bytes");
        h.value           = QByteArray("value-bytes");
        h.lamportCounter  = 42;
        h.lamportReplicaId= 7;
        h.isTombstone     = false;

        QByteArray bytes = Markoff::SiblingMapOpHeader::encode(h);
        QVERIFY(!bytes.isEmpty());

        Markoff::SiblingMapOpHeader out;
        const bool ok = Markoff::SiblingMapOpHeader::decode(bytes, &out);
        QVERIFY(ok);
        QCOMPARE(out.key, h.key);
        QCOMPARE(out.value, h.value);
        QCOMPARE(out.lamportCounter, h.lamportCounter);
        QCOMPARE(out.lamportReplicaId, h.lamportReplicaId);
        QCOMPARE(out.isTombstone, h.isTombstone);
    }
    void encodeDecode_tombstone() {
        Markoff::SiblingMapOpHeader h;
        h.key             = QByteArray("k");
        h.value           = QByteArray();   // empty for tombstone
        h.lamportCounter  = 1;
        h.lamportReplicaId= 1;
        h.isTombstone     = true;

        const QByteArray bytes = Markoff::SiblingMapOpHeader::encode(h);
        Markoff::SiblingMapOpHeader out;
        QVERIFY(Markoff::SiblingMapOpHeader::decode(bytes, &out));
        QCOMPARE(out.isTombstone, true);
        QVERIFY(out.value.isEmpty());
    }
    void decode_emptyBytesFails() {
        Markoff::SiblingMapOpHeader out;
        QVERIFY(!Markoff::SiblingMapOpHeader::decode(QByteArray(), &out));
    }
    void decode_truncatedFails() {
        Markoff::SiblingMapOpHeader h;
        h.key            = QByteArray("k");
        h.value          = QByteArray("v");
        h.lamportCounter = 1;
        h.lamportReplicaId = 1;
        const QByteArray bytes = Markoff::SiblingMapOpHeader::encode(h);
        const QByteArray truncated = bytes.left(bytes.size() / 2);
        Markoff::SiblingMapOpHeader out;
        QVERIFY(!Markoff::SiblingMapOpHeader::decode(truncated, &out));
    }
};
QTEST_MAIN(TstD5SiblingMapEnvelope)
#include "tst_d5_sibling_map_envelope.moc"
```

- [ ] **Step 2: Append to `libs/markoff-core/tests/d5/CMakeLists.txt`**

```cmake
qt_add_executable(tst_d5_sibling_map_envelope tst_d5_sibling_map_envelope.cpp)
target_link_libraries(tst_d5_sibling_map_envelope PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d5_sibling_map_envelope COMMAND tst_d5_sibling_map_envelope)
```

- [ ] **Step 3: Run — expect FAIL (no header)**

```bash
cmake --build build-dev -j 8 --target tst_d5_sibling_map_envelope 2>&1 | tail -3
```

- [ ] **Step 4: Create the header**

`libs/markoff-core/include/markoff/core/SiblingMapOpHeader.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkoffCoreExport.h>

#include <QByteArray>
#include <QtGlobal>

namespace Markoff {

/// Wire-format header for a sibling-map op. Encoded into MarkoffOp::payload.
/// See spec §3.1.
struct MARKOFF_CORE_EXPORT SiblingMapOpHeader {
    QByteArray  key;
    QByteArray  value;             // empty == tombstone
    quint64     lamportCounter   = 0;
    quint16     lamportReplicaId = 0;
    bool        isTombstone      = false;

    /// Serialize to a byte buffer. Format (little-endian):
    ///   [u32 keyLen][u32 valLen][u64 lamportCounter][u16 lamportReplicaId][u8 isTombstone]
    ///   [keyLen bytes of key][valLen bytes of value]
    static QByteArray encode(const SiblingMapOpHeader &h);

    /// Decode from a byte buffer; returns false on truncation/malformation.
    static bool decode(const QByteArray &bytes, SiblingMapOpHeader *out);
};

}  // namespace Markoff
```

- [ ] **Step 5: Create the implementation**

`libs/markoff-core/src/SiblingMapOpHeader.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/SiblingMapOpHeader.h>

#include <QDataStream>
#include <QIODevice>

namespace Markoff {

QByteArray SiblingMapOpHeader::encode(const SiblingMapOpHeader &h)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_8);

    ds << quint32(h.key.size())
       << quint32(h.value.size())
       << h.lamportCounter
       << h.lamportReplicaId
       << quint8(h.isTombstone ? 1 : 0);
    ds.writeRawData(h.key.constData(), h.key.size());
    ds.writeRawData(h.value.constData(), h.value.size());
    return bytes;
}

bool SiblingMapOpHeader::decode(const QByteArray &bytes, SiblingMapOpHeader *out)
{
    if (!out) return false;
    if (bytes.size() < int(sizeof(quint32) * 2 + sizeof(quint64) + sizeof(quint16) + sizeof(quint8))) {
        return false;
    }

    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_8);

    quint32 keyLen = 0, valLen = 0;
    quint8 tomb = 0;
    ds >> keyLen >> valLen >> out->lamportCounter >> out->lamportReplicaId >> tomb;
    if (ds.status() != QDataStream::Ok) return false;
    out->isTombstone = (tomb != 0);

    QByteArray key(int(keyLen), Qt::Uninitialized);
    if (keyLen > 0) {
        const int read = ds.readRawData(key.data(), int(keyLen));
        if (read != int(keyLen)) return false;
    }
    out->key = key;

    QByteArray value(int(valLen), Qt::Uninitialized);
    if (valLen > 0) {
        const int read = ds.readRawData(value.data(), int(valLen));
        if (read != int(valLen)) return false;
    }
    out->value = value;

    return ds.status() == QDataStream::Ok;
}

}  // namespace Markoff
```

- [ ] **Step 6: Add to `libs/markoff-core/CMakeLists.txt`**

Find the existing source list (search for `MarkoffDocument.cpp`) and append `src/SiblingMapOpHeader.cpp`.

- [ ] **Step 7: Run — expect PASS**

```bash
cmake --build build-dev -j 8 --target tst_d5_sibling_map_envelope
cd build-dev && ctest -j 8 -R tst_d5_sibling_map_envelope --output-on-failure
cd ..
```

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff/core/SiblingMapOpHeader.h \
        libs/markoff-core/src/SiblingMapOpHeader.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/d5/tst_d5_sibling_map_envelope.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: add SiblingMapOpHeader wire envelope (D5 phase 1)"
```

### Task 1.3: `MarkoffSerializer::encode` / `decode`

**Files:**
- Create: `libs/markoff-core/include/markoff/core/MarkoffSerializer.h`
- Create: `libs/markoff-core/src/MarkoffSerializer.cpp`
- Test: `libs/markoff-core/tests/d5/tst_d5_serialization.cpp`

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/d5/tst_d5_serialization.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffSerializer.h>
#include <markoff/core/MarkoffOp.h>

class TstD5Serialization : public QObject {
    Q_OBJECT
private slots:
    void roundtrip_emptyBundle() {
        QList<Markoff::MarkoffOp> ops;
        Markoff::MarkoffBundleMeta meta;
        meta.producerReplicaId = 5;
        meta.bundleId          = 12345;
        meta.opCountInBundle   = 0;
        meta.actionId          = Markoff::ActionId::None;
        meta.producerLamport   = 99;

        QByteArray blob = Markoff::MarkoffSerializer::encode(ops, meta);
        QVERIFY(!blob.isEmpty());

        QList<Markoff::MarkoffOp> outOps;
        Markoff::MarkoffBundleMeta outMeta;
        QVERIFY(Markoff::MarkoffSerializer::decode(blob, &outOps, &outMeta));
        QCOMPARE(outOps.size(), 0);
        QCOMPARE(outMeta.producerReplicaId, meta.producerReplicaId);
        QCOMPARE(outMeta.bundleId, meta.bundleId);
        QCOMPARE(outMeta.opCountInBundle, meta.opCountInBundle);
        QCOMPARE(outMeta.actionId, meta.actionId);
        QCOMPARE(outMeta.producerLamport, meta.producerLamport);
    }
    void roundtrip_singleBufferOp() {
        QList<Markoff::MarkoffOp> ops;
        Markoff::MarkoffOp op;
        op.target = Markoff::CrdtTarget::Buffer;
        op.blockId = 0xdead'beefULL;
        op.payload = QByteArray("opaque-buffer-bytes");
        op.producerReplicaId = 5;
        ops.append(op);

        Markoff::MarkoffBundleMeta meta;
        meta.producerReplicaId = 5;
        meta.bundleId          = 1;
        meta.opCountInBundle   = 1;

        QByteArray blob = Markoff::MarkoffSerializer::encode(ops, meta);
        QList<Markoff::MarkoffOp> outOps;
        Markoff::MarkoffBundleMeta outMeta;
        QVERIFY(Markoff::MarkoffSerializer::decode(blob, &outOps, &outMeta));
        QCOMPARE(outOps.size(), 1);
        QCOMPARE(outOps[0].target, Markoff::CrdtTarget::Buffer);
        QCOMPARE(outOps[0].blockId, op.blockId);
        QCOMPARE(outOps[0].payload, op.payload);
        QCOMPARE(outOps[0].producerReplicaId, op.producerReplicaId);
    }
    void roundtrip_mixedOps() {
        QList<Markoff::MarkoffOp> ops;
        for (auto t : { Markoff::CrdtTarget::IdList,
                        Markoff::CrdtTarget::Buffer,
                        Markoff::CrdtTarget::KindTagMap,
                        Markoff::CrdtTarget::BlockAttrsMap,
                        Markoff::CrdtTarget::FrontmatterMap,
                        Markoff::CrdtTarget::LinkRefMap,
                        Markoff::CrdtTarget::FootnoteDefMap }) {
            Markoff::MarkoffOp op;
            op.target = t;
            op.blockId = (t == Markoff::CrdtTarget::Buffer) ? 42 : 0;
            op.payload = QByteArray("p-").append(QByteArray::number(static_cast<int>(t)));
            op.producerReplicaId = 9;
            ops.append(op);
        }
        Markoff::MarkoffBundleMeta meta;
        meta.producerReplicaId = 9;
        meta.bundleId = 100;
        meta.opCountInBundle = quint16(ops.size());

        QByteArray blob = Markoff::MarkoffSerializer::encode(ops, meta);
        QList<Markoff::MarkoffOp> outOps;
        Markoff::MarkoffBundleMeta outMeta;
        QVERIFY(Markoff::MarkoffSerializer::decode(blob, &outOps, &outMeta));
        QCOMPARE(outOps.size(), ops.size());
        for (int i = 0; i < ops.size(); ++i) {
            QCOMPARE(outOps[i].target, ops[i].target);
            QCOMPARE(outOps[i].blockId, ops[i].blockId);
            QCOMPARE(outOps[i].payload, ops[i].payload);
            QCOMPARE(outOps[i].producerReplicaId, ops[i].producerReplicaId);
        }
    }
    void decode_truncatedFails() {
        QList<Markoff::MarkoffOp> ops;
        Markoff::MarkoffOp op;
        op.target = Markoff::CrdtTarget::Buffer;
        op.blockId = 1;
        op.payload = QByteArray("xx");
        ops.append(op);
        Markoff::MarkoffBundleMeta meta;
        meta.opCountInBundle = 1;
        const QByteArray blob = Markoff::MarkoffSerializer::encode(ops, meta);
        const QByteArray truncated = blob.left(blob.size() / 2);

        QList<Markoff::MarkoffOp> outOps;
        Markoff::MarkoffBundleMeta outMeta;
        QVERIFY(!Markoff::MarkoffSerializer::decode(truncated, &outOps, &outMeta));
    }
};
QTEST_MAIN(TstD5Serialization)
#include "tst_d5_serialization.moc"
```

- [ ] **Step 2: Append to `libs/markoff-core/tests/d5/CMakeLists.txt`**

```cmake
qt_add_executable(tst_d5_serialization tst_d5_serialization.cpp)
target_link_libraries(tst_d5_serialization PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d5_serialization COMMAND tst_d5_serialization)
```

- [ ] **Step 3: Run — expect FAIL (no MarkoffSerializer header)**

```bash
cmake --build build-dev -j 8 --target tst_d5_serialization 2>&1 | tail -3
```

- [ ] **Step 4: Create the header**

`libs/markoff-core/include/markoff/core/MarkoffSerializer.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkoffOp.h>
#include <markoff/core/MarkoffCoreExport.h>

#include <QByteArray>
#include <QList>

namespace Markoff::MarkoffSerializer {

/// Encode a bundle of ops + metadata into a single canonical byte blob.
/// The blob is the unit a consumer pushes to its transport.
/// Format (little-endian):
///   [u32 magic = 0x4D4B4F46 'MKOF']
///   [u16 schemaVersion = 1]
///   [u16 producerReplicaId][u64 bundleId][u16 opCountInBundle]
///   [u32 actionId][u64 producerLamport]
///   for each op:
///     [u8 target][u64 blockId][u16 producerReplicaId][u32 payloadLen][payloadLen bytes]
MARKOFF_CORE_EXPORT QByteArray encode(const QList<MarkoffOp> &ops,
                                      const MarkoffBundleMeta &meta);

/// Decode a blob produced by encode(). Returns false on truncation,
/// magic mismatch, unknown schemaVersion, or any other malformation.
MARKOFF_CORE_EXPORT bool decode(const QByteArray &bytes,
                                QList<MarkoffOp> *outOps,
                                MarkoffBundleMeta *outMeta);

}  // namespace Markoff::MarkoffSerializer
```

**Note (post-Phase-0):** the `payload` for `Buffer` and `IdList` ops is **collabtext's serialised op bytes, byte-for-byte**, produced by the public functions `collabtext::encode_operation(Operation)` and `collabtext::encode_idlist_operation(IdListOperation)` (Phase 0 prerequisite — `include/collabtext/Serialization.h`). `MarkoffSerializer` does not interpret these bytes; it just embeds them as opaque length-prefixed payloads. Sibling-map payloads are produced by `SiblingMapOpHeader::encode`, see Task 1.2.

- [ ] **Step 5: Create the implementation**

`libs/markoff-core/src/MarkoffSerializer.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/MarkoffSerializer.h>

#include <QDataStream>
#include <QIODevice>

namespace Markoff::MarkoffSerializer {

namespace {
constexpr quint32 kMagic = 0x4D4B4F46u;   // 'MKOF'
constexpr quint16 kSchemaVersion = 1;
}  // namespace

QByteArray encode(const QList<MarkoffOp> &ops, const MarkoffBundleMeta &meta)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_8);

    ds << kMagic
       << kSchemaVersion
       << meta.producerReplicaId
       << meta.bundleId
       << meta.opCountInBundle
       << quint32(static_cast<int>(meta.actionId))
       << meta.producerLamport;

    for (const MarkoffOp &op : ops) {
        ds << quint8(op.target)
           << op.blockId
           << op.producerReplicaId
           << quint32(op.payload.size());
        ds.writeRawData(op.payload.constData(), op.payload.size());
    }
    return bytes;
}

bool decode(const QByteArray &bytes, QList<MarkoffOp> *outOps, MarkoffBundleMeta *outMeta)
{
    if (!outOps || !outMeta) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_8);

    quint32 magic = 0;
    quint16 schema = 0;
    ds >> magic >> schema;
    if (magic != kMagic || schema != kSchemaVersion) return false;

    quint32 actionIdRaw = 0;
    ds >> outMeta->producerReplicaId
       >> outMeta->bundleId
       >> outMeta->opCountInBundle
       >> actionIdRaw
       >> outMeta->producerLamport;
    outMeta->actionId = static_cast<ActionId>(actionIdRaw);
    if (ds.status() != QDataStream::Ok) return false;

    outOps->clear();
    for (quint16 i = 0; i < outMeta->opCountInBundle; ++i) {
        quint8 targetRaw = 0;
        quint32 payloadLen = 0;
        MarkoffOp op;
        ds >> targetRaw >> op.blockId >> op.producerReplicaId >> payloadLen;
        if (ds.status() != QDataStream::Ok) return false;
        op.target = static_cast<CrdtTarget>(targetRaw);
        QByteArray payload(int(payloadLen), Qt::Uninitialized);
        if (payloadLen > 0) {
            const int read = ds.readRawData(payload.data(), int(payloadLen));
            if (read != int(payloadLen)) return false;
        }
        op.payload = payload;
        outOps->append(op);
    }
    return ds.status() == QDataStream::Ok;
}

}  // namespace Markoff::MarkoffSerializer
```

- [ ] **Step 6: Append to `libs/markoff-core/CMakeLists.txt`**

Add `src/MarkoffSerializer.cpp` to the source list (alongside `src/SiblingMapOpHeader.cpp`).

- [ ] **Step 7: Run — expect PASS**

```bash
cmake --build build-dev -j 8 --target tst_d5_serialization
cd build-dev && ctest -j 8 -R tst_d5_serialization --output-on-failure
cd ..
```

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffSerializer.h \
        libs/markoff-core/src/MarkoffSerializer.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/d5/tst_d5_serialization.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: add MarkoffSerializer encode/decode (D5 phase 1)"
```

### Task 1.4: Phase 1 closeout

- [ ] **Step 1: Run the full test suite**

```bash
cd build-dev && ctest -j 8 --output-on-failure 2>&1 | tail -3
cd ..
```

Expected: 100% tests passed (count = pre-D5 baseline + 3 new D5 tests).

- [ ] **Step 2: Append d-arc-status entry**

Add to `docs/d-arc/d-arc-status.md`'s recent-changes log (just below the most-recent entry):

```
| 2026-05-08 | <SHA> | D5 phase 1 complete: boundary types (`CrdtTarget`, `MarkoffOp`, `MarkoffBundleMeta`, `SiblingMapOpHeader`) + `MarkoffSerializer::encode/decode`. Round-trip tests green. |
```

- [ ] **Step 3: Commit the status update**

```bash
git add docs/d-arc/d-arc-status.md
git commit -m "d-arc: D5 phase 1 complete (status board update)"
```

---

## Phase 2 — `MarkoffDocument` constructor with replica ID

**Goal:** Add the optional-replica-ID constructor and the `replicaId()` / `isCollabConfigured()` accessors. Single-user default works (sentinel `0x0001`). No emissions yet.

### Task 2.1: Replica-ID constructor

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/MarkoffDocument.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-core/src/MarkoffDocumentPrivate.h`
- Test: `libs/markoff-core/tests/d5/tst_d5_replica_id.cpp`

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/d5/tst_d5_replica_id.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>

class TstD5ReplicaId : public QObject {
    Q_OBJECT
private slots:
    void defaultConstructor_isSingleUser() {
        Markoff::MarkoffDocument doc;
        QVERIFY(!doc.isCollabConfigured());
        QCOMPARE(doc.replicaId(), quint16(0x0001));
    }
    void explicitConstructor_isCollab() {
        Markoff::MarkoffDocument doc(quint16(42));
        QVERIFY(doc.isCollabConfigured());
        QCOMPARE(doc.replicaId(), quint16(42));
    }
    void replicaId_isImmutable_NoSetter() {
        // Compile-time guarantee: there should be no setReplicaId method.
        // This test just locks in the API surface by constructing and
        // reading; if a setter is added in future, it should not appear
        // here.
        Markoff::MarkoffDocument doc(quint16(7));
        QCOMPARE(doc.replicaId(), quint16(7));
    }
};
QTEST_MAIN(TstD5ReplicaId)
#include "tst_d5_replica_id.moc"
```

- [ ] **Step 2: Append to `libs/markoff-core/tests/d5/CMakeLists.txt`**

```cmake
qt_add_executable(tst_d5_replica_id tst_d5_replica_id.cpp)
target_link_libraries(tst_d5_replica_id PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d5_replica_id COMMAND tst_d5_replica_id)
```

- [ ] **Step 3: Run — expect FAIL (`isCollabConfigured` does not exist)**

```bash
cmake --build build-dev -j 8 --target tst_d5_replica_id 2>&1 | tail -3
```

- [ ] **Step 4: Modify `MarkoffDocument.h`**

In `libs/markoff-core/include/markoff/core/MarkoffDocument.h`, locate the existing constructor declaration. Add the replica-ID overload alongside it, and the two accessors:

```cpp
public:
    /// Single-user mode: replica ID defaults to a sentinel (0x0001).
    explicit MarkoffDocument(QObject *parent = nullptr);

    /// Collab mode: replica ID is explicit and immutable.
    explicit MarkoffDocument(quint16 replicaId, QObject *parent = nullptr);

    /// The replica ID this document was constructed with.
    /// Single-user mode returns 0x0001.
    quint16 replicaId() const noexcept;

    /// True iff constructed with the explicit-replica-ID overload.
    bool isCollabConfigured() const noexcept;
```

- [ ] **Step 5: Modify `MarkoffDocumentPrivate.h`**

Add fields to the `Private` (or whatever the existing private struct is named):

```cpp
quint16 replicaId         = 0x0001;
bool    collabConfigured  = false;
```

- [ ] **Step 6: Modify `MarkoffDocument.cpp`**

Locate the existing default constructor. Add the new overload that delegates to a shared init path. Suggested skeleton (adapt to actual existing structure):

```cpp
MarkoffDocument::MarkoffDocument(QObject *parent)
    : MarkoffDocument(quint16(0x0001), parent)
{
    d->collabConfigured = false;   // override the value the delegated ctor set
}

MarkoffDocument::MarkoffDocument(quint16 replicaId, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->replicaId = replicaId;
    d->collabConfigured = true;
    // ... rest of existing initialisation
}
```

If the existing default constructor has substantial body, refactor it so both constructors call a private `init()` helper. The key behaviour: `collabConfigured` is `true` only when the explicit-replica-ID overload was used.

Add accessor implementations:

```cpp
quint16 MarkoffDocument::replicaId() const noexcept { return d->replicaId; }
bool    MarkoffDocument::isCollabConfigured() const noexcept { return d->collabConfigured; }
```

- [ ] **Step 7: Wire `replicaId` through to existing CRDT constructions**

Today the document constructs `Buffer`s and the `IdList` with a hard-coded replica ID (likely 1 or random). Ensure those now use `d->replicaId`. Search:

```bash
grep -n "replicaId\|m_replicaId\|d->replica" libs/markoff-core/src/MarkoffDocument.cpp libs/markoff-core/src/MarkoffDocumentPrivate.h | head
```

Wherever a hard-coded value or a separate replica-id field exists, replace with `d->replicaId`. Single-user mode keeps working (sentinel `0x0001`).

- [ ] **Step 8: Run — expect PASS**

```bash
cmake --build build-dev -j 8 --target tst_d5_replica_id
cd build-dev && ctest -j 8 -R tst_d5_replica_id --output-on-failure
cd ..
```

- [ ] **Step 9: Run the full test suite to confirm no regressions**

```bash
cd build-dev && ctest -j 8 --output-on-failure 2>&1 | tail -3
cd ..
```

Expected: same green count as Phase 1 + 1 new test (or +1 with multiple sub-cases).

- [ ] **Step 10: Commit**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/src/MarkoffDocumentPrivate.h \
        libs/markoff-core/tests/d5/tst_d5_replica_id.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: MarkoffDocument replica-ID constructor + accessors (D5 phase 2)"
```

### Task 2.2: Phase 2 closeout

- [ ] **Step 1: Append d-arc-status entry** (same pattern as Task 1.4 Step 2).
- [ ] **Step 2: Commit status update.**

---

## Phase 3 — `localOpsProduced` emission

**Goal:** Emit `localOpsProduced(ops, meta)` exactly once per `UndoLog::Transaction` commit, with all ops produced inside the transaction. Single emission per user-action; no emission per CRDT op.

### Task 3.1: Add the signal declaration

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/MarkoffDocument.h`
- Test: `libs/markoff-core/tests/d5/tst_d5_local_ops_signal.cpp`

- [ ] **Step 1: Write the failing test (compile-only signal existence)**

`libs/markoff-core/tests/d5/tst_d5_local_ops_signal.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>

class TstD5LocalOpsSignal : public QObject {
    Q_OBJECT
private slots:
    void signalExists_canBeSpiedOn() {
        Markoff::MarkoffDocument doc(quint16(42));
        QSignalSpy spy(&doc, SIGNAL(localOpsProduced(QList<Markoff::MarkoffOp>,
                                                      Markoff::MarkoffBundleMeta)));
        QVERIFY(spy.isValid());
    }
};
QTEST_MAIN(TstD5LocalOpsSignal)
#include "tst_d5_local_ops_signal.moc"
```

- [ ] **Step 2: Append to `libs/markoff-core/tests/d5/CMakeLists.txt`** (same triplet pattern).

- [ ] **Step 3: Run — expect FAIL** (signal not declared).

- [ ] **Step 4: Add the signal to `MarkoffDocument.h`**

In the `signals:` section (add one if not present):

```cpp
signals:
    /// Emitted once per UndoLog::Transaction commit with all ops the
    /// transaction produced. See spec §2.2.
    void localOpsProduced(QList<Markoff::MarkoffOp> ops,
                          Markoff::MarkoffBundleMeta meta);
```

- [ ] **Step 5: Run — expect PASS** (signal declared, `QSignalSpy::isValid()` succeeds).

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffDocument.h \
        libs/markoff-core/tests/d5/tst_d5_local_ops_signal.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: declare localOpsProduced signal (D5 phase 3)"
```

### Task 3.2: Bundle-ID counter and meta construction

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocumentPrivate.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` (helper)

- [ ] **Step 1: Add to `MarkoffDocumentPrivate.h`**

```cpp
quint64 nextBundleId = 1;     // monotonic per-document; starts at 1
```

- [ ] **Step 2: Add a helper in `MarkoffDocument.cpp`**

```cpp
namespace Markoff {
namespace {

/// Build a MarkoffBundleMeta for the *next* bundle this document will
/// emit. Increments d->nextBundleId. Caller fills in opCountInBundle
/// after collecting ops.
MarkoffBundleMeta buildBundleMeta(MarkoffDocument::Private *d,
                                   ActionId actionId,
                                   quint64 producerLamport)
{
    MarkoffBundleMeta meta;
    meta.producerReplicaId = d->replicaId;
    meta.bundleId          = d->nextBundleId++;
    meta.opCountInBundle   = 0;   // filled in after collection
    meta.actionId          = actionId;
    meta.producerLamport   = producerLamport;
    return meta;
}

}  // anonymous namespace
}  // namespace Markoff
```

- [ ] **Step 3: Compile to confirm helpers don't break the build**

```bash
cmake --build build-dev -j 8 --target markoff_core
```

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/src/MarkoffDocumentPrivate.h
git commit -m "markoff-core: bundle-meta helper + bundle-id counter (D5 phase 3)"
```

### Task 3.3: Hook transaction commit to emission

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/UndoLog.h`
- Modify: `libs/markoff-core/src/UndoLog.cpp`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Test: `libs/markoff-core/tests/d5/tst_d5_local_ops_emission.cpp`

The strategy: `UndoLog::Transaction` already collects per-op information (which CRDT, op identity). On commit, we need to convert each registered op into a `MarkoffOp` and emit one bundle.

The cleanest hook is a **commit callback** that `MarkoffDocument` registers with `UndoLog`. The callback fires once per commit, receives the list of `(CrdtTarget, blockId, opPayload, producerReplicaId)` tuples, builds the bundle, emits.

- [ ] **Step 1: Inspect existing UndoLog API**

```bash
grep -n "class UndoLog\|class Transaction\|registerOp\|commit\|onCommit" libs/markoff-core/include/markoff/core/UndoLog.h | head -20
```

Note the existing `Transaction::registerOp(...)` signature and the commit path.

- [ ] **Step 2: Write the failing test**

`libs/markoff-core/tests/d5/tst_d5_local_ops_emission.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

class TstD5LocalOpsEmission : public QObject {
    Q_OBJECT
private slots:
    void singleBufferEdit_emitsOneBundle() {
        MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n"));

        QSignalSpy spy(&doc, &MarkoffDocument::localOpsProduced);

        // Apply one buffer edit (insert "!" at end of first block).
        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());
        const BlockId b0 = blockIds.front();

        UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2ApplyBufferEdit(b0, /*offset*/5, /*remove*/0,
                               QByteArrayLiteral("!"), t);
        // Transaction commits when t goes out of scope at end of block.
        // ... but be explicit:
        // (UndoLog API may require an explicit commit; adapt as needed.)
        QCOMPARE(spy.count(), 0);   // not yet committed
        // commit happens at end of scope below
        // (close braces explicit so t destructs)
        {
            const auto consumed = std::move(t);
            (void)consumed;
        }

        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        const auto ops = args.at(0).value<QList<MarkoffOp>>();
        const auto meta = args.at(1).value<MarkoffBundleMeta>();

        QCOMPARE(meta.producerReplicaId, quint16(42));
        QCOMPARE(meta.opCountInBundle, quint16(ops.size()));
        QVERIFY(meta.bundleId > 0);
        QCOMPARE(ops.size(), 1);
        QCOMPARE(ops[0].target, CrdtTarget::Buffer);
        QCOMPARE(ops[0].blockId, b0.raw());
        QVERIFY(!ops[0].payload.isEmpty());
        QCOMPARE(ops[0].producerReplicaId, quint16(42));
    }
    void compoundCmd_emitsOneBundleWithMultipleOps() {
        // Cmd::enterAtEnd touches IdList + Buffer in one transaction.
        MarkoffDocument doc(quint16(7));
        doc.loadFromMarkdown(QByteArrayLiteral("First\n"));
        QSignalSpy spy(&doc, &MarkoffDocument::localOpsProduced);

        // Use whatever Cmd is appropriate; this test asserts that
        // a multi-CRDT user action produces exactly ONE emission with
        // multiple ops in it.
        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());

        // Replace this with the actual Cmd::enterAtEnd call once you
        // verify its signature in the codebase. The shape:
        Cmd::enterAtEnd(doc, blockIds.front());

        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        const auto ops = args.at(0).value<QList<MarkoffOp>>();
        QVERIFY(ops.size() >= 2);   // at least an IdList op + a Buffer op
        QCOMPARE(args.at(1).value<MarkoffBundleMeta>().opCountInBundle,
                 quint16(ops.size()));
    }
    void noEdit_noEmission() {
        MarkoffDocument doc(quint16(99));
        doc.loadFromMarkdown(QByteArrayLiteral("X\n"));
        QSignalSpy spy(&doc, &MarkoffDocument::localOpsProduced);
        // Take a snapshot but no edits.
        QCOMPARE(spy.count(), 0);
    }
};
QTEST_MAIN(TstD5LocalOpsEmission)
#include "tst_d5_local_ops_emission.moc"
```

**Note for the implementer:** the test references `doc.d2UndoLog()` and `doc.d2ApplyBufferEdit(...)` and `Cmd::enterAtEnd(...)` — verify those exact signatures exist (search the codebase) and adjust the test if names differ. The structural assertions (count, target, blockId) are the load-bearing part.

- [ ] **Step 3: Append to `libs/markoff-core/tests/d5/CMakeLists.txt`**:

```cmake
qt_add_executable(tst_d5_local_ops_emission tst_d5_local_ops_emission.cpp)
target_link_libraries(tst_d5_local_ops_emission PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d5_local_ops_emission COMMAND tst_d5_local_ops_emission)
```

- [ ] **Step 4: Run — expect FAIL** (no emission wired).

- [ ] **Step 5: Add an `onCommit` callback to `UndoLog`**

In `libs/markoff-core/include/markoff/core/UndoLog.h`, add to the public class:

```cpp
public:
    /// Per-op record collected during a Transaction.
    struct CommittedOp {
        CrdtTarget target;
        quint64    blockId;       // valid iff target==Buffer; 0 otherwise
        QByteArray payload;       // serialized op bytes
    };

    using OnCommitFn = std::function<void(const std::vector<CommittedOp> &ops,
                                          ActionId actionId,
                                          quint64 producerLamport)>;

    /// Register a single global commit callback. The callback fires
    /// after each Transaction commits, BEFORE Transaction's destructor
    /// returns. Replaces any previous callback.
    void setOnCommit(OnCommitFn fn);
```

(Note: `CrdtTarget` and `ActionId` come from `<markoff/core/MarkoffOp.h>` and `<markoff/core/ActionId.h>`. Add the include.)

- [ ] **Step 6: Implement the callback wiring in `UndoLog.cpp`**

Modify the `Transaction` commit path so it builds a `vector<CommittedOp>` from the registered ops (translating each `CrdtTarget` and serialising the op payload) and calls the registered `OnCommitFn` with it.

The translation:
- `CrdtTarget::buffer(blockId)` → `target = Buffer; blockId = blockId.raw()`
- `CrdtTarget::idList()` → `target = IdList; blockId = 0`
- Sibling-map targets are added in Phase 5 — for now, they're unhandled and the helper logs a warning and skips them. (Phase 5 fills these in.)

For payload serialisation:
- Buffer ops: serialize the registered `OpId` plus enough context to apply on a remote replica. Use collabtext's existing `Buffer::serializeOp(opId)` if it exists; otherwise the implementer adds a thin helper that produces canonical bytes from the op's Lamport + content. **Verify the collabtext API** — search `~/dev/collabtext/libs/collabtext/src/crdt/Buffer.h` for the public serialisation entry-point.
- IdList ops: same pattern — serialize via collabtext's IdList op-serialisation helper.

If collabtext does not expose op serialisation as a single call, the implementer writes a thin wrapper inside `markoff-core` that calls collabtext's segment-writer codec to produce canonical bytes for one op.

- [ ] **Step 7: Wire the callback in `MarkoffDocument`'s constructor**

In `MarkoffDocument.cpp`, after constructing the `UndoLog`:

```cpp
d->undoLog.setOnCommit([this](const std::vector<UndoLog::CommittedOp> &committed,
                               ActionId actionId,
                               quint64 producerLamport) {
    QList<MarkoffOp> ops;
    ops.reserve(int(committed.size()));
    for (const auto &c : committed) {
        MarkoffOp op;
        op.target = c.target;
        op.blockId = c.blockId;
        op.payload = c.payload;
        op.producerReplicaId = d->replicaId;
        ops.append(op);
    }
    auto meta = buildBundleMeta(d.get(), actionId, producerLamport);
    meta.opCountInBundle = quint16(ops.size());
    Q_EMIT localOpsProduced(std::move(ops), std::move(meta));
});
```

- [ ] **Step 8: Run — expect PASS**

```bash
cmake --build build-dev -j 8 --target tst_d5_local_ops_emission
cd build-dev && ctest -j 8 -R tst_d5_local_ops_emission --output-on-failure
cd ..
```

- [ ] **Step 9: Run the full suite, confirm no regressions**

```bash
cd build-dev && ctest -j 8 --output-on-failure 2>&1 | tail -3
cd ..
```

- [ ] **Step 10: Commit**

```bash
git add libs/markoff-core/include/markoff/core/UndoLog.h \
        libs/markoff-core/src/UndoLog.cpp \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/d5/tst_d5_local_ops_emission.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: emit localOpsProduced from UndoLog commit callback (D5 phase 3)"
```

### Task 3.4: Phase 3 closeout

- [ ] **Step 1: Append d-arc-status entry; commit.**

---

## Phase 4 — `applyRemoteOps` and tier-0 convergence

**Goal:** Add `applyRemoteOps(ops, meta)`. Dispatch by `CrdtTarget` to per-CRDT `applyRemote(...)`. Tier-0 two-doc convergence tests **land here** and become the foundational verification for every subsequent phase.

### Task 4.1: Declare the method

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/MarkoffDocument.h`

- [ ] **Step 1: Add the public method declaration**

```cpp
public:
    /// Apply remote ops in arrival order. See spec §2.4.
    void applyRemoteOps(QList<Markoff::MarkoffOp> ops,
                        Markoff::MarkoffBundleMeta meta);
```

- [ ] **Step 2: Build to confirm header-only change is clean**

```bash
cmake --build build-dev -j 8 --target markoff_core 2>&1 | tail -3
```

(Should fail at link time if anything references `applyRemoteOps`; should be clean otherwise.)

- [ ] **Step 3: Commit (header-only)**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffDocument.h
git commit -m "markoff-core: declare applyRemoteOps method (D5 phase 4)"
```

### Task 4.2: Buffer + IdList op application

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Test: `libs/markoff-core/tests/d5/tst_d5_apply_remote_basic.cpp`

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/d5/tst_d5_apply_remote_basic.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/BlockId.h>

using namespace Markoff;

class TstD5ApplyRemoteBasic : public QObject {
    Q_OBJECT
private slots:
    void applyOpsFromAnotherDoc_landsOnTarget() {
        // Two docs with different replica IDs.
        MarkoffDocument a(quint16(1));
        MarkoffDocument b(quint16(2));
        a.loadFromMarkdown(QByteArrayLiteral("Same\n"));
        b.loadFromMarkdown(QByteArrayLiteral("Same\n"));

        QList<MarkoffOp> capturedOps;
        MarkoffBundleMeta capturedMeta;
        QObject::connect(&a, &MarkoffDocument::localOpsProduced,
                         &a, [&](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
            capturedOps = ops;
            capturedMeta = meta;
        });

        // Edit on a.
        const auto aBlocks = a.iterateBlocks();
        QVERIFY(!aBlocks.empty());
        {
            UndoLog::Transaction t(a.d2UndoLog());
            a.d2ApplyBufferEdit(aBlocks.front(), 4, 0,
                                 QByteArrayLiteral("!"), t);
        }
        QVERIFY(!capturedOps.isEmpty());

        // Apply to b. b's text should change.
        const auto bTextBefore = QString::fromUtf8(b.blockText(b.iterateBlocks().front()));
        b.applyRemoteOps(capturedOps, capturedMeta);
        const auto bTextAfter = QString::fromUtf8(b.blockText(b.iterateBlocks().front()));
        QVERIFY(bTextBefore != bTextAfter);
    }
};
QTEST_MAIN(TstD5ApplyRemoteBasic)
#include "tst_d5_apply_remote_basic.moc"
```

- [ ] **Step 2: Add to test CMakeLists** (same triplet pattern).

- [ ] **Step 3: Run — expect FAIL** (link error: `applyRemoteOps` not defined).

- [ ] **Step 4: Implement `applyRemoteOps` in `MarkoffDocument.cpp`**

```cpp
void MarkoffDocument::applyRemoteOps(QList<MarkoffOp> ops, MarkoffBundleMeta meta)
{
    Q_UNUSED(meta);   // bundle metadata used by Phase 6 ack tracking; not yet
    for (const MarkoffOp &op : ops) {
        // Defensive: D5 invariant — every op's producerReplicaId must
        // match the bundle's. Skip and warn on mismatch.
        if (op.producerReplicaId != meta.producerReplicaId) {
            qWarning() << "MarkoffDocument::applyRemoteOps: producer mismatch"
                       << "op=" << op.producerReplicaId
                       << "bundle=" << meta.producerReplicaId
                       << "; skipping op";
            continue;
        }

        switch (op.target) {
        case CrdtTarget::Buffer: {
            const BlockId blockId = BlockId::fromRaw(op.blockId);
            applyRemoteBufferOp(blockId, op.payload);
            break;
        }
        case CrdtTarget::IdList: {
            applyRemoteIdListOp(op.payload);
            break;
        }
        case CrdtTarget::KindTagMap:
        case CrdtTarget::BlockAttrsMap:
        case CrdtTarget::FrontmatterMap:
        case CrdtTarget::LinkRefMap:
        case CrdtTarget::FootnoteDefMap:
            // Phase 5: sibling-map application. For now, skip with a debug log.
            qDebug() << "applyRemoteOps: sibling-map target not yet implemented";
            break;
        }
    }
    scheduleD2Changed();   // existing debounced doc-changed fan-out
}
```

Implement the two private helpers `applyRemoteBufferOp` and `applyRemoteIdListOp`:

```cpp
void MarkoffDocument::applyRemoteBufferOp(BlockId blockId, const QByteArray &payload)
{
    auto it = d->blockBuffers.find(blockId);
    if (it == d->blockBuffers.end()) {
        // Unknown block — possible if the IdList op for this block hasn't
        // arrived yet. Buffer the op for retry, OR simply drop and rely on
        // the producer's transport ordering. D5 spec §2.4: "The consumer
        // must preserve per-stream order for Buffer (per blockId) and
        // IdList streams." Out-of-order IdList-vs-Buffer: queue here.
        // Simple v1 implementation: queue in a pending-ops map, retry on
        // every IdList apply. (Keep the implementation simple; if this
        // proves a real issue, expand later.)
        d->pendingBufferOps[blockId].append(payload);
        return;
    }
    it->second->apply_remote_op(payload.toStdString());
    auto proxyIt = d->bufferProxies.find(blockId);
    if (proxyIt != d->bufferProxies.end() && proxyIt.value()) {
        proxyIt.value()->notifyChanged();
    }
}

void MarkoffDocument::applyRemoteIdListOp(const QByteArray &payload)
{
    d->idList.apply_remote_op(payload.toStdString());
    d->idListProxy->notifyChanged();

    // Drain any pending buffer ops for blocks that have just become known.
    // (Iterate copy of pendingBufferOps because we mutate the map.)
    auto pending = d->pendingBufferOps;
    d->pendingBufferOps.clear();
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        if (d->blockBuffers.find(it.key()) != d->blockBuffers.end()) {
            for (const QByteArray &p : it.value()) {
                applyRemoteBufferOp(it.key(), p);
            }
        } else {
            // Still unknown — keep waiting.
            d->pendingBufferOps[it.key()] = it.value();
        }
    }
}
```

(Add forward declarations to `MarkoffDocument.h` as private methods, or in `MarkoffDocumentPrivate.h` as members of the helper. Adapt to the existing access pattern.)

- [ ] **Step 5: Add `pendingBufferOps` to `MarkoffDocumentPrivate.h`**

```cpp
QMap<BlockId, QList<QByteArray>> pendingBufferOps;
```

- [ ] **Step 6: Add the matching pure-virtual or private declaration**

In `MarkoffDocument.h` (private section):

```cpp
private:
    void applyRemoteBufferOp(Markoff::BlockId blockId, const QByteArray &payload);
    void applyRemoteIdListOp(const QByteArray &payload);
```

- [ ] **Step 7: Use collabtext's public `applyRemoteOp` (Phase 0 prerequisite)**

Per the negotiation response (`~/dev/collabtext/docs/specs/2026-05-08-d5-negotiation-response.md` item 1), `applyRemoteOp(Operation)` is public on `CrdtEngine` and `IdList` after Phase 0 lands. The Markoff side reconstructs typed op values from `payload` bytes via `collabtext::decode_operation(...)` / `collabtext::decode_idlist_operation(...)` (item 2; from `include/collabtext/Serialization.h`):

```cpp
// Buffer op: payload is encode_operation(Operation) bytes.
const auto opt = collabtext::decode_operation(
    std::string(payload.constData(), size_t(payload.size())));
if (!opt) {
    qWarning() << "applyRemoteBufferOp: decode_operation failed; skipping";
    return;
}
buffer->apply_remote_op(*opt);     // public on CrdtEngine/Buffer post-Phase-0

// IdList op: same pattern with decode_idlist_operation + IdList::apply_remote_op.
```

Verify:

```bash
grep -n "apply_remote_op\|applyRemoteOp\|decode_operation\|decode_idlist_operation" \
   /home/clinton/dev/collabtext/include/collabtext/Serialization.h \
   /home/clinton/dev/collabtext/include/collabtext/CrdtEngine.h \
   /home/clinton/dev/collabtext/include/collabtext/IdList.h
```

All four names should appear. If not, Phase 0 hasn't completed — return to Phase 0 wait state.

- [ ] **Step 8: Run — expect PASS**

```bash
cmake --build build-dev -j 8 --target tst_d5_apply_remote_basic
cd build-dev && ctest -j 8 -R tst_d5_apply_remote_basic --output-on-failure
cd ..
```

- [ ] **Step 9: Commit**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/src/MarkoffDocumentPrivate.h \
        libs/markoff-core/tests/d5/tst_d5_apply_remote_basic.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: applyRemoteOps for Buffer + IdList targets (D5 phase 4)"
```

### Task 4.3: Tier-0 two-doc convergence test

**Files:**
- Test: `libs/markoff-core/tests/d5/tst_d5_two_doc_convergence.cpp`

This is the **load-bearing** test for D5. It proves divergent edits on two docs converge when their op streams are routed to each other.

- [ ] **Step 1: Write the convergence-test scaffolding**

`libs/markoff-core/tests/d5/tst_d5_two_doc_convergence.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/BlockId.h>

using namespace Markoff;

namespace {

/// Helper: routes ops from `from` to `to` synchronously.
void wireRouter(MarkoffDocument *from, MarkoffDocument *to)
{
    QObject::connect(from, &MarkoffDocument::localOpsProduced,
                     to,   [to](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
        to->applyRemoteOps(std::move(ops), std::move(meta));
    });
}

/// Helper: assert two docs have identical block sequences and per-block text.
void assertConverged(MarkoffDocument *a, MarkoffDocument *b)
{
    const auto blocksA = a->iterateBlocks();
    const auto blocksB = b->iterateBlocks();
    QCOMPARE(blocksA.size(), blocksB.size());
    for (size_t i = 0; i < blocksA.size(); ++i) {
        QCOMPARE(blocksA[i], blocksB[i]);
        QCOMPARE(a->blockText(blocksA[i]), b->blockText(blocksB[i]));
    }
}

}  // namespace

class TstD5TwoDocConvergence : public QObject {
    Q_OBJECT
private slots:
    void identicalLoad_thenSerialEdits_converges() {
        MarkoffDocument a(quint16(1));
        MarkoffDocument b(quint16(2));
        a.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld\n"));
        b.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld\n"));

        wireRouter(&a, &b);
        wireRouter(&b, &a);

        // a edits.
        {
            UndoLog::Transaction t(a.d2UndoLog());
            const auto blocks = a.iterateBlocks();
            a.d2ApplyBufferEdit(blocks.front(), 5, 0, QByteArrayLiteral("!"), t);
        }
        // b edits.
        {
            UndoLog::Transaction t(b.d2UndoLog());
            const auto blocks = b.iterateBlocks();
            b.d2ApplyBufferEdit(blocks.back(), 5, 0, QByteArrayLiteral("?"), t);
        }
        assertConverged(&a, &b);
    }

    void interleavedEdits_converges() {
        MarkoffDocument a(quint16(1));
        MarkoffDocument b(quint16(2));
        a.loadFromMarkdown(QByteArrayLiteral("Line\n"));
        b.loadFromMarkdown(QByteArrayLiteral("Line\n"));
        wireRouter(&a, &b);
        wireRouter(&b, &a);

        // 10 alternating single-char inserts at end of block.
        for (int i = 0; i < 10; ++i) {
            const QByteArray ch = QByteArray(1, char('A' + i));
            MarkoffDocument *who = (i % 2 == 0) ? &a : &b;
            UndoLog::Transaction t(who->d2UndoLog());
            const auto blocks = who->iterateBlocks();
            const QByteArray current = who->blockText(blocks.front());
            who->d2ApplyBufferEdit(blocks.front(),
                                    quint32(current.size() - 1),  // before trailing \n
                                    0, ch, t);
        }
        assertConverged(&a, &b);
    }

    void structuralInsert_converges() {
        MarkoffDocument a(quint16(1));
        MarkoffDocument b(quint16(2));
        a.loadFromMarkdown(QByteArrayLiteral("First\n"));
        b.loadFromMarkdown(QByteArrayLiteral("First\n"));
        wireRouter(&a, &b);
        wireRouter(&b, &a);

        // a inserts a new block after the first.
        const auto aBlocks = a.iterateBlocks();
        {
            UndoLog::Transaction t(a.d2UndoLog());
            BlockId newBlk = a.d2InsertBlock(aBlocks.front(), BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(newBlk, 0, 0, QByteArrayLiteral("Second\n"), t);
        }
        assertConverged(&a, &b);
    }

    void structuralRemove_converges() {
        MarkoffDocument a(quint16(1));
        MarkoffDocument b(quint16(2));
        a.loadFromMarkdown(QByteArrayLiteral("First\n\nSecond\n"));
        b.loadFromMarkdown(QByteArrayLiteral("First\n\nSecond\n"));
        wireRouter(&a, &b);
        wireRouter(&b, &a);

        const auto aBlocks = a.iterateBlocks();
        QVERIFY(aBlocks.size() >= 2);
        {
            UndoLog::Transaction t(a.d2UndoLog());
            a.d2RemoveBlock(aBlocks[1], t);
        }
        assertConverged(&a, &b);
    }

    void concurrentDivergentEdits_converges() {
        MarkoffDocument a(quint16(1));
        MarkoffDocument b(quint16(2));
        a.loadFromMarkdown(QByteArrayLiteral("X\n"));
        b.loadFromMarkdown(QByteArrayLiteral("X\n"));
        wireRouter(&a, &b);
        wireRouter(&b, &a);

        // Both edit "concurrently" — each completes its transaction
        // before its ops reach the other (in this in-process simulation,
        // signals are synchronous; the alternation simulates interleave).
        const auto aBlocks = a.iterateBlocks();
        const auto bBlocks = b.iterateBlocks();
        // a-side edit
        UndoLog::Transaction ta(a.d2UndoLog());
        a.d2ApplyBufferEdit(aBlocks.front(), 1, 0, QByteArrayLiteral("a"), ta);
        // b-side edit, before ta commits
        UndoLog::Transaction tb(b.d2UndoLog());
        b.d2ApplyBufferEdit(bBlocks.front(), 1, 0, QByteArrayLiteral("b"), tb);
        // commit both (destructors)
        // ta out of scope first
        // tb out of scope after this block
        assertConverged(&a, &b);
    }
};
QTEST_MAIN(TstD5TwoDocConvergence)
#include "tst_d5_two_doc_convergence.moc"
```

- [ ] **Step 2: Add to test CMakeLists.**

- [ ] **Step 3: Run — expect PASS** (or revealing failure)

```bash
cmake --build build-dev -j 8 --target tst_d5_two_doc_convergence
cd build-dev && ctest -j 8 -R tst_d5_two_doc_convergence --output-on-failure
cd ..
```

If a sub-case fails, the failure points at a real architectural bug in the boundary or in the underlying CRDT op routing. **Fix at this phase**, not later. Likely culprits: (a) Buffer/IdList op serialisation skipping required state, (b) cross-stream order assumption violated, (c) the `pendingBufferOps` queue not draining correctly. Read the failure carefully; investigate; correct in `MarkoffDocument.cpp` or `UndoLog.cpp` before continuing.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-core/tests/d5/tst_d5_two_doc_convergence.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: tier-0 two-doc convergence tests (D5 phase 4)"
```

### Task 4.4: Three-doc convergence (catches order-dependence)

**Files:**
- Test: `libs/markoff-core/tests/d5/tst_d5_three_doc_convergence.cpp`

- [ ] **Step 1: Write the test**

`libs/markoff-core/tests/d5/tst_d5_three_doc_convergence.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

namespace {
void wireMesh(QList<MarkoffDocument*> docs) {
    for (auto *from : docs) {
        for (auto *to : docs) {
            if (from == to) continue;
            QObject::connect(from, &MarkoffDocument::localOpsProduced,
                             to, [to](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
                to->applyRemoteOps(std::move(ops), std::move(meta));
            });
        }
    }
}
}

class TstD5ThreeDocConvergence : public QObject {
    Q_OBJECT
private slots:
    void threeWayEdits_converge() {
        MarkoffDocument a(1), b(2), c(3);
        for (auto *d : { &a, &b, &c })
            d->loadFromMarkdown(QByteArrayLiteral("X\n"));
        wireMesh({ &a, &b, &c });

        // Each edits independently.
        for (auto *d : { &a, &b, &c }) {
            UndoLog::Transaction t(d->d2UndoLog());
            const auto blocks = d->iterateBlocks();
            const QByteArray cur = d->blockText(blocks.front());
            d->d2ApplyBufferEdit(blocks.front(), quint32(cur.size() - 1), 0,
                                  QByteArray(1, char('a' + d->replicaId())), t);
        }
        // All three should converge.
        QCOMPARE(a.iterateBlocks().size(), b.iterateBlocks().size());
        QCOMPARE(b.iterateBlocks().size(), c.iterateBlocks().size());
        QCOMPARE(a.blockText(a.iterateBlocks().front()),
                 b.blockText(b.iterateBlocks().front()));
        QCOMPARE(b.blockText(b.iterateBlocks().front()),
                 c.blockText(c.iterateBlocks().front()));
    }
};
QTEST_MAIN(TstD5ThreeDocConvergence)
#include "tst_d5_three_doc_convergence.moc"
```

- [ ] **Step 2: Add to test CMakeLists.**

- [ ] **Step 3: Run — expect PASS.** Failures here indicate ordering bugs the two-doc case missed.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-core/tests/d5/tst_d5_three_doc_convergence.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: three-doc convergence test (D5 phase 4)"
```

### Task 4.5: Idempotency test

**Files:**
- Test: `libs/markoff-core/tests/d5/tst_d5_idempotency.cpp`

- [ ] **Step 1: Write the test**

`libs/markoff-core/tests/d5/tst_d5_idempotency.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

class TstD5Idempotency : public QObject {
    Q_OBJECT
private slots:
    void redeliveredBundle_doesNotDoubleApply() {
        MarkoffDocument a(1), b(2);
        a.loadFromMarkdown(QByteArrayLiteral("Hi\n"));
        b.loadFromMarkdown(QByteArrayLiteral("Hi\n"));

        QList<MarkoffOp> capturedOps;
        MarkoffBundleMeta capturedMeta;
        QObject::connect(&a, &MarkoffDocument::localOpsProduced,
                         &a, [&](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
            capturedOps = ops;
            capturedMeta = meta;
        });

        {
            UndoLog::Transaction t(a.d2UndoLog());
            const auto blocks = a.iterateBlocks();
            a.d2ApplyBufferEdit(blocks.front(), 2, 0, QByteArrayLiteral("!"), t);
        }
        QVERIFY(!capturedOps.isEmpty());

        b.applyRemoteOps(capturedOps, capturedMeta);
        const QByteArray firstApply = b.blockText(b.iterateBlocks().front());

        // Re-deliver.
        b.applyRemoteOps(capturedOps, capturedMeta);
        const QByteArray secondApply = b.blockText(b.iterateBlocks().front());

        QCOMPARE(firstApply, secondApply);
    }
};
QTEST_MAIN(TstD5Idempotency)
#include "tst_d5_idempotency.moc"
```

- [ ] **Step 2: Add to test CMakeLists.**

- [ ] **Step 3: Run — expect PASS.** If this fails, collabtext's CRDT op-id deduplication is missing or broken; check `Buffer::apply_remote_op` semantics.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-core/tests/d5/tst_d5_idempotency.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: idempotency test for re-delivered bundles (D5 phase 4)"
```

### Task 4.6: Phase 4 closeout

- [ ] **Step 1: Run full suite, confirm green.**
- [ ] **Step 2: Append d-arc-status entry; commit.**

---

## Phase 5 — Sibling-map ops

**Goal:** Sibling-map ops cross the boundary in `MarkoffOp`s with payloads encoded via `SiblingMapOpHeader`. Outbound: `CausalLwwMap::setWithNextStamp` produces an op recorded in the active transaction. Inbound: `applyRemoteOps` decodes and applies via LWW resolution.

### Task 5.1: Hook outbound emission in CausalLwwMap

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/CausalLwwMap.h` (template) or `.cpp`
- Modify: `libs/markoff-core/src/UndoLog.cpp` (extend `CommittedOp` registration)
- Test: `libs/markoff-core/tests/d5/tst_d5_sibling_map_emit.cpp`

The 5 sibling maps are all `CausalLwwMap<K, V>` instances. The hook needs to fire at `setWithNextStamp(...)` / `removeWithNextStamp(...)` time, recording an op into the current transaction.

- [ ] **Step 1: Inspect existing CausalLwwMap API**

```bash
grep -n "class CausalLwwMap\|setWithNextStamp\|removeWithNextStamp" libs/markoff-core/include/markoff/core/CausalLwwMap.h
```

- [ ] **Step 2: Write the failing test**

`libs/markoff-core/tests/d5/tst_d5_sibling_map_emit.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/Cmd.h>

using namespace Markoff;

class TstD5SiblingMapEmit : public QObject {
    Q_OBJECT
private slots:
    void changeKind_emitsKindTagMapOp() {
        MarkoffDocument doc(quint16(7));
        doc.loadFromMarkdown(QByteArrayLiteral("para\n"));
        QSignalSpy spy(&doc, &MarkoffDocument::localOpsProduced);

        const auto blocks = doc.iterateBlocks();
        Cmd::changeKind(doc, blocks.front(), BlockKind::Heading,
                        { AttrNames::Level }, { 1 });

        QCOMPARE(spy.count(), 1);
        const auto ops = spy.takeFirst().at(0).value<QList<MarkoffOp>>();
        bool sawKindOp = false;
        bool sawAttrOp = false;
        for (const MarkoffOp &op : ops) {
            if (op.target == CrdtTarget::KindTagMap) sawKindOp = true;
            if (op.target == CrdtTarget::BlockAttrsMap) sawAttrOp = true;
        }
        QVERIFY(sawKindOp);
        QVERIFY(sawAttrOp);
    }
};
QTEST_MAIN(TstD5SiblingMapEmit)
#include "tst_d5_sibling_map_emit.moc"
```

- [ ] **Step 3: Add to test CMakeLists.**

- [ ] **Step 4: Run — expect FAIL** (sibling-map ops not registered yet).

- [ ] **Step 5: Add a recorder hook to `CausalLwwMap`**

The cleanest design: the map calls a registered callback after each `setWithNextStamp` / `removeWithNextStamp`. The document registers a callback that builds a `SiblingMapOpHeader`, encodes it, and registers a `CommittedOp` in the current transaction.

In `libs/markoff-core/include/markoff/core/CausalLwwMap.h`:

```cpp
public:
    using OpRecorderFn = std::function<void(const QByteArray &keyBytes,
                                             const QByteArray &valueBytes,
                                             quint64 lamportCounter,
                                             quint16 lamportReplicaId,
                                             bool isTombstone)>;

    void setOpRecorder(OpRecorderFn fn) { m_opRecorder = std::move(fn); }

private:
    OpRecorderFn m_opRecorder;
```

In each `setWithNextStamp(...)` and `removeWithNextStamp(...)` body, after the local mutation, fire the recorder if set. Pass:
- `keyBytes` — caller-defined encoding of `K`
- `valueBytes` — caller-defined encoding of `V` (empty for tombstone)
- `lamportCounter`, `lamportReplicaId` — from the new entry's lamport
- `isTombstone` — `false` for set, `true` for remove

The `CausalLwwMap` template doesn't know how to encode `K`/`V`. The caller (e.g., `KindTagMap`) provides codecs at construction or via member methods.

**Cleaner alternative:** add codec template parameters or pass codec functions at construction. Detail design is the implementer's call; the test just asserts that the doc emits the right ops.

- [ ] **Step 6: Wire recorders in `MarkoffDocument`'s constructor**

For each of the five sibling maps, register a recorder that:
1. Encodes the op into `SiblingMapOpHeader::encode(...)`.
2. Registers a `CommittedOp` with the current transaction's `OnCommit` payload via `UndoLog::Transaction::registerOp(...)` extended to take payload bytes.

This requires extending `UndoLog::Transaction::registerOp` (or adding a parallel method) to accept a payload directly, since today's `registerOp` just takes a `(CrdtTarget, OpId)` pair.

Suggested addition to `UndoLog.h`:

```cpp
void registerOpWithPayload(CrdtTarget target,
                            quint64 blockId,
                            QByteArray payload);
```

- [ ] **Step 7: Run — expect PASS**

If the test fails because `Cmd::changeKind` doesn't actually go through the maps (e.g., it uses `applyStructural` directly), trace the path and ensure the recorder fires. Add a debug log in the recorder body if needed; remove it before commit.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff/core/CausalLwwMap.h \
        libs/markoff-core/include/markoff/core/UndoLog.h \
        libs/markoff-core/src/UndoLog.cpp \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/d5/tst_d5_sibling_map_emit.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: emit sibling-map ops on local writes (D5 phase 5)"
```

### Task 5.2: Inbound sibling-map application + LWW resolution

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` (extend `applyRemoteOps`)
- Test: `libs/markoff-core/tests/d5/tst_d5_sibling_map_lww.cpp`

- [ ] **Step 1: Write the failing test**

`libs/markoff-core/tests/d5/tst_d5_sibling_map_lww.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/Cmd.h>

using namespace Markoff;

namespace {
void route(MarkoffDocument *from, MarkoffDocument *to) {
    QObject::connect(from, &MarkoffDocument::localOpsProduced,
                     to, [to](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
        to->applyRemoteOps(std::move(ops), std::move(meta));
    });
}
}

class TstD5SiblingMapLww : public QObject {
    Q_OBJECT
private slots:
    void concurrentKindChange_higherLamportWins() {
        MarkoffDocument a(1), b(2);
        a.loadFromMarkdown(QByteArrayLiteral("p\n"));
        b.loadFromMarkdown(QByteArrayLiteral("p\n"));
        route(&a, &b);
        route(&b, &a);

        // Concurrent kind changes — higher lamport (lex-order on
        // counter, then replica id) wins.
        Cmd::changeKind(a, a.iterateBlocks().front(), BlockKind::Heading,
                        { AttrNames::Level }, { 1 });
        Cmd::changeKind(b, b.iterateBlocks().front(), BlockKind::CodeBlock,
                        {}, {});

        // Both should converge to the same kind.
        QCOMPARE(a.blockKind(a.iterateBlocks().front()),
                 b.blockKind(b.iterateBlocks().front()));
    }
};
QTEST_MAIN(TstD5SiblingMapLww)
#include "tst_d5_sibling_map_lww.moc"
```

- [ ] **Step 2: Add to test CMakeLists.**

- [ ] **Step 3: Run — expect FAIL** (sibling-map application not implemented).

- [ ] **Step 4: Implement sibling-map cases in `applyRemoteOps`**

Replace the placeholder `qDebug() << "sibling-map target not yet implemented"` with:

```cpp
case CrdtTarget::KindTagMap: {
    SiblingMapOpHeader h;
    if (!SiblingMapOpHeader::decode(op.payload, &h)) {
        qWarning() << "applyRemoteOps: KindTagMap decode failed; skipping";
        break;
    }
    applyRemoteKindTagMapOp(h);
    break;
}
case CrdtTarget::BlockAttrsMap: {
    SiblingMapOpHeader h;
    if (!SiblingMapOpHeader::decode(op.payload, &h)) {
        qWarning() << "applyRemoteOps: BlockAttrsMap decode failed; skipping";
        break;
    }
    applyRemoteBlockAttrsMapOp(h);
    break;
}
// ... same pattern for FrontmatterMap, LinkRefMap, FootnoteDefMap
```

Implement each `applyRemote*MapOp(const SiblingMapOpHeader &)`:

```cpp
void MarkoffDocument::applyRemoteKindTagMapOp(const SiblingMapOpHeader &h)
{
    // Decode key (8-byte little-endian blockId).
    if (h.key.size() != 8) {
        qWarning() << "KindTagMap key has wrong size; skipping"; return;
    }
    quint64 blockIdRaw = 0;
    QDataStream ks(h.key);
    ks.setByteOrder(QDataStream::LittleEndian);
    ks >> blockIdRaw;
    const BlockId blockId = BlockId::fromRaw(blockIdRaw);

    if (h.isTombstone) {
        d->kindTagMap.applyRemoteRemove(blockId,
                                         { h.lamportReplicaId, h.lamportCounter });
        d->kindTagMapProxy->notifyChanged();
        return;
    }
    if (h.value.isEmpty()) {
        qWarning() << "KindTagMap value empty but not tombstone; skipping"; return;
    }
    // Decode value (1-byte BlockKind, optional 1-byte level for Heading).
    const auto kind = static_cast<BlockKind>(quint8(h.value[0]));
    d->kindTagMap.applyRemoteSet(blockId, kind,
                                  { h.lamportReplicaId, h.lamportCounter });
    d->kindTagMapProxy->notifyChanged();
}
```

(The `applyRemoteSet` / `applyRemoteRemove` methods on `CausalLwwMap` may not exist yet — add them as a sibling to `setWithNextStamp` that takes an explicit lamport rather than minting a new one. They run LWW resolution: if the incoming lamport is greater than the current entry's, replace; otherwise ignore.)

Implement the remaining four similarly. The codecs are spec §3.1's table.

- [ ] **Step 5: Run — expect PASS**

If LWW resolution is wrong (e.g., lower lamport overwrites higher), fix in `CausalLwwMap`. The convergence depends on this.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/include/markoff/core/CausalLwwMap.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/d5/tst_d5_sibling_map_lww.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: applyRemoteOps for sibling-map targets + LWW (D5 phase 5)"
```

### Task 5.3: Edge cases — orphan, unseen-block, idempotency

**Files:**
- Test: `libs/markoff-core/tests/d5/tst_d5_sibling_map_edges.cpp`

- [ ] **Step 1: Write the test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/Cmd.h>

using namespace Markoff;

class TstD5SiblingMapEdges : public QObject {
    Q_OBJECT
private slots:
    void opForDeletedBlock_appliesAsOrphan() {
        // a deletes a block; b sets its kind concurrently.
        // Convergent: orphan entry lives in the map, no block to attach to.
        MarkoffDocument a(1), b(2);
        a.loadFromMarkdown(QByteArrayLiteral("first\n\nsecond\n"));
        b.loadFromMarkdown(QByteArrayLiteral("first\n\nsecond\n"));

        QList<MarkoffOp> aOps; MarkoffBundleMeta aMeta;
        QObject::connect(&a, &MarkoffDocument::localOpsProduced,
                         &a, [&](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
            aOps = std::move(ops); aMeta = std::move(meta);
        });
        QList<MarkoffOp> bOps; MarkoffBundleMeta bMeta;
        QObject::connect(&b, &MarkoffDocument::localOpsProduced,
                         &b, [&](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
            bOps = std::move(ops); bMeta = std::move(meta);
        });

        const auto aBlocks = a.iterateBlocks();
        const auto bBlocks = b.iterateBlocks();
        QVERIFY(aBlocks.size() >= 2);

        // a removes second block.
        {
            UndoLog::Transaction t(a.d2UndoLog());
            a.d2RemoveBlock(aBlocks[1], t);
        }
        // b changes second block to heading concurrently.
        Cmd::changeKind(b, bBlocks[1], BlockKind::Heading,
                        { AttrNames::Level }, { 2 });

        // Cross-deliver.
        b.applyRemoteOps(aOps, aMeta);
        a.applyRemoteOps(bOps, bMeta);

        // Both converge: the block is gone (IdList remove wins);
        // KindTagMap entry exists as orphan.
        QCOMPARE(a.iterateBlocks().size(), b.iterateBlocks().size());
        QCOMPARE(a.iterateBlocks().size(), size_t(1));
    }
};
QTEST_MAIN(TstD5SiblingMapEdges)
#include "tst_d5_sibling_map_edges.moc"
```

- [ ] **Step 2: Add to test CMakeLists.**

- [ ] **Step 3: Run — expect PASS** (or fix if the orphan path doesn't work).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-core/tests/d5/tst_d5_sibling_map_edges.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: sibling-map edge-case convergence tests (D5 phase 5)"
```

### Task 5.4: Phase 5 closeout

- [ ] **Step 1: Re-run all tier-0 convergence tests** (sibling-map ops are now in the wire).
- [ ] **Step 2: Append d-arc-status entry; commit.**

---

## Phase 6 — Watermark/ack gate

**Goal:** Wire `wantsAcksAtWatermark` and `notifyAcksAtWatermark` to gate compaction. D2 §7.4 prescribed the predicate; D5 implements it.

### Task 6.1: Add the signals and method

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/MarkoffDocument.h`

- [ ] **Step 1: Add signals**

```cpp
signals:
    void localWatermarkAdvanced(quint64 watermark);
    void wantsAcksAtWatermark(quint64 snapshotWatermark);
```

- [ ] **Step 2: Add method**

```cpp
public:
    void notifyAcksAtWatermark(quint64 watermark);
```

- [ ] **Step 3: Build to confirm no link errors yet** (method has no impl; OK because nothing calls it).

- [ ] **Step 4: Commit (header-only change)**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffDocument.h
git commit -m "markoff-core: declare watermark/ack signals + method (D5 phase 6)"
```

### Task 6.2: Implement the gate

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-core/src/WatermarkCoordinator.cpp` (or `.h`-only)
- Modify: `libs/markoff-core/src/MarkoffDocumentPrivate.h`
- Test: `libs/markoff-core/tests/d5/tst_d5_watermark_gate.cpp`

- [ ] **Step 1: Inspect existing WatermarkCoordinator**

```bash
grep -n "class WatermarkCoordinator\|advance\|compact\|onSave" libs/markoff-core/include/markoff/core/WatermarkCoordinator.h libs/markoff-core/src/WatermarkCoordinator.cpp 2>/dev/null
```

- [ ] **Step 2: Write the failing test**

`libs/markoff-core/tests/d5/tst_d5_watermark_gate.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

class TstD5WatermarkGate : public QObject {
    Q_OBJECT
private slots:
    void saveInCollabMode_emitsWantsAcks() {
        MarkoffDocument doc(quint16(7));
        doc.loadFromMarkdown(QByteArrayLiteral("x\n"));

        QSignalSpy spy(&doc, &MarkoffDocument::wantsAcksAtWatermark);

        // Edit, save.
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            const auto b = doc.iterateBlocks();
            doc.d2ApplyBufferEdit(b.front(), 1, 0, QByteArrayLiteral("!"), t);
        }
        // Trigger save (the existing API; substitute the right method).
        // Simulate save-success path:
        doc.simulateSaveSucceeded();

        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        const quint64 watermark = args.at(0).toULongLong();
        QVERIFY(watermark > 0);
    }
    void notifyAcks_belowWatermark_doesNotCompact() {
        MarkoffDocument doc(quint16(7));
        doc.loadFromMarkdown(QByteArrayLiteral("x\n"));
        // Drive an emission then call notifyAcks with too-low value.
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            const auto b = doc.iterateBlocks();
            doc.d2ApplyBufferEdit(b.front(), 1, 0, QByteArrayLiteral("!"), t);
        }
        doc.simulateSaveSucceeded();
        // Calling with 0 should not compact.
        QSignalSpy compactSpy(&doc, &MarkoffDocument::watermarkCompacted);
        doc.notifyAcksAtWatermark(0);
        QCOMPARE(compactSpy.count(), 0);
    }
    void notifyAcks_atOrAboveWatermark_compacts() {
        MarkoffDocument doc(quint16(7));
        doc.loadFromMarkdown(QByteArrayLiteral("x\n"));
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            const auto b = doc.iterateBlocks();
            doc.d2ApplyBufferEdit(b.front(), 1, 0, QByteArrayLiteral("!"), t);
        }
        QSignalSpy wantsSpy(&doc, &MarkoffDocument::wantsAcksAtWatermark);
        doc.simulateSaveSucceeded();
        QCOMPARE(wantsSpy.count(), 1);
        const quint64 W = wantsSpy.takeFirst().at(0).toULongLong();

        QSignalSpy compactSpy(&doc, &MarkoffDocument::watermarkCompacted);
        doc.notifyAcksAtWatermark(W);
        QCOMPARE(compactSpy.count(), 1);
    }
    void singleUserMode_uintMaxAck_compactsOnSave() {
        MarkoffDocument doc;   // single-user
        doc.loadFromMarkdown(QByteArrayLiteral("x\n"));
        doc.notifyAcksAtWatermark(std::numeric_limits<quint64>::max());

        QSignalSpy compactSpy(&doc, &MarkoffDocument::watermarkCompacted);
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            const auto b = doc.iterateBlocks();
            doc.d2ApplyBufferEdit(b.front(), 1, 0, QByteArrayLiteral("!"), t);
        }
        doc.simulateSaveSucceeded();
        QCOMPARE(compactSpy.count(), 1);
    }
};
QTEST_MAIN(TstD5WatermarkGate)
#include "tst_d5_watermark_gate.moc"
```

(`watermarkCompacted` is a small extra signal we add for this test's observability. The implementer adds it to `MarkoffDocument.h` alongside the others.)

- [ ] **Step 3: Add `simulateSaveSucceeded` (test-only) and `watermarkCompacted` signal**

In `MarkoffDocument.h`:

```cpp
signals:
    /// Test-observability: emitted after WatermarkCoordinator compacts.
    void watermarkCompacted(quint64 watermark);

#ifdef MARKOFF_TESTING
public:
    /// Test hook: pretend save succeeded. Production code calls this
    /// from the actual save path.
    void simulateSaveSucceeded();
#endif
```

- [ ] **Step 4: Add to test CMakeLists** (and ensure tests compile with `MARKOFF_TESTING` defined):

```cmake
qt_add_executable(tst_d5_watermark_gate tst_d5_watermark_gate.cpp)
target_link_libraries(tst_d5_watermark_gate PRIVATE Qt6::Test markoff_core)
target_compile_definitions(tst_d5_watermark_gate PRIVATE MARKOFF_TESTING=1)
add_test(NAME tst_d5_watermark_gate COMMAND tst_d5_watermark_gate)
```

- [ ] **Step 5: Run — expect FAIL.**

- [ ] **Step 6: Implement the gate**

In `MarkoffDocumentPrivate.h`:

```cpp
quint64 currentSnapshotWatermark = 0;     // last W from a save
quint64 ackedWatermark            = 0;    // monotonic; consumer-supplied
```

In `MarkoffDocument.cpp`, add `simulateSaveSucceeded` (real save path code already exists; call into `WatermarkCoordinator`):

```cpp
void MarkoffDocument::simulateSaveSucceeded()
{
    // Advance snapshot watermark.
    const quint64 W = computeCurrentLamportCeiling();
    d->currentSnapshotWatermark = W;
    Q_EMIT localWatermarkAdvanced(W);

    // Decide gate.
    if (d->ackedWatermark >= W) {
        compactToWatermark(W);
    } else if (d->collabConfigured) {
        Q_EMIT wantsAcksAtWatermark(W);
    } else {
        // Single-user without explicit notifyAcks: no compaction, no event.
    }
}

void MarkoffDocument::notifyAcksAtWatermark(quint64 watermark)
{
    if (watermark <= d->ackedWatermark) return;   // monotonic
    d->ackedWatermark = watermark;
    if (d->currentSnapshotWatermark > 0
        && d->ackedWatermark >= d->currentSnapshotWatermark)
    {
        compactToWatermark(d->currentSnapshotWatermark);
    }
}

void MarkoffDocument::compactToWatermark(quint64 W)
{
    // Dispatch to each CRDT's compact(W).
    d->idList.compact(W);
    for (auto &kv : d->blockBuffers) kv.second->compact(W);
    d->kindTagMap.compact(W);
    d->blockAttrsMap.compact(W);
    d->frontmatterMap.compact(W);
    d->linkRefMap.compact(W);
    d->footnoteDefMap.compact(W);
    Q_EMIT watermarkCompacted(W);
}
```

`computeCurrentLamportCeiling()` returns the maximum Lamport counter across all CRDTs. Implementation: each CRDT exposes a `lamportCeiling()` accessor; document takes the max. If the accessor doesn't exist, add it — it's a one-line read of the CRDT's internal counter.

- [ ] **Step 7: Run — expect PASS.**

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/src/MarkoffDocumentPrivate.h \
        libs/markoff-core/include/markoff/core/WatermarkCoordinator.h \
        libs/markoff-core/src/WatermarkCoordinator.cpp \
        libs/markoff-core/tests/d5/tst_d5_watermark_gate.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: watermark/ack gate (D5 phase 6)"
```

### Task 6.3: Phase 6 closeout

- [ ] **Step 1: Run full suite, confirm green.**
- [ ] **Step 2: Append d-arc-status entry; commit.**

---

## Phase 7 — Remote cursor rendering (markoff-live)

**Goal:** `setRemoteCursor`, `clearRemoteCursor`, `clearAllRemoteCursors` on the document. Cursor-survival logic for remote cursors. QML overlay renders them.

### Task 7.1: Add document methods + state

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/MarkoffDocument.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-core/src/MarkoffDocumentPrivate.h`
- Test: `libs/markoff-core/tests/d5/tst_d5_remote_cursor_state.cpp`

- [ ] **Step 1: Add `Markoff::Cursor` access** (already exists from D2; verify):

```bash
grep -n "struct Cursor\|class Cursor\|TextCaret\|BlockSelected\|BlockInternalEdit" libs/markoff-core/include/markoff/core/*.h libs/markoff-live/include/markoff/live/*.h | head
```

- [ ] **Step 2: Write the failing test**

`libs/markoff-core/tests/d5/tst_d5_remote_cursor_state.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>

using namespace Markoff;

class TstD5RemoteCursorState : public QObject {
    Q_OBJECT
private slots:
    void setRemoteCursor_storesAndEmits() {
        MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("text\n"));

        QSignalSpy spy(&doc, &MarkoffDocument::remoteCursorChanged);

        Markoff::Cursor c;       // some default; depends on existing API
        // Construct a TextCaret variant attached to first block:
        // Markoff::Cursor::makeTextCaret(blockAnchor, 0)  — adapt to API.
        // ...
        doc.setRemoteCursor(quint16(2), c, QColor("#ff0000"), QStringLiteral("Bob"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).value<quint16>(), quint16(2));
    }
    void clearRemoteCursor_emitsClear() {
        MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("text\n"));
        Markoff::Cursor c;
        doc.setRemoteCursor(quint16(3), c, QColor("#00ff00"), QStringLiteral("Carol"));

        QSignalSpy spy(&doc, &MarkoffDocument::remoteCursorCleared);
        doc.clearRemoteCursor(quint16(3));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).value<quint16>(), quint16(3));
    }
    void clearAllRemoteCursors_emitsForEach() {
        MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("text\n"));
        Markoff::Cursor c;
        doc.setRemoteCursor(2, c, QColor("#0000ff"), "X");
        doc.setRemoteCursor(3, c, QColor("#00ff00"), "Y");

        QSignalSpy spy(&doc, &MarkoffDocument::remoteCursorCleared);
        doc.clearAllRemoteCursors();
        QCOMPARE(spy.count(), 2);
    }
};
QTEST_MAIN(TstD5RemoteCursorState)
#include "tst_d5_remote_cursor_state.moc"
```

- [ ] **Step 3: Add to test CMakeLists.**

- [ ] **Step 4: Run — expect FAIL.**

- [ ] **Step 5: Add the methods + signals to `MarkoffDocument`**

In `MarkoffDocument.h`:

```cpp
public:
    void setRemoteCursor(quint16 replicaId,
                         Markoff::Cursor cursor,
                         QColor color,
                         QString label);
    void clearRemoteCursor(quint16 replicaId);
    void clearAllRemoteCursors();

signals:
    void remoteCursorChanged(quint16 replicaId,
                             Markoff::Cursor cursor,
                             QColor color,
                             QString label);
    void remoteCursorCleared(quint16 replicaId);
```

In `MarkoffDocumentPrivate.h`:

```cpp
struct RemoteCursorRecord {
    Markoff::Cursor cursor;
    QColor          color;
    QString         label;
};
QHash<quint16, RemoteCursorRecord> remoteCursors;
```

In `MarkoffDocument.cpp`:

```cpp
void MarkoffDocument::setRemoteCursor(quint16 r, Markoff::Cursor c,
                                       QColor color, QString label)
{
    d->remoteCursors[r] = { c, color, label };
    Q_EMIT remoteCursorChanged(r, std::move(c), std::move(color), std::move(label));
}

void MarkoffDocument::clearRemoteCursor(quint16 r)
{
    if (d->remoteCursors.remove(r) > 0) {
        Q_EMIT remoteCursorCleared(r);
    }
}

void MarkoffDocument::clearAllRemoteCursors()
{
    const auto keys = d->remoteCursors.keys();
    d->remoteCursors.clear();
    for (quint16 r : keys) Q_EMIT remoteCursorCleared(r);
}
```

- [ ] **Step 6: Run — expect PASS.**

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/src/MarkoffDocumentPrivate.h \
        libs/markoff-core/tests/d5/tst_d5_remote_cursor_state.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: setRemoteCursor / clearRemoteCursor (D5 phase 7)"
```

### Task 7.2: Cursor-survival propagation under local edits

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Test: `libs/markoff-core/tests/d5/tst_d5_remote_cursor_survival.cpp`

The C-spec §3.5 cursor-survival logic exists for the local cursor; D5 extends it to remote cursors. When a local edit shifts text, every remote cursor whose anchor is at or after the edit point must shift too.

- [ ] **Step 1: Locate existing survival logic**

```bash
grep -rn "cursor-survival\|survival\|onAnchorRenumbered\|anchorRenumbered" libs/markoff-core/ libs/markoff-live/ | head
```

The D2 `BlockId == IdList element id` change made many of the C-arc workarounds vestigial (per the developmental-history doc). The remaining valid survival case: local insertions/deletions inside a block shift `TextCaret` byte-positions of remote cursors in that block.

- [ ] **Step 2: Write the test**

`libs/markoff-core/tests/d5/tst_d5_remote_cursor_survival.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

class TstD5RemoteCursorSurvival : public QObject {
    Q_OBJECT
private slots:
    void localInsertBefore_remoteCursorShifts() {
        MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("0123456789\n"));
        const auto blocks = doc.iterateBlocks();

        Cursor c;
        // Construct a TextCaret at byte offset 5 in the first block.
        // (Adapt construction to actual Cursor / TextAnchor API.)
        // c = Cursor::makeTextCaret(blockAnchor(blocks.front()), /*offset*/5);
        doc.setRemoteCursor(2, c, QColor("#f00"), "Bob");

        // Local insert "XYZ" at offset 2 — should push offset 5 to 8.
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(blocks.front(), 2, 0,
                                   QByteArrayLiteral("XYZ"), t);
        }
        // Read back the remote cursor; its byte offset should now be 8.
        const Cursor &after = doc.remoteCursorOf(2);
        // QCOMPARE(after.textCaretOffset(), 8);
        // ... adapt to the actual cursor accessor.
        Q_UNUSED(after);
    }
};
QTEST_MAIN(TstD5RemoteCursorSurvival)
#include "tst_d5_remote_cursor_survival.moc"
```

(The test body has commented-out lines because the exact `Cursor` construction and accessors depend on the existing API. The implementer adapts.)

- [ ] **Step 3: Add to test CMakeLists.**

- [ ] **Step 4: Implement survival propagation**

After every `d2ApplyBufferEdit`, walk `d->remoteCursors` and translate any cursor whose anchor is in the affected block. For `TextCaret` variants, shift the byte offset by `(insertedBytes - removedBytes)` if the cursor's offset is `>= editOffset`. For `BlockSelected` variants, no translation needed (the block id is stable). For `BlockInternalEdit`, similar offset translation.

- [ ] **Step 5: Run — expect PASS.**

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/d5/tst_d5_remote_cursor_survival.cpp \
        libs/markoff-core/tests/d5/CMakeLists.txt
git commit -m "markoff-core: cursor-survival propagation for remote cursors (D5 phase 7)"
```

### Task 7.3: QML overlay rendering

**Files:**
- Create: `libs/markoff-live/qml/delegates/RemoteCursorOverlay.qml`
- Modify: `libs/markoff-live/qml/LiveView.qml`
- Modify: `libs/markoff-live/include/markoff/live/LiveListModelBinding.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`
- Test: `libs/markoff-live/tests/d5/tst_d5_remote_cursor_render.cpp`

- [ ] **Step 1: Add QML overlay model in `LiveListModelBinding`**

Expose a Q_PROPERTY `remoteCursorsModel` to QML. The model is a thin `QAbstractListModel` populated from the document's `remoteCursorChanged` / `remoteCursorCleared` signals. Roles: `replicaId`, `cursorVariant`, `blockAnchor`, `byteOffset`, `color`, `label`.

- [ ] **Step 2: Create the overlay delegate**

`libs/markoff-live/qml/delegates/RemoteCursorOverlay.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Rectangle {
    id: overlay
    property var record               // model entry
    property real caretX: 0
    property real caretY: 0
    property real caretHeight: 16
    width: 2
    height: caretHeight
    color: record ? record.color : "transparent"
    x: caretX; y: caretY

    Rectangle {
        id: labelTag
        anchors { left: parent.right; bottom: parent.top }
        color: parent.color
        radius: 2
        height: labelText.implicitHeight + 2
        width: labelText.implicitWidth + 4
        Text {
            id: labelText
            anchors.centerIn: parent
            text: record ? record.label : ""
            color: "#ffffff"
            font.pixelSize: 9
        }
    }
}
```

- [ ] **Step 3: Wire the overlay into `LiveView.qml`**

Add a `Repeater` over `remoteCursorsModel` inside the editor's content area. For each entry, position the `RemoteCursorOverlay` at the cursor's screen rectangle (use the existing `BlockHitTester` or analogous geometry helper to compute `caretX`/`caretY`/`caretHeight` from `(blockAnchor, byteOffset)`).

- [ ] **Step 4: Write the rendering test**

`libs/markoff-live/tests/d5/tst_d5_remote_cursor_render.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QQuickView>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>

using namespace Markoff;

class TstD5RemoteCursorRender : public QObject {
    Q_OBJECT
private slots:
    void setRemoteCursor_addsOverlay() {
        MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("hello\n"));
        Live::LiveListModelBinding binding;
        binding.setDocument(&doc);

        // Verify model starts empty.
        auto *model = binding.remoteCursorsModel();
        QVERIFY(model);
        QCOMPARE(model->rowCount(), 0);

        // Set a remote cursor.
        Cursor c;   // adapt construction
        doc.setRemoteCursor(2, c, QColor("#f00"), "Bob");
        QCOMPARE(model->rowCount(), 1);

        doc.clearRemoteCursor(2);
        QCOMPARE(model->rowCount(), 0);
    }
};
QTEST_MAIN(TstD5RemoteCursorRender)
#include "tst_d5_remote_cursor_render.moc"
```

- [ ] **Step 5: Wire test into `libs/markoff-live/tests/CMakeLists.txt`** (in a new `d5` subdir following the markoff-core pattern).

- [ ] **Step 6: Run — expect PASS.**

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/qml/delegates/RemoteCursorOverlay.qml \
        libs/markoff-live/qml/LiveView.qml \
        libs/markoff-live/include/markoff/live/LiveListModelBinding.h \
        libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/tests/d5/tst_d5_remote_cursor_render.cpp \
        libs/markoff-live/tests/d5/CMakeLists.txt \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: render remote cursors in QML overlay (D5 phase 7)"
```

### Task 7.4: Phase 7 closeout

- [ ] **Step 1: Run full suite, confirm green.**
- [ ] **Step 2: Append d-arc-status entry; commit.**

---

## Phase 8 — Reference test harness

**Goal:** `apps/markoff-collab-testapp/` — two-window in-process testapp, in-memory `ITransport` mock, convergence tests routed through the mock.

### Task 8.1: Create `apps/` and the testapp scaffold

**Files:**
- Create: `apps/CMakeLists.txt`
- Create: `apps/markoff-collab-testapp/CMakeLists.txt`
- Create: `apps/markoff-collab-testapp/main.cpp`
- Modify: root `CMakeLists.txt`

- [ ] **Step 1: Create root `apps/CMakeLists.txt`**

```cmake
add_subdirectory(markoff-collab-testapp)
```

- [ ] **Step 2: Create `apps/markoff-collab-testapp/CMakeLists.txt`**

```cmake
qt_add_executable(markoff-collab-testapp
    main.cpp
    InMemoryTransport.h
    InMemoryTransport.cpp
    CollabConsumer.h
    CollabConsumer.cpp
    MainWindow.h
    MainWindow.cpp
)
target_link_libraries(markoff-collab-testapp PRIVATE
    Qt6::Core Qt6::Widgets Qt6::Quick Qt6::QuickWidgets
    markoff_core
    markoff_live
)
add_subdirectory(tests)
```

- [ ] **Step 3: Create `apps/markoff-collab-testapp/main.cpp`** (minimal stub that opens an empty window — full UI in later tasks):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
```

- [ ] **Step 4: Create stubs for the other files** (just enough to compile):

`apps/markoff-collab-testapp/MainWindow.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMainWindow>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
};
```

`apps/markoff-collab-testapp/MainWindow.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    resize(1200, 600);
    setWindowTitle("Markoff D5 collab testapp");
}
```

`apps/markoff-collab-testapp/InMemoryTransport.h` / `.cpp` and `CollabConsumer.h` / `.cpp`: empty stubs with the right include guards.

- [ ] **Step 5: Add `apps/` to root CMake**

In `CMakeLists.txt` (root), after `add_subdirectory(libs/markoff-source)`:

```cmake
add_subdirectory(apps)
```

- [ ] **Step 6: Create `apps/markoff-collab-testapp/tests/CMakeLists.txt`** (empty for now):

```cmake
# Tests added in Task 8.4.
```

- [ ] **Step 7: Build and run smoke**

```bash
cmake --build build-dev -j 8 --target markoff-collab-testapp
./build-dev/bin/markoff-collab-testapp &
# (kill the window manually)
kill %1
```

Expected: empty window opens, no crash.

- [ ] **Step 8: Commit**

```bash
git add apps/ CMakeLists.txt
git commit -m "markoff-testapp: scaffold apps/markoff-collab-testapp (D5 phase 8)"
```

### Task 8.2: `InMemoryTransport` mock

**Files:**
- Modify: `apps/markoff-collab-testapp/InMemoryTransport.h`
- Modify: `apps/markoff-collab-testapp/InMemoryTransport.cpp`
- Test: `apps/markoff-collab-testapp/tests/tst_d5_in_memory_transport.cpp`

- [ ] **Step 1: Write the test**

`apps/markoff-collab-testapp/tests/tst_d5_in_memory_transport.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "InMemoryTransport.h"

class TstD5InMemoryTransport : public QObject {
    Q_OBJECT
private slots:
    void pushReceive_singlePeer() {
        InMemoryTransport a("A"), b("B");
        a.connectPeer(&b);
        b.connectPeer(&a);

        QByteArray received;
        QString stream;
        b.setOnInbound([&](QString s, QByteArray blob, quint16 producer) {
            stream = s; received = blob;
        });

        a.push("test", QByteArray("hello"));
        QCOMPARE(received, QByteArray("hello"));
        QCOMPARE(stream, QString("test"));
    }
    void ackTracking_advancesAsBothPeersConfirm() {
        InMemoryTransport a("A"), b("B");
        a.connectPeer(&b);
        b.connectPeer(&a);

        // a publishes a watermark; b confirms it.
        a.publishLocalWatermark(100);
        b.observePeerWatermark("A", 100);
        QCOMPARE(a.lowestPeerAckedLamport(), quint64(100));
    }
};
QTEST_MAIN(TstD5InMemoryTransport)
#include "tst_d5_in_memory_transport.moc"
```

- [ ] **Step 2: Append to `apps/markoff-collab-testapp/tests/CMakeLists.txt`**

```cmake
qt_add_executable(tst_d5_in_memory_transport tst_d5_in_memory_transport.cpp
    ../InMemoryTransport.cpp)
target_link_libraries(tst_d5_in_memory_transport PRIVATE
    Qt6::Test Qt6::Core markoff_core)
target_include_directories(tst_d5_in_memory_transport PRIVATE ..)
add_test(NAME tst_d5_in_memory_transport COMMAND tst_d5_in_memory_transport)
```

- [ ] **Step 3: Run — expect FAIL.**

- [ ] **Step 4: Implement `InMemoryTransport`**

`apps/markoff-collab-testapp/InMemoryTransport.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

class InMemoryTransport : public QObject {
    Q_OBJECT
public:
    explicit InMemoryTransport(QString replicaName, QObject *parent = nullptr);

    using OnInboundFn = std::function<void(QString, QByteArray, quint16)>;
    using OnAckFn     = std::function<void(quint64)>;

    void push(QString streamName, QByteArray blob);
    void setOnInbound(OnInboundFn fn);
    void onAckUpdate(OnAckFn fn);

    void connectPeer(InMemoryTransport *peer);
    void disconnectPeer(InMemoryTransport *peer);

    void publishLocalWatermark(quint64 W);
    void observePeerWatermark(QString peerName, quint64 W);

    quint64 lowestPeerAckedLamport() const;

    QString replicaName() const { return m_name; }

private:
    void deliverFromPeer(QString stream, QByteArray blob, quint16 producer);

    QString m_name;
    QList<InMemoryTransport*> m_peers;
    OnInboundFn m_onInbound;
    OnAckFn     m_onAck;
    quint64     m_localWatermark = 0;
    QHash<QString, quint64> m_peerWatermarks;
};
```

`apps/markoff-collab-testapp/InMemoryTransport.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "InMemoryTransport.h"

#include <QDebug>

#include <algorithm>
#include <limits>

InMemoryTransport::InMemoryTransport(QString replicaName, QObject *parent)
    : QObject(parent), m_name(std::move(replicaName)) {}

void InMemoryTransport::push(QString streamName, QByteArray blob)
{
    for (auto *peer : m_peers) {
        if (peer) peer->deliverFromPeer(streamName, blob, /*producer=*/0);
    }
}

void InMemoryTransport::setOnInbound(OnInboundFn fn) { m_onInbound = std::move(fn); }
void InMemoryTransport::onAckUpdate(OnAckFn fn)      { m_onAck = std::move(fn); }

void InMemoryTransport::connectPeer(InMemoryTransport *peer)
{
    if (peer && !m_peers.contains(peer)) m_peers.append(peer);
}
void InMemoryTransport::disconnectPeer(InMemoryTransport *peer)
{
    m_peers.removeAll(peer);
    m_peerWatermarks.remove(peer ? peer->replicaName() : QString());
}

void InMemoryTransport::publishLocalWatermark(quint64 W)
{
    m_localWatermark = W;
    for (auto *peer : m_peers) {
        if (peer) peer->observePeerWatermark(m_name, W);
    }
}

void InMemoryTransport::observePeerWatermark(QString peerName, quint64 W)
{
    m_peerWatermarks[peerName] = W;
    if (m_onAck) m_onAck(lowestPeerAckedLamport());
}

quint64 InMemoryTransport::lowestPeerAckedLamport() const
{
    if (m_peerWatermarks.isEmpty()) return 0;
    quint64 lo = std::numeric_limits<quint64>::max();
    for (auto v : m_peerWatermarks) lo = std::min(lo, v);
    return lo;
}

void InMemoryTransport::deliverFromPeer(QString stream, QByteArray blob, quint16 producer)
{
    if (m_onInbound) m_onInbound(std::move(stream), std::move(blob), producer);
}
```

- [ ] **Step 5: Run — expect PASS.**

- [ ] **Step 6: Commit**

```bash
git add apps/markoff-collab-testapp/InMemoryTransport.h \
        apps/markoff-collab-testapp/InMemoryTransport.cpp \
        apps/markoff-collab-testapp/tests/tst_d5_in_memory_transport.cpp \
        apps/markoff-collab-testapp/tests/CMakeLists.txt
git commit -m "markoff-testapp: in-memory ITransport mock (D5 phase 8)"
```

### Task 8.3: `CollabConsumer` wiring

**Files:**
- Modify: `apps/markoff-collab-testapp/CollabConsumer.h`
- Modify: `apps/markoff-collab-testapp/CollabConsumer.cpp`

- [ ] **Step 1: Implement** (mirror the spec §4.1 sketch verbatim, adapting to the `InMemoryTransport` shape).

`apps/markoff-collab-testapp/CollabConsumer.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

namespace Markoff { class MarkoffDocument; }
class InMemoryTransport;

class CollabConsumer : public QObject {
    Q_OBJECT
public:
    CollabConsumer(Markoff::MarkoffDocument *doc,
                   InMemoryTransport *transport,
                   QObject *parent = nullptr);
private:
    Markoff::MarkoffDocument *m_doc;
    InMemoryTransport        *m_transport;
};
```

`apps/markoff-collab-testapp/CollabConsumer.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CollabConsumer.h"
#include "InMemoryTransport.h"
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffSerializer.h>
#include <markoff/core/MarkoffOp.h>

CollabConsumer::CollabConsumer(Markoff::MarkoffDocument *doc,
                                InMemoryTransport *transport,
                                QObject *parent)
    : QObject(parent), m_doc(doc), m_transport(transport)
{
    QObject::connect(m_doc, &Markoff::MarkoffDocument::localOpsProduced,
                     this, [this](QList<Markoff::MarkoffOp> ops,
                                   Markoff::MarkoffBundleMeta meta) {
        const QByteArray blob = Markoff::MarkoffSerializer::encode(ops, meta);
        m_transport->push("markoff-bundles", blob);
    });

    QObject::connect(m_doc, &Markoff::MarkoffDocument::wantsAcksAtWatermark,
                     this, [this](quint64 W) {
        const quint64 cur = m_transport->lowestPeerAckedLamport();
        if (cur >= W) m_doc->notifyAcksAtWatermark(cur);
    });

    m_transport->onAckUpdate([this](quint64 W) {
        m_doc->notifyAcksAtWatermark(W);
    });

    m_transport->setOnInbound([this](QString /*stream*/, QByteArray blob,
                                      quint16 /*producer*/) {
        QList<Markoff::MarkoffOp> ops;
        Markoff::MarkoffBundleMeta meta;
        if (!Markoff::MarkoffSerializer::decode(blob, &ops, &meta)) {
            qWarning() << "Markoff bundle decode failed; dropping";
            return;
        }
        m_doc->applyRemoteOps(std::move(ops), std::move(meta));
    });
}
```

- [ ] **Step 2: Build to confirm it compiles.**

- [ ] **Step 3: Commit**

```bash
git add apps/markoff-collab-testapp/CollabConsumer.h \
        apps/markoff-collab-testapp/CollabConsumer.cpp
git commit -m "markoff-testapp: CollabConsumer wiring layer (D5 phase 8)"
```

### Task 8.4: Convergence-through-router test

**Files:**
- Test: `apps/markoff-collab-testapp/tests/tst_d5_testapp_convergence.cpp`

- [ ] **Step 1: Write the test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>
#include "InMemoryTransport.h"
#include "CollabConsumer.h"

using namespace Markoff;

class TstD5TestappConvergence : public QObject {
    Q_OBJECT
private slots:
    void routedConvergence_twoReplicas() {
        MarkoffDocument a(quint16(1)), b(quint16(2));
        a.loadFromMarkdown(QByteArrayLiteral("Hello\n"));
        b.loadFromMarkdown(QByteArrayLiteral("Hello\n"));

        InMemoryTransport ta("A"), tb("B");
        ta.connectPeer(&tb);
        tb.connectPeer(&ta);

        CollabConsumer ca(&a, &ta);
        CollabConsumer cb(&b, &tb);

        // a edits.
        {
            UndoLog::Transaction t(a.d2UndoLog());
            const auto blocks = a.iterateBlocks();
            a.d2ApplyBufferEdit(blocks.front(), 5, 0,
                                 QByteArrayLiteral("!"), t);
        }

        const auto bText = b.blockText(b.iterateBlocks().front());
        QVERIFY(bText.endsWith(QByteArray("!\n")));
    }
};
QTEST_MAIN(TstD5TestappConvergence)
#include "tst_d5_testapp_convergence.moc"
```

- [ ] **Step 2: Append to `apps/markoff-collab-testapp/tests/CMakeLists.txt`**:

```cmake
qt_add_executable(tst_d5_testapp_convergence
    tst_d5_testapp_convergence.cpp
    ../InMemoryTransport.cpp
    ../CollabConsumer.cpp)
target_link_libraries(tst_d5_testapp_convergence PRIVATE
    Qt6::Test Qt6::Core markoff_core)
target_include_directories(tst_d5_testapp_convergence PRIVATE ..)
add_test(NAME tst_d5_testapp_convergence COMMAND tst_d5_testapp_convergence)
```

- [ ] **Step 3: Run — expect PASS.**

- [ ] **Step 4: Commit**

```bash
git add apps/markoff-collab-testapp/tests/tst_d5_testapp_convergence.cpp \
        apps/markoff-collab-testapp/tests/CMakeLists.txt
git commit -m "markoff-testapp: convergence-through-router test (D5 phase 8)"
```

### Task 8.5: Two-pane window UI

**Files:**
- Modify: `apps/markoff-collab-testapp/MainWindow.h`
- Modify: `apps/markoff-collab-testapp/MainWindow.cpp`

- [ ] **Step 1: Add the two `MarkoffDocument` + `Markoff::Live::Editor` panes side-by-side**

```cpp
// MainWindow.h
#pragma once
#include <QMainWindow>
#include <memory>

namespace Markoff { class MarkoffDocument; namespace Live { class Editor; } }
class InMemoryTransport;
class CollabConsumer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    std::unique_ptr<Markoff::MarkoffDocument> m_docA;
    std::unique_ptr<Markoff::MarkoffDocument> m_docB;
    std::unique_ptr<InMemoryTransport>        m_transportA;
    std::unique_ptr<InMemoryTransport>        m_transportB;
    std::unique_ptr<CollabConsumer>           m_consumerA;
    std::unique_ptr<CollabConsumer>           m_consumerB;
    Markoff::Live::Editor *m_editorA = nullptr;
    Markoff::Live::Editor *m_editorB = nullptr;
};
```

`MainWindow.cpp`: construct two docs, two transports, two consumers, two editors. `QHBoxLayout` side-by-side. Each editor has a label tag showing the replica ID. Documents start with a sample document.

- [ ] **Step 2: Build, run, manually verify**

```bash
cmake --build build-dev -j 8 --target markoff-collab-testapp
./build-dev/bin/markoff-collab-testapp
```

Manual test: type in left pane, observe text appearing on right pane. Type in right pane, observe on left.

- [ ] **Step 3: Commit**

```bash
git add apps/markoff-collab-testapp/MainWindow.h \
        apps/markoff-collab-testapp/MainWindow.cpp
git commit -m "markoff-testapp: two-pane window UI (D5 phase 8)"
```

### Task 8.6: Phase 8 closeout

- [ ] **Step 1: Run full suite, confirm green.**
- [ ] **Step 2: Append d-arc-status entry; commit.**

---

## Phase 9 — Manual dogfood + acceptance

**Goal:** Run a manual two-window dogfood pass driven by the user. This is the acceptance gate for D5.

### Task 9.1: Dogfood script

- [ ] **Step 1: Run the testapp**

```bash
./build-dev/bin/markoff-collab-testapp
```

- [ ] **Step 2: Manual checks**

Confirm each of the following works between the two panes (left = replica A, right = replica B):

- [ ] Type in A; text appears in B within ~50 ms.
- [ ] Type in B; text appears in A within ~50 ms.
- [ ] Press Enter in A to insert a new block; B's pane shows the new block at the right position.
- [ ] Backspace-merge in A; B's pane reflects the merge.
- [ ] `# ` to convert a paragraph to a heading in A; B's pane re-renders as a heading.
- [ ] `- ` to start a list item in A; B's pane shows the list marker.
- [ ] Cmd-Z (undo) in A; B's pane reverts. Cmd-Shift-Z (redo) in A; B re-applies.
- [ ] Open the testapp twice in different windows (manually edit `MainWindow.cpp` to start with different content); confirm initial state is reproducible.

- [ ] **Step 3: Document any issues found**

If a check fails, the bug lives in one of the earlier phases. Add a regression test reproducing the issue (in the appropriate phase's test directory) and fix before declaring D5 complete.

- [ ] **Step 4: Update d-arc-status to record acceptance**

```
| 2026-05-XX | <SHA> | D5 manual two-pane dogfood passed; user signed off. D5 → complete. |
```

- [ ] **Step 5: Final phase board update**

In `docs/d-arc/d-arc-status.md`, change the D5 row from `spec-approved` (or `in-progress` if it transitioned through that) to `complete` with summary.

- [ ] **Step 6: Commit the status update**

```bash
git add docs/d-arc/d-arc-status.md
git commit -m "d-arc: D5 complete (status board update)"
```

### Task 9.2: Optional collabtext-side handoff

If during phases 1–8 the collabtext negotiation opener received a response, draft a brief follow-up reply summarising what Markoff implemented vs what changed in collabtext. Otherwise skip.

---

## Joint-design coordination

The collabtext maintainers requested a joint-design pass per the negotiation response. This is a **calendar commitment, not a code task**, but is tracked in this plan because it occurs during the implementation window.

### Two open design calls collabtext flagged

From the maintainers' follow-up:

> Two design calls still open that the joint-design pass with Markoff should resolve before we start writing public headers:
> - Public type surface for `Operation`/`IdListOperation` — which fields are contract vs. evolution-reserved
> - Ack-frontier file format — extend presence or sibling `acks.json`

Markoff's positions on each are documented in
`docs/handoff/2026-05-08-collabtext-joint-design-positions.md` (created in
the response work-unit). The joint-design pass refines those positions with
the maintainers and produces the final shape both sides build to.

### Schedule

- **~6–8 weeks from 2026-05-08** (after IdList β + items 1–2 ship; before items 3–6 set).
- **~1 week of back-and-forth on headers**, no more (per response).
- **Natural slot in this plan:** between Phase 4 (`applyRemoteOps` lands; we know what Markoff actually sends) and Phase 6 (watermark/ack gate; we benefit from a settled ack-frontier format). If implementation is paused waiting for Phase 0 anyway, the design pass can happen entirely during the Phase 0 wait without affecting our pace.

### What Markoff brings to the pass

- The contract-vs-evolution-reserved field positions from `docs/handoff/2026-05-08-collabtext-joint-design-positions.md` §1.
- The ack-frontier format choice from `docs/handoff/2026-05-08-collabtext-joint-design-positions.md` §2.
- Naming feedback on the four `OpStream` methods (the response noted names are negotiable).
- Use cases from this implementation plan (testapp wire-up, multi-CRDT routing, sibling-map ack interaction).

### Outputs

- A short follow-up doc at `docs/handoff/2026-05-XX-collabtext-joint-design-outcomes.md` recording the resolved decisions and any naming changes.
- Updates to this plan's Phase 1 Task 1.3, Phase 4 Task 4.2, and Phase 8 Task 8.2 if the public-API names change from what's currently written.
- Updates to the D5 spec's §0.1 if the boundary shape evolves.

---

## 4. Self-review checklist

Before declaring this plan complete, the implementer should confirm:

- [ ] Every spec section in `docs/specs/2026-05-07-d5-collab-activation-design.md` has at least one task that implements it.
- [ ] No task references a method, type, or property name that wasn't defined in an earlier task.
- [ ] All tier-0, tier-1, tier-2, tier-3 tests from spec §6 are present in the appropriate phase.
- [ ] No "TODO", "TBD", or "implement later" remains in the plan.
- [ ] Each commit message follows the `<area>: <subject>` convention.
- [ ] D-arc status board updated after each phase's final commit.

## 5. Execution

**Phase 0 is the gate.** Implementation does not begin until collabtext's items 1–2 land in the public include path. Verify with the commands in Phase 0; if they don't return matches, wait.

Once Phase 0 is met, two execution options:

**1. Subagent-Driven (recommended)** — a fresh subagent per task, two-stage review between tasks, fast iteration. Use `superpowers:subagent-driven-development`.

**2. Inline Execution** — execute tasks in this session using `superpowers:executing-plans`, batch execution with checkpoints.

Either way, **Phase 4 is the critical-path review checkpoint.** If Tier-0 convergence tests reveal an architectural flaw, fix at Phase 4 — not at Phase 9.

The joint-design pass with collabtext (see "Joint-design coordination" above) happens in parallel with Phases 1–6 and ideally lands resolved before Phase 8.
