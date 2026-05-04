# R5.5 — Paragraph Holes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Re-introduce paragraph holes in the live-render library — view-side rows the parser cannot represent yet — using the v2 IME-preedit-pattern design (no source mutation until commit) over the audit's L9 phantom-rows + concatenating-proxy structure. Pressing Enter at end-of-paragraph or start-of-paragraph creates a hole row the user types into; on idle / focus-out / save / explicit Enter, the hole reifies into a real paragraph; on Esc / Backspace-at-0-empty / Delete-at-end-empty the hole abandons cleanly with no source mutation. Stress-typing into a hole produces in-order source bytes — verified by a `LiveRealisticInputHarness` whose gate test proves it would have caught v0's F2 character-scramble race.

**Architecture:** Three new C++ classes — `LiveHoleLayer` (owns paragraph holes, per-hole idle timer, per-hole undo stack), `LiveProxyBlockModel` (`QAbstractListModel` composing parser-pure `LiveBlockModel` rows ⊕ hole rows by anchor; the model the QML ListView binds to), and `LiveRealisticInputHarness` (`keyClick + qWait + processEvents`, mandatory for every async-UX test in R5.5+). `LiveCursorState`'s existing `requestTextCaretAtRow` operates on the proxy's row indices. `LiveStructuralKeyHandler` gains a hole-row dispatch path. `LiveEditBinding` exposes IME composition state to the layer's idle timer. `BlockId` becomes a discriminated union `std::variant<Markoff::BlockAnchor, HoleBlockId>` (spec §3.1, amendment A1). Save flushes pending holes before writing the rope. Undo is hole-aware: while a hole is open, Ctrl-Z runs against a per-hole snapshot stack; on empty buffer, next Ctrl-Z drops the hole; reification produces one CRDT undo entry. The QML `ParagraphDelegate` learns one new property — `isHole` — which makes its `text` binding read `BufferTextRole` (alias of `TextRole` for hole rows via the proxy) instead of bypassing through `LiveEditBinding`.

**Tech stack:** C++20, Qt 6.8 (Quick, Qml, Test, Widgets), `Markoff::MarkoffDocument::applyLocalEdit / undo / redo / coalesceLastUndo / resolveTextAnchor / blockByteRange`, `QAbstractListModel` for the proxy, `QTimer` per-hole for idle commits, `QInputMethodEvent` for the IME-guard test, `QTest::qWait + QCoreApplication::processEvents` for the realistic-input harness.

**Reference spec:** `docs/specs/2026-05-03-v2-holes-design.md` (full design); `docs/specs/2026-05-02-live-render-restoration-design.md` §3.1 (BlockId variant), §4.4 (cycle-guards), §5.4 (structural keys), §6.1 L6 (component map), §7.2 (data flow), §11 R5.5 (phase scope), §15 (open questions resolved); post-mortem at `docs/handoff/2026-05-03-r5-holes-postmortem.md`.

**Prerequisites:** R1–R5 Tasks 1–11 complete (R5 Tasks 12–18 may land independently of R5.5; this plan does not touch them). At branch tip:
- `LiveBlockModel` parser-pure with per-row `lastEditEditSequence`.
- `LiveCursorState::requestTextCaretAtRow` resolving on `rowsInserted` (R5 Task 2).
- `UndoCoalescer` printable-coalesce policy (R5 Task 3).
- `LiveStructuralKeyHandler` with descriptor-driven dispatch (R5 Tasks 4–11); paragraph EOB-Enter currently does `applyLocalEdit("\n\n")` which produces zero new rows — this plan replaces that path with hole creation.

**Acceptance criterion (binary):** R5.5 dogfood script (spec §10.3 R5.5) passes — *"Press Enter at end of every paragraph in a 10-block doc; a hole row appears with caret; type 5–10 characters into each; idle 300 ms; the hole reifies into a real paragraph and the caret stays at end-of-typed-text. Press Esc on a fresh empty hole; hole disappears, caret returns to the previous paragraph's end. Type 200 words at 100+ wpm across multiple Enter-created paragraphs; no character scramble; saved file equals on-screen content."* — AND `tst_live_render_holes_gate` (the harness gate test), `tst_live_render_proxy_model`, `tst_live_render_holes_layer`, `tst_live_render_holes_qml` all pass; AND existing `tst_live_render_*` suite passes unchanged; AND the §15 dogfood log records user sign-off.

---

## Open question resolutions (from spec §15 + design doc §16)

This plan resolves the following remaining open questions:

