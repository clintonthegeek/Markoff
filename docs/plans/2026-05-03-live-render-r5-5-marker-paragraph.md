# R5.5 — Marker-Paragraph Implementation Plan

> **2026-05-04 — CANCELLED by D-evolution pivot.** Tasks 1–17 are landed in tree (commits `a895817..5473e81` plus four post-Task-17 fixes). Task 18 (dogfood gate) is **cancelled, not paused** — Bug 3 lives inside the parser-vs-CRDT race window that D removes; investigating it further has no value. The marker-paragraph code in tree stays for now (deletion happens during D2 implementation when the per-block-CRDT structural layer replaces L4–L5). See `docs/handoff/2026-05-04-c-restoration-bookend-d-pivot.md`.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the v2 paragraph-hole abstraction (`LiveHoleLayer` + `LiveProxyBlockModel`) with the marker-paragraph design (`docs/specs/2026-05-03-marker-paragraph-design.md`). EOB-Enter and start-of-paragraph Enter insert `"\n\n​"` directly into the CRDT source; the parser produces a real paragraph block; the cursor lands via the existing parser-driven row pipeline; an atomic-bundled-edit on the user's first keystroke replaces the marker; a `MarkerScrubber` service strips any marker that leaks at three event points (focus-out, pre-save, post-load).

**Architecture:** Single source of truth (CRDT only). The QML `ListView` binds directly to the parser-pure `LiveBlockModel`. New library code: a tiny `Marker.h` constants header and a `MarkerScrubber` (header + ~120 LOC source). Modified: `LiveStructuralKeyHandler` (drop hole branches; add marker insertion + stacked-Enter no-op), `LiveEditBinding` (atomic-bundled-edit on first edit into a marker block; drop hole-routing), `LiveCursorState` (drop hole-aware paths), `LiveListModelBinding` (drop holeLayer/proxyModel; wire MarkerScrubber), `UndoCoalescer` (single regime). Deleted: `LiveHoleLayer.{h,cpp}`, `LiveProxyBlockModel.{h,cpp}`, `BlockHole.h`. The R5 mid-block-Enter, soft-Enter, and (modulo a marker-aware case) backspace-merge paths are unchanged.

**Tech Stack:** C++20, Qt 6.8 (Core, Quick, Qml, Test, Widgets), `Markoff::MarkoffDocument` (`applyLocalEdit / undo / redo / coalesceLastUndo / textAnchorAt / blockByteRange / documentReloaded`), `Markoff::Document::topLevelBlocks` for parser-side checks, `LiveBlockModel`/`LiveCursorState`/`LiveStructuralKeyHandler` per the C-restoration spec, the existing `LiveRealisticInputHarness` for async-UX assertions, `Markoff::Document::fromMarkdown` for parser-only unit tests.

**Working environment.** All work happens inside `.worktrees/foundation-exploration/`. The build dir is `build-dev`. The fast inner-loop test command is `ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8`. The full configure-then-build is `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build-dev -j 8`. Cap parallelism at `-j 8` (project policy).

**Conventions.** Each task ends with `cmake --build build-dev -j 8 && ctest --test-dir build-dev -R '<scoped pattern>' --output-on-failure -j 8` and a commit. Commit messages: `r5.5(marker): <task summary>` for new code, `r5.5(marker): retire <thing>` for deletions, `r5.5(marker): test <thing>` for test-only commits.

---

## Spec coverage map

| Spec section | Plan task(s) |
|--------------|--------------|
| §3 marker constant | Task 1 |
| §4 source-edit contract | Tasks 6, 7 |
| §5 atomic-bundled-edit | Task 5 |
| §6 MarkerScrubber service | Tasks 2, 3, 8 |
| §7 cursor delivery (initial qtPos rule) | Task 4 |
| §8 LiveStructuralKeyHandler integration | Tasks 6, 7, 9 |
| §9 LiveEditBinding integration | Tasks 5, 8 (focus-out wiring) |
| §10 LiveSelectionView clipboard scrub | Task 10 |
| §11 UndoCoalescer single regime | Task 11 |
| §12 BlockId revert | Task 12 |
| §13 test plan harness rows | Task 16 |
| §14 spec amendments | Task 17 |
| §16 acceptance criteria — file deletions | Tasks 13, 14, 15 |
| §16 acceptance criteria — dogfood gate | Task 18 |
| §17 open question 1 (soft-Enter predicate) | Task 2 (predicate matches `^[​\n]+$`) |
| §17 open question 4 (IME granularity) | Task 5 (use post-composing-end commit) |
| §17 open question 5 (setRowEditSequence) | Task 6 |

---

## File structure

| File | Status | Responsibility |
|------|--------|----------------|
| `libs/markoff-live/include/markoff/live-render/Marker.h` | **Create** | Marker constants — `kMarkerChar`, `kMarkerUtf8`, `kMarkerUtf8Len`. |
| `libs/markoff-live/include/markoff/live-render/MarkerScrubber.h` | **Create** | `MarkerScrubber` API: predicate + 3 entry points. |
| `libs/markoff-live/src/MarkerScrubber.cpp` | **Create** | Implementation. |
| `libs/markoff-live/tests/tst_live_render_marker.cpp` | **Create** | Unit tests for the constants and the predicate. |
| `libs/markoff-live/tests/tst_live_render_marker_scrubber.cpp` | **Create** | Unit tests for `MarkerScrubber`. |
| `libs/markoff-live/tests/tst_live_render_marker_flow.cpp` | **Create** | Harness-driven end-to-end tests (race / save / load / undo / stacked-Enter / backspace-merge). |
| `libs/markoff-parser/tests/tst_document_top_level_blocks.cpp` | Modify | Add `markerProducesParagraph` and `markerRunProducesMultiple` test slots (promote spike findings to permanent unit tests). |
| `libs/markoff-live/include/markoff/live-render/Cursor.h` | Modify | Revert `BlockId` to `Markoff::BlockAnchor`. |
| `libs/markoff-live/include/markoff/live-render/LiveStructuralKeyHandler.h` | Modify | Drop `holeLayer` / `proxyModel` ctor parameters; drop hole-row dispatch helpers; add `MarkerScrubber*` member. |
| `libs/markoff-live/src/LiveStructuralKeyHandler.cpp` | Modify | Replace EOB-Enter / start-of-block-Enter branches with marker insertion; add stacked-Enter no-op rule; add marker-aware backspace-merge case; drop hole-row dispatch. |
| `libs/markoff-live/include/markoff/live-render/LiveEditBinding.h` | Modify | Drop `holeId` Q_PROPERTY and friend grants for hole tests; add `m_pendingMarkerScrub` flag and `markerScrubber` setter. |
| `libs/markoff-live/src/LiveEditBinding.cpp` | Modify | First-edit bundling on focus-in to a marker block; drop hole-routing in `onContentsChange`. |
| `libs/markoff-live/include/markoff/live-render/LiveCursorState.h` | Modify | Drop `focusedHoleId` Q_PROPERTY (and its definition). |
| `libs/markoff-live/src/LiveCursorState.cpp` | Modify | Drop hole-aware code paths. |
| `libs/markoff-live/include/markoff/live-render/UndoCoalescer.h` | Modify | Drop `holeLayer` ctor parameter; drop hole-routing in `undo()` / `redo()`. |
| `libs/markoff-live/src/UndoCoalescer.cpp` | Modify | Implementation drop. |
| `libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h` | Modify | Drop `holeLayer` / `proxyModel` properties; add `markerScrubber` property; rewire `LiveCursorState::setSignalModel` to use the inner `LiveBlockModel` directly. |
| `libs/markoff-live/src/LiveListModelBinding.cpp` | Modify | Construct `MarkerScrubber`; wire `documentReloaded` → `scrubAfterLoad`; remove `flushPendingHoles`. |
| `libs/markoff-live/qml/LiveView.qml` | Modify | Drop `proxyModel` references; bind `ListView.model` directly to `binding.model`. |
| `libs/markoff-live/qml/delegates/ParagraphDelegate.qml` | Modify | Drop `isHole` / `bufferText` / `holeId` branches; bind to `model.text` only. |
| `libs/markoff-live/include/markoff/live-render/LiveHoleLayer.h` | **Delete** | — |
| `libs/markoff-live/src/LiveHoleLayer.cpp` | **Delete** | — |
| `libs/markoff-live/include/markoff/live-render/LiveProxyBlockModel.h` | **Delete** | — |
| `libs/markoff-live/src/LiveProxyBlockModel.cpp` | **Delete** | — |
| `libs/markoff-live/include/markoff/live-render/BlockHole.h` | **Delete** | — |
| `libs/markoff-live/tests/tst_live_render_holes_layer.cpp` | **Delete** | — |
| `libs/markoff-live/tests/tst_live_render_holes_qml.cpp` | **Delete** | — |
| `libs/markoff-live/tests/tst_live_render_proxy_model.cpp` | **Delete** | — |
| `libs/markoff-live/tests/CMakeLists.txt` | Modify | Add three new test executables; remove three deleted ones. |
| `libs/markoff-live/CMakeLists.txt` | Modify | Add `MarkerScrubber.cpp` to sources; remove `LiveHoleLayer.cpp` and `LiveProxyBlockModel.cpp`. |
| `libs/markoff-live/src/LiveSelectionView.cpp` | Modify | Strip ZWSP from clipboard bytes in `serializeForCopy`. |
| `docs/specs/2026-05-02-live-render-restoration-design.md` | Modify | Apply spec amendments per design §14. |
| `docs/restoration-status.md` | Modify | Update phase board to point at the marker design + this plan; archive references. |

