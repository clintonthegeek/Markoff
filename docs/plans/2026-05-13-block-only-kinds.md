# Block-only kinds — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `HorizontalRule` and `Image` block kinds a coherent BlockSelected-as-intermediate UX. Arrow nav lands on them, Backspace/Delete adjacent selects them (no merge across the boundary), Backspace/Delete on a selected block-only removes it, Enter on a selected block-only opens a fresh paragraph after, typing on a selected block-only is a no-op. Replace three near-identical per-kind copies (delegate boilerplate + structural-key handlers) with a single shared base. Math is explicitly excluded; its rework is a separate future spec.

**Architecture:** Add a single explicit field `BlockKindDescriptor::isBlockOnly` (set `true` for HR and Image, `false` everywhere else including Math). The `BlockKindRegistry::isBlockOnly(kind)` predicate is the single decision point for the new behaviours in (1) `LiveNavigationController::previousNavigableRow`/`nextNavigableRow` (land-and-step instead of skip), (2) `LiveStructuralKeyHandler::tryHandle` (a "block-only adjacent? select, don't merge" precondition before per-kind dispatch), and (3) a shared set of `blockOnly*` handlers (`Navigate`, `Delete`, `Enter`, `TypePrintable`) registered for every `isBlockOnly == true` kind. A shared `BlockOnlyDelegateBase.qml` collapses the per-delegate `isSelected`/`Keys.onPressed`/registration boilerplate.

**Tech Stack:** C++20, Qt 6.8+, QML, CMake 3.19+. Tests via QTest + `LiveRealisticInputHarness`. Build cap: **`-j 8` always** (per project memory).

**Spec:** `docs/specs/2026-05-13-block-only-kinds-design.md` is authoritative for every design decision in this plan. Cite by section number when in doubt.

**Reading order for executor before starting:**
1. `docs/INVARIANTS.md` (especially invariants 1–5 and 8)
2. `docs/specs/2026-05-13-block-only-kinds-design.md` (full)
3. `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md` (D-fc-1..4 context)
4. `libs/markoff-live/include/markoff/live/BlockKindDescriptor.h` + `BlockKindRegistry.h` (current shape)
5. `libs/markoff-live/src/BlockKindRegistry.cpp` (the registration site)
6. `libs/markoff-live/src/LiveNavigationController.cpp` (lines 30–55 for the predicates, 60–170 for the nav handlers)
7. `libs/markoff-live/src/LiveStructuralKeyHandler.cpp` (lines 300–400 for the paragraph/heading/HR handlers — the merge-across-boundary path is at `paragraphBackspace` ~line 303 and `paragraphDelete` ~line 322)
8. `libs/markoff-live/qml/delegates/HorizontalRuleDelegate.qml` (the to-be-shared boilerplate)
9. `libs/markoff-live/qml/delegates/ImageDelegate.qml` (same)

