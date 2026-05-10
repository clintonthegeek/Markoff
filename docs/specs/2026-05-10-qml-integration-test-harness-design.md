# QML integration test harness — design

**Date:** 2026-05-10
**Branch:** `exploration/new-foundation`
**Queue ref:** `docs/queue.md` item #3
**Status:** approved; plan next.

## 1. Why

The 64 `tst_live_render_*` executables in `libs/markoff-live/tests/` are unit
tests against C++ types. They never load real QML, never dispatch events
through `QQuickWindow`, never observe a realised `TextEdit` delegate.
Four recent typing-class regressions (typing-reverses-chars, Shift+Enter
inert, arrow-up skips paragraphs, cursor lost on Enter — see
`docs/handoff/2026-05-09-setext-dogfood-findings.md` and queue.md #2)
all reproduced only via interactive dogfood; the unit tests that should
have caught them passed because they called `d2ApplyBufferEdit` directly
and bypassed the QML pipeline.

Two test-infrastructure pieces already exist and have **zero consumers**:

- `tests/LiveRealisticInputHarness.h` — interposes `qWait + processEvents`
  between key events to expose async races. Authored for the v0 holes
  F2 character-scramble investigation.
- `tests/qml/HoleStressView.qml` — a QML fixture from the (now-archived)
  c-restoration arc.

This spec wires both up via a new fixture that loads the production
`Main.qml`, drives it through the harness, and asserts on three layers
(buffer / model / delegate) per regression.

## 2. Scope

In scope:

- One new test executable `tst_live_render_qml_integration` with five
  Q_SLOTS covering the regression class from queue.md #3.
- One new fixture header `tests/QmlIntegrationFixture.h` that owns the
  document, session, MainController, and `QQmlApplicationEngine`.
- One small refactor of `libs/markoff-live/app/CMakeLists.txt` to expose
  `MainController` + the `Main.qml` qml_module to the test target without
  duplication.
- One small extension to `LiveRealisticInputHarness` adding wheel-event
  dispatch.
- `QT_QPA_PLATFORM=offscreen` test environment.

Out of scope:

- Test coverage beyond the five queue.md #3 scenarios. Future tests
  reuse the same fixture; this spec does not enumerate them.
- Replacing existing mock-based unit tests. Both styles coexist.
- A `WITH_QML_INTEGRATION_TESTS` CMake gate. The single-executable
  shape pays QML startup once (~1–3 s); not worth gating.

## 3. Architecture

### 3.1 Target

- Name: `tst_live_render_qml_integration`. CMake target lives in
  `libs/markoff-live/tests/CMakeLists.txt`.
- Single executable, single test class, five `Q_SLOT`s. One `init()`
  builds a fresh fixture per slot; `cleanup()` tears it down. Pays QML
  startup cost five times within one process — measured budget < 15 s
  total wall-clock, well within the suite's existing per-test budget.
- Linked libraries: `Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2
  Qt6::Qml Qt6::Widgets Qt6::Test markoff_live markoff_core
  markoff-live-app-internal`.
- CTest property: `ENVIRONMENT "QT_QPA_PLATFORM=offscreen"` — proven
  by the three E2.6 tests added in commits `9fff98d..de05f90`.

### 3.2 App-target refactor

`libs/markoff-live/app/CMakeLists.txt` today defines exactly one target:
the executable `markoff-live-app`, which owns both `MainController.{h,cpp}`
and the `org.markoff.live.app` qml module registering `Main.qml`.

The refactor extracts an **OBJECT library** `markoff-live-app-internal`
that holds `MainController.{h,cpp}` and the qml module. The executable
target `markoff-live-app` now consists of `main.cpp` + a link against
`markoff-live-app-internal`. The new test target links against the same
OBJECT library.

Why OBJECT and not STATIC:

- OBJECT libraries don't produce link artefacts of their own — they're
  source-list aggregators. The qml-module registration's static-cache
  symbols emit into both consumers without duplicate-symbol risk.
- `qt_add_qml_module` works with OBJECT targets on Qt 6.8+.

Result: both the production executable and the test target reach
`engine.loadFromModule("org.markoff.live.app", "Main")` against the
**byte-identical** Main.qml. No fixture-side QML drift.

### 3.3 Fixture surface

New header at `libs/markoff-live/tests/QmlIntegrationFixture.h`. RAII
wrapper. Public API:

```cpp
namespace Markoff::Live::Test {

class QmlIntegrationFixture {
public:
    /// Loads `markdown` into a fresh MarkoffDocument and brings the
    /// production Main.qml up against it. Blocks until the window is
    /// exposed and the model has the expected row count.
    explicit QmlIntegrationFixture(const QByteArray &markdown,
                                   int expectedRowCount);
    ~QmlIntegrationFixture();

    QmlIntegrationFixture(const QmlIntegrationFixture &) = delete;
    QmlIntegrationFixture &operator=(const QmlIntegrationFixture &) = delete;

    // Ownership accessors
    Markoff::MarkoffDocument *document() const;
    Markoff::Session         *session()  const;
    QQmlApplicationEngine    *engine()   const;
    QQuickWindow             *window()   const;

    // Resolved QML objects (cached on first access)
    QObject            *binding();           // LiveListModelBinding*
    QAbstractItemModel *model();
    QQuickItem         *listView();          // the LiveView ListView
    QQuickItem         *delegateAt(int row); // realised delegate or nullptr
    QQuickItem         *focusedDelegate();   // delegate with activeFocus

    // Three-layer state (per assertion convention §5.1)
    QByteArray bufferText(Markoff::BlockId);     // doc->blockText(id)
    QString    modelText(int row);                // model.data(idx, TextRole)
    QString    delegateText(int row);             // TextEdit.text
    int        delegateCursorPos(int row);

    // Wait helpers (QSignalSpy + QTRY_VERIFY internally)
    bool waitForRowCount(int expected, int timeoutMs = 2000);
    bool waitForDelegateAt(int row, int timeoutMs = 2000);

    // Input harness (configured against window())
    LiveRealisticInputHarness &harness();
};

} // namespace
```

**Construction flow** (per slot, < 200 ms target):

1. Random replicaId; `MarkoffDocument doc(replicaId); doc.loadFromMarkdown(markdown); doc.markSaved(doc.d2EditSequence());`.
2. `Session *session = doc.createSession();`.
3. `auto tmpFile = std::make_unique<QTemporaryFile>(); tmpFile->open();` — gives a real filesystem path for MainController without ever writing.
4. `MainController(doc, tmpFile->fileName())`.
5. `QQmlApplicationEngine engine;` + three `setContextProperty` calls.
6. `engine.loadFromModule("org.markoff.live.app", "Main");` — assert root non-empty.
7. `window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());` — assert non-null.
8. `QVERIFY(QTest::qWaitForWindowExposed(window));` — succeeds on offscreen.
9. `waitForRowCount(expectedRowCount)` — `QSignalSpy` on the binding's `model->rowsInserted` + `model->modelReset`; loop until `rowCount() == expected` or timeout.

**Destruction:** engine destroyed first (releases QML object graph and the
binding's per-delegate state); then `MainController`, `Session`, `doc`,
`tmpFile`.

### 3.4 LiveRealisticInputHarness extension

Add one method to the existing header:

```cpp
void wheelEvent(QPoint posInWindow, int deltaY,
                Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QWheelEvent ev(/*pos=*/posInWindow, /*globalPos=*/m_window->mapToGlobal(posInWindow),
                   /*pixelDelta=*/{}, /*angleDelta=*/{0, deltaY},
                   /*buttons=*/Qt::NoButton, /*modifiers=*/mods,
                   /*phase=*/Qt::NoScrollPhase, /*inverted=*/false);
    QCoreApplication::sendEvent(m_window, &ev);
    QTest::qWait(m_defaultGapMs);
    QCoreApplication::processEvents();
}
```

No other harness changes.

## 4. Test scenarios

Each slot constructs its own fixture, drives input via the harness, then
asserts on the three layers per §5.1. Slot order is the queue.md #3
priority order.

### 4.1 `typing_preserves_insertion_order`

- Fixture: empty markdown (`""`), expected rows = 1 (one paragraph block
  on empty load — verified by `tst_live_render_empty_doc_focus`).
- Drive: `harness.typeString("abc")`.
- Assert:
  - `fixture.bufferText(firstBlockId) == "abc"`
  - `fixture.modelText(0) == "abc"`
  - `fixture.delegateText(0) == "abc"`
  - `fixture.delegateCursorPos(0) == 3`

This is the typing-reverses-chars regression killer. Production failure
mode was `"cba"` in the delegate while buffer held `"abc"`; a passing
mock-based test exists for the buffer side.

### 4.2 `shift_enter_creates_visible_newline`

- Fixture: `"Heading"`, expected rows = 1.
- Drive: position cursor at end (qtPos = 7) via
  `binding->property("focusedQtPos").set(7)` or by sending End key; then
  `harness.keyClick(Qt::Key_Return, Qt::ShiftModifier)`.
- Assert:
  - `fixture.bufferText(...) == "Heading\n"`
  - `fixture.delegateText(0)` contains a `\n` (or `contentHeight` ≈
    2× single-line height — read via `delegateAt(0)->property("contentHeight")`).
  - `fixture.delegateCursorPos(0) == 8`.

Connects to queue.md #4 (the chop-`\n` investigation): if Option A/C
from #4 lands, this test's assertion on `delegateText` becomes the
regression guard for it. If neither lands, this test exposes the
discrepancy and motivates #4.

### 4.3 `enter_at_paragraph_end_migrates_focus`

- Fixture: `"A"`, expected rows = 1.
- Drive: position cursor at end; `harness.keyClick(Qt::Key_Return)`.
- Assert:
  - `fixture.waitForRowCount(2)` returns true.
  - `fixture.focusedDelegate()` is `fixture.delegateAt(1)` (the new row).
  - `fixture.delegateCursorPos(1) == 0`.

Regression class: the "cursor lost on Enter" symptom from queue.md #2
concern #7 — `Component.onCompleted` match check captured at construction
time may miss late-arriving structural signals.

### 4.4 `arrow_up_walks_then_crosses_blocks`

- Fixture: `"first paragraph\n\nsecond paragraph"`, expected rows = 2.
- Drive: focus row 1, position cursor at end (qtPos = 16);
  `harness.keyClick(Qt::Key_Up)`.
- Assert (single-line each block, no wrapping):
  - Focus stays on row 1 if and only if the paragraph wraps and there's a
    visual line above the cursor; otherwise crosses to row 0. With a
    single-line fixture, the first arrow-up crosses immediately.
  - Final state: `fixture.focusedDelegate() == fixture.delegateAt(0)`.

Test the cross-block path; within-block wrapped-line behaviour is a
follow-up (the Option B / MockTextEdit `positionAt` path is already
unit-tested by `tst_live_render_e2_nav_arrows`).

### 4.5 `ctrl_wheel_zooms_font_scale`

- Fixture: `"sample"`, expected rows = 1.
- Initial state: capture `binding->property("fontScale").toReal()` (default 1.0).
- Drive: `harness.wheelEvent(QPoint(100, 100), /*deltaY=*/120, Qt::ControlModifier);`.
- Assert:
  - `binding->property("fontScale").toReal()` is now > 1.0.
  - First delegate's text content's `font.pixelSize` (via QML property
    traversal) is the new scaled value.

Note: Ctrl+wheel wiring belongs to `LiveView.qml`'s
`WheelHandler`/`MouseArea`; if the path isn't wired (E2.6 added the
QActions but `Ctrl+wheel` may be a separate item — check before
asserting), this test reveals the gap rather than passes vacuously.

## 5. Conventions

### 5.1 Three-layer assertion convention

Every test slot that exercises an edit MUST assert on:

1. **Buffer**: `doc->blockText(blockId)` — the CRDT canonical bytes.
2. **Model**: `model.data(model.index(row, 0), TextRole).toString()` — the
   projection the QML sees.
3. **Delegate**: `delegateAt(row)->findChild<QQuickItem*>("textEditObjectName")->property("text").toString()` — the realised state.

When buffer and model agree but delegate disagrees (or vice versa), the
test failure pinpoints which layer in the pipeline broke. This is the
diagnostic value the mock-based tests lack.

A test that asserts on only one layer must document why. Default is all
three.

### 5.2 Test naming

Slot names use `lowercase_with_underscores`. Per existing pattern
(`controller_is_exposed_on_binding`, `prev_navigable_row_walks_back`).
File-level Q_OBJECT class: `TestLiveRenderQmlIntegration`.

### 5.3 SPDX + namespace

- `// SPDX-License-Identifier: GPL-3.0-or-later` header.
- C++20, Qt 6.8+.
- Fixture in `Markoff::Live::Test` namespace, matching the harness header.
- `tst_live_render_*` prefix per the per-lib CLAUDE.md.

## 6. Risks

### 6.1 ApplicationWindow on offscreen

Main.qml's root is `ApplicationWindow` (QtQuick.Controls). Offscreen
QPA may not deliver expose events the same way as `xcb`. Mitigation:

- First fallback: `QTest::qWaitForWindowActive` instead of
  `qWaitForWindowExposed`.
- Second fallback: replace with `QTRY_VERIFY(window->isVisible() &&
  window->width() > 0, 5000)`.
- Third fallback: drop `ApplicationWindow`-rootedness by adding a
  test-only Main.qml — explicitly rejected from §3.2 because it
  defeats production fidelity, but documented as the escape hatch.

If all three fail, this is a real Qt offscreen limitation and the
spec needs revision. Surface it loudly in the plan; do not paper over.

### 6.2 Delegate text-edit object name

`fixture.delegateText(row)` needs to find the TextEdit inside the
delegate. The delegate hierarchies (`ParagraphDelegate`, `HeadingDelegate`,
`BlockquoteDelegate`, `CodeBlockDelegate`, `ListItemDelegate`) each have
their own `TextEdit` — and the per-item `objectName` may not be set.

Mitigation: standardise. The plan task that sets up the fixture must
either (a) add `objectName: "textEdit"` to each delegate's TextEdit
or (b) recurse via `QQuickItem::childItems` and pick the first
`TextEdit`-typed child. Option (b) is reversible and doesn't require
delegate edits, so it's the default; option (a) is the fallback if (b)
is brittle.

### 6.3 Wheel event on offscreen

Wheel events go through `QQuickWindow::wheelEvent` and propagate to
`WheelHandler`/`MouseArea`. Offscreen-platform wheel-event handling is
less battle-tested than keys. If §4.5 flakes, fallback in priority order:

1. Reproduce with a synthetic mouse-position via `QQuickWindow::setMouseGrabEnabled`.
2. Drop the wheel slot from this spec with a `QSKIP` and a follow-up
   queue item. The four key-driven slots still land. Ctrl+wheel can
   be re-attempted after queue.md #1's interactive dogfood signs off
   (which is the real source of truth for E2.6 zoom).

### 6.4 App-target refactor breaking production app

Extracting the OBJECT library changes `markoff-live-app`'s build graph.
Risk: production app binary changes behaviour subtly (wrong qmlcachegen
output, wrong static-init order). Mitigation: the plan's final pre-tag
task rebuilds `markoff-live-app` and runs `./build-dev/bin/markoff-live-app
/tmp/smoke.md`; binary must launch and render. The plan's final activity-log
entry notes whether this smoke pass ran.

### 6.5 Per-slot QML startup cost

Five fixtures = five `QQmlApplicationEngine` constructions. Each is
~200–500 ms on a warm cache; first one is 1–3 s. Total budget < 15 s
wall-clock for the executable. If this exceeds the existing per-test
budget (no documented limit, but the comparable `tst_live_render_e2_5_perf_bulk_paste`
runs in <500 ms), batch the slots into shared-fixture groups — but only
after measurement, not preemptively.

## 7. Acceptance criteria

1. `tst_live_render_qml_integration` builds against
   `cmake --build build-dev --target tst_live_render_qml_integration -j 8`.
2. `ctest --test-dir build-dev -R '^tst_live_render_qml_integration$' --output-on-failure`
   reports `Passed`.
3. All five slots pass on the default offscreen-platform configuration.
   If §4.5 has to skip per risk 6.3, the skip is annotated with a follow-up
   queue item.
4. `cmake --build build-dev --target markoff-live-app -j 8` still succeeds
   after the §3.2 OBJECT-library refactor; the resulting binary launches
   against `/tmp/smoke.md` and shows the first delegate.
5. The full `ctest --test-dir build-dev -j 8` run is still green (191/191
   or higher — five new tests but slot count, not target count).
6. `LiveRealisticInputHarness.h` gains exactly one new method
   (`wheelEvent`) and one new consumer; no other harness changes.
7. `Main.qml` is **unchanged**. The §3.2 refactor is purely CMake-side.

## 8. File manifest

New files:

- `libs/markoff-live/tests/QmlIntegrationFixture.h`
- `libs/markoff-live/tests/QmlIntegrationFixture.cpp`
- `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

Modified files:

- `libs/markoff-live/app/CMakeLists.txt` — extract OBJECT library.
- `libs/markoff-live/tests/CMakeLists.txt` — register new test target.
- `libs/markoff-live/tests/LiveRealisticInputHarness.h` — add `wheelEvent`.

No source-tree files outside `libs/markoff-live/` change. No public API
changes. No QML changes.

## 9. Decisions recorded

1. **One executable, five slots** over five small executables. Rationale:
   amortise QML startup; matches queue.md #3's named target. Trade-off:
   broken cleanup can taint sibling slots — `init()`/`cleanup()` discipline
   mandatory.
2. **Production Main.qml via OBJECT-library extraction** over a thin
   test-only QML. Rationale: drift is the real cost of duplication;
   the CMake refactor is one-time.
3. **Three-layer assertion convention** mandatory. Rationale: the
   diagnostic value is in pinpointing which layer broke; weaker
   assertions degrade the harness's purpose over time.
4. **`QT_QPA_PLATFORM=offscreen` not gated.** E2.6 set the precedent;
   the existing test suite already has three offscreen tests with no
   `WITH_QML_INTEGRATION_TESTS` option. Match that.
5. **Reuse `LiveRealisticInputHarness`** rather than build a new
   keystroke driver. The header was authored for exactly this purpose
   and has been dormant since the v0 holes investigation; this work
   activates it.
6. **Wheel-event slot is best-effort.** If offscreen wheel dispatch
   doesn't work, skip with a follow-up rather than block the four
   key-driven slots.

---

End of design.