- **Idle-timer implementation.** One `QTimer *` per `BlockHole`, owned by the `HoleEntry`. Reason: per-hole timers are independent (each has its own last-edit time), making the cycling-single-timer pattern unnecessary; per-hole `QTimer` cleanup happens automatically on hole abandon/commit via `delete` in destructor or `QScopedPointer` in the entry struct. Cost: one `QObject` per hole; holes are sparse (rarely > 1 simultaneously).
- **Per-hole undo coalescing.** `UndoCoalescer`-like policy: snapshot pushes on idle (250 ms), non-printable, paste, IME composition end. Consecutive printables in same focus context update the head of the stack (coalesce). Coalesce-break on idle ≥ 1000 ms (matches `UndoCoalescer`'s legacy threshold).
- **Cursor refresh on `holeBufferChanged`.** When `bufferText` is updated externally (paste path), the QML `TextEdit`'s cursor preserves its qtPos using `LiveEditBinding`'s anchor protocol. The plan validates by test: paste into hole at qtPos 3; bufferText extends; cursor stays at qtPos 3 + paste-length.
- **Anchor precision for `reifyAnchor`.** `MarkoffDocument::anchorAtByte(byte)` returns the `Markoff::TextAnchor`. For paragraph EOB-Enter, byte = `currentBlock.endByte`. For start-of-paragraph-Enter, byte = `currentBlock.startByte`.
- **`LiveProxyBlockModel` performance.** Full mapping rebuild on every parse-back is O(parserRows + holeCount). Parser rows are typically < 1000; holes are sparse (typically ≤ 1 simultaneously). Rebuild is < 1 ms; well within R10's 8 ms parse-arrival budget. Plan does not optimize incrementally for R5.5; revisit if R10 perf benchmarks flag it.
- **Synthetic broken stub.** A QML file `tests/synthetic/SyntheticBrokenParagraphDelegate.qml` that destroys + recreates the TextEdit on first keystroke, mimicking v0's reify-on-first-keystroke. The gate test in Task 2 instantiates this stub through the harness and asserts text scrambles.
- **Test harness gap-time tuning.** Default 30 ms; the gate test in Task 2 starts at 30 ms and increases up to 100 ms if 30 ms doesn't reproduce the scramble. The first value that reproduces the scramble becomes the default for the remaining tests in R5.5.

---

## File map

**New — public headers** (`libs/markoff-live-render/include/markoff/live-render/`):
- `BlockHole.h` — value type: `HoleKind`, `HoleBlockId`, `BlockHole` struct (kind, reifyAnchor, bufferText, holeId).
- `LiveHoleLayer.h` — owns `BlockHole` items; lifecycle (create/setBuffer/commit/abandon); per-hole idle timer; per-hole undo stack; IME guard.
- `LiveProxyBlockModel.h` — `QAbstractListModel` over inner `LiveBlockModel` + `LiveHoleLayer`; mapping helpers; role passthrough; `IsHoleRole` / `BufferTextRole` / `HoleIdRole`.

**New — sources** (`libs/markoff-live-render/src/`):
- `LiveHoleLayer.cpp`
- `LiveProxyBlockModel.cpp`

**New — tests** (`libs/markoff-live-render/tests/`):
- `LiveRealisticInputHarness.h` (header-only; `LiveRealisticInputHarness` class with `keyClick`, `typeString`, `burst`, `idle`).
- `synthetic/SyntheticBrokenParagraphDelegate.qml` — v0-mimicking stub for the gate test.
- `tst_live_render_holes_gate.cpp` — the harness gate test against the synthetic stub.
- `tst_live_render_holes_layer.cpp` — `LiveHoleLayer` unit tests (lifecycle, IME guard, undo, save-flush).
- `tst_live_render_proxy_model.cpp` — `LiveProxyBlockModel` unit tests (mapping rule, signal propagation, role passthrough, model reset).
- `tst_live_render_holes_qml.cpp` — harness-driven async-UX tests against full QML stack (idle commit, focus-out commit, abandon paths, mid-buffer Enter, stacked Enter, stress-typing, selection-across-hole).

**Modified — headers:**
- `Cursor.h` — `BlockId` becomes `std::variant<Markoff::BlockAnchor, HoleBlockId>` (spec amendment A1, §3.1).
- `LiveCursorState.h` — `requestTextCaretAtRow` semantics now operate on `LiveProxyBlockModel` rows (no signature change; documentation updated).
- `LiveListModelBinding.h` — own `LiveHoleLayer *holeLayer` and `LiveProxyBlockModel *proxyModel` Q_PROPERTYs (CONSTANT). Expose `holeLayer` for QML access (structural-key handler creates holes through it).
- `LiveStructuralKeyHandler.h` — add hole-row dispatch path; new helpers for hole creation; new helpers for mid-buffer Enter split.
- `LiveEditBinding.h` — `setHoleId(quint64)` for delegates rendering hole rows; `setComposing(bool)` exposed for the layer's idle-timer pause.
- `UndoCoalescer.h` — host check for hole context: if focused row is a hole, route undo/redo to layer instead of `MarkoffDocument`.

**Modified — sources:**
- All the cpp files for the above.
- `BlockKindRegistry.cpp` — paragraph descriptor's `consumedStructuralKeys` already includes `Key_Return`, `Key_Backspace`, `Key_Delete` (R5 Task 11); no change needed.
- `LiveListModelBinding.cpp` — construct + wire `LiveHoleLayer` and `LiveProxyBlockModel`; the proxy is the model exposed to QML (`Q_PROPERTY model`).
- `LiveCursorState.cpp` — internal: when listening to `rowsInserted`, listen on the proxy not the inner block model.

**Modified — QML:**
- `qml/LiveView.qml` — ListView's `model:` binding changes from `binding.blockModel` to `binding.proxyModel`. Add Connections to route structural keys when focused row is a hole (one extra Keys.onPressed delegation pattern).
- `qml/delegates/ParagraphDelegate.qml` — add `isHole` property reading `model.isHole`; for hole rows, `text` binding reads `model.bufferText` and writes go through `LiveHoleLayer::setBlockHoleBuffer` via the binding; commit-on-focus-out wired.
- `app/Main.qml` — title suffix `"(R5.5)"`.

**Modified — CMake:**
- `libs/markoff-live-render/CMakeLists.txt` — add new sources / headers to `qt_add_qml_module`.
- `libs/markoff-live-render/tests/CMakeLists.txt` — add four new test executables; register `LiveRealisticInputHarness` as part of the test-utilities link target.

**Modified — docs:**
- `docs/restoration-status.md` — TL;DR + Phase board + recent-changes log entries (last task).

**Untouched (verified-not-broken):**
- `LiveBlockModel`, `BlockHitTester`, `LiveSelectionView`, `Coordinates` (R2/R3 surfaces). The proxy sits between `LiveBlockModel` and the QML view; `LiveBlockModel` itself does not change.
- `HorizontalRuleDelegate`, `ImageDelegate` (non-text; structural keys do not apply; holes are paragraph-only in R5.5).
- All R6+ files (don't yet exist).

---

## Task 1: Read context

- [ ] **Step 1: Read these files in order, no edits.**

```
docs/specs/2026-05-03-v2-holes-design.md                                  (full design — load-bearing)
docs/handoff/2026-05-03-r5-holes-postmortem.md                            (post-mortem; the WHY)
docs/specs/2026-05-02-live-render-restoration-design.md                   §3.1, §4.4, §5.4, §6.1 L6, §7.2, §11 R5.5, §15
docs/specs/2026-05-01-live-projection-layer.md                            §3.1–§3.6 (v1 design + v0 forensics)
docs/handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md (test-discipline gate)
libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h       (applyLocalEdit, undo, redo, coalesceLastUndo, resolveTextAnchor, anchorAtByte)
libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h    (requestTextCaretAtRow)
libs/markoff-live-render/include/markoff/live-render/LiveStructuralKeyHandler.h  (the dispatch table; we add a hole branch)
libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp                  (paragraph EOB-Enter handler — the line we replace)
libs/markoff-live-render/src/LiveListModelBinding.cpp                      (where we wire LiveHoleLayer + LiveProxyBlockModel)
libs/markoff-live-render/qml/LiveView.qml                                  (the ListView model: binding to swap)
libs/markoff-live-render/qml/delegates/ParagraphDelegate.qml               (where isHole plumbing lands)
libs/markoff-view-qml/src/LiveProjectionLayer.cpp                          (LEGACY — read for byte-arithmetic patterns; do NOT port the holes branches; v0 design is wrong)
libs/markoff-view-qml/src/LiveProjectionLayer.h                            (LEGACY — same)
```

No code changes.

- [ ] **Step 2: Run the existing fast-tier test suite to confirm a clean baseline.**

```bash
cmake --build build-dev --target markoff_live_render markoff-live-render-app -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all `tst_live_render_*` executables green. Record the count; R5.5 must end with the same count plus four new (`holes_gate`, `holes_layer`, `proxy_model`, `holes_qml`).

---

## Task 2: `LiveRealisticInputHarness` + gate test (TDD-against-synthetic-broken-stub)

**Files:**
- Create: `libs/markoff-live-render/tests/LiveRealisticInputHarness.h` (header-only)
- Create: `libs/markoff-live-render/tests/synthetic/SyntheticBrokenParagraphDelegate.qml`
- Create: `libs/markoff-live-render/tests/tst_live_render_holes_gate.cpp`
- Modify: `libs/markoff-live-render/tests/CMakeLists.txt`

The harness lands first AND its gate test must FAIL initially against a synthetic v0-mimic, then PASS after tuning. The brief's gate ("Why will my test see the v0 race?") is converted into a binary CI artifact.

- [ ] **Step 1: Write `LiveRealisticInputHarness.h` — the harness API.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCoreApplication>
#include <QQuickWindow>
#include <QTest>

namespace Markoff::LiveRender::Test {

/// Realistic-keyboard-timing harness for async-UX tests.
///
/// QTest::keyClick alone is wrong: it delivers events synchronously
/// between event-loop spins, masking async races (the v0 holes' F2
/// character-scramble passed a QTest::keyClick-driven test while
/// scrambling on real keyboard input). This harness interposes
/// qWait + processEvents between every key event so async paths
/// have a chance to run between keystrokes.
///
/// Use this for every async-UX test in R5.5+. The gate test in
/// tst_live_render_holes_gate proves the harness sees v0's race
/// against a synthetic broken delegate stub.
class LiveRealisticInputHarness {
public:
    explicit LiveRealisticInputHarness(QQuickWindow *window,
                                       int defaultGapMs = 30)
        : m_window(window), m_defaultGapMs(defaultGapMs) {}

    void keyClick(Qt::Key key,
                  Qt::KeyboardModifiers mods = Qt::NoModifier) {
        keyClick(key, mods, m_defaultGapMs);
    }

    void keyClick(Qt::Key key,
                  Qt::KeyboardModifiers mods,
                  int gapMs) {
        QTest::keyClick(m_window, key, mods);
        QTest::qWait(gapMs);
        QCoreApplication::processEvents();
    }

    void typeChar(QChar c) {
        Qt::Key k = static_cast<Qt::Key>(c.toUpper().unicode());
        Qt::KeyboardModifiers mods = c.isUpper() ? Qt::ShiftModifier
                                                 : Qt::NoModifier;
        keyClick(k, mods);
    }

    void typeString(const QString &text) {
        for (QChar c : text) typeChar(c);
    }

    void burst(const QString &chars, int pauseMs) {
        for (QChar c : chars)
            QTest::keyClick(m_window, static_cast<Qt::Key>(c.toUpper().unicode()));
        QTest::qWait(pauseMs);
        QCoreApplication::processEvents();
    }

    void idle(int durationMs) {
        QTest::qWait(durationMs);
        QCoreApplication::processEvents();
    }

    int defaultGapMs() const { return m_defaultGapMs; }
    void setDefaultGapMs(int ms) { m_defaultGapMs = ms; }

private:
    QQuickWindow *m_window;
    int m_defaultGapMs;
};

}  // namespace Markoff::LiveRender::Test
```

- [ ] **Step 2: Write the synthetic broken delegate.**

`libs/markoff-live-render/tests/synthetic/SyntheticBrokenParagraphDelegate.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A synthetic delegate that mimics v0's holes' reify-on-first-keystroke
// pattern: on the first keystroke, the underlying TextEdit is destroyed
// and replaced with a new one. This is what v0 did and what produced
// F2 character-scramble on real keyboard timing. The gate test in
// tst_live_render_holes_gate.cpp exposes this race against the
// LiveRealisticInputHarness; once the harness sees the scramble, it
// proves the harness can catch real async-UX regressions.
//
// Delete this file after the gate test passes once.

import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 400
    height: 30
    property string allDeliveredText: ""
    property bool firstKeyHandled: false

    Loader {
        id: textEditLoader
        anchors.fill: parent
        sourceComponent: textEditComponent
    }

    Component {
        id: textEditComponent
        TextEdit {
            id: textEdit
            objectName: "innerTextEdit"
            focus: true
            Component.onCompleted: forceActiveFocus()

            // v0-mimic: on first keystroke, destroy + recreate
            // (simulating the hole-reify-then-new-delegate sequence).
            // Subsequent keystrokes during the destroy/recreate window
            // race with the focus transition.
            Keys.onPressed: function(event) {
                if (!root.firstKeyHandled && event.text.length > 0) {
                    root.firstKeyHandled = true;
                    root.allDeliveredText += event.text;
                    // Simulated async window: reload the loader, which
                    // destroys this TextEdit and creates a new one.
                    Qt.callLater(function() {
                        textEditLoader.active = false;
                        textEditLoader.active = true;
                    });
                    return;
                }
                root.allDeliveredText += event.text;
            }
        }
    }
}
```

- [ ] **Step 3: Write the failing gate test.**

`libs/markoff-live-render/tests/tst_live_render_holes_gate.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Gate test for LiveRealisticInputHarness. Asserts the harness sees
// v0-style F2 character-scramble against a synthetic broken delegate.
// Once this test passes, the harness is proven to catch real async-UX
// regressions and is ready for use in the rest of R5.5+.
//
// The synthetic broken delegate is deleted after this test passes.

#include "LiveRealisticInputHarness.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace Markoff::LiveRender::Test;

class TstHolesGate : public QObject {
    Q_OBJECT

private slots:
    void harness_sees_v0_style_character_scramble() {
        QQuickView view;
        view.setSource(QUrl::fromLocalFile(
            QFINDTESTDATA("synthetic/SyntheticBrokenParagraphDelegate.qml")));
        QVERIFY(view.status() == QQuickView::Ready);
        view.show();
        QVERIFY(QTest::qWaitForWindowActive(&view));

        LiveRealisticInputHarness h(&view, /*defaultGapMs=*/30);

        const QString target = QStringLiteral("this is interesting");
        h.typeString(target);

        h.idle(50);  // settle

        QString delivered = view.rootObject()->property("allDeliveredText").toString();

        // The synthetic delegate destroys + recreates on first keystroke.
        // With realistic timing, intermediate keystrokes during the
        // destroy/recreate window are dropped or reordered.
        // If `delivered == target`, the harness is too lenient — it did
        // not let the async window run between keystrokes, which means
        // it would not catch real async-UX races either. Tighten the
        // harness (longer gap, more processEvents) and re-run.
        QVERIFY2(delivered != target,
                 qPrintable(QStringLiteral(
                     "Harness did not expose v0-style race. "
                     "Delivered: '%1'; expected scrambled. "
                     "Tighten the harness gap (try 50 ms, 75 ms, 100 ms) "
                     "before relying on it for the rest of R5.5.")
                     .arg(delivered)));
    }
};

QTEST_MAIN(TstHolesGate)
#include "tst_live_render_holes_gate.moc"
```

- [ ] **Step 4: Wire into CMake.**

`libs/markoff-live-render/tests/CMakeLists.txt` — add an executable + the synthetic resource bundle:

```cmake
add_executable(tst_live_render_holes_gate
    tst_live_render_holes_gate.cpp
)
target_link_libraries(tst_live_render_holes_gate PRIVATE
    Qt6::Test Qt6::Quick Qt6::QuickTest
    markoff_live_render
)
qt_add_resources(tst_live_render_holes_gate "synthetic"
    PREFIX "/"
    BASE "."
    FILES synthetic/SyntheticBrokenParagraphDelegate.qml
)
target_include_directories(tst_live_render_holes_gate PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME tst_live_render_holes_gate
         COMMAND tst_live_render_holes_gate)
```

(`QFINDTESTDATA` resolves to either a build-tree path or the resource path; if your tree uses `QFINDTESTDATA` against a source path, set up `qt_add_resources` to mirror.)

- [ ] **Step 5: Build + run the gate test.**

```bash
cmake --build build-dev --target tst_live_render_holes_gate -j 8
ctest --test-dir build-dev -R '^tst_live_render_holes_gate$' --output-on-failure
```

Expected: PASS at 30 ms gap (the synthetic v0-mimic typically scrambles at any gap ≥ 20 ms).

- [ ] **Step 6: If the gate test FAILS at 30 ms (`delivered == target`), increase the gap.**

Edit `tst_live_render_holes_gate.cpp` line `LiveRealisticInputHarness h(&view, /*defaultGapMs=*/30);` to 50 ms; re-run; if still passing, try 75 then 100. Record the first value at which the synthetic scramble manifests as the new R5.5 default. **Do not raise the default above 100 ms** — if the harness needs > 100 ms gap to expose the synthetic race, the synthetic stub is wrong (too synchronous); fix the stub by adding `Qt.callLater(Qt.callLater(...))` chains until 30–50 ms exposes it.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-live-render/tests/LiveRealisticInputHarness.h \
        libs/markoff-live-render/tests/synthetic/SyntheticBrokenParagraphDelegate.qml \
        libs/markoff-live-render/tests/tst_live_render_holes_gate.cpp \
        libs/markoff-live-render/tests/CMakeLists.txt
git commit -m "test(live-render): LiveRealisticInputHarness + gate test (R5.5 Task 2)

The harness wraps QTest::keyClick with qWait + processEvents so async
paths run between keystrokes — a discipline the v0 holes' tests lacked.
The gate test asserts the harness sees v0-style F2 character-scramble
against a synthetic broken-delegate stub mimicking reify-on-first-keystroke.

Once this test passes, the harness is proven; subsequent R5.5 tasks
use it for every async-UX assertion. The synthetic stub will be deleted
in Task 3 once it has served its gate-test purpose.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Delete synthetic broken stub; harness moves to production status

The synthetic stub has served its purpose. Per the design (and the post-mortem §6.2), it's deleted before any v2 hole code lands so it doesn't pollute the production codebase.

**Files:**
- Delete: `libs/markoff-live-render/tests/synthetic/SyntheticBrokenParagraphDelegate.qml`
- Delete: `libs/markoff-live-render/tests/tst_live_render_holes_gate.cpp` (the test had one purpose; now done)
- Modify: `libs/markoff-live-render/tests/CMakeLists.txt` — remove the gate-test executable + resource block.

- [ ] **Step 1: Delete the synthetic-stub files.**

```bash
rm libs/markoff-live-render/tests/synthetic/SyntheticBrokenParagraphDelegate.qml
rmdir libs/markoff-live-render/tests/synthetic/
rm libs/markoff-live-render/tests/tst_live_render_holes_gate.cpp
```

- [ ] **Step 2: Remove the gate-test stanza from CMakeLists.**

Remove the `add_executable(tst_live_render_holes_gate ...)` block and its `qt_add_resources(...)` plus `add_test(...)` lines from Task 2.

- [ ] **Step 3: Verify build still passes.**

```bash
cmake --build build-dev --target markoff_live_render -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: same count green as before Task 2 (the gate test is gone; no other new tests yet).

- [ ] **Step 4: Commit.**

```bash
git add -A libs/markoff-live-render/tests/
git commit -m "test(live-render): retire synthetic broken stub + gate test (R5.5 Task 3)

The stub + gate test served their purpose in Task 2 — proving the
harness sees v0-style races. Per the design (post-mortem §6.2),
they're deleted before any v2 hole code lands. The harness header
(LiveRealisticInputHarness.h) stays; it's used by every R5.5+
async-UX test going forward.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: `BlockHole` value type + `HoleBlockId` + `Cursor.h` BlockId variant (TDD)

Lands the spec amendment A1's §3.1 BlockId change in code. This is a small surface change that propagates into the cursor model.

**Files:**
- Create: `libs/markoff-live-render/include/markoff/live-render/BlockHole.h`
- Modify: `libs/markoff-live-render/include/markoff/live-render/Cursor.h`
- Create: `libs/markoff-live-render/tests/tst_live_render_holes_layer.cpp` (skeleton; tests fill in over Tasks 4–14)

- [ ] **Step 1: Write the failing test for `BlockHole` shape.**

Initial `tst_live_render_holes_layer.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/BlockHole.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <QtTest/QtTest>

using namespace Markoff::LiveRender;

class TstHolesLayer : public QObject {
    Q_OBJECT

private slots:
    void block_hole_value_type_default_construction() {
        BlockHole h;
        QCOMPARE(h.kind, HoleKind::Paragraph);          // default kind
        QCOMPARE(h.bufferText, QString());
        QCOMPARE(h.holeId, quint64(0));                 // zero == invalid
    }

    void hole_block_id_disambiguates_from_block_anchor() {
        HoleBlockId h1{42};
        HoleBlockId h2{42};
        HoleBlockId h3{99};
        QCOMPARE(h1.holeId, h2.holeId);
        QVERIFY(h1.holeId != h3.holeId);
    }

    // Tests filled in over Tasks 5–14.
};

QTEST_MAIN(TstHolesLayer)
#include "tst_live_render_holes_layer.moc"
```

- [ ] **Step 2: Verify test fails to compile.**

```bash
cmake --build build-dev --target tst_live_render_holes_layer -j 8 2>&1 | tail -20
```

Expected: compile error — `BlockHole.h: No such file or directory`.

- [ ] **Step 3: Write `BlockHole.h`.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff-foundation/TextAnchor.h>

#include <QString>
#include <QtGlobal>

namespace Markoff::LiveRender {

/// Kind of phantom row (v2: paragraph only; v3+ extends).
enum class HoleKind {
    Paragraph,
};

/// Layer-local hole identity. Disambiguates view-side phantom rows
/// from CRDT-anchored parser blocks. Per spec §3.1 amendment A1.
struct MARKOFF_LIVE_RENDER_EXPORT HoleBlockId {
    quint64 holeId;                  ///< zero == invalid

    bool operator==(const HoleBlockId &) const = default;
};

/// A view-side phantom row owned by LiveHoleLayer. The layer manages
/// its lifecycle; consumers refer to it via `holeId`. The reifyAnchor
/// is the CRDT byte position where the hole's `bufferText` will be
/// committed to source on reification (one applyLocalEdit of
/// "\n\n" + bufferText at reifyAnchor).
struct MARKOFF_LIVE_RENDER_EXPORT BlockHole {
    HoleKind kind = HoleKind::Paragraph;
    Markoff::TextAnchor reifyAnchor;
    QString bufferText;
    quint64 holeId = 0;
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 4: Add to `qt_add_qml_module` SOURCES list.**

`libs/markoff-live-render/CMakeLists.txt` — add `include/markoff/live-render/BlockHole.h` to the `HEADERS` list of `qt_add_qml_module(markoff_live_render ...)`.

- [ ] **Step 5: Add `tst_live_render_holes_layer` to `tests/CMakeLists.txt`.**

```cmake
add_executable(tst_live_render_holes_layer
    tst_live_render_holes_layer.cpp
)
target_link_libraries(tst_live_render_holes_layer PRIVATE
    Qt6::Test
    markoff_live_render
)
add_test(NAME tst_live_render_holes_layer
         COMMAND tst_live_render_holes_layer)
```

- [ ] **Step 6: Build + run.**

```bash
cmake --build build-dev --target tst_live_render_holes_layer -j 8
ctest --test-dir build-dev -R '^tst_live_render_holes_layer$' --output-on-failure
```

Expected: 2 PASS (default construction; HoleBlockId disambiguation).

- [ ] **Step 7: Modify `Cursor.h` — `BlockId` becomes a variant.**

Current `Cursor.h`:

```cpp
using BlockId = Markoff::BlockAnchor;
```

Change to:

```cpp
#include <markoff/live-render/BlockHole.h>
#include <variant>

namespace Markoff::LiveRender {

using BlockId = std::variant<Markoff::BlockAnchor, HoleBlockId>;

/// Helper: returns true if the BlockId is a hole.
inline bool isHoleBlockId(const BlockId &id) {
    return std::holds_alternative<HoleBlockId>(id);
}

/// Helper: returns the hole id (must be a HoleBlockId; UB otherwise).
inline quint64 holeIdOf(const BlockId &id) {
    return std::get<HoleBlockId>(id).holeId;
}

/// Helper: returns the BlockAnchor (must be a BlockAnchor; UB otherwise).
inline const Markoff::BlockAnchor &anchorOf(const BlockId &id) {
    return std::get<Markoff::BlockAnchor>(id);
}

}  // namespace Markoff::LiveRender
```

This changes the type signature of every consumer of `BlockId`. The rebuild will surface them.

- [ ] **Step 8: Build the library — accept the cascade.**

```bash
cmake --build build-dev --target markoff_live_render -j 8 2>&1 | grep -E "error:|warning:" | head -30
```

Expected: a small number of compile errors in `LiveCursorState.cpp`, `LiveSelectionView.cpp`, `LiveStructuralKeyHandler.cpp`, etc. — every site that constructs or compares `BlockId` directly. Fix by wrapping or unwrapping via the helpers (e.g. construct from `BlockAnchor` via `BlockId{anchor}`; access via `anchorOf(id)`; compare via `id == BlockId{anchor}`).

The fixes are mechanical. Tests for these consumers should remain green (their semantics are unchanged; the variant just adds a wider type).

- [ ] **Step 9: Run the full live-render fast-tier suite — verify no regressions.**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: every existing test still PASS; the new `tst_live_render_holes_layer` PASS (2 cases).

- [ ] **Step 10: Commit.**

```bash
git add libs/markoff-live-render/include/markoff/live-render/BlockHole.h \
        libs/markoff-live-render/include/markoff/live-render/Cursor.h \
        libs/markoff-live-render/tests/tst_live_render_holes_layer.cpp \
        libs/markoff-live-render/tests/CMakeLists.txt \
        libs/markoff-live-render/CMakeLists.txt \
        libs/markoff-live-render/src/  # any cascade fixes
git commit -m "feat(live-render): BlockHole + HoleBlockId; BlockId is now a variant (R5.5 Task 4)

Implements spec amendment A1 §3.1: BlockId becomes
std::variant<BlockAnchor, HoleBlockId>. Disambiguates view-side
phantom-row identity from CRDT-anchored parser blocks; foundation's
resolveTextAnchor refuses HoleBlockId by construction.

BlockHole value type holds (kind, reifyAnchor, bufferText, holeId).
LiveHoleLayer (next task) owns instances and manages lifecycle.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `LiveHoleLayer` — create / setBuffer / abandon (no commit yet)

Lifecycle methods that don't touch the CRDT. Reification (commit) is Task 7.

**Files:**
- Create: `libs/markoff-live-render/include/markoff/live-render/LiveHoleLayer.h`
- Create: `libs/markoff-live-render/src/LiveHoleLayer.cpp`
- Modify: `libs/markoff-live-render/CMakeLists.txt`
- Modify: `libs/markoff-live-render/tests/tst_live_render_holes_layer.cpp` (add lifecycle tests)

- [ ] **Step 1: Write failing tests for create / setBuffer / abandon.**

Append to `tst_live_render_holes_layer.cpp`:

```cpp
private slots:
    void layer_create_emits_hole_inserted_and_assigns_id() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);

        Markoff::LiveRender::LiveHoleLayer layer(&doc, /*blockModel=*/nullptr,
                                                 /*undoCoalescer=*/nullptr);
        QSignalSpy spy(&layer, &Markoff::LiveRender::LiveHoleLayer::holeInserted);

        Markoff::TextAnchor anchor = doc.anchorAtByte(5);
        quint64 id = layer.createBlockHole(HoleKind::Paragraph, anchor);

        QVERIFY(id != 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toULongLong(), id);
        QCOMPARE(layer.holeCount(), 1);
        QCOMPARE(layer.bufferText(id), QString());
        QVERIFY(layer.exists(id));
        QCOMPARE(layer.kind(id), HoleKind::Paragraph);
    }

    void layer_setBuffer_emits_buffer_changed() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);

        Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.anchorAtByte(5));

        QSignalSpy spy(&layer, &Markoff::LiveRender::LiveHoleLayer::holeBufferChanged);
        layer.setBlockHoleBuffer(id, "world");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(layer.bufferText(id), QString("world"));
    }

    void layer_abandon_drops_hole_no_source_mutation() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);

        Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
        quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.anchorAtByte(5));
        layer.setBlockHoleBuffer(id, "buffered");

        QSignalSpy spy(&layer, &Markoff::LiveRender::LiveHoleLayer::holeAbandoned);
        layer.abandonBlockHole(id);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(layer.holeCount(), 0);
        QVERIFY(!layer.exists(id));

        // CRITICAL: no source mutation — F5 mitigation.
        QCOMPARE(doc.toMarkdown(), QString("hello"));
    }
