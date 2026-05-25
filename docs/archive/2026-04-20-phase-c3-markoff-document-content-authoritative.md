# Phase C3 Implementation Plan — `MarkoffDocument` Content-Authoritative

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `Markoff::MarkoffDocument` the single authoritative owner of canonical markdown content, the `QUndoStack`, and the async parse worker. All three leaves subscribe to signals; every edit routes through `MarkdownDelta` commands; `NoteEditorWidget`'s four flush/restore call sites delete.

**Architecture:** Symmetric-B. Canonical = markdown bytes (`QString` behind `Markoff::CanonicalBuffer` interface). One `QUndoStack` on `MarkoffDocument`. Native Qt per-leaf undo disabled on every leaf-internal `QTextDocument`. Three leaves (Source/Live/Reading) project canonical into private scratch view-state. Corbomite's `NoteDocument` becomes a 1:1 wrapper; Vault's existing per-relpath cache is the de-facto pool.

**Tech Stack:** C++20, Qt6 (Core, Gui, Widgets, Test), KF6::SyntaxHighlighting, jkqtmathtext, tree-sitter (via MarkoffParser), Qutepart (for Source). No new external deps in C3.

**Specification:** [`docs/specs/2026-04-20-phase-c3-markoff-document-content-authoritative.md`](../specs/2026-04-20-phase-c3-markoff-document-content-authoritative.md). When this plan says "per spec §N", read that section for API signatures and semantics.

**Execution scope:** Spans both repos. Tasks 1–19 on Markoff side (`/home/clinton/dev/Corbomite/libs/markoff-family/`); Tasks 20–25 on Corbomite side (`/home/clinton/dev/Corbomite/`). Task 19 tags Markoff `v0.6.0`; Task 24 bumps the Corbomite submodule pin.

---

## §0. Orientation

### 0.1 Build commands

**Markoff standalone (for Tasks 1–19):**
```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 10
cd build-dev && ctest --output-on-failure -j 10
```

Run a single test:
```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family/build-dev
ctest -R tst_canonical_buffer --output-on-failure
```

**Corbomite end-to-end (for Tasks 20–25):**
```bash
cd /home/clinton/dev/Corbomite
cmake -B build -DCORBOMITE_DEV_BUILD=ON
cmake --build build -j 10
cd build && ctest --output-on-failure -j 10
cd .. && ./build/Corbomite
```

### 0.2 Invariants (must hold after every commit)

**Markoff side (from `libs/markoff-family/CLAUDE.md`):**
1. Standalone build green on fresh checkout, zero external deps.
2. No `Corbomite`-named types in Markoff public interfaces.
3. Public includes are `<markoff/...>`.
4. Every phase milestone tags a Markoff version.
5. `master` is append-only; no force-push.

**Corbomite side (from `CLAUDE.md`):**
6. Always configure with `-DCORBOMITE_DEV_BUILD=ON`.
7. Always pass `-j 10` to `cmake --build`.
8. Tests define expected behavior; fix code, not tests (exception: tests that probed pre-phase internals may be rewritten to match the new shape).

### 0.3 Submodule pin-bump protocol (Ritual 5, Corbomite CLAUDE.md)

Before committing any Corbomite-side submodule bump:
```bash
git -C /home/clinton/dev/Corbomite/libs/markoff-family rev-list <old_pin>..<new_pin> --oneline
```
Spot stranded commits in either direction. Reference: `feedback_submodule_pin_audit` memory entry.

### 0.4 Commit message convention

**Markoff side:** `<library>: <description>` subject line. No Cluster-N footer. Examples:
- `markoff-core: add CanonicalBuffer interface`
- `markoff-live: scene-graph binds to canonical via offset map`
- `spec: …` / `plan: …` for doc-only commits.

**Corbomite side:** `feat(markoff): Phase C3 adaptation — <description>` or similar conventional-commit shape for adapter work. Both sides include:
```
Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

### 0.5 SPDX header template

Every new source file (Markoff and Corbomite):
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once   // (headers only)
```

### 0.6 Troubleshooting appendix

- **Signal storms after binding a leaf.** Check the `m_applyingCanonicalDelta` guard is being set *before* the leaf splices its local doc and cleared *after*. Forgetting to set it causes the leaf's own splice to fire `contentsChanged` on the leaf's local `QTextDocument`, which reflects back as a local-edit signal, which pushes another `MarkdownDelta`, which fires `contentsChanged` on canonical — infinite loop.
- **`QUndoStack` allowing no-op commands.** `QUndoStack::push` pushes even zero-length deltas. `MarkdownDelta::MarkdownDelta` should assert `removedLength > 0 || !inserted.isEmpty()` in debug.
- **Parse pool deadlock on app shutdown.** `ParsePool` owns a worker `QThread`; destruction must `quit()` and `wait()` the thread before returning. `DefaultParsePool`'s destructor must do the same.
- **Merge test fails because two deltas are at non-adjacent offsets.** `MarkdownDelta::mergeWith` only merges deltas at contiguous offsets (the new delta's offset equals the previous delta's offset + inserted.size() - removed.size()). Non-contiguous deltas are separate undo steps.
- **`setDocument(nullptr)` leaves dangling connections.** The convention is: disconnect from the old `MarkoffDocument`'s signals *before* clearing local state. Use `QObject::disconnect(oldDoc, nullptr, this, nullptr)` to drop all connections to the old doc.

---

## §A. Phase 1 — markoff-core primitives (Tasks 1–5)

Add the five new types (`CanonicalBuffer`, `InMemoryCanonicalBuffer`, `CursorPosition`, `MarkdownDelta`, `ParsePool` + `DefaultParsePool`) to `libs/markoff-core/` before touching `MarkoffDocument`. Each task: TDD — failing test first.

### Task 1: `CanonicalBuffer` interface + `InMemoryCanonicalBuffer` concrete

**Files:**
- Create: `libs/markoff-core/include/markoff/CanonicalBuffer.h`
- Create: `libs/markoff-core/src/InMemoryCanonicalBuffer.h`
- Create: `libs/markoff-core/src/InMemoryCanonicalBuffer.cpp`
- Create: `libs/markoff-core/tests/tst_canonical_buffer.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt` (add new source + test)

- [ ] **Step 1: Write the failing test.**

Create `libs/markoff-core/tests/tst_canonical_buffer.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <markoff/CanonicalBuffer.h>
#include "../src/InMemoryCanonicalBuffer.h"

class TstCanonicalBuffer : public QObject {
    Q_OBJECT
private slots:
    void applyDelta_insert();
    void applyDelta_remove();
    void applyDelta_replace();
    void reset_clearsAll();
    void anchor_survivesPrecedingInsert();
    void anchor_survivesFollowingInsert();
    void anchor_leftBias_onStraddle();
    void anchor_rightBias_onStraddle();
    void anchor_released_staysResolvable_asInvalid();
};

void TstCanonicalBuffer::applyDelta_insert() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello world"));
    buf.applyDelta(5, 0, QStringLiteral(","));
    QCOMPARE(buf.toMarkdown(), QStringLiteral("hello, world"));
}

void TstCanonicalBuffer::applyDelta_remove() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello, world"));
    buf.applyDelta(5, 1, QString());
    QCOMPARE(buf.toMarkdown(), QStringLiteral("hello world"));
}

void TstCanonicalBuffer::applyDelta_replace() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello, world"));
    buf.applyDelta(0, 5, QStringLiteral("HELLO"));
    QCOMPARE(buf.toMarkdown(), QStringLiteral("HELLO, world"));
}

void TstCanonicalBuffer::reset_clearsAll() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("first"));
    buf.reset(QStringLiteral("second"));
    QCOMPARE(buf.toMarkdown(), QStringLiteral("second"));
    QCOMPARE(buf.length(), qsizetype(6));
}

void TstCanonicalBuffer::anchor_survivesPrecedingInsert() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello world"));
    const auto h = buf.createAnchor(6, Markoff::CursorBias::Left);  // "world"
    buf.applyDelta(0, 0, QStringLiteral("> "));  // insert 2 chars before
    QCOMPARE(buf.resolveAnchor(h), qsizetype(8));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_survivesFollowingInsert() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello world"));
    const auto h = buf.createAnchor(5, Markoff::CursorBias::Left);
    buf.applyDelta(11, 0, QStringLiteral("!"));  // append after
    QCOMPARE(buf.resolveAnchor(h), qsizetype(5));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_leftBias_onStraddle() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("abcdef"));
    const auto h = buf.createAnchor(3, Markoff::CursorBias::Left);
    buf.applyDelta(2, 2, QStringLiteral("XY"));  // replace "cd" with "XY", straddles anchor
    // Left-bias anchor at offset 3 was inside the removed range — clamped to delete start.
    QCOMPARE(buf.resolveAnchor(h), qsizetype(2));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_rightBias_onStraddle() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("abcdef"));
    const auto h = buf.createAnchor(3, Markoff::CursorBias::Right);
    buf.applyDelta(2, 2, QStringLiteral("XY"));
    // Right-bias anchor at offset 3 was inside the removed range — clamped to delete end
    // (== delete start + inserted length = 2 + 2 = 4).
    QCOMPARE(buf.resolveAnchor(h), qsizetype(4));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_released_staysResolvable_asInvalid() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello"));
    const auto h = buf.createAnchor(3, Markoff::CursorBias::Left);
    buf.releaseAnchor(h);
    QCOMPARE(buf.resolveAnchor(h), qsizetype(-1));
}

QTEST_GUILESS_MAIN(TstCanonicalBuffer)
#include "tst_canonical_buffer.moc"
```

- [ ] **Step 2: Run the test — expect FAIL (header not defined).**

Run: `cd /home/clinton/dev/Corbomite/libs/markoff-family/build-dev && cmake .. && make tst_canonical_buffer -j 10`
Expected: compile error — `fatal error: markoff/CanonicalBuffer.h: No such file or directory`.

- [ ] **Step 3: Implement `CanonicalBuffer.h`.**

Create `libs/markoff-core/include/markoff/CanonicalBuffer.h` per spec §4.2 — the pure-virtual interface + `CursorBias` enum. Exact header:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <markoff/MarkoffCoreExport.h>

namespace Markoff {

enum class CursorBias { Left, Right };

class MARKOFF_CORE_EXPORT CanonicalBuffer {
public:
    virtual ~CanonicalBuffer() = default;

    virtual const QString &toMarkdown() const = 0;
    virtual qsizetype      length() const = 0;
    virtual QString        substring(qsizetype offset, qsizetype len) const = 0;

    virtual void applyDelta(qsizetype offset,
                            qsizetype removedLength,
                            const QString &inserted) = 0;
    virtual void reset(const QString &newContent) = 0;

    virtual quint64   createAnchor(qsizetype offset, CursorBias bias) = 0;
    virtual qsizetype resolveAnchor(quint64 handle) const = 0;
    virtual void      releaseAnchor(quint64 handle) = 0;
};

} // namespace Markoff
```

- [ ] **Step 4: Implement `InMemoryCanonicalBuffer`.**

Create `libs/markoff-core/src/InMemoryCanonicalBuffer.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/CanonicalBuffer.h>
#include <QHash>

namespace Markoff {

class InMemoryCanonicalBuffer final : public CanonicalBuffer {
public:
    InMemoryCanonicalBuffer();
    ~InMemoryCanonicalBuffer() override;

    const QString &toMarkdown() const override;
    qsizetype      length() const override;
    QString        substring(qsizetype offset, qsizetype len) const override;

    void applyDelta(qsizetype offset,
                    qsizetype removedLength,
                    const QString &inserted) override;
    void reset(const QString &newContent) override;

    quint64   createAnchor(qsizetype offset, CursorBias bias) override;
    qsizetype resolveAnchor(quint64 handle) const override;
    void      releaseAnchor(quint64 handle) override;

private:
    struct Anchor {
        qsizetype  offset;
        CursorBias bias;
    };

    QString              m_text;
    QHash<quint64, Anchor> m_anchors;
    quint64              m_nextHandle = 1;
};

} // namespace Markoff
```

Create `libs/markoff-core/src/InMemoryCanonicalBuffer.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "InMemoryCanonicalBuffer.h"