---

## Phase 1 — Foundation pieces (no integration yet)

These tasks add the new code beside the existing v2 hole code without changing any caller. The build stays green at every commit; the new code is unused but tested.

### Task 1: Marker constants header + parser-level test

**Files:**
- Create: `libs/markoff-live/include/markoff/live-render/Marker.h`
- Modify: `libs/markoff-parser/tests/tst_document_top_level_blocks.cpp`

- [ ] **Step 1: Write the failing parser-level test**

Append two slots to `tst_document_top_level_blocks.cpp`. First, declare them in the `private Q_SLOTS:` block:

```cpp
    void markerProducesParagraph();
    void markerRunProducesMultiple();
```

Then add the implementations near the bottom of the file:

```cpp
void TestDocumentTopLevelBlocks::markerProducesParagraph()
{
    // U+200B ZWSP at end of "hello\n\n" must produce a 2-block parse:
    // paragraph "hello", paragraph "<ZWSP>". This contract is what the
    // marker-paragraph design relies on (spec §3, premise M2).
    const QString src = QStringLiteral("hello\n\n") + QChar(0x200B);
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].kind, Kind::Paragraph);
    QCOMPARE(blocks[1].kind, Kind::Paragraph);
    QCOMPARE(blocks[1].byteEnd - blocks[1].byteStart, 3); // ZWSP is 3 UTF-8 bytes
}

void TestDocumentTopLevelBlocks::markerRunProducesMultiple()
{
    // Two consecutive marker-only paragraphs (separated by \n\n) parse
    // as two distinct paragraph blocks. The MarkerScrubber's run-collapse
    // mode (premise M6) targets exactly this shape.
    const QString src = QStringLiteral("hello\n\n")
                       + QChar(0x200B) + QStringLiteral("\n\n")
                       + QChar(0x200B);
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[1].kind, Kind::Paragraph);
    QCOMPARE(blocks[2].kind, Kind::Paragraph);
}
```

- [ ] **Step 2: Run the test to verify it fails-to-compile or fails-to-link**

```bash
cmake --build build-dev --target tst_markoff_parser_document_top_level_blocks -j 8 \
  && ctest --test-dir build-dev -R '^tst_markoff_parser_document_top_level_blocks$' --output-on-failure
```

Expected: build OK (these are pure assertions; the parser already supports them — they're contract tests). The two new tests should PASS immediately. If they fail, the spike's premise about tree-sitter accepting ZWSP is wrong; STOP and re-evaluate.

- [ ] **Step 3: Create the marker constants header**

```cpp
// libs/markoff-live/include/markoff/live-render/Marker.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QChar>
#include <QString>

namespace Markoff::LiveRender {

/// The marker character used by the marker-paragraph design
/// (`docs/specs/2026-05-03-marker-paragraph-design.md` §3).
/// U+200B ZERO WIDTH SPACE — invisible in every renderer; produces a
/// real paragraph block when present in source.
constexpr QChar       kMarkerChar    = QChar(0x200B);

/// UTF-8 encoding of `kMarkerChar`. Used when emitting `MarkoffEdit`s
/// (which carry `QByteArray newText` in UTF-8 byte coordinates).
constexpr const char *kMarkerUtf8    = "\xE2\x80\x8B";

/// Length of `kMarkerUtf8` in bytes. Compile-time constant for use in
/// `MarkoffEdit::oldEnd - oldStart` arithmetic.
constexpr int         kMarkerUtf8Len = 3;

}  // namespace Markoff::LiveRender
```

- [ ] **Step 4: Verify the header builds**

```bash
cmake --build build-dev --target markoff_live_render -j 8
```

Expected: builds OK (header is unused but valid).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live-render/Marker.h \
        libs/markoff-parser/tests/tst_document_top_level_blocks.cpp
git commit -m "$(cat <<'EOF'
r5.5(marker): land marker constants header + parser-acceptance contract tests

Adds Marker.h with kMarkerChar / kMarkerUtf8 / kMarkerUtf8Len, and two
parser-level tests promoting the §3.1 spike findings to permanent
contracts: ZWSP after "\n\n" produces a paragraph block; consecutive
marker-only paragraphs produce distinct paragraph blocks.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: `MarkerScrubber` predicate

**Files:**
- Create: `libs/markoff-live/include/markoff/live-render/MarkerScrubber.h`
- Create: `libs/markoff-live/src/MarkerScrubber.cpp`
- Create: `libs/markoff-live/tests/tst_live_render_marker.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test for the predicate**

```cpp
// libs/markoff-live/tests/tst_live_render_marker.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live-render/Marker.h>
#include <markoff/live-render/MarkerScrubber.h>

using namespace Markoff::LiveRender;

class TstMarker : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void constants();
    void predicate_singleMarker_returnsTrue();
    void predicate_markerRun_returnsTrue();
    void predicate_markerWithSoftBreaks_returnsTrue();
    void predicate_markerWithContent_returnsFalse();
    void predicate_emptyString_returnsFalse();
    void predicate_plainContent_returnsFalse();
};

void TstMarker::constants() {
    QCOMPARE(kMarkerChar.unicode(), quint16(0x200B));
    QCOMPARE(QString::fromUtf8(kMarkerUtf8), QString(kMarkerChar));
    QCOMPARE(kMarkerUtf8Len, 3);
}

void TstMarker::predicate_singleMarker_returnsTrue() {
    QVERIFY(MarkerScrubber::isMarkerOnly(QString(kMarkerChar)));
}

void TstMarker::predicate_markerRun_returnsTrue() {
    QString s; s.append(kMarkerChar); s.append(kMarkerChar);
    QVERIFY(MarkerScrubber::isMarkerOnly(s));
}

void TstMarker::predicate_markerWithSoftBreaks_returnsTrue() {
    // Spec §17 open question 1: predicate matches markers + soft-break
    // newlines so a marker paragraph that has been Shift-Enter'd into
    // multiple lines is still recognised as marker-only.
    QString s; s.append(kMarkerChar); s.append('\n'); s.append(kMarkerChar);
    QVERIFY(MarkerScrubber::isMarkerOnly(s));
}

void TstMarker::predicate_markerWithContent_returnsFalse() {
    QString s; s.append(kMarkerChar); s.append('x');
    QVERIFY(!MarkerScrubber::isMarkerOnly(s));
}

void TstMarker::predicate_emptyString_returnsFalse() {
    QVERIFY(!MarkerScrubber::isMarkerOnly(QString()));
}

void TstMarker::predicate_plainContent_returnsFalse() {
    QVERIFY(!MarkerScrubber::isMarkerOnly(QStringLiteral("hello")));
}

QTEST_APPLESS_MAIN(TstMarker)
#include "tst_live_render_marker.moc"
```

- [ ] **Step 2: Add the test executable to `tests/CMakeLists.txt`**

Append after the existing test entries:

```cmake
qt_add_executable(tst_live_render_marker
    tst_live_render_marker.cpp
)
target_link_libraries(tst_live_render_marker PRIVATE
    Qt6::Core Qt6::Test markoff_live_render)
add_test(NAME tst_live_render_marker COMMAND tst_live_render_marker)
```

- [ ] **Step 3: Run the test to verify it fails to compile**

```bash
cmake -S . -B build-dev && cmake --build build-dev --target tst_live_render_marker -j 8 2>&1 | tail -20
```

Expected: FAIL with `MarkerScrubber.h: No such file or directory`.

- [ ] **Step 4: Create the header**

```cpp
// libs/markoff-live/include/markoff/live-render/MarkerScrubber.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/Marker.h>

#include <QObject>
#include <QPointer>
#include <QString>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::LiveRender {

class LiveBlockModel;

/// Marker-paragraph design (`docs/specs/2026-05-03-marker-paragraph-design.md` §6).
/// Removes leaked U+200B markers from source at three deterministic events:
/// focus-out from a marker-only paragraph, pre-save flush, post-load cleanup.
class MARKOFF_LIVE_RENDER_EXPORT MarkerScrubber : public QObject {
    Q_OBJECT
public:
    explicit MarkerScrubber(Markoff::MarkoffDocument *doc,
                            LiveBlockModel           *model,
                            QObject                  *parent = nullptr);

    /// True iff `text` consists exclusively of marker characters and
    /// soft-break newlines (and is non-empty). Spec §6.2 + §17 open
    /// question 1.
    static bool isMarkerOnly(const QString &text);

    /// Called by LiveEditBinding when focus leaves a paragraph whose
    /// content currently matches `isMarkerOnly`. Emits one
    /// `applyLocalEdit` removing the marker paragraph + its leading
    /// `\n\n` separator. No-op if `blockIndex` is out of range or the
    /// block is no longer marker-only.
    void scrubOnFocusOut(int blockIndex);

    /// Called by the host's save handler before serializing bytes.
    /// Walks all paragraph rows; collects every marker-only paragraph
    /// (or run of them); applies one batched `applyLocalEdit` removing
    /// them. Returns the number of marker bytes removed.
    int scrubBeforeSave();