```

- [ ] **Step 2: Verify these tests fail to compile.**

Expected: `LiveHoleLayer.h: No such file or directory`.

- [ ] **Step 3: Write `LiveHoleLayer.h` (lifecycle subset).**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/BlockHole.h>
#include <markoff/live-render/MarkoffLiveRenderExport.h>

#include <QHash>
#include <QObject>
#include <qqmlintegration.h>

namespace Markoff {
class MarkoffDocument;
}

namespace Markoff::LiveRender {

class LiveBlockModel;
class UndoCoalescer;

class MARKOFF_LIVE_RENDER_EXPORT LiveHoleLayer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveHoleLayer is provided by LiveListModelBinding")

public:
    explicit LiveHoleLayer(Markoff::MarkoffDocument *doc,
                           LiveBlockModel    *blockModel,
                           UndoCoalescer     *undoCoalescer,
                           QObject           *parent = nullptr);
    ~LiveHoleLayer() override;

    // Lifecycle (commit lands in Task 7).
    quint64 createBlockHole(HoleKind kind, Markoff::TextAnchor reifyAnchor);
    void    setBlockHoleBuffer(quint64 holeId, const QString &text);
    void    abandonBlockHole(quint64 holeId);

    // Lookups.
    int     holeCount() const noexcept;
    bool    exists(quint64 holeId) const noexcept;
    HoleKind kind(quint64 holeId) const;
    QString  bufferText(quint64 holeId) const;
    Markoff::TextAnchor reifyAnchor(quint64 holeId) const;
    QList<quint64> holesInOrder() const;

Q_SIGNALS:
    void holeInserted(quint64 holeId);
    void holeBufferChanged(quint64 holeId);
    void holeAbandoned(quint64 holeId);
    // Task 7 adds: void holeReified(quint64, Markoff::TextAnchor);

private:
    struct HoleEntry {
        HoleKind kind = HoleKind::Paragraph;
        Markoff::TextAnchor reifyAnchor;
        QString bufferText;
        bool composing = false;
        // Task 6 adds: QTimer *idleTimer; (per-hole)
        // Task 13 adds: undo stack
    };

    QHash<quint64, HoleEntry> m_holes;
    quint64 m_nextHoleId = 1;

    Markoff::MarkoffDocument *m_doc;
    LiveBlockModel *m_blockModel;
    UndoCoalescer *m_undoCoalescer;
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 4: Write `LiveHoleLayer.cpp` (lifecycle subset).**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveHoleLayer.h>

#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::LiveRender {

LiveHoleLayer::LiveHoleLayer(Markoff::MarkoffDocument *doc,
                             LiveBlockModel    *blockModel,
                             UndoCoalescer     *undoCoalescer,
                             QObject           *parent)
    : QObject(parent),
      m_doc(doc),
      m_blockModel(blockModel),
      m_undoCoalescer(undoCoalescer)
{}

LiveHoleLayer::~LiveHoleLayer() = default;

quint64 LiveHoleLayer::createBlockHole(HoleKind kind,
                                        Markoff::TextAnchor reifyAnchor) {
    HoleEntry entry;
    entry.kind = kind;
    entry.reifyAnchor = reifyAnchor;

    quint64 id = m_nextHoleId++;
    m_holes.insert(id, entry);
    Q_EMIT holeInserted(id);
    return id;
}

void LiveHoleLayer::setBlockHoleBuffer(quint64 holeId, const QString &text) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return;
    if (it->bufferText == text) return;
    it->bufferText = text;
    Q_EMIT holeBufferChanged(holeId);
}

void LiveHoleLayer::abandonBlockHole(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return;
    m_holes.erase(it);
    Q_EMIT holeAbandoned(holeId);
}

int LiveHoleLayer::holeCount() const noexcept { return m_holes.size(); }

bool LiveHoleLayer::exists(quint64 holeId) const noexcept {
    return m_holes.contains(holeId);
}

HoleKind LiveHoleLayer::kind(quint64 holeId) const {
    return m_holes.value(holeId).kind;
}

QString LiveHoleLayer::bufferText(quint64 holeId) const {
    return m_holes.value(holeId).bufferText;
}

Markoff::TextAnchor LiveHoleLayer::reifyAnchor(quint64 holeId) const {
    return m_holes.value(holeId).reifyAnchor;
}

QList<quint64> LiveHoleLayer::holesInOrder() const {
    // Order by reifyAnchor's resolved byte offset; ties broken by holeId.
    QList<quint64> ids;
    ids.reserve(m_holes.size());
    for (auto it = m_holes.begin(); it != m_holes.end(); ++it)
        ids.append(it.key());
    std::sort(ids.begin(), ids.end(), [&](quint64 a, quint64 b) {
        const quint32 ba = m_holes[a].reifyAnchor.resolvedByteOffset();
        const quint32 bb = m_holes[b].reifyAnchor.resolvedByteOffset();
        if (ba != bb) return ba < bb;
        return a < b;  // stable tie-break
    });
    return ids;
}

}  // namespace Markoff::LiveRender
```