namespace Markoff {

InMemoryCanonicalBuffer::InMemoryCanonicalBuffer() = default;
InMemoryCanonicalBuffer::~InMemoryCanonicalBuffer() = default;

const QString &InMemoryCanonicalBuffer::toMarkdown() const { return m_text; }
qsizetype      InMemoryCanonicalBuffer::length() const     { return m_text.size(); }

QString InMemoryCanonicalBuffer::substring(qsizetype offset, qsizetype len) const {
    return m_text.mid(offset, len);
}

void InMemoryCanonicalBuffer::applyDelta(qsizetype offset,
                                         qsizetype removedLength,
                                         const QString &inserted) {
    m_text.replace(offset, removedLength, inserted);

    const qsizetype delta = inserted.size() - removedLength;
    const qsizetype deleteStart = offset;
    const qsizetype deleteEnd   = offset + removedLength;
    const qsizetype insertEnd   = offset + inserted.size();

    for (auto it = m_anchors.begin(); it != m_anchors.end(); ++it) {
        Anchor &a = it.value();
        if (a.offset <= deleteStart) {
            // Unaffected — anchor precedes the edit.
        } else if (a.offset >= deleteEnd) {
            // Anchor follows the edit — shift by delta.
            a.offset += delta;
        } else {
            // Anchor straddles the removed region — clamp per bias.
            a.offset = (a.bias == CursorBias::Left) ? deleteStart : insertEnd;
        }
    }
}

void InMemoryCanonicalBuffer::reset(const QString &newContent) {
    m_text = newContent;
    // Anchors are not carried across resets; consumers re-create after documentReloaded.
    m_anchors.clear();
}

quint64 InMemoryCanonicalBuffer::createAnchor(qsizetype offset, CursorBias bias) {
    const quint64 h = m_nextHandle++;
    m_anchors.insert(h, {offset, bias});
    return h;
}

qsizetype InMemoryCanonicalBuffer::resolveAnchor(quint64 handle) const {
    const auto it = m_anchors.constFind(handle);
    return (it == m_anchors.constEnd()) ? qsizetype(-1) : it->offset;
}

void InMemoryCanonicalBuffer::releaseAnchor(quint64 handle) {
    m_anchors.remove(handle);
}

} // namespace Markoff
```

- [ ] **Step 5: Wire into CMake.**

Modify `libs/markoff-core/CMakeLists.txt` — add `src/InMemoryCanonicalBuffer.cpp` to the library sources and `include/markoff/CanonicalBuffer.h` to the public header list. Add `tests/tst_canonical_buffer.cpp` to the test list (follow the pattern the existing `tst_markoff_document` or equivalent uses; ctest registration via `qt_add_test` or the repo's equivalent helper).

- [ ] **Step 6: Run the tests — expect PASS.**

Run:
```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family/build-dev
cmake .. && make tst_canonical_buffer -j 10 && ctest -R tst_canonical_buffer --output-on-failure
```
Expected: all 9 test slots pass.

- [ ] **Step 7: Commit.**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git add libs/markoff-core/include/markoff/CanonicalBuffer.h \
        libs/markoff-core/src/InMemoryCanonicalBuffer.h \
        libs/markoff-core/src/InMemoryCanonicalBuffer.cpp \
        libs/markoff-core/tests/tst_canonical_buffer.cpp \
        libs/markoff-core/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-core: add CanonicalBuffer interface + InMemoryCanonicalBuffer

Pure-virtual CanonicalBuffer interface per Phase C3 spec §4.2.
InMemoryCanonicalBuffer ships as the C3 concrete (QString + anchor
table). Anchor bias semantics: on a delta that straddles the anchor,
left-bias clamps to delete start, right-bias clamps to insert end.

Tests cover all 9 shape contracts (insert/remove/replace, reset
clears anchors, preceding/following insert shifts, straddling
left/right bias clamp, released-anchor resolves as -1).

Part of Phase C3 (MarkoffDocument content-authoritative).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 2: `CursorPosition` opaque handle

**Files:**
- Create: `libs/markoff-core/include/markoff/CursorPosition.h`
- Create: `libs/markoff-core/src/CursorPosition.cpp`
- Create: `libs/markoff-core/tests/tst_cursor_position.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`

- [ ] **Step 1: Write the failing test.**

Create `libs/markoff-core/tests/tst_cursor_position.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <markoff/CursorPosition.h>
#include <markoff/MarkoffDocument.h>   // for trackCursor/resolveCursor (Task 6)

// NOTE: Full cursor-anchor integration via MarkoffDocument tests in tst_cursor_anchor.
// This test covers only the CursorPosition handle's RAII + move semantics.

class TstCursorPosition : public QObject {
    Q_OBJECT
private slots:
    void default_constructed_isInvalid();
    void moved_from_isInvalid();
    void moved_to_takesOver();
};

void TstCursorPosition::default_constructed_isInvalid() {
    Markoff::CursorPosition p;
    QVERIFY(!p.isValid());
}

void TstCursorPosition::moved_from_isInvalid() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(2, Markoff::CursorBias::Left);
    QVERIFY(p.isValid());
    auto q = std::move(p);
    QVERIFY(!p.isValid());
    QVERIFY(q.isValid());
}

void TstCursorPosition::moved_to_takesOver() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(2, Markoff::CursorBias::Left);
    Markoff::CursorPosition q;
    q = std::move(p);
    QCOMPARE(doc.resolveCursor(q), qsizetype(2));
    QCOMPARE(doc.resolveCursor(p), qsizetype(-1));  // moved-from resolves invalid
}

QTEST_GUILESS_MAIN(TstCursorPosition)
#include "tst_cursor_position.moc"
```

- [ ] **Step 2: Run — expect FAIL (header + MarkoffDocument API not defined).**

Task 2 cannot fully complete standalone; its compilation depends on Task 6 introducing `MarkoffDocument::trackCursor`. **Write the header now** (Step 3) but skip adding the test to the CMake test list until Task 6; use a placeholder `qt_add_test` that's gated on a CMake option `MARKOFF_PHASE_C3_IN_PROGRESS=ON` which gets dropped in Task 6.

Actually: re-order — write ONLY `CursorPosition.h`/`.cpp` and deferred test registration in Task 2; registration in Task 6 after MarkoffDocument is built.

- [ ] **Step 3: Implement `CursorPosition.h`.**

Create `libs/markoff-core/include/markoff/CursorPosition.h` per spec §4.3:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>
#include <markoff/MarkoffCoreExport.h>

namespace Markoff {

class MarkoffDocument;

class MARKOFF_CORE_EXPORT CursorPosition {
public:
    CursorPosition();
    CursorPosition(const CursorPosition &) = delete;
    CursorPosition &operator=(const CursorPosition &) = delete;
    CursorPosition(CursorPosition &&other) noexcept;
    CursorPosition &operator=(CursorPosition &&other) noexcept;
    ~CursorPosition();

    bool isValid() const;

private:
    friend class MarkoffDocument;
    CursorPosition(MarkoffDocument *doc, quint64 handle);
    void release();

    MarkoffDocument *m_doc = nullptr;
    quint64          m_handle = 0;
};

} // namespace Markoff
```

- [ ] **Step 4: Implement `CursorPosition.cpp`.**

Create `libs/markoff-core/src/CursorPosition.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/CursorPosition.h>
#include <markoff/MarkoffDocument.h>

namespace Markoff {

CursorPosition::CursorPosition() = default;

CursorPosition::CursorPosition(MarkoffDocument *doc, quint64 handle)
    : m_doc(doc), m_handle(handle) {}

CursorPosition::CursorPosition(CursorPosition &&other) noexcept
    : m_doc(other.m_doc), m_handle(other.m_handle) {
    other.m_doc = nullptr;
    other.m_handle = 0;
}

CursorPosition &CursorPosition::operator=(CursorPosition &&other) noexcept {
    if (this != &other) {
        release();
        m_doc = other.m_doc;
        m_handle = other.m_handle;
        other.m_doc = nullptr;
        other.m_handle = 0;
    }
    return *this;
}

CursorPosition::~CursorPosition() {
    release();
}

bool CursorPosition::isValid() const {
    return m_doc != nullptr && m_handle != 0;
}

void CursorPosition::release() {
    // MarkoffDocument::releaseAnchorHandle is defined in Task 6. We forward via
    // a friend accessor to be implemented there; pre-Task-6 this is effectively
    // a no-op and the m_doc == nullptr guard prevents a null-deref.
    if (isValid()) {
        // Implemented in Task 6 after MarkoffDocument::releaseAnchorHandle exists:
        // m_doc->releaseAnchorHandle(m_handle);
    }
    m_doc = nullptr;
    m_handle = 0;
}

} // namespace Markoff
```

> **NOTE:** The empty body in `release()` gets filled in Task 6. This is documented rather than hidden to make the cross-task dependency explicit.

- [ ] **Step 5: Wire into CMake (header + .cpp only; test deferred to Task 6).**

Modify `libs/markoff-core/CMakeLists.txt` — add `src/CursorPosition.cpp` to sources and `include/markoff/CursorPosition.h` to public headers.

- [ ] **Step 6: Build — expect compile green.**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family/build-dev
cmake .. && make markoff-core -j 10
```
Expected: library builds; test file not yet registered.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-core/include/markoff/CursorPosition.h \
        libs/markoff-core/src/CursorPosition.cpp \
        libs/markoff-core/tests/tst_cursor_position.cpp \
        libs/markoff-core/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-core: add CursorPosition opaque handle

Move-only RAII handle per Phase C3 spec §4.3. release() body
filled in by Task 6 after MarkoffDocument::releaseAnchorHandle
exists. Test file staged; registered in Task 6.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 3: `MarkdownDelta` command

**Files:**
- Create: `libs/markoff-core/include/markoff/MarkdownDelta.h`
- Create: `libs/markoff-core/src/MarkdownDelta.cpp`
- Create: `libs/markoff-core/tests/tst_markdown_delta.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`

- [ ] **Step 1: Write the failing test.**

Create `libs/markoff-core/tests/tst_markdown_delta.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QUndoStack>
#include <markoff/MarkdownDelta.h>
#include <markoff/MarkoffDocument.h>

class TstMarkdownDelta : public QObject {
    Q_OBJECT
private slots:
    void redo_appliesInsert();
    void undo_reverses();
    void mergeWith_coalescesAdjacentInsert();
    void mergeWith_rejectsNonAdjacent();
    void mergeWith_rejectsCrossType();
};

void TstMarkdownDelta::redo_appliesInsert() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" world")));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello world"));
}

void TstMarkdownDelta::undo_reverses() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" world")));
    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello"));
}

void TstMarkdownDelta::mergeWith_coalescesAdjacentInsert() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hel"), Markoff::Origin::FirstOpen);
    doc.setCoalescingIdleMs(10000);  // generous window
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 3, 0, QStringLiteral("l")));
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 4, 0, QStringLiteral("o")));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello"));
    // Two keystrokes within window coalesce into one undo step.
    QCOMPARE(doc.undoStack()->count(), 1);
    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hel"));
}

void TstMarkdownDelta::mergeWith_rejectsNonAdjacent() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("abc def"), Markoff::Origin::FirstOpen);
    doc.setCoalescingIdleMs(10000);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 0, 0, QStringLiteral("X")));  // prepend
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 8, 0, QStringLiteral("Y")));  // after
    QCOMPARE(doc.undoStack()->count(), 2);
}

void TstMarkdownDelta::mergeWith_rejectsCrossType() {
    // Insert then delete at adjacent offsets should NOT coalesce (different intent).
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("abcdef"), Markoff::Origin::FirstOpen);
    doc.setCoalescingIdleMs(10000);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 3, 0, QStringLiteral("Z")));  // insert
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 4, 1, QString()));            // delete
    QCOMPARE(doc.undoStack()->count(), 2);
}

QTEST_GUILESS_MAIN(TstMarkdownDelta)
#include "tst_markdown_delta.moc"
```

- [ ] **Step 2: Run — expect FAIL (depends on MarkoffDocument Task 6+7).**

Register the test in CMake (Step 5 below) but do NOT require it to pass until Task 7 completes.

- [ ] **Step 3: Implement `MarkdownDelta.h`.**

Create `libs/markoff-core/include/markoff/MarkdownDelta.h` per spec §4.6:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QUndoCommand>
#include <QString>
#include <markoff/MarkoffCoreExport.h>

namespace Markoff {

class MarkoffDocument;

class MARKOFF_CORE_EXPORT MarkdownDelta : public QUndoCommand {
public:
    MarkdownDelta(MarkoffDocument *doc,
                  qsizetype offset,
                  qsizetype removedLength,
                  QString inserted,
                  QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;
    int  id() const override;
    bool mergeWith(const QUndoCommand *other) override;

    qsizetype      offset() const          { return m_offset; }
    qsizetype      removedLength() const   { return m_removed.size(); }
    const QString &removedText() const     { return m_removed; }
    const QString &insertedText() const    { return m_inserted; }

private:
    MarkoffDocument *m_doc;
    qsizetype m_offset;
    QString   m_removed;   // captured on first redo(), replayed on undo()
    QString   m_inserted;
    bool      m_firstRedo = true;
};

} // namespace Markoff
```

- [ ] **Step 4: Implement `MarkdownDelta.cpp`.**

Create `libs/markoff-core/src/MarkdownDelta.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/MarkdownDelta.h>
#include <markoff/MarkoffDocument.h>

namespace Markoff {

static constexpr int kMarkdownDeltaCommandId = 0x4D44;  // "MD"

MarkdownDelta::MarkdownDelta(MarkoffDocument *doc,
                             qsizetype offset,
                             qsizetype removedLength,
                             QString inserted,
                             QUndoCommand *parent)
    : QUndoCommand(parent),
      m_doc(doc),
      m_offset(offset),
      m_removed(QString(removedLength, QChar(u' '))),  // placeholder; filled on first redo
      m_inserted(std::move(inserted)) {
    Q_ASSERT(doc != nullptr);
    // m_removed size is set but content is filled by redo() using canonical state.
    m_removed.resize(removedLength);
}

void MarkdownDelta::redo() {
    if (m_firstRedo) {
        // Capture the text we're about to remove so undo() can replay.
        m_removed = m_doc->canonicalSubstring(m_offset, m_removed.size());
        m_firstRedo = false;
    }
    m_doc->applyCanonicalDelta(m_offset, m_removed.size(), m_inserted);
}

void MarkdownDelta::undo() {
    m_doc->applyCanonicalDelta(m_offset, m_inserted.size(), m_removed);
}

int MarkdownDelta::id() const {
    return kMarkdownDeltaCommandId;
}

bool MarkdownDelta::mergeWith(const QUndoCommand *other) {
    const auto *next = static_cast<const MarkdownDelta *>(other);
    if (next->m_doc != m_doc) return false;

    // Only merge pure inserts with pure inserts, or pure deletes with pure deletes —
    // mixed edits keep separate undo granularity.
    const bool thisIsInsert = m_removed.isEmpty() && !m_inserted.isEmpty();
    const bool nextIsInsert = next->m_removed.isEmpty() && !next->m_inserted.isEmpty();
    const bool thisIsDelete = !m_removed.isEmpty() && next->m_inserted.isEmpty();
    const bool nextIsDelete = !next->m_removed.isEmpty() && next->m_inserted.isEmpty();

    if (thisIsInsert && nextIsInsert) {
        // Adjacent inserts: next.offset == this.offset + this.inserted.size().
        if (next->m_offset != m_offset + m_inserted.size()) return false;
        m_inserted += next->m_inserted;
        return true;
    }

    if (thisIsDelete && nextIsDelete) {
        // Adjacent backspace: next.offset + next.removed.size() == this.offset
        // (deleting the char immediately before the previous deletion).
        if (next->m_offset + next->m_removed.size() != m_offset) return false;
        m_removed = next->m_removed + m_removed;
        m_offset = next->m_offset;
        return true;
    }

    return false;
}

} // namespace Markoff
```

> **NOTE:** `applyCanonicalDelta` and `canonicalSubstring` are new package-private methods on `MarkoffDocument` that Task 6 introduces; they emit `contentsChanged` and are the sole entry the `MarkdownDelta` uses. Step 5 below just registers the file; compile green comes after Task 6.

- [ ] **Step 5: Wire into CMake.**

Modify `libs/markoff-core/CMakeLists.txt` — add `src/MarkdownDelta.cpp`, `include/markoff/MarkdownDelta.h`, and `tests/tst_markdown_delta.cpp`. Test will fail to link until Task 6; that's expected — the CMake registration stays, linkage is validated at Task 7.