    /// Called from `MarkoffDocument::documentReloaded`. Same logic as
    /// `scrubBeforeSave` but for the just-loaded document. Defends
    /// against marker-bearing files written by other tools.
    int scrubAfterLoad();

private:
    QPointer<Markoff::MarkoffDocument> m_doc;
    QPointer<LiveBlockModel>           m_model;
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 5: Create the implementation (predicate only; other methods are stubs for now)**

```cpp
// libs/markoff-live/src/MarkerScrubber.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/MarkerScrubber.h>

#include <markoff/live-render/LiveBlockModel.h>
#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::LiveRender {

MarkerScrubber::MarkerScrubber(Markoff::MarkoffDocument *doc,
                                LiveBlockModel           *model,
                                QObject                  *parent)
    : QObject(parent), m_doc(doc), m_model(model)
{}

bool MarkerScrubber::isMarkerOnly(const QString &text) {
    if (text.isEmpty()) return false;
    for (QChar c : text) {
        if (c == kMarkerChar) continue;
        if (c == QChar('\n')) continue;  // soft-break, per spec §17 q1
        return false;
    }
    return true;
}

void MarkerScrubber::scrubOnFocusOut(int /*blockIndex*/) {
    // Implemented in Task 3.
}

int MarkerScrubber::scrubBeforeSave() {
    // Implemented in Task 3.
    return 0;
}

int MarkerScrubber::scrubAfterLoad() {
    // Implemented in Task 3.
    return 0;
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 6: Add `MarkerScrubber.cpp` to the library `CMakeLists.txt`**

In `libs/markoff-live/CMakeLists.txt`, find the source list (probably under `qt_add_library(markoff_live_render ...)`) and add `src/MarkerScrubber.cpp` next to the other entries (preserve alphabetic order around `LiveListModelBinding.cpp` and `LiveProxyBlockModel.cpp`).

- [ ] **Step 7: Run the test to verify it passes**

```bash
cmake --build build-dev --target tst_live_render_marker -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_marker$' --output-on-failure
```

Expected: 7 PASS, 0 FAIL.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-live/include/markoff/live-render/MarkerScrubber.h \
        libs/markoff-live/src/MarkerScrubber.cpp \
        libs/markoff-live/tests/tst_live_render_marker.cpp \
        libs/markoff-live/CMakeLists.txt \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
r5.5(marker): MarkerScrubber predicate + skeleton

isMarkerOnly() returns true for non-empty strings consisting only of
U+200B and soft-break newlines (per spec §6.2 + §17 q1). The three
public methods are stubbed; Task 3 fills them in.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: `MarkerScrubber` edit-emission methods

**Files:**
- Modify: `libs/markoff-live/src/MarkerScrubber.cpp`
- Create: `libs/markoff-live/tests/tst_live_render_marker_scrubber.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests for the three entry points**

```cpp
// libs/markoff-live/tests/tst_live_render_marker_scrubber.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live-render/Marker.h>
#include <markoff/live-render/MarkerScrubber.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff-foundation/MarkoffDocument.h>

using namespace Markoff;
using namespace Markoff::LiveRender;

class TstMarkerScrubber : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void scrubOnFocusOut_singleMarkerOnly_removesParagraphAndSeparator();
    void scrubOnFocusOut_blockNoLongerMarkerOnly_isNoOp();
    void scrubBeforeSave_runOfMarkers_collapsesAll();
    void scrubBeforeSave_noMarkers_returnsZero();
    void scrubAfterLoad_markersInLoadedSource_areRemoved();
private:
    static QString sourceOf(MarkoffDocument *doc);
};

QString TstMarkerScrubber::sourceOf(MarkoffDocument *doc) {
    return QString::fromUtf8(doc->toMarkdownUtf8());
}

void TstMarkerScrubber::scrubOnFocusOut_singleMarkerOnly_removesParagraphAndSeparator() {
    MarkoffDocument doc;
    BlockKindRegistry registry;
    LiveBlockModel model(&doc, &registry);
    doc.resetContent(QStringLiteral("alpha\n\n%1\n").arg(kMarkerChar).toUtf8());
    QTRY_COMPARE(model.rowCount(), 2);

    MarkerScrubber scrubber(&doc, &model);
    scrubber.scrubOnFocusOut(/*blockIndex=*/1);
    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(sourceOf(&doc), QStringLiteral("alpha\n"));
}

void TstMarkerScrubber::scrubOnFocusOut_blockNoLongerMarkerOnly_isNoOp() {
    MarkoffDocument doc;
    BlockKindRegistry registry;
    LiveBlockModel model(&doc, &registry);
    doc.resetContent(QStringLiteral("alpha\n\nbeta\n").toUtf8());
    QTRY_COMPARE(model.rowCount(), 2);

    MarkerScrubber scrubber(&doc, &model);
    scrubber.scrubOnFocusOut(/*blockIndex=*/1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(sourceOf(&doc), QStringLiteral("alpha\n\nbeta\n"));
}

void TstMarkerScrubber::scrubBeforeSave_runOfMarkers_collapsesAll() {
    MarkoffDocument doc;
    BlockKindRegistry registry;
    LiveBlockModel model(&doc, &registry);
    doc.resetContent(QStringLiteral("alpha\n\n%1\n\n%1\n\nbeta\n")
                       .arg(kMarkerChar).toUtf8());
    QTRY_COMPARE(model.rowCount(), 4);

    MarkerScrubber scrubber(&doc, &model);
    int removed = scrubber.scrubBeforeSave();
    QVERIFY(removed > 0);
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(sourceOf(&doc), QStringLiteral("alpha\n\nbeta\n"));
}

void TstMarkerScrubber::scrubBeforeSave_noMarkers_returnsZero() {
    MarkoffDocument doc;
    BlockKindRegistry registry;
    LiveBlockModel model(&doc, &registry);
    doc.resetContent(QStringLiteral("alpha\n\nbeta\n").toUtf8());
    QTRY_COMPARE(model.rowCount(), 2);

    MarkerScrubber scrubber(&doc, &model);
    QCOMPARE(scrubber.scrubBeforeSave(), 0);
    QCOMPARE(model.rowCount(), 2);
}

void TstMarkerScrubber::scrubAfterLoad_markersInLoadedSource_areRemoved() {
    MarkoffDocument doc;
    BlockKindRegistry registry;
    LiveBlockModel model(&doc, &registry);
    MarkerScrubber scrubber(&doc, &model);

    // Simulate a load: a file written by another tool that has a marker.
    doc.resetContent(QStringLiteral("alpha\n\n%1\nbeta\n").arg(kMarkerChar).toUtf8());
    QTRY_VERIFY(model.rowCount() >= 1);

    int removed = scrubber.scrubAfterLoad();
    QVERIFY(removed > 0);
    QVERIFY(!sourceOf(&doc).contains(kMarkerChar));
}

QTEST_MAIN(TstMarkerScrubber)
#include "tst_live_render_marker_scrubber.moc"
```

- [ ] **Step 2: Add the test executable to `tests/CMakeLists.txt`**

```cmake
qt_add_executable(tst_live_render_marker_scrubber
    tst_live_render_marker_scrubber.cpp
)
target_link_libraries(tst_live_render_marker_scrubber PRIVATE
    Qt6::Core Qt6::Test markoff_live_render markoff_core)
add_test(NAME tst_live_render_marker_scrubber COMMAND tst_live_render_marker_scrubber)
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cmake --build build-dev --target tst_live_render_marker_scrubber -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_marker_scrubber$' --output-on-failure
```

Expected: 5 FAIL (the stub methods do nothing).

- [ ] **Step 4: Implement the three methods**

Replace the stub bodies in `MarkerScrubber.cpp` with:

```cpp
namespace {

/// Returns the byte range [start, end) of the marker paragraph at
/// `blockIndex` PLUS its leading "\n\n" separator (or just preceding
/// "\n" if it's the first paragraph). Returns std::nullopt if the row
/// is not marker-only at the moment of the call.
struct ScrubRange { quint32 start; quint32 end; };

std::optional<ScrubRange> markerScrubRangeFor(int blockIndex,
                                                Markoff::LiveRender::LiveBlockModel *model,
                                                Markoff::MarkoffDocument *doc) {
    if (!model || !doc) return std::nullopt;
    if (blockIndex < 0 || blockIndex >= model->rowCount()) return std::nullopt;
    const auto &rec = model->recordAt(blockIndex);
    if (!Markoff::LiveRender::MarkerScrubber::isMarkerOnly(rec.text))
        return std::nullopt;
    auto range = doc->blockByteRange(rec.blockAnchor);
    if (!range) return std::nullopt;
    quint32 start = range->first;
    quint32 end   = range->second;
    // Extend `start` backwards by up to 2 bytes to absorb the leading
    // "\n\n" separator. The first paragraph in the document has only one
    // (or zero) preceding newlines.
    quint32 desiredAbsorb = (start >= 2) ? 2 : start;
    start -= desiredAbsorb;
    return ScrubRange{ start, end };
}

}  // namespace

void Markoff::LiveRender::MarkerScrubber::scrubOnFocusOut(int blockIndex) {
    if (!m_doc || !m_model) return;
    auto range = markerScrubRangeFor(blockIndex, m_model, m_doc);
    if (!range) return;
    Markoff::MarkoffEdit ed;
    ed.oldStart = range->start;
    ed.oldEnd   = range->end;
    ed.newText  = QByteArray();
    m_doc->applyLocalEdit({ ed });
}

int Markoff::LiveRender::MarkerScrubber::scrubBeforeSave() {
    if (!m_doc || !m_model) return 0;
    // Walk paragraphs in REVERSE order so each scrub edit's byte
    // arithmetic doesn't shift the indices of paragraphs we haven't
    // visited yet. (Forward order would require recomputing every
    // subsequent block's range after each edit.)
    int totalRemoved = 0;
    for (int i = m_model->rowCount() - 1; i >= 0; --i) {
        auto range = markerScrubRangeFor(i, m_model, m_doc);
        if (!range) continue;
        Markoff::MarkoffEdit ed;
        ed.oldStart = range->start;
        ed.oldEnd   = range->end;
        ed.newText  = QByteArray();
        m_doc->applyLocalEdit({ ed });
        totalRemoved += int(range->end - range->start);
    }
    return totalRemoved;
}

int Markoff::LiveRender::MarkerScrubber::scrubAfterLoad() {
    return scrubBeforeSave();
}
```

Add `#include <optional>` to the top of `MarkerScrubber.cpp` and `#include <markoff-foundation/MarkoffEdit.h>` if not already present.

- [ ] **Step 5: Run the test to verify it passes**

```bash
cmake --build build-dev --target tst_live_render_marker_scrubber -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_marker_scrubber$' --output-on-failure
```

Expected: 5 PASS, 0 FAIL.

If `scrubBeforeSave_runOfMarkers_collapsesAll` fails because reverse iteration mis-counts the leading-`\n\n` absorption when two markers are adjacent (the second's `oldStart` overlaps the first's `oldEnd`), inspect actual edits emitted with `qInfo()` and adjust the absorption to be capped at the previous block's `endByte`. The contract: after the batched edits, the document text contains no marker bytes and no doubled blank-line runs.

- [ ] **Step 6: Run the full live-render test suite to confirm no regression**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: every previously-passing test still passes; the two new tests pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/src/MarkerScrubber.cpp \
        libs/markoff-live/tests/tst_live_render_marker_scrubber.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
r5.5(marker): MarkerScrubber edit-emission methods

scrubOnFocusOut emits one applyLocalEdit removing the marker paragraph
plus its leading separator. scrubBeforeSave/scrubAfterLoad walk in
reverse and emit per-block edits, returning the byte count removed.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: `LiveCursorState` initial-qtPos delivery (no new public API)

The spec §7.1 says cursor delivery uses `requestTextCaretAtNewRow(row, /*qtPos=*/0)`. The existing API already supports this; the *caller* (LiveStructuralKeyHandler) does the qtPos=0 part. So no `LiveCursorState` change is needed for the marker design's primary path. This task is *only* about confirming the existing API behaves correctly when the target row's text starts with the marker.

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_cursor.cpp`

- [ ] **Step 1: Add a regression test verifying qtPos=0 lands the cursor before the marker**

Append a slot to `tst_live_render_cursor.cpp` (declare it in `private Q_SLOTS:`):

```cpp
    void requestTextCaretAtNewRow_markerParagraph_landsAtQtPos0();
```

Implementation:

```cpp
void TstLiveRenderCursor::requestTextCaretAtNewRow_markerParagraph_landsAtQtPos0()
{
    using namespace Markoff;
    using namespace Markoff::LiveRender;
    MarkoffDocument doc;
    BlockKindRegistry registry;
    LiveBlockModel model(&doc, &registry);
    LiveCursorState cs(&registry, &model);

    // Initial: one paragraph; cursor at end of it.
    doc.resetContent(QByteArrayLiteral("alpha\n"));
    QTRY_COMPARE(model.rowCount(), 1);

    // Schedule a pending request for "the row that's about to be born".
    cs.requestTextCaretAtNewRow(/*expectedRow=*/1, /*qtPos=*/0);

    // Insert "\n\n<ZWSP>" at end of "alpha". The new row arrives
    // asynchronously via parse-back; the pending request resolves on
    // its rowsInserted.
    MarkoffEdit ed;
    ed.oldStart = 5; ed.oldEnd = 5;
    ed.newText  = QByteArrayLiteral("\n\n\xE2\x80\x8B");
    doc.applyLocalEdit({ ed });

    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(cs.focusedAnchorRow(), 1);
    QCOMPARE(cs.focusedQtPos(), 0);
}
```

- [ ] **Step 2: Run the test**

```bash
cmake --build build-dev --target tst_live_render_cursor -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_cursor$' --output-on-failure
```

Expected: PASS. (The existing `requestTextCaretAtNewRow` already supports this; the test is a *contract* test, not a behavior change.) If it fails, the existing pending-resolution mechanism doesn't survive ZWSP-bearing rows; investigate before continuing.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_cursor.cpp
git commit -m "$(cat <<'EOF'
r5.5(marker): contract test for cursor delivery into a marker paragraph

requestTextCaretAtNewRow(row, qtPos=0) resolves on the marker
paragraph's rowsInserted with the cursor at byte 0 (before the marker).
This is the cursor-side leg of the marker design's EOB-Enter flow.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: `LiveEditBinding` atomic-bundled-edit primitive

**Files:**
- Modify: `libs/markoff-live/include/markoff/live-render/LiveEditBinding.h`
- Modify: `libs/markoff-live/src/LiveEditBinding.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_paragraph_edit.cpp`

- [ ] **Step 1: Add a failing test for the bundling behaviour**

In `tst_live_render_paragraph_edit.cpp`, declare a new slot:

```cpp
    void firstEdit_intoMarkerOnlyBlock_bundlesScrubAndInsert();
```

Implementation:

```cpp
void TstLiveRenderParagraphEdit::firstEdit_intoMarkerOnlyBlock_bundlesScrubAndInsert()
{
    using namespace Markoff;
    using namespace Markoff::LiveRender;
    MarkoffDocument doc;
    BlockKindRegistry registry;
    LiveBlockModel model(&doc, &registry);
    LiveCursorState cs(&registry, &model);
    LiveListModelBinding binding;
    binding.setDocument(&doc);

    LiveEditBinding eb;
    eb.setBinding(&binding);
    QTextDocument td;
    eb.setRawTextDocument(&td);

    // Set up: a marker-only paragraph at row 1.
    doc.resetContent(QStringLiteral("alpha\n\n%1\n").arg(kMarkerChar).toUtf8());
    QTRY_COMPARE(model.rowCount(), 2);

    eb.setModelIndex(1);
    eb.setText(QString(kMarkerChar));               // simulate model.text binding
    QCOMPARE(td.toPlainText(), QString(kMarkerChar));

    // Sanity: pre-edit doc has the marker.
    QVERIFY(QString::fromUtf8(doc.toMarkdownUtf8()).contains(kMarkerChar));

    const quint64 preEditSeq = doc.editSequence();

    // Simulate the user typing 'x' at qtPos 0 (before the marker).
    QTextCursor cur(&td);
    cur.setPosition(0);
    cur.insertText(QStringLiteral("x"));

    // After the bundling: source paragraph contains "x" only (no marker),
    // and exactly ONE editSequence bump occurred (one batched edit, not two).
    QTRY_VERIFY(!QString::fromUtf8(doc.toMarkdownUtf8()).contains(kMarkerChar));
    QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()),
             QStringLiteral("alpha\n\nx\n"));
    QCOMPARE(doc.editSequence(), preEditSeq + 1);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build-dev --target tst_live_render_paragraph_edit -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_paragraph_edit$' --output-on-failure