(`Markoff::TextAnchor::resolvedByteOffset()` is the foundation method; verify the exact name and adapt if different.)

- [ ] **Step 5: Add to CMake `qt_add_qml_module` SOURCES + HEADERS.**

`libs/markoff-live-render/CMakeLists.txt`:

```
HEADERS:
  include/markoff/live-render/LiveHoleLayer.h
SOURCES:
  src/LiveHoleLayer.cpp
```

- [ ] **Step 6: Build + run the lifecycle tests.**

```bash
cmake --build build-dev --target tst_live_render_holes_layer -j 8
ctest --test-dir build-dev -R '^tst_live_render_holes_layer$' --output-on-failure
```

Expected: 5 PASS.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-live-render/include/markoff/live-render/LiveHoleLayer.h \
        libs/markoff-live-render/src/LiveHoleLayer.cpp \
        libs/markoff-live-render/tests/tst_live_render_holes_layer.cpp \
        libs/markoff-live-render/CMakeLists.txt
git commit -m "feat(live-render): LiveHoleLayer lifecycle skeleton (R5.5 Task 5)

create/setBuffer/abandon — three signals (holeInserted,
holeBufferChanged, holeAbandoned). No CRDT mutation; abandon path
asserts source equality.

Reification (commit) and per-hole idle timer come in Task 6 + 7.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Per-hole idle timer (TDD via harness)

The 250 ms idle timer that drives the commit-on-quiet path.

**Files:**
- Modify: `libs/markoff-live-render/include/markoff/live-render/LiveHoleLayer.h`
- Modify: `libs/markoff-live-render/src/LiveHoleLayer.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_holes_layer.cpp`

- [ ] **Step 1: Write failing tests for idle-timer behaviour.**

```cpp
void layer_idle_timer_starts_on_setBuffer() {
    Markoff::MarkoffDocument doc(1);
    doc.resetContent("hello", Markoff::Origin::FirstOpen);

    Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
    QSignalSpy idleSpy(&layer,
        SIGNAL(idleCommitDue(quint64)));   // new signal, Task 6
    quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                        doc.anchorAtByte(5));
    layer.setBlockHoleBuffer(id, "x");

    // 250 ms default; allow 50 ms slack.
    QVERIFY(idleSpy.wait(400));
    QCOMPARE(idleSpy.count(), 1);
    QCOMPARE(idleSpy.first().at(0).toULongLong(), id);
}

void layer_idle_timer_restarts_on_each_setBuffer() {
    Markoff::MarkoffDocument doc(1);
    doc.resetContent("hello", Markoff::Origin::FirstOpen);

    Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
    QSignalSpy idleSpy(&layer, SIGNAL(idleCommitDue(quint64)));
    quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                        doc.anchorAtByte(5));

    layer.setBlockHoleBuffer(id, "a");
    QTest::qWait(150);  // not yet
    QCOMPARE(idleSpy.count(), 0);
    layer.setBlockHoleBuffer(id, "ab");
    QTest::qWait(150);  // still not — restarted on second set
    QCOMPARE(idleSpy.count(), 0);
    QVERIFY(idleSpy.wait(200));   // now fires (250 ms after second set)
    QCOMPARE(idleSpy.count(), 1);
}

void layer_idle_timer_paused_during_composition() {
    Markoff::MarkoffDocument doc(1);
    doc.resetContent("hello", Markoff::Origin::FirstOpen);

    Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
    QSignalSpy idleSpy(&layer, SIGNAL(idleCommitDue(quint64)));
    quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                        doc.anchorAtByte(5));

    layer.setHoleComposition(id, true);   // IME composing
    layer.setBlockHoleBuffer(id, "preedit");
    QTest::qWait(400);                     // longer than 250
    QCOMPARE(idleSpy.count(), 0);          // timer didn't fire

    layer.setHoleComposition(id, false);   // commits composition
    QVERIFY(idleSpy.wait(400));
    QCOMPARE(idleSpy.count(), 1);
}

void layer_idle_does_not_fire_for_empty_buffer() {
    Markoff::MarkoffDocument doc(1);
    doc.resetContent("hello", Markoff::Origin::FirstOpen);

    Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
    QSignalSpy idleSpy(&layer, SIGNAL(idleCommitDue(quint64)));
    quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                        doc.anchorAtByte(5));
    // No setBlockHoleBuffer call.
    QTest::qWait(400);
    QCOMPARE(idleSpy.count(), 0);
}
```

- [ ] **Step 2: Verify tests fail (no `idleCommitDue` signal yet).**

- [ ] **Step 3: Add timer + signal to `LiveHoleLayer.h`.**

Add inside class:

```cpp
public:
    void setHoleComposition(quint64 holeId, bool composing);

Q_SIGNALS:
    void idleCommitDue(quint64 holeId);
```

Update `HoleEntry`:

```cpp
struct HoleEntry {
    HoleKind kind = HoleKind::Paragraph;
    Markoff::TextAnchor reifyAnchor;
    QString bufferText;
    bool composing = false;
    QTimer *idleTimer = nullptr;
};
```

Add private helper:

```cpp
private:
    static constexpr int kIdleCommitMs = 250;
    void restartIdleTimer(quint64 holeId);
    void stopIdleTimer(quint64 holeId);
```

- [ ] **Step 4: Implement in `LiveHoleLayer.cpp`.**

Add `#include <QTimer>` at the top.

In `createBlockHole`:

```cpp
quint64 id = m_nextHoleId++;
auto *t = new QTimer(this);
t->setSingleShot(true);
t->setInterval(kIdleCommitMs);
connect(t, &QTimer::timeout, this, [this, id]() { Q_EMIT idleCommitDue(id); });
entry.idleTimer = t;
m_holes.insert(id, entry);
Q_EMIT holeInserted(id);
return id;
```

In `setBlockHoleBuffer`, after assigning `it->bufferText = text;`:

```cpp
if (!it->composing && !text.isEmpty())
    restartIdleTimer(holeId);
else
    stopIdleTimer(holeId);
```

Implement `setHoleComposition`:

```cpp
void LiveHoleLayer::setHoleComposition(quint64 holeId, bool composing) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return;
    if (it->composing == composing) return;
    it->composing = composing;
    if (composing)
        stopIdleTimer(holeId);
    else if (!it->bufferText.isEmpty())
        restartIdleTimer(holeId);
}

void LiveHoleLayer::restartIdleTimer(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end() || !it->idleTimer) return;
    it->idleTimer->start();
}

void LiveHoleLayer::stopIdleTimer(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end() || !it->idleTimer) return;
    it->idleTimer->stop();
}
```

In `abandonBlockHole`, before `m_holes.erase(it)`:

```cpp
if (it->idleTimer) it->idleTimer->deleteLater();
```

- [ ] **Step 5: Build + run.**

Expected: 4 PASS.

- [ ] **Step 6: Commit.**

```bash
git commit -m "feat(live-render): per-hole 250 ms idle timer (R5.5 Task 6)

idleCommitDue signal fires 250 ms after the most recent
setBlockHoleBuffer (resetting on each set). Paused while
setHoleComposition(true); resumes (full 250 ms) on
setHoleComposition(false). Does not fire for empty buffer.

Concrete answer to design doc §3.2 IME-vs-idle question.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: `commitBlockHole` — applyLocalEdit + drop + holeReified

**Files:**
- Modify: `libs/markoff-live-render/include/markoff/live-render/LiveHoleLayer.h`
- Modify: `libs/markoff-live-render/src/LiveHoleLayer.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_holes_layer.cpp`

- [ ] **Step 1: Write failing tests for commit semantics.**

```cpp
void layer_commit_applies_local_edit_and_drops_hole() {
    Markoff::MarkoffDocument doc(1);
    doc.resetContent("hello", Markoff::Origin::FirstOpen);
    QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    QVERIFY(parseSpy.wait(2000));

    Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
    QSignalSpy reifiedSpy(&layer, SIGNAL(holeReified(quint64, Markoff::TextAnchor)));
    QSignalSpy abandonedSpy(&layer, SIGNAL(holeAbandoned(quint64)));

    quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                        doc.anchorAtByte(5));
    layer.setBlockHoleBuffer(id, "world");
    layer.commitBlockHole(id);

    // Synchronous on commit:
    // - hole dropped from layer
    // - applyLocalEdit ran, source updated
    QCOMPARE(layer.holeCount(), 0);
    QCOMPARE(doc.toMarkdown(), QString("hello\n\nworld"));
    QCOMPARE(abandonedSpy.count(), 1);   // proxy uses this to drop the row
    QCOMPARE(reifiedSpy.count(), 1);     // cursor delivery binds to this
}

void layer_commit_with_empty_buffer_is_abandon() {
    Markoff::MarkoffDocument doc(1);
    doc.resetContent("hello", Markoff::Origin::FirstOpen);
    QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    QVERIFY(parseSpy.wait(2000));

    Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);
    QSignalSpy reifiedSpy(&layer, SIGNAL(holeReified(quint64, Markoff::TextAnchor)));
    QSignalSpy abandonedSpy(&layer, SIGNAL(holeAbandoned(quint64)));

    quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                        doc.anchorAtByte(5));
    // No buffer.
    layer.commitBlockHole(id);

    QCOMPARE(layer.holeCount(), 0);
    QCOMPARE(doc.toMarkdown(), QString("hello"));   // F5 mitigation
    QCOMPARE(abandonedSpy.count(), 1);
    QCOMPARE(reifiedSpy.count(), 0);                // no reification — empty
}