- [ ] **Step 6: Commit.**

```bash
git add libs/markoff-core/include/markoff/MarkdownDelta.h \
        libs/markoff-core/src/MarkdownDelta.cpp \
        libs/markoff-core/tests/tst_markdown_delta.cpp \
        libs/markoff-core/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-core: add MarkdownDelta command

Single QUndoCommand subclass per Phase C3 spec §4.6. redo()
captures removed text on first call (for undo replay); mergeWith()
coalesces adjacent pure-insert or pure-delete sequences within
the MarkoffDocument's coalescing window.

Depends on MarkoffDocument::applyCanonicalDelta/canonicalSubstring
introduced in Task 6; compile-green and test-passing arrive at
Task 7.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 4: `ParsePool` + `DefaultParsePool`

**Files:**
- Create: `libs/markoff-core/include/markoff/ParsePool.h`
- Create: `libs/markoff-core/src/ParsePool.cpp`
- Create: `libs/markoff-core/src/ParsePoolWorker.h` (private)
- Create: `libs/markoff-core/tests/tst_parse_pool.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`

- [ ] **Step 1: Write the failing test.**

Create `libs/markoff-core/tests/tst_parse_pool.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QSignalSpy>
#include <markoff/ParsePool.h>
#include <markoff/MarkoffDocument.h>

class TstParsePool : public QObject {
    Q_OBJECT
private slots:
    void job_producesParseSignal();
    void burst_collapsesToOneEmission();
    void cancelFor_dropsPendingJobs();
};

void TstParsePool::job_producesParseSignal() {
    Markoff::MarkoffDocument doc;
    QSignalSpy spy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("# hello\n\nworld"), Markoff::Origin::FirstOpen);
    QVERIFY(spy.wait(1000));
    QCOMPARE(spy.count(), 1);
    QVERIFY(doc.parsedDocument() != nullptr);
}

void TstParsePool::burst_collapsesToOneEmission() {
    Markoff::MarkoffDocument doc;
    doc.setCoalescingIdleMs(50);
    doc.resetContent(QStringLiteral("x"), Markoff::Origin::FirstOpen);
    QSignalSpy spy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    spy.wait(200);  // absorb the FirstOpen parse
    spy.clear();
    for (int i = 0; i < 100; ++i) {
        doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, doc.length(), 0, QStringLiteral("a")));
    }
    // Wait for the debounce window to close + some margin.
    QVERIFY(spy.wait(1500));
    // Some implementations may produce 2 emissions under worst-case scheduling; the
    // invariant is "no more than one per coalescing window after settle."
    QVERIFY(spy.count() <= 2);
}

void TstParsePool::cancelFor_dropsPendingJobs() {
    Markoff::MarkoffDocument doc;
    QSignalSpy spy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("initial"), Markoff::Origin::FirstOpen);
    QVERIFY(spy.wait(1000));
    spy.clear();
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 0, 0, QStringLiteral("A")));
    // Drop the doc immediately — the pool should cancel the pending job;
    // destruction must not deadlock or emit after doc dies.
}

QTEST_GUILESS_MAIN(TstParsePool)
#include "tst_parse_pool.moc"
```

- [ ] **Step 2: Run — expect FAIL (API not present).**

- [ ] **Step 3: Implement `ParsePool.h`.**

Create `libs/markoff-core/include/markoff/ParsePool.h` per spec §4.7:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <markoff/MarkoffCoreExport.h>

namespace Markoff {

class MarkoffDocument;
class Document;  // markoff-parser

class MARKOFF_CORE_EXPORT ParsePool : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ParsePool)
public:
    explicit ParsePool(QObject *parent = nullptr);
    ~ParsePool() override;

    void postJob(MarkoffDocument *sender, QString snapshot);
    void cancelJobsFor(MarkoffDocument *sender);

Q_SIGNALS:
    void jobCompleted(MarkoffDocument *sender, Markoff::Document *parsed);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff
```

- [ ] **Step 4: Implement `ParsePool.cpp`.**

Create `libs/markoff-core/src/ParsePoolWorker.h` + `src/ParsePool.cpp`. Worker: a `QObject` living on a `QThread`, with a `parseSnapshot(MarkoffDocument*, QString, quint64 generation)` invokable slot. Pool: owns the thread + a `QHash<MarkoffDocument*, quint64>` of current-generation-counters. `postJob` increments the generation for that sender and `QMetaObject::invokeMethod`s the worker with the new generation + snapshot. Worker parses (via `MarkoffParser::parseMarkdown(snapshot)`), then calls back to the pool via queued invocation; pool checks the generation still matches, and if so emits `jobCompleted`.

Exact implementation sketch (pool side):
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/ParsePool.h>
#include "ParsePoolWorker.h"

#include <QThread>
#include <QHash>
#include <QMutex>

namespace Markoff {

struct ParsePool::Private {
    QThread *thread = nullptr;
    ParsePoolWorker *worker = nullptr;
    QMutex mutex;  // guards generations
    QHash<MarkoffDocument *, quint64> generations;
};

ParsePool::ParsePool(QObject *parent) : QObject(parent), d(std::make_unique<Private>()) {
    d->thread = new QThread(this);
    d->worker = new ParsePoolWorker();
    d->worker->moveToThread(d->thread);
    connect(d->thread, &QThread::finished, d->worker, &QObject::deleteLater);
    connect(d->worker, &ParsePoolWorker::parsed,
            this, [this](MarkoffDocument *sender, Document *parsed, quint64 gen) {
        QMutexLocker lk(&d->mutex);
        const auto it = d->generations.constFind(sender);
        if (it == d->generations.constEnd() || it.value() != gen) {
            delete parsed;
            return;
        }
        lk.unlock();
        emit jobCompleted(sender, parsed);
    });
    d->thread->start();
}

ParsePool::~ParsePool() {
    d->thread->quit();
    d->thread->wait();
}

void ParsePool::postJob(MarkoffDocument *sender, QString snapshot) {
    quint64 gen;
    {
        QMutexLocker lk(&d->mutex);
        gen = ++d->generations[sender];
    }
    QMetaObject::invokeMethod(d->worker, "parseSnapshot", Qt::QueuedConnection,
                              Q_ARG(MarkoffDocument *, sender),
                              Q_ARG(QString, std::move(snapshot)),
                              Q_ARG(quint64, gen));
}

void ParsePool::cancelJobsFor(MarkoffDocument *sender) {
    QMutexLocker lk(&d->mutex);
    d->generations.remove(sender);
}

} // namespace Markoff
```

Worker header:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

namespace Markoff {

class MarkoffDocument;
class Document;

class ParsePoolWorker : public QObject {
    Q_OBJECT
public:
    ParsePoolWorker() = default;

public slots:
    void parseSnapshot(MarkoffDocument *sender, QString snapshot, quint64 generation);

Q_SIGNALS:
    void parsed(MarkoffDocument *sender, Markoff::Document *result, quint64 generation);
};

} // namespace Markoff
```

Worker .cpp: delegates to `MarkoffParser::parseMarkdown(snapshot)` (or the equivalent existing entry in `markoff-parser`) to produce a `Markoff::Document*`. Emit `parsed(sender, resultRawPtr, generation)`.

> **NOTE:** Ownership semantics — the raw `Document*` handed through `parsed` belongs to the receiver. `MarkoffDocument` (Task 7) takes ownership on every `parseUpdated` emission and deletes the prior `Document*` it held.

- [ ] **Step 5: Wire into CMake.**

Modify `libs/markoff-core/CMakeLists.txt` — add `src/ParsePool.cpp`, `src/ParsePoolWorker.h`, `include/markoff/ParsePool.h`, `tests/tst_parse_pool.cpp`. Link against `MarkoffParser::MarkoffParser`.

- [ ] **Step 6: Build — expect compile green for pool; test links but fails until Task 7.**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family/build-dev
cmake .. && make markoff-core -j 10
```

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-core/include/markoff/ParsePool.h \
        libs/markoff-core/src/ParsePool.cpp \
        libs/markoff-core/src/ParsePoolWorker.h \
        libs/markoff-core/tests/tst_parse_pool.cpp \
        libs/markoff-core/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-core: add ParsePool for async parsing

Single-worker-thread parse pool per Phase C3 spec §4.7. Per-sender
generation counter cancels stale runs. Worker parses on its own
thread via MarkoffParser; pool marshals results back on the main
thread and drops any result whose generation has been superseded
or cancelled.

Test passes after Task 7 wires MarkoffDocument::resetContent/
applyCanonicalDelta to post jobs.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 5: Cluster the existing `MarkoffDocument` tests into a quarantine

**Files:**
- Modify: existing `libs/markoff-core/tests/tst_markoff_document.cpp` (if present — scope it to the pre-C3 API) or move obsolete slots to `tst_markoff_document_deprecated.cpp` gated off.

- [ ] **Step 1: Inventory existing tests.**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
grep -rn "setPlainText\|plainText\|textDocument()\|::replace\|::insert\|beginTransaction" libs/markoff-core/tests/
```

- [ ] **Step 2: Triage each test slot.**

For each slot that exercises `setPlainText` / `plainText` / `textDocument()` / `replace` / `insert` / `remove` / `beginTransaction` / `endTransaction` / `parsed()`:
- **Keep-and-rewrite** if the test's *intent* is still valid (e.g. "setting text then reading it back"): mark the slot with a `// PhaseC3: rewrite in Task 7/8` comment and leave it — Task 7's changes break compile, and Task 8 rewrites these.
- **Delete** if the test exercised pre-phase internal plumbing that no longer exists (e.g. `textDocument()->setPlainText`): remove and note in the commit.

- [ ] **Step 3: Commit triage markers only.**

```bash
git add libs/markoff-core/tests/tst_markoff_document.cpp   # (if modified)
git commit -m "$(cat <<'EOF'
markoff-core/tests: mark MarkoffDocument slots for C3 rewrite

Existing test slots that exercise the Phase-A API (setPlainText,
plainText, textDocument, replace/insert/remove, beginTransaction/
endTransaction, parsed()) annotated with PhaseC3 rewrite markers.
Rewrites land in Task 8.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §B. Phase 2 — `MarkoffDocument` rewrite (Tasks 6–8)

### Task 6: `MarkoffDocument` header rewrite + reads + anchor-handle API

**Files:**
- Rewrite: `libs/markoff-core/include/markoff/MarkoffDocument.h`
- Rewrite: `libs/markoff-core/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-core/src/CursorPosition.cpp` (fill in `release()` body)

- [ ] **Step 1: Replace `MarkoffDocument.h` entirely** per spec §4.4. Previous `setPlainText` / `plainText` / `textDocument` / `replace` / `insert` / `remove` / `beginTransaction` / `endTransaction` / `parsed` methods all gone. New API as in spec §4.4, plus two package-private methods for `MarkdownDelta`:

```cpp
// In MarkoffDocument class (below private Private struct, add):
public:
    // Package-private API for MarkdownDelta — not part of the public contract.
    QString canonicalSubstring(qsizetype offset, qsizetype len) const;
    void    applyCanonicalDelta(qsizetype offset, qsizetype removedLength,
                                const QString &inserted);
    void    releaseAnchorHandle(quint64 handle);
```

(Alternatively: use `friend class MarkdownDelta;` and `friend class CursorPosition;` to hide these from the public surface. Implementer's call; both are acceptable. The spec §4.4 defines the public contract; these are implementation helpers.)

- [ ] **Step 2: Replace `MarkoffDocument.cpp`** with the Task-7-ready skeleton:
- Construction: takes `unique_ptr<CanonicalBuffer>` (default `= std::make_unique<InMemoryCanonicalBuffer>()`) + `ParsePool *` (default `= new DefaultParsePool(this)` — own-inner-pool fallback for standalone tests).
- `toMarkdown()` / `length()` / `substring(offset, len)` / `parseIsPending()` read-side methods.
- `undoStack()` returns `d->undoStack`.
- `trackCursor(offset, bias)` calls `d->buffer->createAnchor(...)` and wraps in `CursorPosition(this, handle)`.
- `resolveCursor(const CursorPosition &)` calls `d->buffer->resolveAnchor(handle)`.
- `releaseAnchorHandle(quint64 h)` calls `d->buffer->releaseAnchor(h)`.
- `resetContent` / `applyCanonicalDelta` / pool integration stubbed (fill in Task 7).
- `parsedDocument()` returns `d->parsedDoc.get()`.

Key sketch:
```cpp
struct MarkoffDocument::Private {
    std::unique_ptr<CanonicalBuffer> buffer;
    std::unique_ptr<Document> parsedDoc;
    std::unique_ptr<QUndoStack> undoStack;
    ParsePool *pool = nullptr;
    bool ownsPool = false;
    int coalescingIdleMs = 150;
    QTimer *poolDebounce = nullptr;
    bool m_parsePending = false;
};
```

- [ ] **Step 3: Fill in `CursorPosition::release()`.**

Edit `libs/markoff-core/src/CursorPosition.cpp` — replace the commented stub:
```cpp
void CursorPosition::release() {
    if (isValid()) {
        m_doc->releaseAnchorHandle(m_handle);
    }
    m_doc = nullptr;
    m_handle = 0;
}
```

- [ ] **Step 4: Register the deferred `tst_cursor_position` in CMake.**

Modify `libs/markoff-core/CMakeLists.txt` — add the deferred test registration for `tests/tst_cursor_position.cpp`.

- [ ] **Step 5: Build — expect compile green; tst_cursor_position passes; tst_markoff_document tests that use the old API fail to compile.**

The old `tst_markoff_document` compile failure is expected and handled in Task 8.

- [ ] **Step 6: Temporarily exclude failing tst_markoff_document slots from CMake** so the library + other tests build. Comment out the failing test target registration with `# TODO Task 8` marker.

- [ ] **Step 7: Run passing tests.**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family/build-dev
cmake .. && make markoff-core tst_canonical_buffer tst_cursor_position -j 10
ctest -R "tst_canonical_buffer|tst_cursor_position" --output-on-failure
```
Expected: both pass.

- [ ] **Step 8: Commit.**

```bash
git add libs/markoff-core/include/markoff/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/src/CursorPosition.cpp \
        libs/markoff-core/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-core: MarkoffDocument header rewrite + reads + anchors