**Build / test commands:**
```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
cmake --build build-dev --target markoff_live -j 8
cmake --build build-dev --target markoff-live-app -j 8
cmake --build build-dev --target tst_live_render_focus_chokepoint_invariant -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

**Commit-message prefix convention:** `markoff-live: <slot summary>` per the recent commit history pattern. Doc-only commits use `docs:`.

---

## Files touched

| File | Change |
|---|---|
| `libs/markoff-live/include/markoff/live/BlockKindDescriptor.h` | Add `bool isBlockOnly = false;` field |
| `libs/markoff-live/include/markoff/live/BlockKindRegistry.h` | Add `isBlockOnly(QString) const` predicate |
| `libs/markoff-live/src/BlockKindRegistry.cpp` | Set `isBlockOnly = true` on HR and Image descriptors |
| `libs/markoff-live/src/LiveNavigationController.cpp` | `previousNavigableRow`/`nextNavigableRow` change semantics: skip only the rows that are *neither* text-bearing *nor* `isBlockOnly` (Math remains skipped). |
| `libs/markoff-live/src/LiveStructuralKeyHandler.cpp` | Add `blockOnlyNavigate{Up,Down}`, `blockOnlyDelete`, `blockOnlyEnter`, `blockOnlyTypePrintable` handlers and register them for every `isBlockOnly` kind. Add the "block-only adjacent — select, don't merge" precondition in `paragraphBackspace` / `paragraphDelete` (and copy to `headingBackspace` etc. via the shared site). Retire `hrNavigate{Up,Down}`, `hrDelete`, `imgDelete`. |
| `libs/markoff-live/qml/delegates/BlockOnlyDelegateBase.qml` | **New** — owns `blockAnchor`, `isSelected`, `Keys.onPressed`, chokepoint registration, selected-state ink. |
| `libs/markoff-live/qml/delegates/HorizontalRuleDelegate.qml` | Refactored to use the base; keeps only the rule's content layer. |
| `libs/markoff-live/qml/delegates/ImageDelegate.qml` | Same. |
| `libs/markoff-live/qml/delegates/MathDelegate.qml` | **Unchanged** — Math is out of scope. |
| `libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp` | Add nine R-rule tests (§4 of the spec); tighten the existing `backspace_after_typed_hr_lands_somewhere_sensible` to BlockSelected expectation; add Image variants of substantive rules. |
| `docs/queue.md` | Append discipline-log entry for the Math asymmetry; update the queue #2 banner with the spec/plan landing. |
| `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md` | Add a "D-fc-4 + block-only rework — resolved in <hash>" note alongside the existing entries. |

---

## Task 1: Pre-flight checks

**Files:** none (read-only)

- [ ] Confirm worktree is clean: `git status` shows no uncommitted changes (`.superpowers/`, `selection*.txt`, `Testing/Temporary/LastTest.log` are ignorable noise from prior sessions).
- [ ] Confirm build is green:
  ```bash
  cmake --build build-dev -j 8 2>&1 | tail -3
  ```
  Expect: all targets built, no errors.
- [ ] Confirm chokepoint invariant suite passes most tests (the 8 preexisting failures elsewhere are not our problem):
  ```bash
  ctest --test-dir build-dev -R 'tst_live_render_focus_chokepoint_invariant' -j 8
  ```
  Expect: `25 passed, 0 failed` or close to it (the `backspace_after_typed_hr_lands_somewhere_sensible` test is currently failing 5/5 — that's the D-fc-4 reproducer we'll tighten in Task 6).
- [ ] Read the spec sections referenced in the prelude.

---

## Task 2: Name the category

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/BlockKindDescriptor.h`
- Modify: `libs/markoff-live/include/markoff/live/BlockKindRegistry.h`
- Modify: `libs/markoff-live/src/BlockKindRegistry.cpp`

This is a pure renaming + flag-wiring commit. No behaviour changes. Lands first so subsequent tasks cite a stable API.

- [ ] **Step 1:** Add `bool isBlockOnly = false;` to `BlockKindDescriptor`. Place it next to `supportedCursorVariants` with the doc-comment from spec §3 (the "explicit rather than derived" rationale).
- [ ] **Step 2:** Add a predicate to `BlockKindRegistry`:
  ```cpp
  bool isBlockOnly(const QString &kind) const {
      const auto *d = find(kind);
      return d && d->isBlockOnly;
  }
  ```
- [ ] **Step 3:** In `BlockKindRegistry::registerBuiltins()`, set `d.isBlockOnly = true` on the HorizontalRule and Image descriptors. Verify by inspection that Math and every text-bearing kind leaves it at the default `false`.
- [ ] **Step 4:** Build and run the full live-render suite:
  ```bash
  cmake --build build-dev -j 8 2>&1 | tail -3
  ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
  ```
  Expect: zero changes in pass/fail count vs the Task 1 baseline (this is a no-op refactor).
- [ ] **Step 5:** Commit:
  ```
  markoff-live: slot — BlockKindRegistry::isBlockOnly + descriptor flag
  ```
  Body cites spec §3 and notes Math is intentionally false despite lacking TextCaret.

---