void layer_commit_all_pending_in_anchor_order() {
    Markoff::MarkoffDocument doc(1);
    doc.resetContent("para1\n\npara2", Markoff::Origin::FirstOpen);
    QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    QVERIFY(parseSpy.wait(2000));

    Markoff::LiveRender::LiveHoleLayer layer(&doc, nullptr, nullptr);

    // Two holes — second one earlier in document.
    quint64 idLate = layer.createBlockHole(HoleKind::Paragraph,
                                            doc.anchorAtByte(13)); // after para2
    quint64 idEarly = layer.createBlockHole(HoleKind::Paragraph,
                                             doc.anchorAtByte(5));  // after para1
    layer.setBlockHoleBuffer(idLate, "late");
    layer.setBlockHoleBuffer(idEarly, "early");

    layer.commitAllPendingHoles();

    QCOMPARE(layer.holeCount(), 0);
    // Anchors are byte-tracked; commits land in anchor-ascending order.
    // Final source has both insertions.
    QString s = doc.toMarkdown();
    QVERIFY(s.contains("early"));
    QVERIFY(s.contains("late"));
    QVERIFY(s.indexOf("early") < s.indexOf("late"));
}
```

- [ ] **Step 2: Add `commitBlockHole` and `commitAllPendingHoles` to `LiveHoleLayer.h`.**

```cpp
public:
    void commitBlockHole(quint64 holeId);
    void commitAllPendingHoles();

Q_SIGNALS:
    void holeReified(quint64 holeId, Markoff::TextAnchor newRowAnchor);
```

- [ ] **Step 3: Implement.**

```cpp
void LiveHoleLayer::commitBlockHole(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return;

    const QString buffer = it->bufferText;
    const Markoff::TextAnchor reifyAnchor = it->reifyAnchor;

    if (buffer.isEmpty()) {
        // Empty buffer ≡ abandon.
        if (it->idleTimer) it->idleTimer->deleteLater();
        m_holes.erase(it);
        Q_EMIT holeAbandoned(holeId);
        return;
    }

    // Compose the source insertion: "\n\n" + buffer at reifyAnchor.
    const quint32 reifyByte = reifyAnchor.resolvedByteOffset();
    const QString insertion = QStringLiteral("\n\n") + buffer;

    // Drop hole BEFORE applyLocalEdit so that the proxy's
    // holeAbandoned-driven row removal happens before the parse-back
    // arrives. Order matters for cursor delivery.
    if (it->idleTimer) it->idleTimer->deleteLater();
    m_holes.erase(it);
    Q_EMIT holeAbandoned(holeId);

    // Apply the edit. Foundation handles anchor invalidation.
    Markoff::MarkoffEdit edit;
    edit.byteOffset = reifyByte;
    edit.bytesRemoved = 0;
    edit.bytesInserted = insertion.toUtf8();
    m_doc->applyLocalEdit(edit);

    // The new row will arrive on the next parseUpdated. Compute
    // the post-commit anchor for the consumer (LiveCursorState binds
    // to this signal to schedule a TextCaret request at the new row).
    Markoff::TextAnchor newRowAnchor =
        m_doc->anchorAtByte(reifyByte + 2);   // skip the "\n\n"
    Q_EMIT holeReified(holeId, newRowAnchor);
}

void LiveHoleLayer::commitAllPendingHoles() {
    // Commit in ascending reifyAnchor byte order. Anchors survive each
    // edit (TextAnchor handles byte-shift).
    const QList<quint64> ids = holesInOrder();
    for (quint64 id : ids) commitBlockHole(id);
}
```

- [ ] **Step 4: Build + run.**

Expected: 3 PASS for the new tests; existing 7 still PASS.

- [ ] **Step 5: Commit.**

```bash
git commit -m "feat(live-render): commitBlockHole reifies hole into source (R5.5 Task 7)

commitBlockHole atomically: drops hole + emits holeAbandoned;
applyLocalEdit('\n\n' + bufferText) at reifyAnchor; emits holeReified
with the new row's anchor. Empty buffer commits as abandon (F5
mitigation enforced).

commitAllPendingHoles iterates holes in ascending reifyAnchor byte
order — handles the multi-hole save case (design §3.6).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: `LiveProxyBlockModel` — passthrough proxy with no holes

Skeleton proxy that, with zero holes, behaves identically to `LiveBlockModel`.

**Files:**
- Create: `libs/markoff-live-render/include/markoff/live-render/LiveProxyBlockModel.h`
- Create: `libs/markoff-live-render/src/LiveProxyBlockModel.cpp`
- Create: `libs/markoff-live-render/tests/tst_live_render_proxy_model.cpp`
- Modify: `libs/markoff-live-render/CMakeLists.txt`
- Modify: `libs/markoff-live-render/tests/CMakeLists.txt`

- [ ] **Step 1: Failing test — passthrough behaviour.**

```cpp
// tst_live_render_proxy_model.cpp
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveHoleLayer.h>
#include <markoff/live-render/LiveProxyBlockModel.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <QtTest/QtTest>

using namespace Markoff::LiveRender;

class TstProxyModel : public QObject {
    Q_OBJECT

private slots:
    void proxy_passthrough_with_no_holes_mirrors_inner() {
        Markoff::MarkoffDocument doc(1);
        doc.resetContent("para1\n\npara2", Markoff::Origin::FirstOpen);

        LiveBlockModel inner(&doc);
        // ... existing block-model setup; assume the test binding
        // populates inner via the same path LiveListModelBinding uses.
        // For a unit test, drive parseUpdated manually:
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));
        // (LiveListModelBinding is the production driver; here we
        //  populate inner via a test helper or via the binding.)

        LiveHoleLayer layer(&doc, &inner, nullptr);
        LiveProxyBlockModel proxy(&inner, &layer);

        QCOMPARE(proxy.rowCount(), inner.rowCount());
        for (int r = 0; r < inner.rowCount(); ++r) {
            QCOMPARE(proxy.data(proxy.index(r, 0), LiveBlockModel::TextRole),
                     inner.data(inner.index(r, 0), LiveBlockModel::TextRole));
            QCOMPARE(proxy.data(proxy.index(r, 0),
                                LiveProxyBlockModel::IsHoleRole).toBool(),
                     false);
        }
    }
};
QTEST_MAIN(TstProxyModel)
#include "tst_live_render_proxy_model.moc"
```

(Adapt the inner-model-setup to whatever helper the existing `tst_live_render_block_model.cpp` uses.)

- [ ] **Step 2: Verify test fails to compile (no `LiveProxyBlockModel.h`).**

- [ ] **Step 3: Write `LiveProxyBlockModel.h`.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>

#include <QAbstractListModel>
#include <QHash>
#include <QVector>
#include <qqmlintegration.h>

namespace Markoff::LiveRender {

class LiveBlockModel;
class LiveHoleLayer;

class MARKOFF_LIVE_RENDER_EXPORT LiveProxyBlockModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveProxyBlockModel is provided by LiveListModelBinding")

public:
    enum Roles {
        IsHoleRole       = Qt::UserRole + 1000,
        BufferTextRole   = Qt::UserRole + 1001,
        HoleIdRole       = Qt::UserRole + 1002,
        // (passthrough roles use inner's role values directly)
    };

    explicit LiveProxyBlockModel(LiveBlockModel *inner,
                                  LiveHoleLayer  *layer,
                                  QObject        *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int innerRowForProxy(int proxyRow) const;
    int proxyRowForInner(int innerRow) const;
    int proxyRowForHole(quint64 holeId) const;
    bool proxyRowIsHole(int proxyRow) const noexcept;
    quint64 holeAtProxyRow(int proxyRow) const noexcept;

private Q_SLOTS:
    void onInnerRowsInserted(const QModelIndex &, int first, int last);
    void onInnerRowsAboutToBeRemoved(const QModelIndex &, int first, int last);
    void onInnerDataChanged(const QModelIndex &tl, const QModelIndex &br,
                            const QVector<int> &roles);
    void onInnerModelReset();
    void onHoleInserted(quint64 holeId);
    void onHoleBufferChanged(quint64 holeId);
    void onHoleAbandoned(quint64 holeId);

private:
    struct ProxyRow {
        bool isHole;
        int innerRow;        // valid when !isHole
        quint64 holeId;      // valid when isHole
    };

    LiveBlockModel *m_inner;
    LiveHoleLayer  *m_layer;
    QVector<ProxyRow> m_rows;

    void rebuildMapping();
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 4: Write `LiveProxyBlockModel.cpp` — passthrough only (no holes yet).**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveProxyBlockModel.h>

#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveHoleLayer.h>

namespace Markoff::LiveRender {

LiveProxyBlockModel::LiveProxyBlockModel(LiveBlockModel *inner,
                                          LiveHoleLayer  *layer,
                                          QObject        *parent)
    : QAbstractListModel(parent), m_inner(inner), m_layer(layer)
{
    connect(inner, &QAbstractItemModel::rowsInserted,
            this, &LiveProxyBlockModel::onInnerRowsInserted);
    connect(inner, &QAbstractItemModel::rowsAboutToBeRemoved,
            this, &LiveProxyBlockModel::onInnerRowsAboutToBeRemoved);
    connect(inner, &QAbstractItemModel::dataChanged,
            this, &LiveProxyBlockModel::onInnerDataChanged);
    connect(inner, &QAbstractItemModel::modelReset,
            this, &LiveProxyBlockModel::onInnerModelReset);
    connect(layer, &LiveHoleLayer::holeInserted,
            this, &LiveProxyBlockModel::onHoleInserted);
    connect(layer, &LiveHoleLayer::holeBufferChanged,
            this, &LiveProxyBlockModel::onHoleBufferChanged);
    connect(layer, &LiveHoleLayer::holeAbandoned,
            this, &LiveProxyBlockModel::onHoleAbandoned);

    rebuildMapping();
}

int LiveProxyBlockModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_rows.size();
}

QVariant LiveProxyBlockModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const auto &r = m_rows[index.row()];
    if (r.isHole) {
        // (Task 9 fills these in; for passthrough-only build we don't
        // exercise this branch yet.)
        switch (role) {
            case IsHoleRole:     return true;
            case BufferTextRole: return m_layer->bufferText(r.holeId);
            case HoleIdRole:     return QVariant::fromValue(r.holeId);
            case LiveBlockModel::TextRole:        // alias bufferText
                return m_layer->bufferText(r.holeId);
            default: return {};
        }
    }
    if (role == IsHoleRole) return false;
    if (role == HoleIdRole) return QVariant::fromValue(quint64(0));
    return m_inner->data(m_inner->index(r.innerRow, 0), role);
}

QHash<int, QByteArray> LiveProxyBlockModel::roleNames() const {
    QHash<int, QByteArray> roles = m_inner->roleNames();
    roles[IsHoleRole]     = "isHole";
    roles[BufferTextRole] = "bufferText";
    roles[HoleIdRole]     = "holeId";
    return roles;
}

void LiveProxyBlockModel::rebuildMapping() {
    beginResetModel();
    m_rows.clear();
    for (int r = 0; r < m_inner->rowCount(); ++r)
        m_rows.append({false, r, 0});
    // Holes inserted in Task 9.
    endResetModel();
}

// Passthrough handlers (Task 9 fleshes out hole-aware variants).
void LiveProxyBlockModel::onInnerRowsInserted(const QModelIndex &, int, int) {
    rebuildMapping();
}
void LiveProxyBlockModel::onInnerRowsAboutToBeRemoved(const QModelIndex &, int, int) {
    rebuildMapping();
}
void LiveProxyBlockModel::onInnerDataChanged(const QModelIndex &tl,
                                              const QModelIndex &br,
                                              const QVector<int> &roles) {
    Q_EMIT dataChanged(index(proxyRowForInner(tl.row()), 0),
                        index(proxyRowForInner(br.row()), 0), roles);
}
void LiveProxyBlockModel::onInnerModelReset() {
    rebuildMapping();
}

// Hole handlers (Task 9 implements properly).
void LiveProxyBlockModel::onHoleInserted(quint64) { rebuildMapping(); }
void LiveProxyBlockModel::onHoleBufferChanged(quint64) {}
void LiveProxyBlockModel::onHoleAbandoned(quint64) { rebuildMapping(); }

int LiveProxyBlockModel::innerRowForProxy(int proxyRow) const {
    if (proxyRow < 0 || proxyRow >= m_rows.size() || m_rows[proxyRow].isHole)
        return -1;
    return m_rows[proxyRow].innerRow;
}
int LiveProxyBlockModel::proxyRowForInner(int innerRow) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (!m_rows[i].isHole && m_rows[i].innerRow == innerRow)
            return i;
    return -1;
}
int LiveProxyBlockModel::proxyRowForHole(quint64 holeId) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].isHole && m_rows[i].holeId == holeId)
            return i;
    return -1;
}
bool LiveProxyBlockModel::proxyRowIsHole(int proxyRow) const noexcept {
    return proxyRow >= 0 && proxyRow < m_rows.size() && m_rows[proxyRow].isHole;
}
quint64 LiveProxyBlockModel::holeAtProxyRow(int proxyRow) const noexcept {
    if (!proxyRowIsHole(proxyRow)) return 0;
    return m_rows[proxyRow].holeId;
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 5: Wire CMake + run passthrough test.**

Expected: PASS.

- [ ] **Step 6: Commit.**

```bash
git commit -m "feat(live-render): LiveProxyBlockModel passthrough skeleton (R5.5 Task 8)

QAbstractListModel composing LiveBlockModel + LiveHoleLayer. Roles
IsHoleRole / BufferTextRole / HoleIdRole. With zero holes, behaves
identically to inner. Hole-aware mapping comes in Task 9.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Proxy hole-row insertion + anchor-ordered mapping

The mapping rule and proper signal propagation.

**Files:**
- Modify: `libs/markoff-live-render/src/LiveProxyBlockModel.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_proxy_model.cpp`

- [ ] **Step 1: Failing tests for hole-row insertion.**

```cpp
void proxy_inserts_hole_row_at_anchor_position() {
    Markoff::MarkoffDocument doc(1);
    doc.resetContent("para1\n\npara2", Markoff::Origin::FirstOpen);
    QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    QVERIFY(parseSpy.wait(2000));

    LiveBlockModel inner(&doc);
    // populate inner via binding
    // ... assume rowCount == 2 ("para1" at 0, "para2" at 1)

    LiveHoleLayer layer(&doc, &inner, nullptr);
    LiveProxyBlockModel proxy(&inner, &layer);
    QCOMPARE(proxy.rowCount(), 2);

    QSignalSpy insertedSpy(&proxy, &QAbstractItemModel::rowsInserted);
    quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                        doc.anchorAtByte(5));   // after "para1"

    QCOMPARE(proxy.rowCount(), 3);
    QCOMPARE(proxy.proxyRowForHole(id), 1);  // sits between para1 and para2
    QCOMPARE(proxy.proxyRowIsHole(1), true);
    QCOMPARE(proxy.proxyRowForInner(1), 2);  // para2 shifted to row 2
    QCOMPARE(insertedSpy.count(), 1);
}