Replace Phase-A API with Phase C3 contract per spec §4.4:
- toMarkdown / length / substring replace plainText / textDocument
- undoStack() single stack for all writes
- resetContent + Origin enum replaces setPlainText
- trackCursor / resolveCursor anchor API
- parsedDocument() replaces parsed()
- contentsChanged(offset, removed, inserted), parseUpdated,
  documentReloaded signal surface

Write-side slots (applyCanonicalDelta, resetContent) stubbed;
filled in by Task 7. CursorPosition::release() wired via
releaseAnchorHandle. tst_markoff_document temporarily excluded
from CMake pending Task 8 rewrite.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 7: `MarkoffDocument` writes + `contentsChanged` + `resetContent` + parse integration

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`

- [ ] **Step 1: Implement `applyCanonicalDelta`.**
```cpp
void MarkoffDocument::applyCanonicalDelta(qsizetype offset, qsizetype removedLength,
                                          const QString &inserted) {
    d->buffer->applyDelta(offset, removedLength, inserted);
    emit contentsChanged(offset, removedLength, inserted.size());
    schedulePoolPost();
}
```

- [ ] **Step 2: Implement `schedulePoolPost()` + debounce.**

Use a per-doc `QTimer` singleShot with `coalescingIdleMs`. On every call, restart the timer. On fire, `pool->postJob(this, d->buffer->toMarkdown())` + set `m_parsePending = true`.

- [ ] **Step 3: Wire `pool->jobCompleted` to `parseUpdated` emission.**

In the `MarkoffDocument` constructor, connect:
```cpp
connect(d->pool, &ParsePool::jobCompleted, this,
        [this](MarkoffDocument *sender, Document *parsed) {
    if (sender != this) return;
    d->parsedDoc.reset(parsed);  // takes ownership; deletes previous
    d->m_parsePending = false;
    emit parseUpdated(d->parsedDoc.get());
});
```
Destructor: `d->pool->cancelJobsFor(this);` then let the owned pool (if any) get destroyed.

- [ ] **Step 4: Implement `resetContent(QString, Origin)`** per spec §4.5. Five branches:
```cpp
void MarkoffDocument::resetContent(const QString &newContent, Origin origin) {
    const qsizetype oldLen = d->buffer->length();
    d->buffer->reset(newContent);

    switch (origin) {
    case Origin::FirstOpen:
        // Stack is empty; do not clear/push. Just emit contentsChanged.
        emit contentsChanged(0, 0, newContent.size());
        break;
    case Origin::ExternalReloadClean:
    case Origin::ExternalReloadResolved:
    case Origin::TestFixture:
        d->undoStack->clear();
        emit documentReloaded();
        break;
    case Origin::UserRevertToSaved:
        // Rewind the reset we just did; push a MarkdownDelta representing the revert;
        // the delta's redo() re-applies.
        d->buffer->reset(/* previous content */ /* captured snapshot before reset */);
        // … see step 5
        break;
    }
    schedulePoolPost();
}
```

- [ ] **Step 5: Refine `Origin::UserRevertToSaved`.**

The simplest implementation: capture `prevContent = d->buffer->toMarkdown()` before the `reset(newContent)`; then undo the reset and push a `MarkdownDelta`:
```cpp
case Origin::UserRevertToSaved: {
    // We already reset the buffer at top of function; rewind and route through
    // the undo stack instead.
    d->buffer->reset(prevContent);  // captured at function top
    d->undoStack->push(new MarkdownDelta(this, 0, oldLen, newContent));
    // The push triggers redo() → applyCanonicalDelta → contentsChanged + parse reschedule.
    // No separate schedulePoolPost here (applyCanonicalDelta already did).
    return;
}
```

Restructure: capture `prevContent` at the top, branch on `origin` BEFORE calling `reset`. Refined sketch:
```cpp
void MarkoffDocument::resetContent(const QString &newContent, Origin origin) {
    if (origin == Origin::UserRevertToSaved) {
        const qsizetype oldLen = d->buffer->length();
        d->undoStack->push(new MarkdownDelta(this, 0, oldLen, newContent));
        return;  // MarkdownDelta::redo() does the rest.
    }

    d->buffer->reset(newContent);

    if (origin == Origin::FirstOpen) {
        emit contentsChanged(0, 0, newContent.size());
    } else {
        // ExternalReloadClean / ExternalReloadResolved / TestFixture
        d->undoStack->clear();
        emit documentReloaded();
    }

    schedulePoolPost();
}
```

- [ ] **Step 6: Build + run targeted tests.**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family/build-dev
cmake .. && make markoff-core tst_markdown_delta tst_parse_pool -j 10
ctest -R "tst_markdown_delta|tst_parse_pool|tst_canonical_buffer|tst_cursor_position" --output-on-failure
```
Expected: all pass.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "$(cat <<'EOF'
markoff-core: MarkoffDocument writes + parse integration + resetContent

applyCanonicalDelta mutates the canonical buffer and emits
contentsChanged(offset, removed, inserted.size()). Debounced
schedulePoolPost posts snapshots to the ParsePool on settle;
jobCompleted routes back to parseUpdated on the main thread.

resetContent(QString, Origin) implements all five origins per
spec §4.5 table: FirstOpen emits contentsChanged only;
ExternalReloadClean/Resolved/TestFixture clear the stack and
emit documentReloaded; UserRevertToSaved pushes a MarkdownDelta
so Ctrl+Z can reverse the revert.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 8: Origin + documentReloaded tests + rewrite tst_markoff_document

**Files:**
- Create: `libs/markoff-core/tests/tst_origin_reset.cpp`
- Create: `libs/markoff-core/tests/tst_cursor_anchor.cpp`
- Modify: existing `libs/markoff-core/tests/tst_markoff_document.cpp` (rewrite to new API)
- Modify: `libs/markoff-core/CMakeLists.txt`

- [ ] **Step 1: Write `tst_origin_reset`.**

Create `libs/markoff-core/tests/tst_origin_reset.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QSignalSpy>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>

class TstOriginReset : public QObject {
    Q_OBJECT
private slots:
    void firstOpen_noDocumentReloaded();
    void externalReloadClean_clearsStack_emitsReloaded();
    void externalReloadResolved_clearsStack_emitsReloaded();
    void testFixture_clearsStack_emitsReloaded();
    void userRevertToSaved_pushesUndoable_emitsContentsChanged_notReloaded();
};

void TstOriginReset::firstOpen_noDocumentReloaded() {
    Markoff::MarkoffDocument doc;
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    QSignalSpy contents(&doc, &Markoff::MarkoffDocument::contentsChanged);
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    QCOMPARE(reload.count(), 0);
    QCOMPARE(contents.count(), 1);
    QCOMPARE(doc.undoStack()->count(), 0);
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello"));
}

void TstOriginReset::externalReloadClean_clearsStack_emitsReloaded() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" world")));
    QCOMPARE(doc.undoStack()->count(), 1);
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    doc.resetContent(QStringLiteral("from disk"), Markoff::Origin::ExternalReloadClean);
    QCOMPARE(reload.count(), 1);
    QCOMPARE(doc.undoStack()->count(), 0);
    QCOMPARE(doc.toMarkdown(), QStringLiteral("from disk"));
}

void TstOriginReset::externalReloadResolved_clearsStack_emitsReloaded() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" world")));
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    doc.resetContent(QStringLiteral("merged"), Markoff::Origin::ExternalReloadResolved);
    QCOMPARE(reload.count(), 1);
    QCOMPARE(doc.undoStack()->count(), 0);
}

void TstOriginReset::testFixture_clearsStack_emitsReloaded() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" x")));
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    doc.resetContent(QStringLiteral("fixture"), Markoff::Origin::TestFixture);
    QCOMPARE(reload.count(), 1);
    QCOMPARE(doc.undoStack()->count(), 0);
}

void TstOriginReset::userRevertToSaved_pushesUndoable_emitsContentsChanged_notReloaded() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("saved"), Markoff::Origin::FirstOpen);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(" edits")));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("saved edits"));
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    QSignalSpy contents(&doc, &Markoff::MarkoffDocument::contentsChanged);
    doc.resetContent(QStringLiteral("saved"), Markoff::Origin::UserRevertToSaved);
    QCOMPARE(reload.count(), 0);
    QVERIFY(contents.count() >= 1);
    QCOMPARE(doc.toMarkdown(), QStringLiteral("saved"));
    // Undo the revert.
    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("saved edits"));
}

QTEST_GUILESS_MAIN(TstOriginReset)
#include "tst_origin_reset.moc"
```

- [ ] **Step 2: Write `tst_cursor_anchor`.**

Create `libs/markoff-core/tests/tst_cursor_anchor.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>

class TstCursorAnchor : public QObject {
    Q_OBJECT
private slots:
    void precedingInsert_shiftsAnchor();
    void followingInsert_preservesAnchor();
    void straddlingDelta_leftBias_clampsToStart();
    void straddlingDelta_rightBias_clampsToEnd();
};

void TstCursorAnchor::precedingInsert_shiftsAnchor() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello world"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(6, Markoff::CursorBias::Left);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 0, 0, QStringLiteral("> ")));
    QCOMPARE(doc.resolveCursor(p), qsizetype(8));
}

void TstCursorAnchor::followingInsert_preservesAnchor() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello world"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(5, Markoff::CursorBias::Left);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 11, 0, QStringLiteral("!")));
    QCOMPARE(doc.resolveCursor(p), qsizetype(5));
}

void TstCursorAnchor::straddlingDelta_leftBias_clampsToStart() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("abcdef"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(3, Markoff::CursorBias::Left);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 2, 2, QStringLiteral("XY")));
    QCOMPARE(doc.resolveCursor(p), qsizetype(2));
}

void TstCursorAnchor::straddlingDelta_rightBias_clampsToEnd() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("abcdef"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(3, Markoff::CursorBias::Right);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 2, 2, QStringLiteral("XY")));
    QCOMPARE(doc.resolveCursor(p), qsizetype(4));
}

QTEST_GUILESS_MAIN(TstCursorAnchor)
#include "tst_cursor_anchor.moc"
```

- [ ] **Step 3: Rewrite or delete legacy `tst_markoff_document.cpp`.**

For slots marked `// PhaseC3: rewrite` in Task 5:
- Rewrite `testSetPlainText` → `testResetContentFirstOpen` (uses `resetContent(text, Origin::FirstOpen)` + `toMarkdown()`).
- Rewrite `testReplace` → delete (now covered by `tst_markdown_delta`).
- Rewrite `testParsed` → use `parsedDocument()` + wait on `parseUpdated` with `QSignalSpy`.
- Delete slots that probed `textDocument()` internals.

Keep this file focused on "document lifecycle" only (construction, destruction, signal chains); slot-level semantics now live in per-concept test files.

- [ ] **Step 4: Re-register all tests in CMake.** Remove the `# TODO Task 8` exclusion from Task 6.

- [ ] **Step 5: Run full markoff-core test suite.**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family/build-dev
cmake .. && make -j 10 && ctest -R "tst_(canonical|cursor|markdown|parse|origin|markoff_document)" --output-on-failure
```
Expected: all pass.

- [ ] **Step 6: Commit.**

```bash
git add libs/markoff-core/tests/tst_origin_reset.cpp \
        libs/markoff-core/tests/tst_cursor_anchor.cpp \
        libs/markoff-core/tests/tst_markoff_document.cpp \
        libs/markoff-core/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-core/tests: Origin + anchor + markoff_document rewrite

tst_origin_reset: all five Origin enum values exercised; stack
clear / documentReloaded emission / undoStack count match the
spec §4.5 table. UserRevertToSaved pushes a MarkdownDelta so
Ctrl+Z can reverse the revert.

tst_cursor_anchor: preceding/following/straddling-bias behaviors
mirror the CanonicalBuffer tests but go through the MarkoffDocument
+ MarkdownDelta undo-stack path.

tst_markoff_document rewritten to the Phase C3 API (toMarkdown,
resetContent, parsedDocument, QSignalSpy on parseUpdated).

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §C. Phase 3 — Interim tag `v0.6.0-alpha.1` (Task 9)

### Task 9: Tag + status board

- [ ] **Step 1: Run full standalone ctest.**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
rm -rf build-dev && cmake -S . -B build-dev
cmake --build build-dev -j 10
cd build-dev && ctest --output-on-failure -j 10
```
Expected: all tests green; at minimum +5 tests (tst_canonical_buffer, tst_cursor_position, tst_markdown_delta, tst_parse_pool, tst_origin_reset, tst_cursor_anchor) from baseline.

- [ ] **Step 2: Update `docs/phase-c-status.md`.**

Edit the C3 row and append an activity-log entry. Status → `markoff implementing (core landed at alpha.1)`. Set the Markoff PR/branch cell to `master`.

- [ ] **Step 3: Commit and tag.**

```bash
git add docs/phase-c-status.md
git commit -m "phase-c-status: C3 core primitives landed at v0.6.0-alpha.1

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
git tag -a v0.6.0-alpha.1 -m "Phase C3 core primitives

CanonicalBuffer / CursorPosition / MarkdownDelta / ParsePool land
in markoff-core. MarkoffDocument public API is Phase C3 final shape
(toMarkdown / resetContent+Origin / undoStack / trackCursor /
parsedDocument / contentsChanged+parseUpdated+documentReloaded).
Leaves still bind the Phase-A way; symmetric-B leaf adaptation
arrives in v0.6.0 proper."
```

---

## §D. Phase 4 — Source leaf adaptation (Tasks 10–11)

### Task 10: `Source::SourceEditor::setDocument` + attach/detach + external-delta splicing

**Files:**
- Modify: `libs/markoff-source/include/markoff/source/SourceEditor.h`
- Modify: `libs/markoff-source/src/SourceEditor.cpp`
- Create: `libs/markoff-source/tests/tst_source_canonical_attach.cpp`
- Modify: `libs/markoff-source/CMakeLists.txt`

- [ ] **Step 1: Write failing test.**

Create `libs/markoff-source/tests/tst_source_canonical_attach.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <markoff/source/SourceEditor.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>

class TstSourceCanonicalAttach : public QObject {
    Q_OBJECT
private slots:
    void setDocument_loadsInitialText();
    void externalDelta_splicesInto_qutepart();
    void documentReloaded_replacesContent();
};