```

Expected: FAIL — without bundling, `editSequence` bumps twice (insert + scrub) and the source briefly contains both `x` and the marker, then ends with `x` only after the post-edit scrub fires asynchronously, OR contains `x<ZWSP>` if no scrub fires.

- [ ] **Step 3: Add the bundling state to `LiveEditBinding`**

In `LiveEditBinding.h`, in the `private:` section, add:

```cpp
    bool m_pendingMarkerScrub = false;
```

In `LiveEditBinding.cpp`, in `setText` (or wherever the model→TextDocument push happens), after the push completes:

```cpp
void LiveEditBinding::setText(const QString &t) {
    if (m_text == t) return;
    m_text = t;
    pushTextToDocument();
    // Marker-aware first-edit bundling: if the model just told us this
    // delegate's content is exactly the marker, mark the *next* user
    // edit as the one that should bundle the marker scrub.
    m_pendingMarkerScrub = MarkerScrubber::isMarkerOnly(t);
    Q_EMIT textChanged();
}
```

In `LiveEditBinding.cpp::onContentsChange`, BEFORE the existing applyLocalEdit branch (and after the early-returns for `applyingModelUpdate` and `composing`):

```cpp
    if (m_pendingMarkerScrub) {
        // Bundle: a single MarkoffEdit replaces the entire marker
        // content with the user's typed bytes. The qtPos / charsAdded
        // / charsRemoved arithmetic from QTextDocument is irrelevant
        // here — we know the entire pre-edit content was the marker
        // and the post-edit content is whatever the QTextDocument now
        // shows. Capture both, build the edit, apply, clear flag.
        m_pendingMarkerScrub = false;
        const QString postEditText = m_textDocument
            ? m_textDocument->textDocument()->toPlainText()
            : QString();
        // Block byte range from the foundation:
        const auto rec = m_binding->model()->recordAt(m_modelIndex);
        const auto range = m_binding->document()->blockByteRange(rec.blockAnchor);
        if (range) {
            Markoff::MarkoffEdit ed;
            ed.oldStart = range->first;
            ed.oldEnd   = range->second;
            ed.newText  = postEditText.toUtf8();
            m_binding->document()->applyLocalEdit({ ed });
            m_binding->model()->setRowEditSequence(
                m_modelIndex, m_binding->document()->editSequence());
        }
        return;
    }