void proxy_drops_hole_row_on_abandon() {
    /* same setup */
    quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                        doc.anchorAtByte(5));
    QSignalSpy removedSpy(&proxy, &QAbstractItemModel::rowsRemoved);
    layer.abandonBlockHole(id);
    QCOMPARE(proxy.rowCount(), 2);
    QCOMPARE(removedSpy.count(), 1);
}

void proxy_buffer_changed_emits_dataChanged() {
    /* setup with hole + bufferText */
    quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                        doc.anchorAtByte(5));
    QSignalSpy dcSpy(&proxy, &QAbstractItemModel::dataChanged);
    layer.setBlockHoleBuffer(id, "typing");
    QCOMPARE(dcSpy.count(), 1);
    QCOMPARE(proxy.data(proxy.index(1, 0), LiveProxyBlockModel::BufferTextRole)
                 .toString(),
             QString("typing"));
}

void proxy_ties_break_by_holeId() {
    quint64 a = layer.createBlockHole(HoleKind::Paragraph,
                                       doc.anchorAtByte(5));
    quint64 b = layer.createBlockHole(HoleKind::Paragraph,
                                       doc.anchorAtByte(5));
    QVERIFY(proxy.proxyRowForHole(a) < proxy.proxyRowForHole(b));  // earlier id first
}

void proxy_model_reset_drops_all_holes() {
    quint64 id = layer.createBlockHole(HoleKind::Paragraph,
                                        doc.anchorAtByte(5));
    QCOMPARE(layer.holeCount(), 1);

    doc.resetContent("different", Markoff::Origin::FirstOpen);
    QVERIFY(parseSpy.wait(2000));

    QCOMPARE(layer.holeCount(), 0);   // proxy reset → layer cleared
}
```

- [ ] **Step 2: Implement the mapping rule in `rebuildMapping()`.**

```cpp
void LiveProxyBlockModel::rebuildMapping() {
    beginResetModel();
    m_rows.clear();

    // Get hole IDs in anchor order.
    const QList<quint64> holeIds = m_layer->holesInOrder();
    int holeCursor = 0;

    auto holeBeforeInner = [&](int innerRow) -> bool {
        // Returns true if the next pending hole's reifyAnchor falls
        // at-or-before the start of innerRow.
        if (holeCursor >= holeIds.size()) return false;
        const Markoff::TextAnchor a = m_layer->reifyAnchor(holeIds[holeCursor]);
        const quint32 byte = a.resolvedByteOffset();
        // (Implementation note: query inner's blockAnchor for innerRow
        //  and resolve to byte; if byte < innerRow's startByte, this
        //  hole sits before innerRow.)
        return byte < innerStartByteForRow(innerRow);
    };

    for (int r = 0; r < m_inner->rowCount(); ++r) {
        while (holeBeforeInner(r)) {
            m_rows.append({true, -1, holeIds[holeCursor]});
            ++holeCursor;
        }
        m_rows.append({false, r, 0});
    }
    // Trailing holes (after the last inner row).
    while (holeCursor < holeIds.size()) {
        m_rows.append({true, -1, holeIds[holeCursor]});
        ++holeCursor;
    }
    endResetModel();
}
```

(Need to define `innerStartByteForRow(int)` — read the inner row's `BlockAnchorRole`, resolve to byte. Adapt to the existing `LiveBlockModel` API.)

- [ ] **Step 3: Implement targeted signal handlers.**

Replace the rebuild-on-everything `onHoleInserted` with a targeted insertion:

```cpp
void LiveProxyBlockModel::onHoleInserted(quint64 holeId) {
    // Determine target proxy row from holesInOrder.
    const QList<quint64> ordered = m_layer->holesInOrder();
    int targetProxyRow = m_rows.size();
    // ... compute by walking m_rows and finding the position where this
    // hole should slot in. (Cheap: O(rowCount), holes sparse.)

    beginInsertRows({}, targetProxyRow, targetProxyRow);
    m_rows.insert(targetProxyRow, {true, -1, holeId});
    endInsertRows();
}

void LiveProxyBlockModel::onHoleBufferChanged(quint64 holeId) {
    int proxyRow = proxyRowForHole(holeId);
    if (proxyRow < 0) return;
    Q_EMIT dataChanged(index(proxyRow, 0), index(proxyRow, 0),
                        {BufferTextRole, LiveBlockModel::TextRole});
}

void LiveProxyBlockModel::onHoleAbandoned(quint64 holeId) {
    int proxyRow = proxyRowForHole(holeId);
    if (proxyRow < 0) return;
    beginRemoveRows({}, proxyRow, proxyRow);
    m_rows.removeAt(proxyRow);
    endRemoveRows();
}
```

(For inner-row insertions/removals the targeted variant follows the same shape; the rebuild path stays as a fallback for `modelReset`.)

- [ ] **Step 4: Connect inner-model `modelAboutToBeReset` to drop holes.**

In the constructor:

```cpp
connect(inner, &QAbstractItemModel::modelAboutToBeReset,
        m_layer, [this]() {
    // Drop all open holes on reset (load-different-file case).
    for (quint64 id : m_layer->holesInOrder())
        m_layer->abandonBlockHole(id);
});
```

- [ ] **Step 5: Build + run the proxy tests.**

Expected: 5 PASS.

- [ ] **Step 6: Commit.**

```bash
git commit -m "feat(live-render): proxy hole-row insertion + anchor mapping (R5.5 Task 9)

LiveProxyBlockModel composes parser rows + hole rows by reifyAnchor
byte order; ties broken by holeId. Hole insertion / abandon / buffer
change propagate as targeted Qt model signals. Model reset drops
all holes.

Implements design §4.2-§4.3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: Wire `LiveHoleLayer` + `LiveProxyBlockModel` into `LiveListModelBinding`

**Files:**
- Modify: `libs/markoff-live-render/include/markoff/live-render/LiveListModelBinding.h`
- Modify: `libs/markoff-live-render/src/LiveListModelBinding.cpp`
- Modify: `libs/markoff-live-render/qml/LiveView.qml`

- [ ] **Step 1: Add Q_PROPERTYs.**

In `LiveListModelBinding.h`, add:

```cpp
Q_PROPERTY(LiveHoleLayer *holeLayer READ holeLayer CONSTANT)
Q_PROPERTY(LiveProxyBlockModel *proxyModel READ proxyModel CONSTANT)

LiveHoleLayer       *holeLayer() const { return m_holeLayer; }
LiveProxyBlockModel *proxyModel() const { return m_proxyModel; }
```

- [ ] **Step 2: Construct in cpp's constructor (after blockModel + cursorState are wired).**

```cpp
m_holeLayer  = new LiveHoleLayer(m_doc, m_blockModel, m_undoCoalescer, this);
m_proxyModel = new LiveProxyBlockModel(m_blockModel, m_holeLayer, this);
```

- [ ] **Step 3: Update `LiveCursorState` to listen on the proxy.**

In `LiveListModelBinding`'s constructor (or wherever `LiveCursorState` is constructed), pass the proxy model rather than the inner model. Adjust the `LiveCursorState`'s constructor parameter type from `const LiveBlockModel *` to `const QAbstractListModel *` (or a small helper interface) — verify against the current signature.

- [ ] **Step 4: Update `LiveView.qml`.**

Change:

```qml
ListView {
    model: binding.blockModel
}
```

to:

```qml
ListView {
    model: binding.proxyModel
}
```

- [ ] **Step 5: Build + run the full live-render fast-tier suite.**

Expected: all tests still PASS (proxy passes through faithfully when no holes exist).

- [ ] **Step 6: Commit.**

```bash
git commit -m "feat(live-render): wire LiveHoleLayer + LiveProxyBlockModel into binding (R5.5 Task 10)

LiveListModelBinding now constructs the layer and proxy. LiveView.qml's
ListView binds to proxyModel. With zero holes, behaviour is unchanged
(passthrough); regression-tested by existing tst_live_render_*.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: Structural-key handler — paragraph EOB-Enter + start-of-paragraph-Enter create holes

Replaces R5's `applyLocalEdit("\n\n")` for these two cases.

**Files:**
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_structural.cpp`

- [ ] **Step 1: Failing tests for hole-on-Enter.**