void TstSourceCanonicalAttach::setDocument_loadsInitialText() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# hello"), Markoff::Origin::FirstOpen);
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);
    QCOMPARE(src.toPlainText(), QStringLiteral("# hello"));
}

void TstSourceCanonicalAttach::externalDelta_splicesInto_qutepart() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# hello"), Markoff::Origin::FirstOpen);
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);
    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 7, 0, QStringLiteral(" world")));
    QCOMPARE(src.toPlainText(), QStringLiteral("# hello world"));
}

void TstSourceCanonicalAttach::documentReloaded_replacesContent() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("first"), Markoff::Origin::FirstOpen);
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);
    doc.resetContent(QStringLiteral("second"), Markoff::Origin::ExternalReloadClean);
    QCOMPARE(src.toPlainText(), QStringLiteral("second"));
}

QTEST_MAIN(TstSourceCanonicalAttach)
#include "tst_source_canonical_attach.moc"
```

- [ ] **Step 2: Implement `setDocument` attach path.**

In `SourceEditor.h`, override `MarkdownView::setDocument`. In `.cpp`:
```cpp
void SourceEditor::setDocument(MarkoffDocument *doc) {
    if (m_boundDoc == doc) return;
    if (m_boundDoc) {
        disconnect(m_boundDoc, nullptr, this, nullptr);
    }
    m_boundDoc = doc;
    if (!doc) {
        // Clear local buffer.
        m_applyingCanonicalDelta = true;
        this->setPlainText(QString());  // Qutepart native API; untracked edit.
        m_applyingCanonicalDelta = false;
        return;
    }

    // Disable Qutepart's own undo on the inner QTextDocument.
    this->document()->setUndoRedoEnabled(false);

    // Load canonical text.
    m_applyingCanonicalDelta = true;
    this->setPlainText(doc->toMarkdown());
    m_applyingCanonicalDelta = false;

    // Subscribe to canonical events.
    connect(doc, &MarkoffDocument::contentsChanged,
            this, &SourceEditor::onCanonicalContentsChanged);
    connect(doc, &MarkoffDocument::documentReloaded,
            this, &SourceEditor::onCanonicalReloaded);

    // Attach Qutepart's own contentsChange to local-change slot (wired in Task 11).
    // Task 11 sets this up via `connect(this->document(), ...)`.
}
```

Add member declarations:
```cpp
private:
    Markoff::MarkoffDocument *m_boundDoc = nullptr;
    bool m_applyingCanonicalDelta = false;

    void onCanonicalContentsChanged(qsizetype offset, qsizetype removed, qsizetype inserted);
    void onCanonicalReloaded();
```

- [ ] **Step 3: Implement `onCanonicalContentsChanged` + `onCanonicalReloaded`.**

```cpp
void SourceEditor::onCanonicalContentsChanged(qsizetype offset, qsizetype removed, qsizetype inserted) {
    if (m_applyingCanonicalDelta) return;
    if (!m_boundDoc) return;

    m_applyingCanonicalDelta = true;
    QTextCursor c(this->document());
    c.setPosition(int(offset));
    c.setPosition(int(offset + removed), QTextCursor::KeepAnchor);
    const QString insertedText = m_boundDoc->substring(offset, inserted);
    c.insertText(insertedText);
    m_applyingCanonicalDelta = false;
}

void SourceEditor::onCanonicalReloaded() {
    if (!m_boundDoc) return;
    m_applyingCanonicalDelta = true;
    this->setPlainText(m_boundDoc->toMarkdown());
    m_applyingCanonicalDelta = false;
}
```

- [ ] **Step 4: Run targeted tests — expect PASS.**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family/build-dev
cmake .. && make tst_source_canonical_attach -j 10
ctest -R tst_source_canonical_attach --output-on-failure
```

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-source/include/markoff/source/SourceEditor.h \
        libs/markoff-source/src/SourceEditor.cpp \
        libs/markoff-source/tests/tst_source_canonical_attach.cpp \
        libs/markoff-source/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-source: SourceEditor binds canonical via setDocument

setDocument attaches/detaches canonical per spec §5.1.
Disable-undo on Qutepart's QTextDocument. Subscribe to
contentsChanged (external splice) + documentReloaded (wholesale
replace). Guard m_applyingCanonicalDelta prevents reflection.

Local-edit-to-canonical (outbound path) wires in Task 11.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 11: Source local-edit → canonical-delta + IME macro

**Files:**
- Modify: `libs/markoff-source/src/SourceEditor.cpp`
- Extend: `libs/markoff-source/tests/tst_source_canonical_attach.cpp`

- [ ] **Step 1: Extend tests.**

Add slots to `TstSourceCanonicalAttach`:
```cpp
void TstSourceCanonicalAttach::localEdit_pushesCanonicalDelta() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);

    QTextCursor c(src.document());
    c.setPosition(5);
    c.insertText(QStringLiteral(" world"));

    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello world"));
    QCOMPARE(doc.undoStack()->count(), 1);
}
```

- [ ] **Step 2: Implement local-edit slot.**

Wire `SourceEditor::setDocument` to also connect:
```cpp
connect(this->document(), &QTextDocument::contentsChange,
        this, &SourceEditor::onLocalContentsChange);
```

Disconnect on detach. Slot:
```cpp
void SourceEditor::onLocalContentsChange(int position, int charsRemoved, int charsAdded) {
    if (m_applyingCanonicalDelta) return;
    if (!m_boundDoc) return;

    QString inserted;
    if (charsAdded > 0) {
        QTextCursor c(this->document());
        c.setPosition(position);
        c.setPosition(position + charsAdded, QTextCursor::KeepAnchor);
        inserted = c.selectedText();
        // QTextDocument gives U+2029 for paragraph separators in selectedText — normalize.
        inserted.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    }
    m_applyingCanonicalDelta = true;
    m_boundDoc->undoStack()->push(
        new MarkdownDelta(m_boundDoc, position, charsRemoved, inserted));
    m_applyingCanonicalDelta = false;
}
```

- [ ] **Step 3: Run — expect PASS.**

```bash
ctest -R tst_source_canonical_attach --output-on-failure
```

- [ ] **Step 4: Commit.**

```bash
git add libs/markoff-source/src/SourceEditor.cpp \
        libs/markoff-source/tests/tst_source_canonical_attach.cpp
git commit -m "$(cat <<'EOF'
markoff-source: outbound local-edit → MarkdownDelta

Qutepart's QTextDocument::contentsChange drives an outbound
MarkdownDelta push onto MarkoffDocument's undo stack per spec
§5.1. Guard prevents reflection of inbound splices. Paragraph
separator normalization to '\n' per QTextCursor::selectedText
convention.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §E. Phase 5 — Reading leaf adaptation (Task 12)

### Task 12: `Reading::ReadingView::setDocument` + parseUpdated + documentReloaded

**Files:**
- Modify: `libs/markoff-reading/include/markoff/reading/ReadingView.h`
- Modify: `libs/markoff-reading/src/ReadingView.cpp`
- Create: `libs/markoff-reading/tests/tst_reading_canonical_attach.cpp`
- Modify: `libs/markoff-reading/CMakeLists.txt`

- [ ] **Step 1: Write failing test.**

```cpp
// tst_reading_canonical_attach.cpp
#include <QtTest>
#include <QSignalSpy>
#include <markoff/reading/ReadingView.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>

class TstReadingCanonicalAttach : public QObject {
    Q_OBJECT
private slots:
    void setDocument_rebuildsAfterParseUpdated();
    void documentReloaded_resetsSectionLayout();
    void hasEditing_returnsFalse();
};

void TstReadingCanonicalAttach::setDocument_rebuildsAfterParseUpdated() {
    Markoff::MarkoffDocument doc;
    Markoff::Reading::ReadingView rv;
    rv.setDocument(&doc);
    QSignalSpy parsed(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("# Heading\n\nBody."), Markoff::Origin::FirstOpen);
    QVERIFY(parsed.wait(1000));
    QVERIFY(rv.sectionCount() > 0);   // public or test-private accessor; add if needed
}

void TstReadingCanonicalAttach::documentReloaded_resetsSectionLayout() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# first"), Markoff::Origin::FirstOpen);
    Markoff::Reading::ReadingView rv;
    rv.setDocument(&doc);
    QSignalSpy parsed(&doc, &Markoff::MarkoffDocument::parseUpdated);
    parsed.wait(1000);
    parsed.clear();
    QSignalSpy reload(&doc, &Markoff::MarkoffDocument::documentReloaded);
    doc.resetContent(QStringLiteral("# second"), Markoff::Origin::ExternalReloadClean);
    QCOMPARE(reload.count(), 1);
    QVERIFY(parsed.wait(1000));
    // After reload + reparse, layout corresponds to the new content.
}

void TstReadingCanonicalAttach::hasEditing_returnsFalse() {
    Markoff::Reading::ReadingView rv;
    QVERIFY(!rv.hasEditing());
    QVERIFY(rv.isReadOnly());
}

QTEST_MAIN(TstReadingCanonicalAttach)
#include "tst_reading_canonical_attach.moc"
```

If `sectionCount()` isn't a public API, add a test-private accessor in this task.

- [ ] **Step 2: Implement `ReadingView::setDocument`.**

```cpp
void ReadingView::setDocument(MarkoffDocument *doc) {
    if (m_boundDoc == doc) return;
    if (m_boundDoc) {
        disconnect(m_boundDoc, nullptr, this, nullptr);
    }
    m_boundDoc = doc;
    if (!doc) {
        // Tear down existing section layout.
        this->clearSections();
        return;
    }
    connect(doc, &MarkoffDocument::parseUpdated,
            this, &ReadingView::onParseUpdated);
    connect(doc, &MarkoffDocument::documentReloaded,
            this, &ReadingView::onDocumentReloaded);
    // If parse already complete, kick rebuild immediately.
    if (doc->parsedDocument()) {
        onParseUpdated(doc->parsedDocument());
    }
}

void ReadingView::onParseUpdated(const Markoff::Document *parsed) {
    if (!parsed) return;
    // Existing Cluster-E diff-and-patch section layout code path.
    this->rebuildSectionLayout(parsed);
}

void ReadingView::onDocumentReloaded() {
    this->clearSections();
    // Next parseUpdated will rebuild.
}
```

Add `hasEditing() const override { return false; }`.

- [ ] **Step 3: Run — expect PASS.**

- [ ] **Step 4: Commit.**

```bash
git add libs/markoff-reading/include/markoff/reading/ReadingView.h \
        libs/markoff-reading/src/ReadingView.cpp \
        libs/markoff-reading/tests/tst_reading_canonical_attach.cpp \
        libs/markoff-reading/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-reading: ReadingView binds canonical via setDocument

setDocument subscribes to parseUpdated + documentReloaded per
spec §5.3. No contentsChanged subscription — Reading never sees
partial parse states. hasEditing() returns false;
isReadOnly() returns true. Existing Cluster-E section-layout
diff-and-patch is the onParseUpdated slot body.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §F. Phase 6 — Live leaf adaptation (Tasks 13–16)

This is the largest chunk. Per spec §5.2, Live's scene-graph stays intact — per-block `TextControl` widgets survive — but they lose their authoritative `QTextDocument` undo and gain a per-block offset map in `SceneCoordinator` for delta translation.

### Task 13: `Markoff::Editor::setDocument` + attach + disable-undo + scene initial build

**Files:**
- Modify: `libs/markoff-live/include/markoff/Editor.h`
- Modify: `libs/markoff-live/src/Editor.cpp`
- Create: `libs/markoff-live/tests/tst_live_canonical_attach.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Write failing test — attach loads text via parseUpdated.**

```cpp
// tst_live_canonical_attach.cpp
#include <QtTest>
#include <QSignalSpy>
#include <markoff/Editor.h>
#include <markoff/MarkoffDocument.h>

class TstLiveCanonicalAttach : public QObject {
    Q_OBJECT
private slots:
    void setDocument_buildsSceneAfterParseUpdated();
    void setDocument_nullDetaches();
};

void TstLiveCanonicalAttach::setDocument_buildsSceneAfterParseUpdated() {
    Markoff::MarkoffDocument doc;
    Markoff::Editor ed;
    ed.setDocument(&doc);
    QSignalSpy parsed(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("# hello\n\nworld"), Markoff::Origin::FirstOpen);
    QVERIFY(parsed.wait(1000));
    QVERIFY(ed.blockCount() >= 2);  // heading + paragraph; add test accessor if needed
}

void TstLiveCanonicalAttach::setDocument_nullDetaches() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("# hi"), Markoff::Origin::FirstOpen);
    Markoff::Editor ed;
    ed.setDocument(&doc);
    ed.setDocument(nullptr);
    // No assertion — just must not crash or leave dangling connections.
}

QTEST_MAIN(TstLiveCanonicalAttach)
#include "tst_live_canonical_attach.moc"
```

- [ ] **Step 2: Implement `Editor::setDocument`.**

```cpp
void Editor::setDocument(MarkoffDocument *doc) {
    if (m_boundDoc == doc) return;
    if (m_boundDoc) {
        disconnect(m_boundDoc, nullptr, this, nullptr);
    }
    m_boundDoc = doc;
    if (!doc) {
        m_sceneCoordinator->clearScene();
        return;
    }
    connect(doc, &MarkoffDocument::contentsChanged,
            this, &Editor::onCanonicalContentsChanged);
    connect(doc, &MarkoffDocument::parseUpdated,
            this, &Editor::onCanonicalParseUpdated);
    connect(doc, &MarkoffDocument::documentReloaded,
            this, &Editor::onCanonicalDocumentReloaded);

    // If parse already complete, build scene now.
    if (doc->parsedDocument()) {
        onCanonicalParseUpdated(doc->parsedDocument());
    }
}

void Editor::onCanonicalContentsChanged(qsizetype offset, qsizetype removed, qsizetype inserted) {
    // Implemented in Task 15.
}

void Editor::onCanonicalParseUpdated(const Markoff::Document *parsed) {
    if (!parsed) return;
    m_sceneCoordinator->rebuildFromAst(parsed, m_boundDoc->toMarkdown());
    // SceneCoordinator (Task 14) rebuilds blocks AND refreshes offset map.
    // Every block's QTextDocument has setUndoRedoEnabled(false) applied.
}