```

Make sure to `#include <markoff/live-render/MarkerScrubber.h>` and `#include <markoff/live-render/Marker.h>` at the top of `LiveEditBinding.cpp` (the include for `MarkerScrubber.h` is for `isMarkerOnly`).

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build build-dev --target tst_live_render_paragraph_edit -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_paragraph_edit$' --output-on-failure
```

Expected: PASS. The full live-render suite should still pass:

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live-render/LiveEditBinding.h \
        libs/markoff-live/src/LiveEditBinding.cpp \
        libs/markoff-live/tests/tst_live_render_paragraph_edit.cpp
git commit -m "$(cat <<'EOF'
r5.5(marker): atomic-bundled-edit primitive in LiveEditBinding

When the model pushes a marker-only string into a delegate, the next
contentsChange becomes a single applyLocalEdit replacing the entire
block content (marker bytes → user-typed bytes). One CRDT op, one
parse-back, one undo entry — no race window between insert and scrub.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase 2 — Switch the EOB path

These tasks change the live behaviour. After Phase 2 commits land, the marker design is *active*; the v2 hole code is dead but still present (it gets deleted in Phase 3).

### Task 6: `LiveStructuralKeyHandler` EOB-Enter and start-of-block-Enter switch to marker insertion

**Files:**
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_structural.cpp`

- [ ] **Step 1: Add a failing test for the new EOB-Enter behaviour**

In `tst_live_render_structural.cpp`, declare:

```cpp
    void paragraphEnter_atEob_insertsMarkerEdit();
    void paragraphEnter_atStartOfBlock_insertsMarkerEdit();
```

Implementations:

```cpp
void TstLiveRenderStructural::paragraphEnter_atEob_insertsMarkerEdit()
{
    using namespace Markoff;
    using namespace Markoff::LiveRender;
    MarkoffDocument doc;
    BlockKindRegistry registry;
    LiveBlockModel model(&doc, &registry);
    LiveCursorState cs(&registry, &model);
    UndoCoalescer uc(&doc, &cs);
    LiveStructuralKeyHandler h(&doc, &model, &cs, &registry, &uc, nullptr);

    doc.resetContent(QByteArrayLiteral("alpha\n"));
    QTRY_COMPARE(model.rowCount(), 1);

    bool handled = h.tryHandle(Qt::Key_Return, /*mods=*/0,
                                /*blockIndex=*/0,
                                /*qtPos=*/5,
                                /*selectionEmpty=*/true,
                                /*blockText=*/QStringLiteral("alpha"));
    QVERIFY(handled);
    QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()),
             QStringLiteral("alpha\n\n%1\n").arg(kMarkerChar));
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(cs.focusedAnchorRow(), 1);
    QCOMPARE(cs.focusedQtPos(), 0);
}

void TstLiveRenderStructural::paragraphEnter_atStartOfBlock_insertsMarkerEdit()
{
    using namespace Markoff;
    using namespace Markoff::LiveRender;
    MarkoffDocument doc;
    BlockKindRegistry registry;
    LiveBlockModel model(&doc, &registry);
    LiveCursorState cs(&registry, &model);
    UndoCoalescer uc(&doc, &cs);
    LiveStructuralKeyHandler h(&doc, &model, &cs, &registry, &uc, nullptr);

    doc.resetContent(QByteArrayLiteral("alpha\n"));
    QTRY_COMPARE(model.rowCount(), 1);

    bool handled = h.tryHandle(Qt::Key_Return, /*mods=*/0,
                                /*blockIndex=*/0,
                                /*qtPos=*/0,
                                /*selectionEmpty=*/true,
                                /*blockText=*/QStringLiteral("alpha"));
    QVERIFY(handled);
    QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()),
             QStringLiteral("%1\n\nalpha\n").arg(kMarkerChar));
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(cs.focusedAnchorRow(), 0);
    QCOMPARE(cs.focusedQtPos(), 0);
}
```