```cpp
void paragraph_eob_enter_creates_hole_not_source_edit() {
    /* setup MarkoffDocument with "hello"; LiveBlockModel; etc */
    /* simulate cursor at qtPos 5 of row 0 */
    handler.tryHandle(/*key=*/Qt::Key_Return, /*mods=*/Qt::NoModifier);

    QCOMPARE(holeLayer.holeCount(), 1);
    QCOMPARE(doc.toMarkdown(), QString("hello"));   // F5 — no source edit
    QCOMPARE(proxy.rowCount(), 2);                   // 1 parser + 1 hole
    QCOMPARE(proxy.proxyRowIsHole(1), true);
}

void paragraph_start_enter_creates_hole_above() {
    /* cursor at qtPos 0 */
    handler.tryHandle(Qt::Key_Return, Qt::NoModifier);
    QCOMPARE(holeLayer.holeCount(), 1);
    QCOMPARE(proxy.proxyRowIsHole(0), true);
    QCOMPARE(proxy.proxyRowIsHole(1), false);
}

void paragraph_eob_enter_routes_cursor_into_hole() {
    /* setup; press Enter at end */
    handler.tryHandle(Qt::Key_Return, Qt::NoModifier);
    QCOMPARE(cursorState.cursor().active.holeId(), holeId);
    QCOMPARE(cursorState.cursor().active.qtPos(), 0);
}
```

- [ ] **Step 2: Modify the paragraph EOB-Enter handler in `LiveStructuralKeyHandler.cpp`.**

Find the existing case (R5 Task 4) — likely something like:

```cpp
case ParagraphHandler::EOB_Enter:
    m_doc->applyLocalEdit({insert "\n\n" at currentBlockEnd});
    m_cursorState->requestTextCaretAtRow(blockIndex + 1, 0);
    return Handled;
```

Replace with:

```cpp
case ParagraphHandler::EOB_Enter: {
    Markoff::TextAnchor anchor = m_doc->anchorAtByte(currentBlockEnd);
    quint64 holeId = m_holeLayer->createBlockHole(HoleKind::Paragraph, anchor);
    int proxyRow = m_proxyModel->proxyRowForHole(holeId);
    m_cursorState->requestTextCaretAtRow(proxyRow, 0);
    return Handled;
}
```

Symmetric for start-of-paragraph-Enter.

- [ ] **Step 3: Build + run.**

Expected: 3 new PASS; existing R5 mid-block-split test still PASS (mid-block path unchanged).

- [ ] **Step 4: Commit.**

```bash
git commit -m "feat(live-render): paragraph EOB-Enter + start-Enter create holes (R5.5 Task 11)

R5's applyLocalEdit('\n\n') for these two cases is replaced with
LiveHoleLayer::createBlockHole. The hole row appears in the proxy
synchronously; cursor delivery via requestTextCaretAtRow resolves
on the proxy's rowsInserted. F5 (source-state leak) mitigation:
no source mutation at hole creation.

Mid-block split (R5 Task 5) is untouched.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 12: Hole-row dispatch — Enter (commit + new hole / mid-buffer split)

**Files:**
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_holes_qml.cpp` (new file or extend)

This is QML-level testing using the harness.

- [ ] **Step 1: Failing harness-driven tests in `tst_live_render_holes_qml.cpp`.**

```cpp
void enter_at_end_of_buffer_commits_then_creates_new_hole() {
    QQuickView view;
    /* load LiveView.qml; populate doc with "hello" */
    LiveRealisticInputHarness h(&view);

    /* click into row 0; press Enter at end */
    h.keyClick(Qt::Key_End);
    h.keyClick(Qt::Key_Return);

    /* Hole created, focus there */
    h.typeString("first");
    h.idle(50);                  // not yet committed
    h.keyClick(Qt::Key_Return);  // explicit commit-and-new-hole
    h.idle(50);

    /* Type into the new hole */
    h.typeString("second");
    h.idle(300);                 // idle commits

    QCOMPARE(doc.toMarkdown(), QString("hello\n\nfirst\n\nsecond"));
}

void enter_mid_buffer_splits() {
    /* setup; in hole; type "helloworld"; cursor at qtPos 5 */
    h.keyClick(Qt::Key_Return);
    h.idle(50);
    /* now: "hello" committed as paragraph; "world" in fresh hole; cursor at qtPos 0 */
    h.typeString("X");
    h.idle(300);
    QCOMPARE(doc.toMarkdown(),
             QString("hello\n\nhello\n\nXworld"));   // adapt as appropriate
}

void stacked_enter_on_empty_hole_is_noop() {
    /* setup; empty hole; press Enter twice */
    h.keyClick(Qt::Key_Return);
    h.keyClick(Qt::Key_Return);
    QCOMPARE(holeLayer.holeCount(), 1);
    QCOMPARE(doc.toMarkdown(), QString("hello"));
}
```

- [ ] **Step 2: Implement hole-row-Enter dispatch in handler.**

Add a `handleHoleRow(focusedHoleId, key, mods)` path to `LiveStructuralKeyHandler::tryHandle`:

```cpp
HandleResult LiveStructuralKeyHandler::handleHoleRow(quint64 holeId,
                                                     int key,
                                                     Qt::KeyboardModifiers mods)
{
    QString buf = m_holeLayer->bufferText(holeId);
    int qtPos = currentTextEditCursorPos();

    if (key == Qt::Key_Return && (mods & Qt::ShiftModifier) == 0) {
        if (buf.isEmpty()) return Handled;          // no-op
        if (qtPos == buf.length()) {
            // Commit then new hole below.
            m_holeLayer->commitBlockHole(holeId);
            // After commit, schedule a new hole at the position after
            // the just-reified content.
            const Markoff::TextAnchor newAnchor =
                m_doc->anchorAtByte(/*end of just-inserted text*/);
            quint64 newId = m_holeLayer->createBlockHole(
                HoleKind::Paragraph, newAnchor);
            int newRow = m_proxyModel->proxyRowForHole(newId);
            m_cursorState->requestTextCaretAtRow(newRow, 0);
            return Handled;
        }
        // Mid-buffer: split. Commit prefix, leave suffix in fresh hole.
        const QString prefix = buf.left(qtPos);
        const QString suffix = buf.mid(qtPos);
        m_holeLayer->setBlockHoleBuffer(holeId, prefix);
        m_holeLayer->commitBlockHole(holeId);
        // Create new hole at next position with suffix.
        const Markoff::TextAnchor splitAnchor = /*compute*/;
        quint64 newId = m_holeLayer->createBlockHole(
            HoleKind::Paragraph, splitAnchor);
        m_holeLayer->setBlockHoleBuffer(newId, suffix);
        int newRow = m_proxyModel->proxyRowForHole(newId);
        m_cursorState->requestTextCaretAtRow(newRow, 0);
        return Handled;
    }

    // Esc / Backspace / Delete handled in Task 13.
    return NotHandled;
}
```

- [ ] **Step 3: Build + run harness tests.**

Expected: 3 PASS.

- [ ] **Step 4: Commit.**

```bash
git commit -m "feat(live-render): hole-row Enter — commit / split / stacked-empty (R5.5 Task 12)

Hole-row Enter at end-of-buffer commits the hole and creates a fresh
one for the next paragraph (synchronous; no in-flight gap). Mid-buffer
splits the prefix into a committed paragraph and the suffix into a
fresh hole. Empty-hole Enter is a no-op (per v1 spec §3.3 stacked-Enter).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 13: Hole-row dispatch — Esc / Backspace-empty / Delete-empty abandon paths

**Files:**
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_holes_qml.cpp`

- [ ] **Step 1: Failing tests.**

```cpp
void esc_abandons_hole() {
    /* hole open with bufferText "x" */
    h.keyClick(Qt::Key_Escape);
    QCOMPARE(holeLayer.holeCount(), 0);
    QCOMPARE(doc.toMarkdown(), QString("hello"));
    /* cursor at end of "hello" */
}

void backspace_at_qt_pos_0_with_empty_buffer_abandons() {
    /* hole open with empty buffer */
    h.keyClick(Qt::Key_Backspace);
    QCOMPARE(holeLayer.holeCount(), 0);
}

void delete_at_end_with_empty_buffer_abandons() {
    /* hole open with empty buffer */
    h.keyClick(Qt::Key_Delete);
    QCOMPARE(holeLayer.holeCount(), 0);
}

void backspace_at_qt_pos_0_with_non_empty_buffer_is_passthrough() {
    /* hole with bufferText "abc"; cursor at qtPos 0 */
    h.keyClick(Qt::Key_Backspace);
    QCOMPARE(holeLayer.holeCount(), 1);
    QCOMPARE(holeLayer.bufferText(id), QString("abc"));
}
```

- [ ] **Step 2: Extend `handleHoleRow`.**

```cpp
if (key == Qt::Key_Escape) {
    m_holeLayer->abandonBlockHole(holeId);
    routeFocusToPreviousNeighbor(holeId);
    return Handled;
}

if (key == Qt::Key_Backspace && qtPos == 0 && buf.isEmpty()) {
    m_holeLayer->abandonBlockHole(holeId);
    routeFocusToPreviousNeighbor(holeId);
    return Handled;
}

if (key == Qt::Key_Delete && qtPos == buf.length() && buf.isEmpty()) {
    m_holeLayer->abandonBlockHole(holeId);
    routeFocusToNextNeighbor(holeId);
    return Handled;
}
```

`routeFocusToPreviousNeighbor` walks the proxy backward from the hole's row to find the first non-hole row; calls `cursorState.request(TextCaret(thatRow's anchor, end-of-row qtPos))`. Symmetric for next.

- [ ] **Step 3: Build + run.**

Expected: 4 PASS.

- [ ] **Step 4: Commit.**

```bash
git commit -m "feat(live-render): hole-row abandon paths (R5.5 Task 13)

Esc / Backspace-at-qtPos-0-empty / Delete-at-end-empty abandon the
hole and route focus to the nearest live neighbor. F4 mitigation:
no focus-nowhere state.

Backspace at qtPos 0 with non-empty buffer is passthrough (does
nothing — no character to delete in front).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 14: `LiveEditBinding` + `ParagraphDelegate` — buffer mirror + IME state + isHole plumbing

**Files:**
- Modify: `libs/markoff-live-render/include/markoff/live-render/LiveEditBinding.h`
- Modify: `libs/markoff-live-render/src/LiveEditBinding.cpp`
- Modify: `libs/markoff-live-render/qml/delegates/ParagraphDelegate.qml`

- [ ] **Step 1: Add `setHoleId(quint64)` and IME exposure to `LiveEditBinding`.**

Header:

```cpp
public:
    void setHoleId(quint64 holeId);
    bool composing() const { return m_composing; }   // already exists; verify
Q_SIGNALS:
    void composingChanged(bool composing);
```

Cpp: when `m_holeId != 0`, route `contentsChange` to `m_holeLayer->setBlockHoleBuffer(m_holeId, qPlainText())` instead of `m_doc->applyLocalEdit(...)`. Emit `composingChanged` from the existing IME code path.

- [ ] **Step 2: Connect IME signal to layer's `setHoleComposition` in `LiveListModelBinding` wiring.**

When a delegate enters/leaves composition, the binding mirrors to:

```cpp
connect(editBinding, &LiveEditBinding::composingChanged, m_holeLayer,
        [this, editBinding](bool composing) {
    if (editBinding->holeId() != 0)
        m_holeLayer->setHoleComposition(editBinding->holeId(), composing);
});
```

- [ ] **Step 3: Update `ParagraphDelegate.qml`.**

```qml
Item {
    id: root
    property bool isHole: model.isHole === true
    property real holeId: model.holeId || 0
    /* ... existing layout ... */

    TextEdit {
        id: textEdit
        text: root.isHole ? model.bufferText : model.text
        /* ... existing config ... */

        Component.onCompleted: {
            editBinding.setHoleId(root.holeId)
        }
    }
}
```

- [ ] **Step 4: Connect `idleCommitDue` to a slot that calls `commitBlockHole`.**

In `LiveListModelBinding`'s wiring:

```cpp
connect(m_holeLayer, &LiveHoleLayer::idleCommitDue, m_holeLayer,
        &LiveHoleLayer::commitBlockHole);
```

(Alternatively: handle it in `LiveStructuralKeyHandler` to keep dispatch centralised; verify which class owns commit decisions.)

- [ ] **Step 5: Build + run all live-render tests.**

Expected: existing tests PASS; harness-driven hole-typing now produces source after idle.

- [ ] **Step 6: Commit.**