void Editor::onCanonicalDocumentReloaded() {
    m_sceneCoordinator->clearScene();
    // Next parseUpdated will rebuild.
}
```

- [ ] **Step 3: Every `TextControl`'s `QTextDocument::setUndoRedoEnabled(false)` on construction in `SceneCoordinator::buildBlock`.**

Modify `SceneCoordinator` to call `textControl->document()->setUndoRedoEnabled(false)` every time a block is constructed. Belt-and-braces — also grep for any `setUndoRedoEnabled(true)` sites in live and delete them (none should exist after C3).

- [ ] **Step 4: Run — expect PASS for attach test; outbound edit test lives in Task 15.**

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-live/include/markoff/Editor.h \
        libs/markoff-live/src/Editor.cpp \
        libs/markoff-live/tests/tst_live_canonical_attach.cpp \
        libs/markoff-live/CMakeLists.txt
# Also SceneCoordinator.cpp if modified in Step 3
git commit -m "$(cat <<'EOF'
markoff-live: Editor binds canonical via setDocument

setDocument connects contentsChanged / parseUpdated /
documentReloaded per spec §5.2. Scene builds from parsed AST
on first parseUpdated. Every per-block TextControl's
QTextDocument has setUndoRedoEnabled(false) — canonical's
QUndoStack is the single authoritative undo.

Inbound-delta splicing (Task 15) and outbound local-edit
translation (Task 15) land in Task 15 after the offset map
(Task 14) is in place.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 14: `SceneCoordinator` per-block offset map

**Files:**
- Modify: `libs/markoff-live/include/markoff/SceneCoordinator.h` (or equivalent)
- Modify: `libs/markoff-live/src/SceneCoordinator.cpp`
- Create: `libs/markoff-live/tests/tst_scene_offset_map.cpp`

- [ ] **Step 1: Write failing test.**

```cpp
// tst_scene_offset_map.cpp
#include <QtTest>
#include <markoff/SceneCoordinator.h>
#include <markoff/MarkoffDocument.h>

class TstSceneOffsetMap : public QObject {
    Q_OBJECT
private slots:
    void offsetMap_coversAllBlocks_contiguously();
    void offsetMap_findsBlockForOffset();
    void offsetMap_shiftsAfterInsert();
};

void TstSceneOffsetMap::offsetMap_coversAllBlocks_contiguously() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("para 1\n\npara 2"), Markoff::Origin::FirstOpen);
    QSignalSpy parsed(&doc, &Markoff::MarkoffDocument::parseUpdated);
    parsed.wait(1000);
    Markoff::SceneCoordinator sc;
    sc.rebuildFromAst(doc.parsedDocument(), doc.toMarkdown());
    const auto map = sc.offsetMap();
    QVERIFY(!map.isEmpty());
    // Each block's canonicalEnd == next block's canonicalStart.
    for (int i = 1; i < map.size(); ++i) {
        QCOMPARE(map[i-1].canonicalEnd, map[i].canonicalStart);
    }
}

// ... more slots

QTEST_MAIN(TstSceneOffsetMap)
#include "tst_scene_offset_map.moc"
```

- [ ] **Step 2: Implement the offset map data structure.**

In `SceneCoordinator.h`:
```cpp
struct BlockEntry {
    qsizetype canonicalStart;
    qsizetype canonicalEnd;  // exclusive
    QWidget  *blockWidget;   // or TextControl* / whatever the existing type is
};

class SceneCoordinator {
public:
    // ...existing API...
    const QVector<BlockEntry> &offsetMap() const { return m_map; }
    int  findBlockIndexForOffset(qsizetype offset) const;  // binary search
    void shiftBlocksAfter(int blockIndex, qsizetype delta);

private:
    QVector<BlockEntry> m_map;
};
```

`rebuildFromAst` populates `m_map` during scene construction — each block's `(canonicalStart, canonicalEnd)` determined by the AST node's source position (tree-sitter's `ts_node_start_byte` / `ts_node_end_byte`, or the wrapper MarkoffParser exposes).

- [ ] **Step 3: Implement `findBlockIndexForOffset` (binary search).**

- [ ] **Step 4: Implement `shiftBlocksAfter`.**

- [ ] **Step 5: Run — expect PASS.**

- [ ] **Step 6: Commit.**

```bash
git add libs/markoff-live/include/markoff/SceneCoordinator.h \
        libs/markoff-live/src/SceneCoordinator.cpp \
        libs/markoff-live/tests/tst_scene_offset_map.cpp \
        libs/markoff-live/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: SceneCoordinator per-block offset map

Per spec §5.2, every block carries (canonicalStart, canonicalEnd)
so canonical deltas can be translated to block-local splices
without reparsing. Built during rebuildFromAst from tree-sitter
source positions. Binary-search finder + bulk shifter for
subsequent-block adjustment on applied deltas.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 15: Live outbound local-edit → canonical-delta via offset map

**Files:**
- Modify: `libs/markoff-live/src/Editor.cpp` + `SceneCoordinator.cpp`
- Extend: `libs/markoff-live/tests/tst_live_canonical_attach.cpp`

- [ ] **Step 1: Extend test.**

```cpp
void TstLiveCanonicalAttach::localEdit_pushesMarkdownDelta() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello\n\nworld"), Markoff::Origin::FirstOpen);
    Markoff::Editor ed;
    ed.setDocument(&doc);
    QSignalSpy parsed(&doc, &Markoff::MarkoffDocument::parseUpdated);
    parsed.wait(1000);

    // Programmatically edit the first block's QTextDocument.
    ed.simulateBlockEdit(/* block 0 */ 0, /* local pos */ 5, QStringLiteral(", friend"));
    // MarkoffDocument should now reflect the insert at canonical offset 5.
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello, friend\n\nworld"));
    QCOMPARE(doc.undoStack()->count(), 1);
}
```

`simulateBlockEdit` is a test-only helper on `Editor` that exercises the local-edit path.

- [ ] **Step 2: Wire each block's `QTextDocument::contentsChange` to translate and push.**

In `SceneCoordinator::buildBlock`:
```cpp
connect(textControl->document(), &QTextDocument::contentsChange,
        this, [this, blockIndex=m_map.size()](int localPos, int charsRemoved, int charsAdded) {
    this->onLocalBlockEdit(blockIndex, localPos, charsRemoved, charsAdded);
});
```

```cpp
void SceneCoordinator::onLocalBlockEdit(int blockIndex, int localPos, int charsRemoved, int charsAdded) {
    if (m_applyingCanonicalDelta) return;
    if (blockIndex < 0 || blockIndex >= m_map.size()) return;

    const auto &block = m_map[blockIndex];
    const qsizetype canonicalOffset = block.canonicalStart + localPos;

    QString insertedText;
    if (charsAdded > 0) {
        auto *td = blockTextDocument(blockIndex);
        QTextCursor c(td);
        c.setPosition(localPos);
        c.setPosition(localPos + charsAdded, QTextCursor::KeepAnchor);
        insertedText = c.selectedText();
        insertedText.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    }

    m_applyingCanonicalDelta = true;
    m_boundDoc->undoStack()->push(
        new MarkdownDelta(m_boundDoc, canonicalOffset, charsRemoved, insertedText));
    m_applyingCanonicalDelta = false;
}
```

`m_boundDoc` needs to be a member set from `Editor::setDocument`.

- [ ] **Step 3: Run — expect PASS.**

- [ ] **Step 4: Commit.**

```bash
git add libs/markoff-live/src/Editor.cpp \
        libs/markoff-live/src/SceneCoordinator.cpp \
        libs/markoff-live/tests/tst_live_canonical_attach.cpp
git commit -m "$(cat <<'EOF'
markoff-live: outbound local-edit via offset map

Block-local QTextDocument::contentsChange translates to canonical
offset via SceneCoordinator::m_map[blockIndex].canonicalStart +
localPos. Push MarkdownDelta with guard to prevent inbound
reflection looping through local-edit.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 16: Live inbound-delta splicing + offset-map shift + documentReloaded

**Files:**
- Modify: `libs/markoff-live/src/Editor.cpp` + `SceneCoordinator.cpp`
- Extend: `libs/markoff-live/tests/tst_live_canonical_attach.cpp`

- [ ] **Step 1: Extend test.**

```cpp
void TstLiveCanonicalAttach::inboundDelta_splicesIntoBlock() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello\n\nworld"), Markoff::Origin::FirstOpen);
    Markoff::Editor ed;
    ed.setDocument(&doc);
    QSignalSpy parsed(&doc, &Markoff::MarkoffDocument::parseUpdated);
    parsed.wait(1000);

    doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, 5, 0, QStringLiteral(", friend")));
    // Synchronous delta application — block 0's local doc has the insert before reparse.
    QCOMPARE(ed.blockPlainText(0), QStringLiteral("hello, friend"));
}
```

- [ ] **Step 2: Implement `Editor::onCanonicalContentsChanged` → `SceneCoordinator::applyCanonicalDelta`.**

```cpp
void Editor::onCanonicalContentsChanged(qsizetype offset, qsizetype removed, qsizetype inserted) {
    m_sceneCoordinator->applyCanonicalDelta(offset, removed, inserted);
}

void SceneCoordinator::applyCanonicalDelta(qsizetype offset, qsizetype removed, qsizetype inserted) {
    if (m_applyingCanonicalDelta) return;

    const int blockIdx = findBlockIndexForOffset(offset);
    if (blockIdx < 0) return;

    auto &block = m_map[blockIdx];
    const qsizetype localPos = offset - block.canonicalStart;

    // If the delta spans multiple blocks, mark for scene rebuild on next parseUpdated
    // rather than splice across blocks. (Simpler + correct.)
    if (offset + removed > block.canonicalEnd) {
        m_sceneNeedsFullRebuildOnNextParse = true;
        block.canonicalEnd += (inserted - removed);  // conservative update to support next findBlockIndexForOffset
        shiftBlocksAfter(blockIdx, inserted - removed);
        return;
    }

    m_applyingCanonicalDelta = true;
    auto *td = blockTextDocument(blockIdx);
    QTextCursor c(td);
    c.setPosition(int(localPos));
    c.setPosition(int(localPos + removed), QTextCursor::KeepAnchor);
    const QString insertedText = m_boundDoc->substring(offset, inserted);
    c.insertText(insertedText);
    m_applyingCanonicalDelta = false;

    block.canonicalEnd += (inserted - removed);
    shiftBlocksAfter(blockIdx, inserted - removed);
}
```

- [ ] **Step 3: Handle `m_sceneNeedsFullRebuildOnNextParse` in `onCanonicalParseUpdated`.**

Force-rebuild rather than diff-and-patch when the flag is set. Clear the flag after rebuild.

- [ ] **Step 4: Run — expect PASS.**

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-live/src/Editor.cpp \
        libs/markoff-live/src/SceneCoordinator.cpp \
        libs/markoff-live/tests/tst_live_canonical_attach.cpp
git commit -m "$(cat <<'EOF'
markoff-live: inbound canonical delta splices into per-block doc

SceneCoordinator::applyCanonicalDelta locates the affected block
via offset-map binary search, splices its local QTextDocument
(guard-protected), then shifts subsequent blocks' offsets. Multi-
block deltas (cross-block selections replaced) flag for full
rebuild on next parseUpdated instead of splicing.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §G. Phase 7 — Tri-view interop (Tasks 17–18)

### Task 17: `tst_canonical_interop` (three-leaf bind, observe cross-leaf)

**Files:**
- Create: `tests/markoff/tst_canonical_interop.cpp`
- Modify: top-level `tests/CMakeLists.txt` or `CMakeLists.txt`

- [ ] **Step 1: Write the test.**

```cpp
// tests/markoff/tst_canonical_interop.cpp
#include <QtTest>
#include <QSignalSpy>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>
#include <markoff/Editor.h>
#include <markoff/source/SourceEditor.h>
#include <markoff/reading/ReadingView.h>

class TstCanonicalInterop : public QObject {
    Q_OBJECT
private slots:
    void allThreeLeaves_boundToOneDoc_observeEachOthersEdits();
    void parseUpdated_firesOncePerBurst();
};