## Task 3: TDD red — invariant tests for the eight rules (HR only)

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp`

Add one test method per rule from spec §4, against the HR kind. These tests must FAIL before the production change lands (TDD red). Image variants come in Task 8.

- [ ] **Step 1:** Add a declaration block in the test class for the new methods (spec §7 lists names verbatim):
  ```cpp
  // Block-only kinds (spec §4 rules R-arrow-into … R-tripleclick-blockonly).
  void arrow_down_lands_blockselected_on_hr();
  void arrow_down_from_blockselected_hr_lands_on_text();
  void backspace_at_para_start_after_hr_selects_hr();
  void delete_at_para_end_before_hr_selects_hr();
  void backspace_on_selected_hr_removes_it();
  void enter_on_selected_hr_inserts_paragraph_after();
  void typing_on_selected_hr_is_noop();
  void tripleclick_on_hr_lands_blockselected();
  ```
- [ ] **Step 2:** Add the test bodies following the existing fixture pattern (`m_fixture.reset()` + `make_unique<QmlIntegrationFixture>` with the doc string from spec §7). Each test:
  1. Sets up the doc and waits for the relevant delegates.
  2. Places the cursor / clicks to the starting state.
  3. Sends the key event via `LiveRealisticInputHarness`.
  4. Asserts `cursorState->property("cursorKind").toString()` and `m_fixture->cursorStateCurrentRow()` AND model-row-count where applicable.
- [ ] **Step 3:** Build and run only the new tests:
  ```bash
  cmake --build build-dev --target tst_live_render_focus_chokepoint_invariant -j 8
  build-dev/bin/tst_live_render_focus_chokepoint_invariant \
    arrow_down_lands_blockselected_on_hr \
    arrow_down_from_blockselected_hr_lands_on_text \
    backspace_at_para_start_after_hr_selects_hr \
    delete_at_para_end_before_hr_selects_hr \
    backspace_on_selected_hr_removes_it \
    enter_on_selected_hr_inserts_paragraph_after \
    typing_on_selected_hr_is_noop \
    tripleclick_on_hr_lands_blockselected
  ```
  Expect: at least one of these fails for each rule that hasn't been implemented yet. `backspace_on_selected_hr_removes_it` may already pass (existing `hrDelete` handler covers it); that's fine.
- [ ] **Step 4:** Document which tests are red and which (if any) pass already. If any unexpected ones pass, tighten them before continuing.
- [ ] **Step 5:** Commit:
  ```
  markoff-live: test — block-only kind rules (TDD red)
  ```

---

## Task 4: Implement R-arrow-into / R-arrow-out (navigation land-and-step)

**Files:**
- Modify: `libs/markoff-live/src/LiveNavigationController.cpp`
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`

- [ ] **Step 1:** In `LiveNavigationController.cpp`, change `previousNavigableRow`/`nextNavigableRow` to land on rows that are *either* text-bearing *or* `isBlockOnly`. The simplest expression:
  ```cpp
  bool isNavLandingTarget(int row) const {
      return isTextBearing(row) || m_registry->isBlockOnly(m_model->recordAt(row).kind);
  }
  ```
  Use this in both `previousNavigableRow` and `nextNavigableRow`. The existing `isTextBearing` accessor stays — it's still used elsewhere.