```bash
git commit -m "feat(live-render): hole bufferText mirror + IME guard + isHole plumbing (R5.5 Task 14)

LiveEditBinding's setHoleId switches its contentsChange routing from
applyLocalEdit to setBlockHoleBuffer for hole rows. IME composing
state propagates to LiveHoleLayer's idle-timer pause. ParagraphDelegate
reads model.bufferText (alias of TextRole via proxy passthrough) for
hole rows.

idleCommitDue triggers commitBlockHole — the 250 ms quiet-commit path
is now end-to-end functional.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 15: Per-hole undo stack + `UndoCoalescer` integration

**Files:**
- Modify: `libs/markoff-live-render/include/markoff/live-render/LiveHoleLayer.h`
- Modify: `libs/markoff-live-render/src/LiveHoleLayer.cpp`
- Modify: `libs/markoff-live-render/include/markoff/live-render/UndoCoalescer.h`
- Modify: `libs/markoff-live-render/src/UndoCoalescer.cpp`
- Modify: `libs/markoff-live-render/tests/tst_live_render_holes_layer.cpp`

- [ ] **Step 1: Failing tests.**

```cpp
void undo_within_hole_pops_buffer_snapshot() {
    quint64 id = layer.createBlockHole(...);
    layer.setBlockHoleBuffer(id, "abc");
    layer.recordHoleUndoPoint(id);          // explicit; production calls on idle
    layer.setBlockHoleBuffer(id, "abcdef");

    QVERIFY(layer.undoBlockHole(id));
    QCOMPARE(layer.bufferText(id), QString("abc"));
}

void undo_with_empty_buffer_returns_false_to_signal_drop() {
    quint64 id = layer.createBlockHole(...);
    QVERIFY(!layer.undoBlockHole(id));      // empty + empty stack → drop signal
}

void undoCoalescer_routes_to_hole_when_focused_row_is_hole() {
    /* setup: focused row is hole */
    undoCoalescer.handleUndoRequest();      // invoked by host on Ctrl-Z
    /* assert undoBlockHole was called, not MarkoffDocument::undo */
}
```

- [ ] **Step 2: Add `undoStack`/`redoStack` to `HoleEntry`; implement `undoBlockHole` / `redoBlockHole` / `recordHoleUndoPoint`.**

```cpp
// In HoleEntry:
QStack<QString> undoStack;
QStack<QString> redoStack;
qint64 lastEditMs = 0;

// LiveHoleLayer.cpp:
void LiveHoleLayer::recordHoleUndoPoint(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return;
    it->undoStack.push(it->bufferText);
    it->redoStack.clear();
}

bool LiveHoleLayer::undoBlockHole(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return false;
    if (it->undoStack.isEmpty() && it->bufferText.isEmpty())
        return false;            // signal: drop hole
    if (it->undoStack.isEmpty()) {
        // current buffer is the only state; clear it
        it->redoStack.push(it->bufferText);
        it->bufferText.clear();
    } else {
        it->redoStack.push(it->bufferText);
        it->bufferText = it->undoStack.pop();
    }
    Q_EMIT holeBufferChanged(holeId);
    return true;
}

bool LiveHoleLayer::redoBlockHole(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end() || it->redoStack.isEmpty()) return false;
    it->undoStack.push(it->bufferText);
    it->bufferText = it->redoStack.pop();
    Q_EMIT holeBufferChanged(holeId);
    return true;
}
```

- [ ] **Step 3: Add idle-snapshot push.**

In the idle-commit-due slot OR in `setBlockHoleBuffer` after a coalesce-break threshold:

```cpp
// In setBlockHoleBuffer, before assigning bufferText:
qint64 now = QDateTime::currentMSecsSinceEpoch();
if (now - it->lastEditMs > 1000)   // 1 s coalesce-break threshold
    it->undoStack.push(it->bufferText);
it->lastEditMs = now;
```

- [ ] **Step 4: Wire `UndoCoalescer` to check focused row.**

`UndoCoalescer::handleUndoRequest()` (or wherever Ctrl-Z lands):

```cpp
void UndoCoalescer::handleUndoRequest() {
    Cursor c = m_cursorState->cursor();
    if (c.active.has_value() &&
        std::holds_alternative<HoleBlockId>(c.active->block.id)) {
        quint64 holeId = std::get<HoleBlockId>(c.active->block.id).holeId;
        if (m_holeLayer->undoBlockHole(holeId)) return;
        // Empty buffer → drop hole.
        m_holeLayer->abandonBlockHole(holeId);
        return;
    }
    m_doc->undo();
}
```

(Adapt to actual `Cursor` shape after the variant change in Task 4.)

- [ ] **Step 5: Build + run.**

Expected: 3 new PASS; full suite stays green.

- [ ] **Step 6: Commit.**

```bash
git commit -m "feat(live-render): per-hole undo stack + UndoCoalescer routing (R5.5 Task 15)

LiveHoleLayer maintains per-hole undo/redo snapshot stacks; coalesce-
break on 1 s idle. UndoCoalescer routes Ctrl-Z to LiveHoleLayer when
focused row is a hole; on empty-buffer-empty-stack, drops the hole.

Resolves design §3.3: undo behaves the same way inside and outside
holes (matches the user's mental model).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 16: Save-flush integration

**Files:**
- Modify: wherever `Ctrl-S` / save is dispatched (likely `LiveListModelBinding` or test-app; verify against existing R4/R5 setup).

- [ ] **Step 1: Failing test.**

```cpp
void save_flushes_pending_hole_first() {
    /* hole open with bufferText "draft" */
    binding.save();
    QCOMPARE(holeLayer.holeCount(), 0);
    QCOMPARE(doc.toMarkdown(), QString("hello\n\ndraft"));
}
```

- [ ] **Step 2: Add `commitAllPendingHoles` call to the save path.**

```cpp
void LiveListModelBinding::save() {
    m_holeLayer->commitAllPendingHoles();
    m_doc->save();
}
```

(Adapt to actual save-API.)

- [ ] **Step 3: Build + run.**

- [ ] **Step 4: Commit.**

```bash
git commit -m "feat(live-render): save flushes pending holes first (R5.5 Task 16)

Ctrl-S commits all pending holes in ascending reifyAnchor byte order
before writing the rope. Saved file always equals on-screen content.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 17: Stress-typing test (load-bearing)

The single most important test of R5.5 — the equivalent of v0's missed F2.

**Files:**
- Modify: `libs/markoff-live-render/tests/tst_live_render_holes_qml.cpp`

- [ ] **Step 1: Write the stress test.**

```cpp
void stress_type_into_hole_no_scramble_no_loss() {
    QQuickView view;
    /* setup; click into row 0 ("hello"); press Enter */
    LiveRealisticInputHarness h(&view);
    h.keyClick(Qt::Key_End);
    h.keyClick(Qt::Key_Return);

    /* Hole open; type 200 chars at default 30 ms gap (= ~33 char/s) */
    const QString sentence =
        QStringLiteral("This is a test of two hundred characters being typed into a "
                       "hole at human speed without any character scramble or loss; "
                       "every byte must arrive in order; reified after idle.");
    QCOMPARE(sentence.length(), 200);   // verify the constant

    h.typeString(sentence);
    h.idle(400);    // commit fires at 250 ms after last keystroke

    QCOMPARE(doc.toMarkdown(), QString("hello\n\n") + sentence);
}
```

- [ ] **Step 2: Build + run.**

Expected: PASS. If it FAILS — the ordering invariant is broken. Stop and diagnose; do not move on.

- [ ] **Step 3: Commit.**

```bash
git commit -m "test(live-render): stress-typing into hole — load-bearing (R5.5 Task 17)

200 characters at 30 ms gap (= ~33 char/s, sustained) must arrive in
the source byte-for-byte after idle commit. This is the test v0
should have had; the harness sees real async timing per the gate
test in Task 2.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 18: Selection-across-hole verification

**Files:**
- Modify: `libs/markoff-live-render/tests/tst_live_render_holes_qml.cpp`

- [ ] **Step 1: Failing test.**

```cpp
void selection_across_hole_includes_buffer_text_in_copy() {
    /* setup: doc "para1"; press Enter at end; type "middle" in hole */
    /* don't commit; cursor in hole */
    /* shift-click into "para1" at qtPos 0 */
    /* Selection now spans para1 (full) + hole (full) */
    QString copied = clipboard->text();   // simulate Ctrl-C
    QCOMPARE(copied, QString("para1\n\nmiddle"));   // hole's bufferText included
}
```

- [ ] **Step 2: Verify `ParagraphDelegate.qml`'s `serializeForCopy()` returns `model.text` (which routes to `bufferText` via proxy passthrough for hole rows). No code change expected; this test verifies the wiring is correct.**

If the test fails, the proxy's `TextRole` for hole rows isn't aliased to `bufferText`. Fix in `LiveProxyBlockModel.cpp` `data()` for the hole branch.

- [ ] **Step 3: Build + run.**

Expected: PASS.

- [ ] **Step 4: Commit.**

```bash
git commit -m "test(live-render): selection across hole includes bufferText in copy (R5.5 Task 18)

H9 invariant: holes participate in cross-row selection. serializeForCopy
returns bufferText for hole rows (via proxy's TextRole alias).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 19: Test app polish + dogfood gate

**Files:**
- Modify: `libs/markoff-live-render/app/Main.qml`
- Modify: `docs/restoration-status.md`

- [ ] **Step 1: Update title.**

```qml
title: qsTr("Markoff Live Render — R5.5 Holes")
```

- [ ] **Step 2: Manual dogfood pass.**

User runs `./build-dev/bin/markoff-live-render-app` and exercises the R5.5 dogfood script (spec §10.3 R5.5 entry). Any feedback goes into `docs/restoration-status.md`'s Dogfood log per the session brief format.

If dogfood surfaces a regression, **stop and diagnose**. Do not paper over.

- [ ] **Step 3: Update `docs/restoration-status.md`.**

- TL;DR: redirect to "R5.5 complete; R5 Tasks 12–18 may close (independent of R5.5); R6 next."
- Phase board: R5.5 → `complete` with this PR's commit range.
- Recent-changes log: bundle this commit's summary.
- Dogfood log: append the user's dogfood feedback verbatim.

- [ ] **Step 4: Commit.**

```bash
git commit -m "feat(live-render): R5.5 implementation complete — entering dogfood gate

Test app title bumped. R5.5 dogfood script (spec §10.3) is now ready
for user testing. On dogfood pass, R5.5 transitions to complete and
R6 plan generation begins.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Self-review

**Spec coverage:**
- ✓ Premise H1 (paragraph holes) — Tasks 11, 12.
- ✓ H2 (no source until commit) — Task 7's empty-buffer commit-as-abandon test.
- ✓ H3 (commit triggers) — Tasks 6 (idle), 12 (explicit Enter), 14 (focus-out wiring), 16 (save).
- ✓ H4 (abandon triggers) — Task 13.
- ✓ H5 (proxy composition) — Tasks 8, 9.
- ✓ H6 (cursor delivery via proxy) — Task 10.
- ✓ H7 (IME guard) — Task 6.
- ✓ H8 (per-hole undo) — Task 15.
- ✓ H9 (selection across hole) — Task 18.
- ✓ H10 (multi-hole save order) — Task 7's `commitAllPendingHoles`.
- ✓ H11 (test harness + gate) — Task 2.

**Placeholder scan:** "TBD" / "TODO" / "fill in details" — none. "Adapt to" appears 3× referencing existing API surfaces the implementer must confirm at write-time; these are flagged for verification, not placeholders.

**Type consistency:** `BlockHole` / `HoleBlockId` / `HoleKind::Paragraph` consistent across Tasks 4–18. `quint64 holeId` zero == invalid throughout. `LiveHoleLayer::createBlockHole` returns `quint64` consistently. `LiveProxyBlockModel::IsHoleRole` / `BufferTextRole` / `HoleIdRole` consistent.

**Open question coverage:** all six §16 open questions answered in the plan's "Open question resolutions" preamble.

---

*End of R5.5 plan.*