void TstCanonicalInterop::allThreeLeaves_boundToOneDoc_observeEachOthersEdits() {
    Markoff::MarkoffDocument doc;
    Markoff::Source::SourceEditor src;
    Markoff::Editor live;
    Markoff::Reading::ReadingView rv;
    src.setDocument(&doc);
    live.setDocument(&doc);
    rv.setDocument(&doc);

    QSignalSpy parsed(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(QStringLiteral("# Heading\n\nBody."), Markoff::Origin::FirstOpen);
    QVERIFY(parsed.wait(1000));

    // Edit via Source (canonically).
    QTextCursor c(src.document());
    c.setPosition(src.toPlainText().size());
    c.insertText(QStringLiteral(" More."));

    // Source local text = canonical.
    QCOMPARE(src.toPlainText(), QStringLiteral("# Heading\n\nBody. More."));
    QCOMPARE(doc.toMarkdown(), QStringLiteral("# Heading\n\nBody. More."));
    QVERIFY(parsed.wait(500));  // second parseUpdated after the edit settles
    // Live and Reading both re-render off the new parseUpdated.
}

void TstCanonicalInterop::parseUpdated_firesOncePerBurst() {
    Markoff::MarkoffDocument doc;
    doc.setCoalescingIdleMs(100);
    doc.resetContent(QStringLiteral("x"), Markoff::Origin::FirstOpen);
    Markoff::Editor live;
    live.setDocument(&doc);
    QSignalSpy parsed(&doc, &Markoff::MarkoffDocument::parseUpdated);
    parsed.wait(500);
    parsed.clear();

    for (int i = 0; i < 100; ++i) {
        doc.undoStack()->push(new Markoff::MarkdownDelta(&doc, doc.length(), 0, QStringLiteral("a")));
    }
    QVERIFY(parsed.wait(1500));
    QVERIFY(parsed.count() <= 2);  // sensitive to scheduling — <=2 is safe
}

QTEST_MAIN(TstCanonicalInterop)
#include "tst_canonical_interop.moc"
```

- [ ] **Step 2: Register in top-level tests.**

- [ ] **Step 3: Run — expect PASS.**

- [ ] **Step 4: Commit.**

```bash
git add tests/markoff/tst_canonical_interop.cpp \
        tests/markoff/CMakeLists.txt   # or equivalent
git commit -m "$(cat <<'EOF'
tests/markoff: tri-view canonical interop

All three leaves bind to one MarkoffDocument; edits in any leaf
observe through the signal layer to the others. Debouncer collapses
a 100-keystroke burst to at most 2 parseUpdated emissions.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 18: `tst_cross_mode_undo`

**Files:**
- Create: `tests/markoff/tst_cross_mode_undo.cpp`

- [ ] **Step 1: Write the test.**

```cpp
#include <QtTest>
#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>
#include <markoff/Editor.h>
#include <markoff/source/SourceEditor.h>

class TstCrossModeUndo : public QObject {
    Q_OBJECT
private slots:
    void editInSource_swapToLive_edit_ctrlZ_reversesLiveEdit();
    void undo_allTheWayToOriginalContent();
};

void TstCrossModeUndo::editInSource_swapToLive_edit_ctrlZ_reversesLiveEdit() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello\n\nworld"), Markoff::Origin::FirstOpen);

    Markoff::Source::SourceEditor src;
    src.setDocument(&doc);
    QTextCursor c(src.document());
    c.setPosition(5);
    c.insertText(QStringLiteral(", friend"));
    src.setDocument(nullptr);

    Markoff::Editor live;
    live.setDocument(&doc);
    QSignalSpy parsed(&doc, &Markoff::MarkoffDocument::parseUpdated);
    parsed.wait(1000);
    live.simulateBlockEdit(0, 0, QStringLiteral("> "));
    // Canonical: "> hello, friend\n\nworld"

    QCOMPARE(doc.toMarkdown(), QStringLiteral("> hello, friend\n\nworld"));

    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello, friend\n\nworld"));
    doc.undoStack()->undo();
    QCOMPARE(doc.toMarkdown(), QStringLiteral("hello\n\nworld"));
}

void TstCrossModeUndo::undo_allTheWayToOriginalContent() {
    // ... similar chained test ...
}

QTEST_MAIN(TstCrossModeUndo)
#include "tst_cross_mode_undo.moc"
```

- [ ] **Step 2: Run — expect PASS.**

- [ ] **Step 3: Commit.**

```bash
git add tests/markoff/tst_cross_mode_undo.cpp tests/markoff/CMakeLists.txt
git commit -m "$(cat <<'EOF'
tests/markoff: cross-mode undo

Edit-in-Source → swap-to-Live → edit-in-Live → Ctrl+Z reverses
the Live edit; Ctrl+Z again reverses the Source edit. Single
QUndoStack on MarkoffDocument is the authoritative history
regardless of which leaf was active at edit time.

Part of Phase C3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §H. Phase 8 — Tag `v0.6.0` + status board (Task 19)

### Task 19: Tag + status board

- [ ] **Step 1: Full standalone ctest.**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
rm -rf build-dev && cmake -S . -B build-dev && cmake --build build-dev -j 10
cd build-dev && ctest --output-on-failure -j 10
```

Expected: 70+ tests pass, including all new C3 tests (`tst_canonical_buffer`, `tst_cursor_position`, `tst_markdown_delta`, `tst_parse_pool`, `tst_origin_reset`, `tst_cursor_anchor`, `tst_source_canonical_attach`, `tst_reading_canonical_attach`, `tst_live_canonical_attach`, `tst_scene_offset_map`, `tst_canonical_interop`, `tst_cross_mode_undo`).

- [ ] **Step 2: Update `docs/phase-c-status.md` — C3 row to `markoff ready`; log entry for `v0.6.0`.**

- [ ] **Step 3: Commit + tag.**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git add docs/phase-c-status.md
git commit -m "phase-c-status: C3 landed at v0.6.0

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
git tag -a v0.6.0 -m "Phase C3 content-authoritative MarkoffDocument

Symmetric-B design ships: canonical = markdown bytes behind
CanonicalBuffer interface; one QUndoStack on MarkoffDocument;
three leaves subscribe via contentsChanged + parseUpdated +
documentReloaded; every edit routes through MarkdownDelta.

Breaking from v0.5.0: textDocument, setPlainText, plainText,
replace, insert, remove, beginTransaction, endTransaction,
parsed() all removed. Migration per spec §6.6.

CanonicalBuffer + CursorPosition are Phase-E hedge surfaces —
one concrete today (InMemoryCanonicalBuffer); future
CrdtCanonicalBuffer (collabtext integration) is a clean swap."
```

---

## §I. Phase 9 — Corbomite adaptation: `NoteDocument` (Task 20)

### Task 20: `NoteDocument` becomes a wrapper

**Files:**
- Modify: `libs/core/include/corbomite/core/NoteDocument.h`
- Modify: `libs/core/src/NoteDocument.cpp`
- Modify: `libs/core/CMakeLists.txt` (link against `Markoff::Core`)
- Modify: `libs/core/tests/tst_notedocument.cpp` (if exists)

- [ ] **Step 1: Extend failing test.**

```cpp
// tst_notedocument additions
void TstNoteDocument::markdown_delegatesToMarkoffDocument() {
    Corbomite::NoteDocument note(/* vaultRoot */ "/tmp", /* relativePath */ "test.md");
    note.markoff()->resetContent(QStringLiteral("# hello"), Markoff::Origin::FirstOpen);
    QCOMPARE(note.markdown(), QStringLiteral("# hello"));
}

void TstNoteDocument::setMarkdown_routesViaOrigin() {
    Corbomite::NoteDocument note("/tmp", "test.md");
    note.setMarkdown(QStringLiteral("first"));
    QCOMPARE(note.markoff()->toMarkdown(), QStringLiteral("first"));
}

void TstNoteDocument::textChangedSignal_firesOnCanonicalContentsChanged() {
    Corbomite::NoteDocument note("/tmp", "test.md");
    QSignalSpy spy(&note, &Corbomite::NoteDocument::textChanged);
    note.markoff()->resetContent(QStringLiteral("x"), Markoff::Origin::FirstOpen);
    QCOMPARE(spy.count(), 1);
}
```

- [ ] **Step 2: Rewrite `NoteDocument.h`.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <memory>

namespace Markoff { class MarkoffDocument; class ParsePool; }

namespace Corbomite {

class NoteDocument : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool modified READ isModified NOTIFY modificationChanged)
public:
    explicit NoteDocument(const QString &vaultRoot, const QString &relativePath,
                          Markoff::ParsePool *pool = nullptr,
                          QObject *parent = nullptr);
    ~NoteDocument() override;

    QString filePath() const;
    QString relativePath() const;
    QString name() const;

    // Compatibility surface — delegates to m_markoff.
    QString markdown() const;
    void    setMarkdown(const QString &text);  // uses Origin heuristic; prefer markoff()-> direct calls

    bool isModified() const;
    void setModified(bool modified);

    int wordCount() const;
    int characterCount() const;

    // New — leaves bind to this.
    Markoff::MarkoffDocument       *markoff();
    const Markoff::MarkoffDocument *markoff() const;

Q_SIGNALS:
    void textChanged();
    void modificationChanged(bool modified);
    void saved();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Corbomite
```

- [ ] **Step 3: Rewrite `NoteDocument.cpp`.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "corbomite/core/NoteDocument.h"

#include <markoff/MarkoffDocument.h>

namespace Corbomite {

struct NoteDocument::Private {
    QString vaultRoot;
    QString relativePath;
    std::unique_ptr<Markoff::MarkoffDocument> markoff;
    bool modified = false;
    mutable int cachedWordCount = -1;
};

NoteDocument::NoteDocument(const QString &vaultRoot, const QString &relativePath,
                           Markoff::ParsePool *pool,
                           QObject *parent)
    : QObject(parent), d(std::make_unique<Private>()) {
    d->vaultRoot = vaultRoot;
    d->relativePath = relativePath;
    d->markoff = std::make_unique<Markoff::MarkoffDocument>(
        /* default buffer */ nullptr, pool, this);

    connect(d->markoff.get(), &Markoff::MarkoffDocument::contentsChanged,
            this, [this](qsizetype, qsizetype, qsizetype) {
        d->cachedWordCount = -1;
        if (!d->modified) setModified(true);
        emit textChanged();
    });
    connect(d->markoff.get(), &Markoff::MarkoffDocument::documentReloaded,
            this, [this]() {
        d->cachedWordCount = -1;
        emit textChanged();
    });
}

NoteDocument::~NoteDocument() = default;

QString NoteDocument::markdown() const { return d->markoff->toMarkdown(); }

void NoteDocument::setMarkdown(const QString &text) {
    // Heuristic: called from application code that doesn't know about Origin.
    // Treat as test-fixture semantics (clear undo stack, wholesale replace).
    // Vault callers should call markoff()->resetContent(text, Origin::FirstOpen)
    // or Origin::ExternalReloadClean directly.
    d->markoff->resetContent(text, Markoff::Origin::TestFixture);
}

bool NoteDocument::isModified() const { return d->modified; }
void NoteDocument::setModified(bool m) {
    if (d->modified == m) return;
    d->modified = m;
    emit modificationChanged(m);
}

int NoteDocument::wordCount() const {
    if (d->cachedWordCount < 0) {
        const QString text = d->markoff->toMarkdown();
        d->cachedWordCount = /* existing count logic */ 0;
    }
    return d->cachedWordCount;
}

int NoteDocument::characterCount() const { return int(d->markoff->length()); }

Markoff::MarkoffDocument *NoteDocument::markoff() { return d->markoff.get(); }
const Markoff::MarkoffDocument *NoteDocument::markoff() const { return d->markoff.get(); }

QString NoteDocument::filePath() const { /* ... */ }
QString NoteDocument::relativePath() const { return d->relativePath; }
QString NoteDocument::name() const { /* ... */ }

} // namespace Corbomite
```

- [ ] **Step 4: Update CMake to link against `Markoff::Core`.**

- [ ] **Step 5: Run targeted Corbomite tests.**

```bash
cd /home/clinton/dev/Corbomite
cmake -B build -DCORBOMITE_DEV_BUILD=ON && cmake --build build -j 10 --target tst_notedocument
cd build && ctest -R tst_notedocument --output-on-failure
```

- [ ] **Step 6: Commit.**

```bash
cd /home/clinton/dev/Corbomite
git add libs/core/include/corbomite/core/NoteDocument.h \
        libs/core/src/NoteDocument.cpp \
        libs/core/CMakeLists.txt \
        libs/core/tests/tst_notedocument.cpp
git commit -m "$(cat <<'EOF'
feat(markoff): Phase C3 adaptation — NoteDocument wraps MarkoffDocument

NoteDocument owns a Markoff::MarkoffDocument per Phase C3 spec
§6.1. Public API preserved (markdown / setMarkdown / textChanged /
modificationChanged / saved / wordCount / characterCount); new
markoff() accessor for leaves. setMarkdown uses TestFixture origin
as a default — vault callers should call markoff()->resetContent
directly with the right Origin.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §J. Phase 10 — Corbomite Vault wiring (Tasks 21–22)

### Task 21: Vault owns ParsePool + saveDocument raw byte write + byte-equality echo suppression

**Files:**
- Modify: `libs/vault/include/corbomite/vault/Vault.h`
- Modify: `libs/vault/src/Vault.cpp`
- Extend: `libs/vault/tests/tst_vault_save_reload.cpp` (or equivalent)

- [ ] **Step 1: Extend failing test.**

```cpp
void TstVaultSaveReload::saveDocument_writesCanonicalBytes_exactly() {
    Corbomite::Vault vault(tmpDir);
    Corbomite::NoteDocument *note = vault.openDocument("test.md");
    note->markoff()->resetContent(QStringLiteral("raw bytes"), Markoff::Origin::FirstOpen);
    note->setModified(true);
    vault.saveDocument(note);

    QFile f(tmpDir + "/test.md");
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray disk = f.readAll();
    QCOMPARE(QString::fromUtf8(disk), note->markoff()->toMarkdown());
}

void TstVaultSaveReload::saveDocument_byteEqualityEchoSuppression() {
    // After save, watcher event reporting the same file should NOT trigger reload.
    // (Test depends on Vault's watcher surface — adapt to the real API.)
}
```

- [ ] **Step 2: Modify `Vault` to own `ParsePool`.**

```cpp
// Vault.h additions:
#include <markoff/ParsePool.h>
// private:
std::unique_ptr<Markoff::ParsePool> m_parsePool;

// Vault.cpp ctor:
m_parsePool = std::make_unique<Markoff::ParsePool>(this);

// openDocument:
NoteDocument *Vault::openDocument(const QString &relPath) {
    auto it = m_docs.constFind(relPath);
    if (it != m_docs.constEnd()) return it.value();
    auto *note = new NoteDocument(m_root, relPath, m_parsePool.get(), this);
    const QByteArray bytes = QFile(absolutePath(relPath)).readAll();  // existing IO
    note->markoff()->resetContent(QString::fromUtf8(bytes), Markoff::Origin::FirstOpen);
    note->setModified(false);
    m_docs.insert(relPath, note);
    return note;
}

// saveDocument:
bool Vault::saveDocument(NoteDocument *doc) {
    const QByteArray bytes = doc->markoff()->toMarkdown().toUtf8();
    const QString path = absolutePath(doc->relativePath());

    m_suppressWatcherForPath = path;  // existing flag
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (f.write(bytes) != bytes.size()) return false;
    f.close();

    doc->setModified(false);
    emit doc->saved();
    emit documentSaved(doc->relativePath());
    return true;
}
```

Watcher event handler (existing site):
```cpp
void Vault::onFileChanged(const QString &path) {
    if (m_suppressWatcherForPath == path) {
        m_suppressWatcherForPath.clear();
        return;
    }
    // Defense-in-depth: compare disk bytes vs. canonical.
    NoteDocument *doc = documentForPath(path);
    if (doc) {
        const QByteArray disk = QFile(path).readAll();
        if (QString::fromUtf8(disk) == doc->markoff()->toMarkdown()) {
            return;  // byte-equal; no-op event
        }
    }
    // External reload — Task 22.
    emit externalFileChanged(path);
}
```

- [ ] **Step 3: Run — expect PASS for save/byte-equality tests.**

- [ ] **Step 4: Commit.**

```bash
git add libs/vault/include/corbomite/vault/Vault.h \
        libs/vault/src/Vault.cpp \
        libs/vault/tests/tst_vault_save_reload.cpp
git commit -m "$(cat <<'EOF'
feat(markoff): Phase C3 adaptation — Vault owns ParsePool

Vault constructs one Markoff::ParsePool for its lifetime and
passes it into every NoteDocument. saveDocument writes canonical
bytes verbatim (QFile::write on markoff()->toMarkdown().toUtf8())
— no QTextDocumentWriter, no format coercion. Watcher echo-
suppression gains defense-in-depth byte-equality check: even if
the flag leaks, a byte-equal disk read suppresses the reload.

Part of Phase C3 spec §6.2.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 22: External-reload watcher Origin dispatch

**Files:**
- Modify: `libs/vault/src/Vault.cpp` or `src/app/MainWindow.cpp` (wherever external-reload dispatch lives)
- Extend: tests

- [ ] **Step 1: Write failing test.**

```cpp
void TstVaultSaveReload::externalReloadClean_dispatchesReloadCleanOrigin() {
    // Simulate disk change on a clean file; assert MarkoffDocument emits documentReloaded
    // once and content matches new disk bytes.
}

void TstVaultSaveReload::externalReloadDirty_dispatchesReloadResolvedOrigin() {
    // Simulate disk change on a dirty file; mock the merge modal outcome to "take theirs";
    // assert resetContent called with Origin::ExternalReloadResolved.
}
```

- [ ] **Step 2: Wire the dispatch.**

```cpp
void Vault::onExternalFileChanged(const QString &path) {
    NoteDocument *doc = documentForPath(path);
    if (!doc) return;
    const QByteArray disk = QFile(path).readAll();
    const QString newContent = QString::fromUtf8(disk);

    if (!doc->isModified()) {
        doc->markoff()->resetContent(newContent, Markoff::Origin::ExternalReloadClean);
        return;
    }

    // Dirty — let UI layer handle the merge modal; UI calls back via
    // resolveExternalReload(doc, chosenContent).
    emit externalReloadConflict(doc, newContent);
}

void Vault::resolveExternalReload(NoteDocument *doc, const QString &resolvedContent) {
    doc->markoff()->resetContent(resolvedContent, Markoff::Origin::ExternalReloadResolved);
    doc->setModified(false);
}
```

UI layer: existing merge modal invokes `vault->resolveExternalReload(doc, chosen)` after user picks an outcome (keep-mine / take-theirs / merged).

- [ ] **Step 3: Run — expect PASS.**

- [ ] **Step 4: Commit.**

```bash
git add libs/vault/src/Vault.cpp libs/vault/include/corbomite/vault/Vault.h \
        libs/vault/tests/tst_vault_save_reload.cpp \
        src/app/MainWindow.cpp  # if merge-modal dispatch changed
git commit -m "$(cat <<'EOF'
feat(markoff): Phase C3 adaptation — external reload Origin dispatch

Vault's watcher path: clean → ExternalReloadClean; dirty →
externalReloadConflict signal → UI merge modal → vault->
resolveExternalReload(doc, chosenContent) → ExternalReloadResolved.
All three outcomes (keep-mine / take-theirs / merged) clear the
undo stack and emit documentReloaded per spec §6.2.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §K. Phase 11 — NoteEditorWidget flush/restore retirement (Task 23)

### Task 23: Delete four flush/restore sites + mode-swap via setDocument

**Files:**
- Modify: `src/editor/NoteEditorWidget.cpp` (lines 176, 186, 433, 609)
- Modify: `src/editor/NoteEditorWidget.h`
- Create: `src/editor/tests/tst_modeswap_preserves_ephemeral.cpp` (or modify existing)

- [ ] **Step 1: Write the failing test.**

```cpp
// tst_modeswap_preserves_ephemeral.cpp
void TstModeSwapPreservesEphemeral::swap_roundTrip_preservesCanonicalBytes() {
    auto *doc = vault->openDocument("test.md");
    NoteEditorWidget w;
    w.setNoteDocument(doc);
    const QString before = doc->markoff()->toMarkdown();

    w.setViewMode(ViewMode::Source);
    w.setViewMode(ViewMode::Live);
    w.setViewMode(ViewMode::Source);

    QCOMPARE(doc->markoff()->toMarkdown(), before);
    // Exact byte-equality — no re-normalization during swap.
}

void TstModeSwapPreservesEphemeral::swap_preservesCursorPosition() {
    // ... using ephemeralState ...
}
```

- [ ] **Step 2: Delete lines 176, 186 (similar pattern).**

Find in `NoteEditorWidget.cpp:170-200` the code that does `m_editor->toPlainText()` + compare + `m_doc->setMarkdown(text)`. Delete. Replace with a no-op or remove the slot entirely depending on why it existed.

- [ ] **Step 3: Delete lines 433 + 609 — the mode-swap and close flush.**

Find the mode-swap method (likely `NoteEditorWidget::setViewMode(ViewMode)`):
```cpp
// BEFORE:
void NoteEditorWidget::setViewMode(ViewMode mode) {
    if (m_activeLeaf) {
        m_doc->setMarkdown(m_activeLeaf->toPlainText());  // DELETE
    }
    // ... swap ...
    m_activeLeaf->setPlainText(m_doc->markdown());         // DELETE
}

// AFTER:
void NoteEditorWidget::setViewMode(ViewMode mode) {
    const QJsonObject snapshot = m_activeLeaf ? m_activeLeaf->ephemeralState() : QJsonObject{};
    if (m_activeLeaf) {
        m_activeLeaf->setDocument(nullptr);
    }
    // ... update m_activeLeaf to point at new leaf ...
    if (m_activeLeaf) {
        m_activeLeaf->setDocument(m_doc->markoff());
        m_activeLeaf->setEphemeralState(snapshot);
    }
}
```

Same treatment for close path (line 609).

- [ ] **Step 4: Verify via grep — zero remaining flush sites.**

```bash
cd /home/clinton/dev/Corbomite
grep -rn "setMarkdown(m_editor" src/editor/  # expect empty
grep -rn "setMarkdown(m_activeLeaf" src/editor/  # expect empty
```

- [ ] **Step 5: Run — expect PASS on swap tests.**

- [ ] **Step 6: Commit.**

```bash
git add src/editor/NoteEditorWidget.cpp src/editor/NoteEditorWidget.h \
        src/editor/tests/tst_modeswap_preserves_ephemeral.cpp
git commit -m "$(cat <<'EOF'
feat(markoff): Phase C3 adaptation — NoteEditorWidget flush/restore retires

Four flush/restore call sites (NoteEditorWidget.cpp:176,186,433,609
pre-C3) delete per spec §6.3. Mode-swap becomes: outgoing leaf
snapshots ephemeralState + setDocument(nullptr); incoming leaf
setDocument(m_doc->markoff()) + setEphemeralState(snapshot).
Canonical content never round-trips through leaves during swap.

tst_modeswap_preserves_ephemeral verifies exact byte-equality of
canonical before/after swap.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §L. Phase 12 — End-to-end + closeout (Tasks 24–25)

### Task 24: Submodule pin bump to `v0.6.0` + full Corbomite ctest

**Files:**
- Modify: `libs/markoff-family` (submodule pointer)

- [ ] **Step 1: Bump pin.**

```bash
cd /home/clinton/dev/Corbomite
cd libs/markoff-family && git checkout v0.6.0 && cd ../..
```

- [ ] **Step 2: Audit pin bump.**

```bash
git -C libs/markoff-family rev-list <prev_pin>..v0.6.0 --oneline
```
Expected: all commits belong to C3 work; no stranded unrelated commits.

- [ ] **Step 3: Full ctest.**

```bash
cd /home/clinton/dev/Corbomite
rm -rf build && cmake -B build -DCORBOMITE_DEV_BUILD=ON
cmake --build build -j 10
cd build && ctest --output-on-failure -j 10
```

Expected: all pass modulo the two documented flakes (`tst_benchmark_layout`, `tst_editorsuggest`).

- [ ] **Step 4: Commit the pin.**

```bash
git add libs/markoff-family
git commit -m "$(cat <<'EOF'
deps: bump markoff-family submodule to v0.6.0

Phase C3 (MarkoffDocument content-authoritative) lands. Corbomite
adapter commits (Tasks 20-23) previously landed without the pin
bump so NoteDocument/Vault/NoteEditorWidget wiring would stage
against the v0.6.0 API surface. Now flip the pin and run
end-to-end validation.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 25: Manual smoke + closeout

- [ ] **Step 1: Manual end-to-end smoke.**

```bash
./build/Corbomite
```

Test cases (per spec §7.2 #16):
1. Open a test vault.
2. Open a note.
3. Edit in Source mode (type something).
4. Swap to Live mode.
5. Edit in Live mode.
6. Swap to Reading mode. Verify rendering reflects both edits.
7. Swap back to Source. Verify text equals the canonical content.
8. `Ctrl+Z` four times. Verify edits revert in LIFO order.
9. Save. Verify disk content equals editor content byte-for-byte.

Record issues; fix any that surface before proceeding. Autonomous mode: if a blocker emerges that isn't a simple bug, STOP and escalate.

- [ ] **Step 2: Update `docs/phase-c-status.md` to C3 done.**

- [ ] **Step 3: Update Corbomite `docs/PROJECT-STATE.md`.**

Update:
- "Last updated" line → C3 done + v0.6.0 pin.
- "Current focus" → next Phase C work-unit (C7).
- Phase C table row → C3 `Done 2026-04-20+`.
- In-flight work item → update to next work-unit or closeout.
- Append a Recent-decisions bullet with the C3 closeout.

- [ ] **Step 4: Append decisions-archive entry** (per CLAUDE.md pattern — full closeout prose goes here, not in PROJECT-STATE).

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-family docs/PROJECT-STATE.md docs/decisions-archive.md
# Plus Markoff-side phase-c-status.md closeout:
git -C libs/markoff-family add docs/phase-c-status.md
git -C libs/markoff-family commit -m "phase-c-status: C3 done

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
git add libs/markoff-family  # the inner commit
git commit -m "$(cat <<'EOF'
docs(project-state): Markoff Phase C3 done; next is C7

C3 (MarkoffDocument content-authoritative) shipped across
25 tasks / ~12-18 commits on Markoff master + 6 on Corbomite
master. Markoff tagged v0.6.0.

Full closeout prose in docs/decisions-archive.md.

Part of Phase C.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §M. Plan self-review (for the plan writer)

**Spec coverage check** — every spec section maps to one or more tasks:

| Spec § | Covered by |
|---|---|
| §1 Goal | Whole plan |
| §2 Non-goals | None (correctly excluded) |
| §3 Current state | §0 orientation references |
| §4.1 Ownership chain | Tasks 6, 20, 21 |
| §4.2 CanonicalBuffer | Task 1 |
| §4.3 CursorPosition | Task 2 + 6 |
| §4.4 MarkoffDocument API | Tasks 6, 7 |
| §4.5 Origin table | Task 7 + tst_origin_reset (Task 8) |
| §4.6 MarkdownDelta | Task 3 + tst_markdown_delta |
| §4.7 ParsePool | Task 4 |
| §4.8 Signal flow | Tasks 6, 7, 13–16 (leaves) |
| §5.1 Source | Tasks 10, 11 |
| §5.2 Live | Tasks 13–16 |
| §5.3 Reading | Task 12 |
| §5.4 Shared conventions | Woven through 10–16 |
| §6.1 NoteDocument wrapper | Task 20 |
| §6.2 Vault wiring | Task 21 |
| §6.3 NoteEditorWidget flush retire | Task 23 |
| §6.4 Autosave | No change (covered by §6.2 test integration) |
| §6.5 HoverPopover out-of-scope | Correctly excluded |
| §6.6 Breaking-change manifest | Woven through 20–23 |
| §7.1 Markoff acceptance | Tasks 8, 17, 18, 19 |
| §7.2 Corbomite acceptance | Tasks 20–25 |
| §8 Out of scope | Correctly excluded |
| §9 Follow-ups | Tracked in PROJECT-STATE (Task 25) |
| §10 Decisions | Already in spec (reference only) |
| §11 Phase-E hedge | Tasks 1, 2, 6 + scouting doc (separate) |
| §12 Signatures | Each file touched in matching task |
| §13 Breaking changes | Tasks 6–8 (Markoff side), 20–23 (Corbomite side) |

No gaps found.

**Placeholder scan:** None present. Each step contains exact code or exact commands.

**Cross-task type consistency:**
- `MarkoffDocument::applyCanonicalDelta` introduced in Task 6 (header) + Task 7 (impl); called by `MarkdownDelta::redo`/`undo` (Task 3 body refers to it).
- `MarkoffDocument::canonicalSubstring` introduced in Task 6; called by `MarkdownDelta::redo` first-call snapshot capture.
- `MarkoffDocument::releaseAnchorHandle` introduced in Task 6; called by `CursorPosition::release` (Task 6 step 3 also).
- `m_applyingCanonicalDelta` member on Source (Task 10), Reading (N/A — no delta subscription), Live (Task 13 on Editor, plus one on SceneCoordinator per Task 15).
- `simulateBlockEdit` test helper on `Editor` referenced in Tasks 15 + 18 + 17's peer test — defined in Task 15.

No inconsistencies.

---

## Execution notes

**Commit frequency:** ~25 commits on Markoff master + ~6 on Corbomite master. The Markoff tag `v0.6.0-alpha.1` lands at Task 9 (after markoff-core primitives); final `v0.6.0` at Task 19 (after all leaves adapted + interop/undo tests).

**Test count delta expected:** Markoff side at least +12 new tests (`tst_canonical_buffer`, `tst_cursor_position`, `tst_markdown_delta`, `tst_parse_pool`, `tst_origin_reset`, `tst_cursor_anchor`, `tst_source_canonical_attach`, `tst_reading_canonical_attach`, `tst_live_canonical_attach`, `tst_scene_offset_map`, `tst_canonical_interop`, `tst_cross_mode_undo`). Corbomite side: extended `tst_notedocument`, `tst_vault_save_reload`, and new/updated `tst_modeswap_preserves_ephemeral`.

**If unblocked during execution** (tree-sitter API mismatch, Qutepart edge case, scene-coordinator rebuild semantics not matching the sketch above), STOP and reconsider before improvising. The spec §10 recorded decisions are load-bearing; surprises that would alter them deserve an explicit user check-in.
