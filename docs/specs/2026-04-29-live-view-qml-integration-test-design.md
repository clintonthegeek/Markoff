# Live View QML integration test — design

**Status:** spec
**Date:** 2026-04-29
**Branch:** `exploration/new-foundation`
**Tracks follow-up:** §3 of `docs/handoff/2026-04-29-live-render-followups-SESSION-BRIEF.md`

## 1. Motivation

The Phase-2 v0 walking skeleton landed with a smoke test (`tst_view_qml_live_view_smoke.cpp`) that stops at the C++ model boundary. Five real bugs surfaced during dogfood (see commit `b03b0d0`); none were caught by the smoke test because the QML delegate wiring was unverified end-to-end.

This test fills that gap: it loads the actual QML scene, verifies delegates render the model's data, and drives a synthesized cross-block selection through `MouseArea` — exactly the interactions where the dogfood bugs hid.

## 2. Scope

In scope:

- Loading `MarkoffEditor.qml` with `mode: "live"` and a 5-block fixture.
- Verifying that each delegate's text-bearing properties reflect the model row (catches "Bug 1": `required property` + `DelegateChoice` silent failure).
- Synthesizing a click+drag across multiple block kinds and verifying `LiveSelectionModel::collectSelectedText` returns text spanning the dragged range (catches Bugs 2, 3, 4: `model.index` not a role, `positionAt` missing, `blockIndex` missing).

Out of scope (deferred to follow-ups):

- Ctrl+C activation via `Shortcut`. Needs key-event simulation + keyboard focus; one bug class at a time.
- Right-click context menu. KDAB Widget bridge popups a native window; not friendly to offscreen QPA.
- Selection highlight on HR / image (brief follow-up §1). Has its own design + test.
- Theming (brief follow-up §2). Has its own design + test.
- Auto-scroll (brief follow-up §4). Not yet implemented.

## 3. Architecture

A new C++ test file `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp` using the same shape as `tst_view_qml_integration.cpp`, with one critical difference: it uses `QQuickView` (not a parent-less `QQmlComponent::create()`) so synthesized mouse events route through a real `QQuickWindow`.

### Why `QQuickView`

`QTest::mousePress`/`mouseMove`/`mouseRelease` post events to a target window. Without a window, the events have nowhere to go and `MouseArea.onPressed` never fires. `QQuickView` is the simplest container that gives us a window plus a QML scene. The window is exposed under `QT_QPA_PLATFORM=offscreen` (the offscreen QPA still creates and exposes windows for input routing).

### Document seeding

The test instantiates a `MarkoffDocument` directly, sets it via `engine.rootContext()->setContextProperty("doc", &doc)`, and constructs `MarkoffEditor { document: doc; mode: "live"; ... }`. `EditorBackend` is created inside `SourceEditor.qml` and consumed by `LiveView.qml` via the `editorBackend` alias — same path as the real app.

### Wait protocol

After `doc.applyLocalEdit({...})`:

1. `QSignalSpy(&doc, &Markoff::MarkoffDocument::parseUpdated)` then `parseSpy.wait(2000)` — waits for the async parse pool.
2. `QTRY_COMPARE(listView->property("count").toInt(), 5)` — waits for `LiveBlockModel` diff to apply and `ListView` to instantiate delegates.

`QTRY_*` spins the event loop, so polish/layout completes before assertions read geometry.

### Locating QML items

`findChild<QQuickItem*>(root, "listView")` after assigning `objectName: "listView"` to the `ListView` in `LiveView.qml`. Delegate retrieval uses `listView->property("contentItem")` then iterating its `childItems()`. We do not modify any QML beyond adding the `objectName` for test discovery — that's a benign change.

## 4. Test plan

### Test 1: `delegates_render_model_text()`

Catches Bug 1 (`required property` + `DelegateChoice` silent failure). The bug rendered every text-bearing delegate empty.

Steps:

1. Build a 5-block fixture identical to the smoke test (heading, paragraph, hr, image, code-block).
2. Load `MarkoffEditor` with `mode: "live"`.
3. Wait for parse + delegate instantiation.
4. For each delegate child of the `ListView.contentItem`:
   - Assert its `blockIndex` property equals the row index.
   - For paragraph + heading: `blockText` matches the model row's `text` role.
   - For image: `imageSrc` matches the model row's `imageSrc` role.
   - For code-block: `codeText` matches the model row's `codeText` role.

If Bug 1 regresses, `blockText`/`codeText` are empty strings — the comparison fails.

### Test 2: `mouse_drag_selects_across_block_kinds()`

Catches Bugs 2, 3, 4 (`model.index` not a role; delegate `positionAt` missing; `blockIndex` missing on non-text delegates).

Steps:

1. Same fixture and load.
2. Wait for delegates.
3. Resolve the on-window position of row 1 (paragraph) and row 3 (image) via `delegate->mapToScene(QPointF(delegate.width / 2, delegate.height / 2))`.
4. `QTest::mousePress(window, Qt::LeftButton, {}, posRow1)`.
5. Several `QTest::mouseMove(window, intermediatePos)` between press and release, with a `QTest::qWait(20)` between to let the MouseArea event handler run.
6. `QTest::mouseRelease(window, Qt::LeftButton, {}, posRow3)`.
7. Read `binding.selectionModel` (via the binding's exposed property).
8. Assert `anchorBlock == 1` and `activeBlock == 3` (the drag spans rows 1 to 3).
9. Assert `anchorOffset > 0` (press landed mid-paragraph; a working `positionAt` returns a non-zero column).
10. Call `collectSelectedText(blockTexts)` and assert the returned text contains the paragraph row's text and the image row's alt text.

If Bug 2 regresses, every hit collapses to `block = 0` — `anchorBlock != 1` fails immediately.
If Bug 3 regresses, `positionAt` is undefined → offset always 0 → `anchorOffset > 0` fails.
If Bug 4 regresses, the HR delegate has no `blockIndex` → drag through it snaps the active block back to 0 → `activeBlock != 3` fails.

## 5. CMake wiring

Add to `libs/markoff-view-qml/tests/CMakeLists.txt`:

```cmake
add_executable(tst_view_qml_live_view_qml tst_view_qml_live_view_qml.cpp)
add_test(NAME tst_view_qml_live_view_qml COMMAND tst_view_qml_live_view_qml)
target_link_libraries(tst_view_qml_live_view_qml
    PRIVATE Qt6::Test Qt6::Qml Qt6::Quick markoff_view_qml markoff_view_qmlplugin markoff_foundation)
qt6_import_qml_plugins(tst_view_qml_live_view_qml)
set_tests_properties(tst_view_qml_live_view_qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

Mirrors `tst_view_qml_integration` plus `markoff_foundation` (for `MarkoffDocument`, `Origin`, etc., used to seed the document directly).

## 6. Non-test changes

Single QML touch: `LiveView.qml` — add `objectName: "listView"` to the `ListView` element so `findChild` can locate it. No semantic change.

## 7. Verification

After landing: `ctest --test-dir build-dev -R '^tst_view_qml_' --output-on-failure -j 8` should report 6 tests passing in this directory (was 5), and the full suite goes from 33 → 34 passing.

## 8. Constraints

- Build with `-j 8` (per project memory: bare `-j` freezes the user's machine).
- No master branch touches; this lands on `exploration/new-foundation`.
- Existing 33 tests must remain green.
- Eight Phase-2 v0 invariants (design spec §4) must continue to hold; none of this work touches them.

## 9. Out of band

The brief's other follow-ups (selection highlight on HR/image, theming, auto-scroll, lifting `EditorBackend` out of `SourceEditor.qml`) each get their own spec when their turn comes. This spec covers only follow-up §3.