Also adjust the `LiveStructuralKeyHandler` ctor call (and the test's `tryHandle` arg list if needed) to *drop* the `holeLayer` and `proxyModel` parameters — Task 6 changes the constructor signature. (See Step 3.)

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8 2>&1 | tail -30
```

Expected: build FAIL — the constructor signature still has `holeLayer` / `proxyModel`. After Step 3, it should compile and the test should fail because EOB-Enter still calls `createBlockHole`.

- [ ] **Step 3: Change the `LiveStructuralKeyHandler` constructor signature**

In `LiveStructuralKeyHandler.h`, change:

```cpp
    LiveStructuralKeyHandler(Markoff::MarkoffDocument *document,
                             LiveBlockModel           *model,
                             LiveCursorState          *cursorState,
                             const BlockKindRegistry  *registry,
                             UndoCoalescer            *undoCoalescer,
                             LiveHoleLayer            *holeLayer,
                             LiveProxyBlockModel      *proxyModel,
                             QObject                  *parent = nullptr);
```

to:

```cpp
    LiveStructuralKeyHandler(Markoff::MarkoffDocument *document,
                             LiveBlockModel           *model,
                             LiveCursorState          *cursorState,
                             const BlockKindRegistry  *registry,
                             UndoCoalescer            *undoCoalescer,
                             QObject                  *parent = nullptr);
```

Also drop `LiveHoleLayer *m_holeLayer;` and `LiveProxyBlockModel *m_proxyModel;` and the corresponding forward declarations + the `Ctx` struct's `holeLayer` / `proxyModel` / `proxyBlockIndex` fields. (`proxyBlockIndex` becomes the same as `blockIndex` because there is no proxy.)

In `LiveStructuralKeyHandler.cpp`, drop `handleHoleRow` and `routeFocusAfterAbandon`. They are not called from anywhere after this commit.

- [ ] **Step 4: Replace the EOB / start-of-block branches in `paragraphEnter`**

In `LiveStructuralKeyHandler.cpp`, find the EOB / start-of-block branch (currently around line 326 — `// EOB or start-of-block: create hole instead of source edit (R5.5 F5).`). Replace the entire branch from that comment through the `return HR::Handled;` with:

```cpp
        // EOB or start-of-block: insert marker paragraph and let the
        // existing parser-driven row pipeline deliver the new row.
        // Spec §4.1 / §4.2.
        const quint32 byteOffset = atStart ? c.currentBlockStart
                                            : c.currentBlockEnd;
        Markoff::MarkoffEdit ed;
        ed.oldStart = byteOffset;
        ed.oldEnd   = byteOffset;
        ed.newText  = QByteArrayLiteral("\n\n\xE2\x80\x8B");
        c.document->applyLocalEdit({ ed });
        c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());

        // Cursor goes to qtPos 0 of the new row. For start-of-block the
        // new row REPLACES the current block's index; for EOB it's
        // blockIndex+1.
        const int newRow = atStart ? c.blockIndex : (c.blockIndex + 1);
        c.cursorState->requestTextCaretAtNewRow(newRow, /*qtPos=*/0);

        if (c.undoCoalescer) c.undoCoalescer->recordStructural();
        return HR::Handled;
```

Drop the `#include`s for `LiveHoleLayer.h` and `LiveProxyBlockModel.h` from the `.cpp` if they were present.

- [ ] **Step 5: Update every caller of `LiveStructuralKeyHandler`'s constructor**

Search for the constructor call sites:

```bash
grep -rn "LiveStructuralKeyHandler(" libs/markoff-live
```

Update each one to drop the `holeLayer` and `proxyModel` arguments. The known sites are `LiveListModelBinding.cpp` and the existing tests `tst_live_render_structural.cpp` and possibly `tst_live_render_paragraph_edit.cpp`. Each call gets two fewer args.

- [ ] **Step 6: Build and run**

```bash
cmake --build build-dev --target markoff_live_render -j 8 \
  && cmake --build build-dev --target tst_live_render_structural -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: PASS for the two new tests AND every previously-passing test in `tst_live_render_structural`.

If the existing R5 mid-block-split test fails because `proxyBlockIndex` is gone, replace its references in the test fixtures with `blockIndex`.

If an existing v2-hole-related test in `tst_live_render_structural` fails (e.g. `paragraphEnter_atEob_createsHole`), DELETE that test slot — it asserted v2 behaviour that is now retired. Note in the commit message which tests were dropped.

- [ ] **Step 7: Run the full live-render suite**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: every test in the suite passes EXCEPT the v2-hole-specific test executables (`tst_live_render_holes_layer`, `tst_live_render_holes_qml`, `tst_live_render_proxy_model`) which Task 14 deletes. Those will likely fail to build at this point because they reference the now-unused `holeLayer`/`proxyModel` constructor args. **If they fail to build, that's expected** — they get retired in Task 14. To keep the inner loop green, temporarily comment out their `qt_add_executable` lines in `tests/CMakeLists.txt` (revert in Task 14).

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-live/include/markoff/live-render/LiveStructuralKeyHandler.h \
        libs/markoff-live/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/tests/tst_live_render_structural.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
r5.5(marker): EOB-Enter and start-of-block-Enter insert marker paragraph

Replaces the createBlockHole path in paragraphEnter with applyLocalEdit
of "\n\n<U+200B>" + a pending requestTextCaretAtNewRow. Drops the
LiveHoleLayer / LiveProxyBlockModel ctor parameters from
LiveStructuralKeyHandler. Drops handleHoleRow / routeFocusAfterAbandon.

The three v2 hole-test executables are temporarily commented out in
tests/CMakeLists.txt; Task 14 deletes them.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: Stacked-Enter no-op rule

**Files:**
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_structural.cpp`

- [ ] **Step 1: Add a failing test**

```cpp
void TstLiveRenderStructural::paragraphEnter_onMarkerOnlyBlock_isNoOp()
{
    using namespace Markoff;
    using namespace Markoff::LiveRender;
    MarkoffDocument doc;
    BlockKindRegistry registry;
    LiveBlockModel model(&doc, &registry);
    LiveCursorState cs(&registry, &model);
    UndoCoalescer uc(&doc, &cs);
    LiveStructuralKeyHandler h(&doc, &model, &cs, &registry, &uc);

    doc.resetContent(QStringLiteral("alpha\n\n%1\n").arg(kMarkerChar).toUtf8());
    QTRY_COMPARE(model.rowCount(), 2);
    const QString preEditSrc = QString::fromUtf8(doc.toMarkdownUtf8());
    const quint64 preEditSeq = doc.editSequence();

    bool handled = h.tryHandle(Qt::Key_Return, 0,
                                /*blockIndex=*/1,
                                /*qtPos=*/0,
                                /*selectionEmpty=*/true,
                                /*blockText=*/QString(kMarkerChar));
    QVERIFY(handled);                          // keystroke consumed
    QCOMPARE(doc.editSequence(), preEditSeq);  // no source edit
    QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()), preEditSrc);
}
```

(Don't forget to declare the slot.)

- [ ] **Step 2: Run the test, see it fail**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: FAIL — without the no-op rule, EOB-Enter on a marker-only block creates ANOTHER marker paragraph.

- [ ] **Step 3: Add the no-op rule**

In `LiveStructuralKeyHandler.cpp`, at the very top of the `paragraphEnter` lambda (before the Shift-key branch), add:

```cpp
        // Marker design §4.5: stacked Enter on a marker-only block is a
        // no-op. CommonMark collapses consecutive blank lines anyway;
        // visual "gap" can't survive a save/load cycle.
        if (Markoff::LiveRender::MarkerScrubber::isMarkerOnly(c.blockText)) {
            return HR::Handled;
        }
```

Add `#include <markoff/live-render/MarkerScrubber.h>` to the top of the file.

- [ ] **Step 4: Verify the test passes**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live/tests/tst_live_render_structural.cpp
git commit -m "$(cat <<'EOF'
r5.5(marker): stacked-Enter on a marker-only block is a no-op

Per spec §4.5 / premise M5 — Markdown's blank-line collapse means a
multi-row vertical gap cannot survive a save cycle anyway, so the editor
matches that constraint upstream.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: Wire `MarkerScrubber` to focus-out, save, and load events

**Files:**
- Modify: `libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`
- Modify: `libs/markoff-live/include/markoff/live-render/LiveEditBinding.h`
- Modify: `libs/markoff-live/src/LiveEditBinding.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_marker_scrubber.cpp`

- [ ] **Step 1: Wire `MarkerScrubber` ownership in `LiveListModelBinding`**

Add a new private member to `LiveListModelBinding.h`:

```cpp
    std::unique_ptr<MarkerScrubber> m_markerScrubber;
```

Plus a getter:

```cpp
    MarkerScrubber *markerScrubber() const;
```

And a Q_PROPERTY:

```cpp
    Q_PROPERTY(Markoff::LiveRender::MarkerScrubber *markerScrubber
               READ markerScrubber CONSTANT)
```

In `LiveListModelBinding.cpp`'s ctor (after `m_model` is constructed), construct the scrubber:

```cpp
    m_markerScrubber = std::make_unique<MarkerScrubber>(m_doc, m_model.get(), this);
```

(adjust ownership idiom to match existing patterns in the file).

In `setDocument`, add the connection:

```cpp
    connect(doc, &Markoff::MarkoffDocument::documentReloaded,
            m_markerScrubber.get(), &MarkerScrubber::scrubAfterLoad);
```

Add a public `Q_INVOKABLE void flushPendingMarkers()` on `LiveListModelBinding` that calls `m_markerScrubber->scrubBeforeSave()` — this is the host-callable save hook (replaces the v2 `flushPendingHoles`). Change the existing `flushPendingHoles` call sites to `flushPendingMarkers`, or just remove `flushPendingHoles` outright if it's only called from a v2 path that's already gone.

- [ ] **Step 2: Wire `LiveEditBinding` focus-out to `MarkerScrubber::scrubOnFocusOut`**

In `LiveEditBinding.h`, add a private member:

```cpp
    QPointer<MarkerScrubber> m_markerScrubber;  // borrowed; owned by LiveListModelBinding
```

In `LiveEditBinding::setBinding`, after the binding is wired:

```cpp
    if (m_binding) m_markerScrubber = m_binding->markerScrubber();
```

Find the focus-out handling in `LiveEditBinding.cpp` (likely a slot connected to `QQuickTextDocument`'s focus signals or a method called from QML). On focus-out, when `m_pendingMarkerScrub` is *still true* (meaning the user focused into a marker block but never typed), call:

```cpp
    if (m_pendingMarkerScrub && m_markerScrubber) {
        m_markerScrubber->scrubOnFocusOut(m_modelIndex);
        m_pendingMarkerScrub = false;
    }
```

If there is no existing focus-out hook on `LiveEditBinding`, add one. The cleanest place is a slot that QML invokes from the delegate's `Component.onDestruction` plus the TextEdit's `onActiveFocusChanged: if (!activeFocus) ...`. Match the existing focus-tracking pattern (the v2 design wired focus-tracking somewhere; reuse that seam if present).

- [ ] **Step 3: Add a failing test for the focus-out wiring**

In `tst_live_render_marker_scrubber.cpp`, add a slot that uses `LiveListModelBinding` end-to-end:

```cpp
void TstMarkerScrubber::endToEnd_focusInOutWithoutTyping_scrubsMarker() {
    using namespace Markoff;
    using namespace Markoff::LiveRender;
    MarkoffDocument doc;
    LiveListModelBinding binding;
    binding.setDocument(&doc);

    // Create marker paragraph via structural-key handler EOB-Enter.
    doc.resetContent(QByteArrayLiteral("alpha\n"));
    QTRY_COMPARE(binding.model()->rowCount(), 1);
    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, 0, 0, 5, true, QStringLiteral("alpha"));
    QTRY_COMPARE(binding.model()->rowCount(), 2);

    // Simulate the focus-out scrub (no LiveEditBinding instance here;
    // call the scrubber directly to verify the wiring of the marker
    // detection and edit emission).
    binding.markerScrubber()->scrubOnFocusOut(/*blockIndex=*/1);
    QTRY_COMPARE(binding.model()->rowCount(), 1);
    QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()), QStringLiteral("alpha\n"));
}
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build-dev --target tst_live_render_marker_scrubber -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_marker_scrubber$' --output-on-failure
```

Expected: PASS (the new test plus the existing 5).

- [ ] **Step 5: Run the full suite to confirm no regression**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: every test passes.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h \
        libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/include/markoff/live-render/LiveEditBinding.h \
        libs/markoff-live/src/LiveEditBinding.cpp \
        libs/markoff-live/tests/tst_live_render_marker_scrubber.cpp
git commit -m "$(cat <<'EOF'
r5.5(marker): wire MarkerScrubber to focus-out / save / load events

LiveListModelBinding owns the scrubber; LiveEditBinding calls
scrubOnFocusOut on focus-leave from a marker-only block; documentReloaded
fires scrubAfterLoad; the host-callable flushPendingMarkers replaces the
v2 flushPendingHoles for the save path.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: Backspace at start of paragraph following a marker block

Per spec §8.3: when the user presses Backspace at qtPos 0 of a paragraph whose preceding block is marker-only, the marker paragraph + its `\n\n` separator are deleted (using the same edit `MarkerScrubber::scrubOnFocusOut` would emit), and the cursor stays at qtPos 0 of the user's original paragraph.

**Files:**
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_structural.cpp`

- [ ] **Step 1: Add a failing test**

```cpp
void TstLiveRenderStructural::backspace_atQt0_afterMarkerBlock_scrubsMarker()
{
    using namespace Markoff;
    using namespace Markoff::LiveRender;
    MarkoffDocument doc;
    BlockKindRegistry registry;
    LiveBlockModel model(&doc, &registry);
    LiveCursorState cs(&registry, &model);
    UndoCoalescer uc(&doc, &cs);
    LiveStructuralKeyHandler h(&doc, &model, &cs, &registry, &uc);

    doc.resetContent(QStringLiteral("alpha\n\n%1\n\nbeta\n").arg(kMarkerChar).toUtf8());
    QTRY_COMPARE(model.rowCount(), 3);

    bool handled = h.tryHandle(Qt::Key_Backspace, 0,
                                /*blockIndex=*/2,
                                /*qtPos=*/0,
                                /*selectionEmpty=*/true,
                                /*blockText=*/QStringLiteral("beta"));
    QVERIFY(handled);
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()),
             QStringLiteral("alpha\n\nbeta\n"));
}
```

- [ ] **Step 2: Run, see fail**

Expected: FAIL — without the marker-aware case, the existing R5 backspace-merge would attempt to merge "beta" into the marker paragraph (resulting in `"​beta"`).

- [ ] **Step 3: Add the marker-aware branch in the backspace handler**

In `LiveStructuralKeyHandler.cpp`, find the paragraph backspace handler (the existing R5 backspace-merge at qtPos 0). Before the existing merge logic, add:

```cpp
    // Marker design §8.3: if the previous block is a marker-only paragraph,
    // delete it (and its leading "\n\n" separator) instead of merging.
    if (c.blockIndex > 0) {
        const auto &prevRec = c.model->recordAt(c.blockIndex - 1);
        if (Markoff::LiveRender::MarkerScrubber::isMarkerOnly(prevRec.text)) {
            const auto prevRange = c.document->blockByteRange(prevRec.blockAnchor);
            if (prevRange) {
                quint32 start = prevRange->first;
                const quint32 absorb = (start >= 2) ? 2 : start;
                start -= absorb;
                Markoff::MarkoffEdit ed;
                ed.oldStart = start;
                ed.oldEnd   = prevRange->second;
                ed.newText  = QByteArray();
                c.document->applyLocalEdit({ ed });
                c.cursorState->requestTextCaretAtRow(c.blockIndex - 1, 0);
                if (c.undoCoalescer) c.undoCoalescer->recordStructural();
                return HR::Handled;
            }
        }
    }
```

(Adjust to match the existing handler's structure — show the new branch as an early-return before the existing merge code.)

- [ ] **Step 4: Verify**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live/tests/tst_live_render_structural.cpp
git commit -m "$(cat <<'EOF'
r5.5(marker): backspace at qtPos 0 after marker block scrubs the marker

Per spec §8.3, Backspace at the start of a paragraph whose preceding
block is marker-only deletes the marker paragraph + its separator
instead of running the regular paragraph-merge.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase 3 — Cleanup (delete dead code)

The marker design is live and tested by Phase 2. These tasks delete the now-unused v2 hole code and its tests, simplify dependent classes, and update specs.

### Task 10: Clipboard scrubber (`LiveSelectionView::serializeForCopy`)

**Files:**
- Modify: `libs/markoff-live/src/LiveSelectionView.cpp`
- Modify: `libs/markoff-live/qml/delegates/ParagraphDelegate.qml` (if `serializeForCopy` lives there)
- Modify: a relevant test (`tst_live_render_paragraph_edit.cpp` or a new selection-test file)

- [ ] **Step 1: Find the `serializeForCopy` implementation**

```bash
grep -rn "serializeForCopy" libs/markoff-live
```

- [ ] **Step 2: Add a failing test asserting the clipboard string contains no markers**

(Test code — adapt to match the existing serialization test pattern.)

```cpp
void TstLiveSelectionViewClipboard::serializeForCopy_acrossMarker_stripsMarkerChars()
{
    using namespace Markoff;
    using namespace Markoff::LiveRender;
    // Set up a doc with a marker paragraph between two real paragraphs;
    // select all three; serialize; assert the result contains no ZWSP.
    // ... (reuse the existing fixture pattern from the file)
    QVERIFY(!serialized.contains(kMarkerChar));
}
```

- [ ] **Step 3: Add the strip in `serializeForCopy`**

After the existing concatenation builds the result string (call it `out`), append:

```cpp
    out.remove(Markoff::LiveRender::kMarkerChar);
```

Add the `Marker.h` include.

- [ ] **Step 4: Run the test**

```bash
cmake --build build-dev --target <relevant-test> -j 8 \
  && ctest --test-dir build-dev -R '<pattern>' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git commit -m "$(cat <<'EOF'
r5.5(marker): clipboard scrubber strips ZWSP from copy output

Per spec §10.2 — cross-row selection that includes a marker paragraph
yields clipboard text with no ZWSP bytes.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Simplify `UndoCoalescer` (drop hole branches)

**Files:**
- Modify: `libs/markoff-live/include/markoff/live-render/UndoCoalescer.h`
- Modify: `libs/markoff-live/src/UndoCoalescer.cpp`
- Modify callers' constructions to drop the `holeLayer` argument.

- [ ] **Step 1: Drop the `holeLayer` parameter from the ctor**

In `UndoCoalescer.h`:

```cpp
    explicit UndoCoalescer(Markoff::MarkoffDocument *document,
                           LiveCursorState          *cursorState = nullptr,
                           QObject                  *parent      = nullptr);
```

Drop the `LiveHoleLayer *m_holeLayer;` member and its forward decl.

- [ ] **Step 2: Drop hole routing in `undo()` / `redo()`**

In `UndoCoalescer.cpp`, replace the bodies of `undo()` / `redo()` with direct calls to `m_document->undo()` / `redo()`:

```cpp
void UndoCoalescer::undo() {
    if (!m_document) return;
    m_document->undo();
    clearLast();
}

void UndoCoalescer::redo() {
    if (!m_document) return;
    m_document->redo();
    clearLast();
}
```

- [ ] **Step 3: Update construction sites**

```bash
grep -rn "UndoCoalescer(" libs/markoff-live
```

Drop the `holeLayer` argument at every call site.

- [ ] **Step 4: Build + run all live-render tests**

```bash
cmake --build build-dev -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: every test passes.

- [ ] **Step 5: Commit**

```bash
git commit -m "$(cat <<'EOF'
r5.5(marker): UndoCoalescer single regime (drop hole-routing branches)

Undo/redo always route to MarkoffDocument; no per-hole undo stack
exists in the marker design.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 12: Revert `BlockId` to `Markoff::BlockAnchor`

**Files:**
- Modify: `libs/markoff-live/include/markoff/live-render/Cursor.h`
- Modify: every consumer that pattern-matched on the variant.

- [ ] **Step 1: Replace the `BlockId` definition**

In `Cursor.h`, change:

```cpp
using BlockId = std::variant<Markoff::BlockAnchor, HoleBlockId>;
```

to:

```cpp
using BlockId = Markoff::BlockAnchor;
```

Drop `isHoleBlockId`, `holeIdOf`, `anchorOf`, and the `#include <markoff/live-render/BlockHole.h>`.

- [ ] **Step 2: Find consumers and adapt**

```bash
grep -rn "isHoleBlockId\|holeIdOf\|anchorOf\|HoleBlockId" libs/markoff-live
```

For each match: if it was checking for the hole variant, the branch is dead — delete it. If it was unwrapping the variant via `anchorOf(...)`, replace with the `BlockId` itself (now a `BlockAnchor`).

`LiveCursorState::focusedHoleId` returns `0` (no hole id exists). Drop the `Q_PROPERTY` for `focusedHoleId` and its accessor.

- [ ] **Step 3: Build + run**

```bash
cmake --build build-dev -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: every test passes.

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
r5.5(marker): revert BlockId to Markoff::BlockAnchor

Drops the HoleBlockId variant and its helpers per spec §12. TextCaret
always references a CRDT-anchored block now.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 13: Delete `LiveHoleLayer`, `LiveProxyBlockModel`, `BlockHole`

**Files:**
- Delete: `libs/markoff-live/src/LiveHoleLayer.cpp`
- Delete: `libs/markoff-live/include/markoff/live-render/LiveHoleLayer.h`
- Delete: `libs/markoff-live/src/LiveProxyBlockModel.cpp`
- Delete: `libs/markoff-live/include/markoff/live-render/LiveProxyBlockModel.h`
- Delete: `libs/markoff-live/include/markoff/live-render/BlockHole.h`
- Modify: `libs/markoff-live/CMakeLists.txt`
- Modify: `libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`

- [ ] **Step 1: Delete the files**

```bash
git rm libs/markoff-live/src/LiveHoleLayer.cpp \
       libs/markoff-live/include/markoff/live-render/LiveHoleLayer.h \
       libs/markoff-live/src/LiveProxyBlockModel.cpp \
       libs/markoff-live/include/markoff/live-render/LiveProxyBlockModel.h \
       libs/markoff-live/include/markoff/live-render/BlockHole.h
```

- [ ] **Step 2: Drop the source entries from the library `CMakeLists.txt`**

Remove `src/LiveHoleLayer.cpp` and `src/LiveProxyBlockModel.cpp` from the source list.

- [ ] **Step 3: Drop `holeLayer()` and `proxyModel()` properties from `LiveListModelBinding`**

Remove the Q_PROPERTYs, getters, and member fields. Drop their #include lines.

- [ ] **Step 4: Adapt QML wiring**

Open `libs/markoff-live/qml/LiveView.qml`. Where it binds `ListView.model: binding.proxyModel` (or similar), change to `ListView.model: binding.model`. Remove any references to `binding.holeLayer`.

Open `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`. Remove `isHole`, `bufferText`, `holeId` references; bind `text:` directly to `model.text`. Drop any `if (model.isHole) ...` branches.

- [ ] **Step 5: Reset the cursor-state's signal-model wiring**

In `LiveListModelBinding.cpp`, where the v2 code did `m_cursorState->setSignalModel(m_proxyModel.get())`, change to `m_cursorState->setSignalModel(m_model.get())` (or simply remove the call if the inner `LiveBlockModel` is the default).

- [ ] **Step 6: Build + run**

```bash
cmake --build build-dev -j 8 2>&1 | tail -30
```

Fix any remaining compile errors (probably a few stragglers in `LiveSelectionView` or an integration file). Each error is a residual reference to a deleted symbol; remove it.

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: every remaining test passes.

- [ ] **Step 7: Commit**

```bash
git commit -m "$(cat <<'EOF'
r5.5(marker): retire LiveHoleLayer / LiveProxyBlockModel / BlockHole

Deletes the v2 hole-abstraction files. The QML ListView now binds
directly to the parser-pure LiveBlockModel; cursor state's signal-model
is the inner block model. ~590 LOC of production code retired.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 14: Delete v2 hole-specific tests

**Files:**
- Delete: `libs/markoff-live/tests/tst_live_render_holes_layer.cpp`
- Delete: `libs/markoff-live/tests/tst_live_render_holes_qml.cpp`
- Delete: `libs/markoff-live/tests/tst_live_render_proxy_model.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt` (un-comment + drop)

- [ ] **Step 1: Delete the files and their CMake entries**

```bash
git rm libs/markoff-live/tests/tst_live_render_holes_layer.cpp \
       libs/markoff-live/tests/tst_live_render_holes_qml.cpp \
       libs/markoff-live/tests/tst_live_render_proxy_model.cpp
```

In `tests/CMakeLists.txt`, remove the three `qt_add_executable` blocks for these tests (they were temporarily commented out in Task 6 — now delete them outright).

- [ ] **Step 2: Build + run**

```bash
cmake -S . -B build-dev && cmake --build build-dev -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: every remaining test passes.

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
r5.5(marker): retire three v2 hole-specific test executables

Deletes tst_live_render_holes_layer (526 LOC),
tst_live_render_holes_qml (88 LOC), tst_live_render_proxy_model (256 LOC).
Their behaviour is now either tested by the marker tests added in
Tasks 1-9 or no longer relevant.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 15: `LiveListModelBinding` final cleanup

**Files:**
- Modify: `libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`

Most of the cleanup landed in Tasks 8 and 13. This task removes the last residue: any leftover `flushPendingHoles` references, stale forward declarations, etc.

- [ ] **Step 1: Audit**

```bash
grep -n "flushPendingHoles\|holeLayer\|proxyModel\|HoleLayer\|ProxyBlockModel" \
  libs/markoff-live/{include,src,qml}
```

For each remaining match: delete or rename to the marker-design equivalent.

- [ ] **Step 2: Build + run**

```bash
cmake --build build-dev -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
r5.5(marker): final cleanup of v2 hole references in LiveListModelBinding

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase 4 — Verification

### Task 16: Harness-driven end-to-end tests

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_marker_flow.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

Tests to land (each one exercises the full LiveListModelBinding stack with the realistic-input harness):

- [ ] **Step 1: EOB-Enter → type → marker scrubbed atomically**

Sets up a paragraph; EOB-Enter via `tryHandle`; the harness types one character; assert source equals `"alpha\n\nx\n"` and `editSequence` bumped exactly twice (EOB-Enter + bundled-edit).

- [ ] **Step 2: Stress-typing race verification**

Same set-up; the harness types 200 characters at 30 ms gap immediately after EOB-Enter. Assert the source ends with the typed string in order, no character scrambling, no marker bytes anywhere in the final source.

- [ ] **Step 3: Focus-out without typing scrubs**

EOB-Enter; harness simulates focus moving away (e.g., calling the focus-out hook on `LiveEditBinding` directly, or — if the focus-out is wired off a Qt focus signal — sending a focus-out event). Assert source returns to `"alpha\n"`.

- [ ] **Step 4: Save-while-marker-present produces clean bytes**

EOB-Enter; before any typing, call `binding.flushPendingMarkers()`. Assert `doc.toMarkdownUtf8()` has no marker bytes.

- [ ] **Step 5: Load-time scrubber removes markers**

Construct a doc; call `doc.resetContent` with bytes containing markers. Assert that after the synchronous `documentReloaded` connection fires, the doc bytes are clean.

- [ ] **Step 6: Stacked-Enter no-op**

EOB-Enter; tryHandle Enter again on the marker paragraph; assert the source did NOT change.

- [ ] **Step 7: Ctrl-Z after typing into marker block returns to marker state, then second Ctrl-Z returns to pre-Enter state**

EOB-Enter; type one character via the harness; call `binding.undoCoalescer()->undo()`; assert source contains the marker again. Call undo() once more; assert source is the pre-Enter state.

After each step, build, run, commit:

```bash
cmake --build build-dev --target tst_live_render_marker_flow -j 8 \
  && ctest --test-dir build-dev -R '^tst_live_render_marker_flow$' --output-on-failure
git add libs/markoff-live/tests/tst_live_render_marker_flow.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "r5.5(marker): test <step name>"
```

After Step 7, run the full live-render suite:

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: every test passes. Any harness flake (timing-dependent) needs investigation per the review doc's §3.5 (use a deterministic gap, validate the harness against a known-broken stub if needed).

---

### Task 17: C-restoration spec amendments

**Files:**
- Modify: `docs/specs/2026-05-02-live-render-restoration-design.md`
- Modify: `docs/restoration-status.md`

- [ ] **Step 1: Apply the amendment deltas from spec §14**

Apply each row of the spec amendment table (premise 6, §3.1 BlockId, §4.4 cycle-guards, §5.4 structural keys, §6.1 L6, §7.2 data flow, §11 R5 acceptance, §11 new R5.5 phase, §15 open questions). Each delta is a small targeted edit; preserve surrounding wording.

- [ ] **Step 2: Update `restoration-status.md`**

Update the phase board: R5.5 phase points at `docs/specs/2026-05-03-marker-paragraph-design.md` and this plan. Add a "Spec-amendment log" entry for the marker-paragraph amendments. Move references to `docs/specs/2026-05-03-v2-holes-design.md` to point at `docs/archive/` instead.

- [ ] **Step 3: Verify document links**

```bash
grep -rn "2026-05-03-v2-holes-design\|2026-05-03-live-render-r5-5-holes" docs/ \
  | grep -v "docs/archive/\|docs/handoff/2026-05-03-section-3-1-spike-findings"
```

Each result is a stale reference; update to point at the marker design / new plan / archived versions as appropriate.

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
r5.5(marker): C-restoration spec amendments + restoration-status update

Applies the §14 amendment deltas from the marker-paragraph design.
Phase board now points at docs/specs/2026-05-03-marker-paragraph-design.md
and docs/plans/2026-05-03-live-render-r5-5-marker-paragraph.md.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 18: Dogfood gate

This is a manual gate, not a test. Per spec §16 acceptance #3.

- [ ] **Step 1: Build the test app**

```bash
cmake --build build-dev --target markoff-live-app -j 8
```

- [ ] **Step 2: Run the dogfood session**

```bash
./build-dev/bin/markoff-live-app
```

Open a real `.md` file (or paste real content). Type ≥ 200 words across ≥ 10 paragraphs. For each Enter:
- A new paragraph row appears immediately.
- The cursor lands in the new paragraph.
- The first keystroke fills the paragraph.
- No characters scramble; no double-spacing; no cursor disappearance; no scroll-to-top.

Save the file. Inspect:

```bash
xxd <saved-file.md> | grep -i "e2 80 8b" || echo "no markers in saved file: GOOD"
```

Reload the file in the same app; the on-screen content matches what was just typed.

- [ ] **Step 3: Document the dogfood pass**

Append a "Dogfood pass" note to `docs/restoration-status.md` (under Recent Changes) with date, word count, paragraph count, and any anomalies observed.

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
r5.5(marker): dogfood pass — N words across M paragraphs, no anomalies

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-review checklist (run before handing off)

- [ ] Every spec section in §14 of the design has a plan task implementing it. (Mapped explicitly under "Spec coverage map" at the top.)
- [ ] No placeholders ("TBD", "implement later", "similar to Task N", "add error handling").
- [ ] Type / method names used in later tasks match earlier tasks (`MarkerScrubber::isMarkerOnly`, `kMarkerChar`, `kMarkerUtf8`, `flushPendingMarkers`).
- [ ] Every test step has its assertion(s) shown.
- [ ] Every code step shows the actual code.
- [ ] Build + test commands are concrete (`ctest -R '<pattern>'`).
- [ ] Commits are frequent (one per task minimum, multiple within Task 16).
- [ ] The plan respects `-j 8` cap.
- [ ] The plan operates entirely inside `.worktrees/foundation-exploration/`.

---

## Notes for the executor

- **If a step's expected output doesn't match,** stop and investigate before proceeding. Never paper over a failure with a workaround at the next step's level — that's the cycle-guard pattern the review doc warns against.
- **The harness tests in Task 16 are load-bearing** for the no-async-race claim. If any of them are flaky, fix the underlying race; do not retry until green.
- **Tasks 13–15 are bulk deletions.** Use `git rm`, not `rm`, to keep history clean. Verify the deletions compile via the full build target before committing.
- **The QML side of Task 13** (LiveView.qml, ParagraphDelegate.qml) needs visual verification in `markoff-live-app` after the changes. The plan does not currently codify a programmatic test for "delegate renders the marker as zero-width"; if a regression slips, add one.
- **Open questions from spec §17** are flagged inline in the relevant tasks. None blocks task progression; each has a default the plan picks (matched-as-soft-break-too predicate; live-render-lib placement; first commit granularity).