- [ ] **Step 2:** Add `blockOnlyNavigateUp` and `blockOnlyNavigateDown` lambdas in `LiveStructuralKeyHandler::registerBuiltins()`, modelled after the existing `hrNavigateUp`/`hrNavigateDown`. The new ones must work for both HR and Image (they use only `c.model`, `c.blockIndex`, and `c.cursorState`, so already kind-agnostic — confirm by inspection).
- [ ] **Step 3:** Register the new handlers against *every* `isBlockOnly` kind. The registration loop is cleanest via:
  ```cpp
  for (const QString &kind : m_registry->kindIds()) {
      if (!m_registry->isBlockOnly(kind)) continue;
      m_handlers[kind][Qt::Key_Up]   = blockOnlyNavigateUp;
      m_handlers[kind][Qt::Key_Down] = blockOnlyNavigateDown;
  }
  ```
  (If `BlockKindRegistry::kindIds()` doesn't exist yet, add it as a small accessor returning `m_descriptors.keys()`.)
- [ ] **Step 4:** Run the two R-arrow tests:
  ```bash
  build-dev/bin/tst_live_render_focus_chokepoint_invariant \
    arrow_down_lands_blockselected_on_hr \
    arrow_down_from_blockselected_hr_lands_on_text
  ```
  Expect: both PASS.
- [ ] **Step 5:** Run the full chokepoint suite. Expect: the existing `arrow_down_traverses_existing_hr`, `arrow_up_traverses_existing_hr`, `arrow_down_traverses_hr_between_headings`, `arrow_down_traverses_consecutive_non_text_blocks` tests still pass — their assertions are about ending up on a text-bearing block, which is still true after two arrow presses. If any of those break (e.g., the test asserted a single arrow press lands on the far-side paragraph), update them per the new semantics: it now takes two presses, and the intermediate state is BlockSelected on the HR.
- [ ] **Step 6:** Commit:
  ```
  markoff-live: slot — block-only nav land-and-step (R-arrow-into/out)
  ```

---

## Task 5: Implement R-backspace-at-text-start-adjacent / R-delete-at-text-end-adjacent (the merge-fence)

**Files:**
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`

This is the D-fc-4 fix proper.

- [ ] **Step 1:** Inside `LiveStructuralKeyHandler::tryHandle()`, *before* the per-kind handler dispatch, add a precondition:
  ```cpp
  // Block-only fence (spec §4 R-backspace-at-text-start-adjacent /
  // R-delete-at-text-end-adjacent): Backspace at qtPos=0 or Delete at
  // qtPos=length, when the adjacent block is `isBlockOnly`, selects
  // the adjacent block instead of running the cross-boundary merge.
  if ((key == Qt::Key_Backspace && qtPos == 0 && blockIndex > 0)
      || (key == Qt::Key_Delete && qtPos == blockText.length()
          && blockIndex < model->rowCount() - 1)) {
      const int adjRow = (key == Qt::Key_Backspace)
                             ? blockIndex - 1 : blockIndex + 1;
      const auto &adjRec = model->recordAt(adjRow);
      if (m_registry->isBlockOnly(adjRec.kind)) {
          cursorState->establishFocus(adjRec.blockAnchor, /*qtPos=*/0);
          return HR::Handled;
      }
  }
  ```
- [ ] **Step 2:** Run the two R-merge-fence tests:
  ```bash
  build-dev/bin/tst_live_render_focus_chokepoint_invariant \
    backspace_at_para_start_after_hr_selects_hr \
    delete_at_para_end_before_hr_selects_hr
  ```
  Expect: both PASS. Model row count unchanged (no merge happened); cursor is BlockSelected on the HR.
- [ ] **Step 3:** Tighten the existing D-fc-4 reproducer. In the same test file, change `backspace_after_typed_hr_lands_somewhere_sensible` to assert:
  ```cpp
  QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("BlockSelected"));
  QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);  // HR row
  QCOMPARE(m_fixture->model()->rowCount(), 3);     // model unchanged
  ```
  And rename it to `backspace_after_typed_hr_selects_hr_blockonly`.
- [ ] **Step 4:** Run that test 5 times in a row to confirm it's no longer flaky:
  ```bash
  for i in 1 2 3 4 5; do build-dev/bin/tst_live_render_focus_chokepoint_invariant \
    backspace_after_typed_hr_selects_hr_blockonly 2>&1 | grep -E '^(PASS|FAIL)'; done
  ```
  Expect: 5/5 PASS.
- [ ] **Step 5:** Commit:
  ```
  markoff-live: slot — block-only merge fence (R-backspace/delete-adjacent, fixes D-fc-4)
  ```

---

## Task 6: Implement R-delete-blockonly / R-enter-blockonly / R-type-blockonly / R-tripleclick-blockonly

**Files:**
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live/qml/LiveView.qml` (for triple-click handling on block-only)

- [ ] **Step 1:** Generalise `hrDelete` to `blockOnlyDelete` — the existing logic only references `c.model->rowCount()` and `c.blockIndex`, no HR-specific code. Register against every `isBlockOnly` kind (same loop pattern as Task 4 step 3).
- [ ] **Step 2:** Add `blockOnlyEnter`: inserts a fresh `Paragraph` after the selected block via `Cmd::enterAtEnd(*doc, BlockId(c.blockAnchor))`, then `establishFocus(BlockAnchor(newBlock), 0)`. Register for every `isBlockOnly` kind under `Qt::Key_Return` AND `Qt::Key_Enter`.
- [ ] **Step 3:** Add `blockOnlyTypePrintable`. The handler's job is to *swallow* printable keystrokes when the block is selected — return `HR::Handled` without touching the model or cursor. Registration is trickier: the structural-key handler dispatches by exact `Qt::Key_*`, but typed characters arrive as `event.text` plus the corresponding `Qt::Key_*` enumerator (e.g., `Qt::Key_X` for "x"). The cleanest path is to add a wildcard entry — see if `LiveStructuralKeyHandler` already has any "any-printable" mechanism; if not, add a `m_blockOnlyPrintableSwallow` map keyed by kind and check it in `tryHandle()` *after* the explicit per-key map misses, before falling through to `NotHandled`. Verify the QML side: the delegate's `Keys.onPressed` only forwards a specific allowlist (Up/Down/Backspace/Delete in HR's current code); we need to widen it for the typing-swallow case. Update `BlockOnlyDelegateBase` (Task 9) to forward *all* printable keys when `isSelected`.
- [ ] **Step 4:** Handle triple-click for block-only kinds. In `LiveView.qml`'s `MouseArea.onClicked`, the existing triple-click branch does `selectionView.begin(0)` + `extend(blockText.length)` for whole-block text selection. For block-only kinds, that produces nothing useful (no text positions). Add a precondition: if the hit row is `isBlockOnly`, treat triple-click identically to single click (BlockSelected via the chokepoint, no selection range), and reset the click counter cleanly. The registry isn't directly accessible from QML; expose `binding.isBlockOnly(kind)` as a `Q_INVOKABLE` on `LiveListModelBinding` that proxies to the registry.
- [ ] **Step 5:** Run all four corresponding tests:
  ```bash
  build-dev/bin/tst_live_render_focus_chokepoint_invariant \
    backspace_on_selected_hr_removes_it \
    enter_on_selected_hr_inserts_paragraph_after \
    typing_on_selected_hr_is_noop \
    tripleclick_on_hr_lands_blockselected
  ```
  Expect: all PASS.
- [ ] **Step 6:** Commit:
  ```
  markoff-live: slot — block-only delete / enter / type-noop / triple-click (R-delete/enter/type/tripleclick)
  ```

---

## Task 7: Full chokepoint suite green

**Files:** none (verification)

- [ ] **Step 1:** Run the full chokepoint suite:
  ```bash
  ctest --test-dir build-dev -R 'tst_live_render_focus_chokepoint_invariant' -V 2>&1 | tail -40
  ```
  Expect: every test passes, including all new R-rule tests and the tightened D-fc-4 reproducer.
- [ ] **Step 2:** Run the suite five times in a row (the chokepoint suite has known flakiness in headless QPA; verify our additions don't make it worse):
  ```bash
  for i in 1 2 3 4 5; do build-dev/bin/tst_live_render_focus_chokepoint_invariant 2>&1 | grep '^Totals'; done
  ```
  Acceptable: same level of flakiness as Task 1 baseline (1–3 failing tests per run is the existing background; if our additions consistently fail, fix them, not the baseline).
- [ ] **Step 3:** Run the wider live-render suite:
  ```bash
  ctest --test-dir build-dev -E 'realistic|benchmark' -j 4 2>&1 | grep -E 'tests passed|^The following' -A 12 | head -20
  ```
  Expect: the same 8 preexisting failures (`tst_live_render_cursor` etc.). No new failures.

---

## Task 8: Image variants of the rules

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp`

Proves the behaviour is genuinely registry-driven, not HR-specific. The Image-kind tests use a parsable image-syntax fixture.

- [ ] **Step 1:** First, verify what document text produces an Image block. Likely `![alt](http://example.com/x.png)` on its own paragraph. Construct a 3-row fixture: `alpha\n\n![alt](url)\n\nbeta\n` and confirm `m_fixture->model()->rowCount() == 3` with row 1 of kind `image`. If the parser doesn't produce a top-level Image block from that input, escalate — check `MarkoffParser` behaviour and adjust the fixture to a syntax that does.
- [ ] **Step 2:** Add Image variants for the substantive rules (skip the trivial ones that don't differ):
  - `arrow_down_lands_blockselected_on_image`
  - `arrow_down_from_blockselected_image_lands_on_text`
  - `backspace_at_para_start_after_image_selects_image`
  - `delete_at_para_end_before_image_selects_image`
  - `backspace_on_selected_image_removes_it`
  - `enter_on_selected_image_inserts_paragraph_after`
  - `typing_on_selected_image_is_noop`
  - `tripleclick_on_image_lands_blockselected`
- [ ] **Step 3:** Run the Image variant tests:
  ```bash
  build-dev/bin/tst_live_render_focus_chokepoint_invariant -tc 'image'
  ```
  (Or list them explicitly. `-tc` filtering may not exist in QTest; if not, just list each test name.)
  Expect: all PASS without any further production changes — the registry-driven dispatch in Tasks 4–6 already covers Image.
- [ ] **Step 4:** Commit:
  ```
  markoff-live: test — block-only rule coverage for Image kind
  ```

---

## Task 9: Shared `BlockOnlyDelegateBase.qml`

**Files:**
- Create: `libs/markoff-live/qml/delegates/BlockOnlyDelegateBase.qml`
- Modify: `libs/markoff-live/qml/delegates/HorizontalRuleDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/ImageDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/qmldir`

- [ ] **Step 1:** Create `BlockOnlyDelegateBase.qml`. It's an `Item` (root) that owns:
  - `property var blockAnchor: undefined` (captured at `onCompleted`)
  - `property int modelIndex: index`
  - `readonly property string blockText: model.text`
  - `readonly property var liveBinding: ListView.view ? ListView.view.binding : null`
  - `readonly property var cursorState: liveBinding ? liveBinding.cursorState : null`
  - `readonly property bool isSelected: cursorState !== null && cursorState.cursorKind === "BlockSelected" && cursorState.focusedAnchorRow === modelIndex`
  - `Keys.priority: Keys.BeforeItem`
  - `Keys.onPressed`: when `isSelected`, forward Up / Down / Backspace / Delete / Return / Enter / printable keys to the structural-key handler. Otherwise, `event.accepted = false`.
  - `function takeFocus(qtPos: int) { root.forceActiveFocus() }`
  - `Component.onCompleted` and `Component.onDestruction` registering with the chokepoint via the cached `blockAnchor`.
  - A default child `default property alias content: contentArea.data` so subclassing delegates can inject their content layer.
- [ ] **Step 2:** Refactor `HorizontalRuleDelegate.qml` to inherit from `BlockOnlyDelegateBase`. All it should contain is the line `Rectangle` and the selected-outline `Rectangle` — no `isSelected` binding, no `Keys`, no chokepoint registration.
- [ ] **Step 3:** Refactor `ImageDelegate.qml` similarly. Keep the image area, the cached blockAnchor logic, the alt-edit `BlockInternalEdit` machinery (out of scope for this spec — leave it).
- [ ] **Step 4:** Update `qmldir` if needed so the new base is loadable by `import org.markoff.live`.
- [ ] **Step 5:** Run the chokepoint suite plus the dogfood manually-runnable app:
  ```bash
  cmake --build build-dev --target markoff-live-app -j 8
  ctest --test-dir build-dev -R 'tst_live_render_focus_chokepoint_invariant' -j 8
  ```
  Expect: green. The base introduces no behaviour changes; the per-delegate logic is just moved.
- [ ] **Step 6:** Commit:
  ```
  markoff-live: slot — BlockOnlyDelegateBase shared base for HR/Image
  ```

---

## Task 10: Retire the old per-kind authority (invariant 3)

**Files:**
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`

The new authority is in place (Tasks 4–6). The old one must go in the same plan.

- [ ] **Step 1:** Delete the `hrNavigateUp`, `hrNavigateDown`, `hrDelete` lambdas and their registrations from `LiveStructuralKeyHandler::registerBuiltins()`.
- [ ] **Step 2:** Delete the `imgDelete` lambda and its registrations.
- [ ] **Step 3:** Verify `mathDelete` and the rest of the Math-specific registrations are unchanged. (Math is out of scope, but a careless `grep + sed` could easily snip them too.)
- [ ] **Step 4:** Re-run the chokepoint suite. Expect: still green.
- [ ] **Step 5:** Commit:
  ```
  markoff-live: slot — retire hrNavigate/Delete + imgDelete (folded into blockOnly* handlers)
  ```

---

## Task 11: Falsifiability stub

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/BlockKindRegistry.h`

Per spec §7 falsifiability strategy and invariant 4. The stub is kept in history as the audit trail.

- [ ] **Step 1:** Modify `BlockKindRegistry::isBlockOnly` to unconditionally return `false`:
  ```cpp
  bool isBlockOnly(const QString &) const {
      // FALSIFIABILITY PROOF — REVERTS NEXT COMMIT.
      // The block-only kind treatment lives entirely behind this predicate.
      // Making it false should turn the chokepoint, navigation, and
      // structural-key paths back into pre-spec behaviour and break every
      // R-rule test. Mirrors commits 2d609ba (takeFocus stub) and 20dcaee
      // (establishFocus stub) — see docs/specs/2026-05-13-block-only-kinds-design.md §7.
      return false;
  }
  ```
- [ ] **Step 2:** Build and run the chokepoint suite:
  ```bash
  cmake --build build-dev --target tst_live_render_focus_chokepoint_invariant -j 8
  build-dev/bin/tst_live_render_focus_chokepoint_invariant 2>&1 | grep '^Totals'
  ```
  Expect: a substantial number of failures. Specifically: every R-rule test in §7 of the spec should be red (HR + Image variants alike). If any pass, the corresponding test is too lenient — tighten it and re-stub before continuing.
- [ ] **Step 3:** Commit:
  ```
  markoff-live: stub — isBlockOnly always false (FALSIFIABILITY PROOF, REVERTS NEXT)
  ```
  Body cites the falsifiability strategy and the count of tests that went red.

---

## Task 12: Revert the falsifiability stub

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/BlockKindRegistry.h`

- [ ] **Step 1:** Use `git revert HEAD --no-edit` to revert Task 11's stub commit.
- [ ] **Step 2:** Verify the chokepoint suite is green again:
  ```bash
  cmake --build build-dev --target tst_live_render_focus_chokepoint_invariant -j 8
  build-dev/bin/tst_live_render_focus_chokepoint_invariant 2>&1 | grep '^Totals'
  ```
- [ ] **Step 3:** No new commit needed; the revert from Step 1 is the commit.

---

## Task 13: Discipline-log + queue update + dogfood-doc update

**Files:**
- Modify: `docs/queue.md`
- Modify: `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md`

- [ ] **Step 1:** In `docs/queue.md` § Discipline Log, add the Math-asymmetry entry from spec §10:
  ```
  - 2026-05-13 `libs/markoff-live/src/BlockKindRegistry.cpp:Math` — inv #8 — `isBlockOnly` is explicit-false for Math despite Math having no `TextCaret` in `supportedCursorVariants`. Transitional asymmetry; will be removed when Math becomes text-bearing in its own spec.
  ```
- [ ] **Step 2:** In `docs/queue.md` § #2 (cursor architecture cleanup), append a 2026-05-13 sub-banner referencing the spec, plan, and the commit chain landed in this work.
- [ ] **Step 3:** In `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md`, add a "D-fc-4 + block-only generalisation — resolved 2026-05-13" subsection pointing to the spec/plan and listing the new behaviours from spec §4. The tag wait-list expands to include the new behaviours.
- [ ] **Step 4:** Commit:
  ```
  docs: discipline log + queue + dogfood update for block-only kinds spec
  ```

---

## Task 14: Final regression check + dogfood-ready binary

**Files:** none (verification + manual prep)

- [ ] **Step 1:** Run the full live-render suite three times to confirm stability:
  ```bash
  for i in 1 2 3; do
    ctest --test-dir build-dev -E 'realistic|benchmark' -j 4 2>&1 | grep -E 'tests passed' | tail -1
  done
  ```
  Expect: same 8 preexisting failures across all runs.
- [ ] **Step 2:** Rebuild the app:
  ```bash
  cmake --build build-dev --target markoff-live-app -j 8
  ```
- [ ] **Step 3:** The binary at `build-dev/bin/markoff-live-app` is now ready for interactive dogfood. The user's testing checklist is in `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md` plus the new behaviours from spec §4. Specific scenarios to confirm:
  - Open a doc with an HR. Click into the paragraph above. Press Down twice. First press: HR is visibly selected. Second press: caret on the paragraph below.
  - From an empty paragraph after a typed `---<Enter>`: press Backspace. HR becomes selected (visible). Second Backspace deletes the HR.
  - On a selected HR: press Enter. New empty paragraph after, caret in it.
  - On a selected HR: type "x". Nothing happens; HR stays selected. (No alert, no document change.)
  - Repeat all of the above for an Image block (drop a `![alt](url)` reference into a doc).
  - Triple-click on an HR or Image — it gets selected, no surrounding rows go with it.

---

## Definition of done

- [ ] All 14 tasks complete.
- [ ] Chokepoint invariant suite: every new R-rule test passes for HR; every parallel Image variant passes. Suite-wide flakiness no worse than Task 1 baseline.
- [ ] D-fc-4 reproducer (`backspace_after_typed_hr_selects_hr_blockonly`) passes 5/5.
- [ ] Wider live-render suite: same 8 preexisting failures, no new ones.
- [ ] Falsifiability stub (Task 11) preserved in history with its revert (Task 12).
- [ ] `docs/queue.md` updated; discipline-log entry for Math asymmetry filed.
- [ ] Dogfood doc updated; binary ready.
- [ ] No new `Qt.callLater` or re-entrance guards introduced. (Verify by grepping the diff:
  `git log --oneline <baseline>..HEAD -- libs/markoff-live/ | xargs -I{} git show {} -- libs/markoff-live/ | grep -E 'Qt\.callLater|m_applying' | head`. Expect: zero new sites.)
- [ ] No new "kind-name fanning" introduced. (Verify by grepping for `kind == "hr"`, `kind == "image"` outside the registry registration site. Expect: zero new hits.)

---

## Self-review checklist (executor: run before declaring done)

- [ ] Spec §3 — `isBlockOnly` is an explicit field on the descriptor, not a derived predicate. HR + Image are `true`; Math is `false`.
- [ ] Spec §4 — every rule has a passing test; falsifiability stub demonstrates every rule's test breaks under the stub.
- [ ] Spec §5.4 — `MathDelegate.qml` is unchanged; only `HorizontalRuleDelegate` and `ImageDelegate` inherit the new base.
- [ ] Spec §6 — `hrNavigateUp/Down`, `hrDelete`, `imgDelete` are retired (deleted, not deprecated).
- [ ] Spec §10 — discipline-log entry filed.
- [ ] Invariant 3 — old authority retired in same plan: ✓ (Task 10).
- [ ] Invariant 4 — falsifiability proven by stub: ✓ (Task 11/12).
- [ ] Invariant 5 — tests exercise the production callsite via `LiveRealisticInputHarness`, not C++-only fakes: ✓.
- [ ] Invariant 8 — discipline log updated: ✓ (Task 13).
