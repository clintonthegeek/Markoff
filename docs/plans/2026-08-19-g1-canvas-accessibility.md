# Plan — markoff-canvas accessibility (G1 arc)

**Spec (normative — read §2, §3, §4 before Task A1.0):**
[`../specs/2026-08-19-g1-canvas-accessibility-design.md`](../specs/2026-08-19-g1-canvas-accessibility-design.md)
**Platform findings you will need (do not re-derive):** spec §4.6 —
the AT-SPI bridge is inside `libQt6Gui` (no plugin to install), Orca
is not installed yet, `Attribute::Level` probably does not reach
AT-SPI, and `ROLE_BLOCK_QUOTE`/`ROLE_MATH` are unreachable from Qt.
**Contract reference:** `libs/markoff-canvas/CLAUDE.md` (the four hard
rules) and the closed production plan
[`2026-08-13-canvas-production-plan.md`](2026-08-13-canvas-production-plan.md)
for the session protocol precedent this file follows.

This plan is written for **consecutive fresh agent sessions**. Each
task is sized for one session. Do the topmost unchecked task in the
current phase. Phase-close tasks (⏸) and the user gate (A-G1) are hard
stops.

---

## Session protocol

**Start:**
1. Read this file top to bottom, then spec §2 (why the shape is what
   it is), §4 (architecture), §4.6 (platform findings).
2. `git pull`, build, test:
   ```bash
   cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   cmake --build build-dev -j 4          # never more than -j 4
   scripts/run-tests.sh -R canvas
   ```
3. Confirm the previous task's checklist state matches reality. If it
   doesn't, fixing that IS your session.

**During:**
- Scope: `libs/markoff-canvas/` is open. **`libs/markoff-core/` is
  expected to need nothing** (spec §8) — if your task seems to need a
  core change, that is a finding to log and end the session on, not a
  fix to make. live/styled are bug-fix-only; source is untouched;
  live is retired.
- `tests/check-constitution.sh` must pass before every commit. **This
  arc should never strain C1–C4** — the whole point of the per-block
  shape (spec §2) is that a11y needs no new coordinate space, no
  guards, and no deferrals. If a task appears to need one, that is
  strong evidence the task is being done wrong; stop and log.
- Falsification protocol for every functional test: make it pass →
  plant a break in a throwaway commit → watch it fail → revert →
  record both SHAs. Probe/audit/perf tasks exempt (A1.0, A5.2, A5.3).
- **Test-run tier:** `scripts/run-tests.sh -R canvas` is the default.
  Full suite at every ⏸ phase close, and on any task that somehow
  touches core. A2.1 also runs full (UTF-8 conversion shares
  `coords::` with every leaf).

**End:** tick the checkbox, fill SHAs, append surprises to the
findings log at the bottom. Commit `canvas(A<n>.<m>): <summary>`.
Push.

**Decision rules:**
- Decide yourself + log: class shape, test mechanics, role/name
  wording, anything invisible outside the leaf.
- Stop + log + end session: any C1–C4 strain, any core change, any
  weakening of a done-when.
- Ask the user: scope changes, gate A-G1, and **every `--direct` run**
  (A5.3 is the only task that needs one).

---

## Checklist

| Task | Status | Commit | Falsification (break/revert) |
|---|---|---|---|
| **A1 — tree, roles, registration** | | | |
| A1.0 Bridge probe + `Attribute::Level` verdict (spike, throwaway) | ☑ | — | exempt |
| A1.1 `CanvasAccessible` container + factory registration | ☑ | `48c5ac1d` | break `7117c023` / revert `8c54a2e3` |
| A1.2 `CanvasBlockAccessible` skeleton + role/state mapping | ☑ | `21c371c8` | break `e4915b30` / revert `7e11e4ec` |
| A1.3 `accessibleDocumentName` public property + name resolution | ☑ | `893f68ef` | break `39e02ea6` / revert `7a2a30b1` |
| A1.4 ⏸ phase close (full suite) | ☑ | `PENDING_SHA` | exempt |
| **A2 — text interface** | | | |
| A2.1 `QAccessibleTextInterface` core: text/characterCount/offsets | ☐ | | |
| A2.2 Caret + selection, including cross-block presentation | ☐ | | |
| A2.3 Geometry: `characterRect`, `offsetAtPoint`, line boundaries | ☐ | | |
| A2.4 ⏸ phase close (full suite) | ☐ | | exempt |
| **A3 — notifications** | | | |
| A3.1 Event spy test harness | ☐ | | |
| A3.2 Caret/selection/focus events | ☐ | | |
| A3.3 Text insert/remove + block create/destroy events | ☐ | | |
| A3.4 ⏸ phase close (full suite) | ☐ | | exempt |
| **A4 — folding, actions, editable text** | | | |
| A4.1 Hidden/folded state + expand-collapse action | ☐ | | |
| A4.2 `QAccessibleEditableTextInterface` (decide in-task, see notes) | ☐ | | |
| A4.3 ⏸ phase close (full suite) | ☐ | | exempt |
| **A5 — acceptance** | | | |
| A5.1 Realization-bound test (spec §5) | ☐ | | exempt |
| A5.2 Audit pass: role table, event table, limitations log | ☐ | | exempt |
| **A-G1 — user gate: run the Orca pass now or defer?** | ☐ | — | — |
| A5.3 Manual Orca pass (`--direct`, needs permission) | ☐ | | exempt |
| A5.4 ⏸ arc close | ☐ | | exempt |

---

## Phase A1 — tree, roles, registration

### A1.0 — bridge probe + `Attribute::Level` verdict

**This is a throwaway spike, not shipped code.** It exists to settle
spec §4.6 finding 3 before A1.2 commits to a heading-level strategy.

Do:
- `sudo pacman -S orca` is **not** needed for this task — only
  `at-spi2-core` (installed) and a session bus.
- Build a scratch widget (in the scratchpad, not the repo) that
  subclasses `QAccessibleWidget`, implements
  `QAccessibleAttributesInterface` returning
  `{Attribute::Level: 2}`, and registers a factory.
- Run it with `QT_ACCESSIBILITY=1` / `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1`
  under a D-Bus session, then dump the object's AT-SPI attributes
  from a second process (`python-atspi`, or `busctl call` against
  `org.a11y.atspi.Accessible.GetAttributes`).
- Record: does `level` appear in the attribute dict?

**Done when:** the findings log carries a definitive yes/no plus the
exact probe command, and — if no — a one-line decision on the
fallback (description text is the expected answer; spec §4.6 finding
3 pre-authorizes it).

**Note:** if standing up an a11y bus in this environment turns out to
be a rabbit hole, timebox it. Falling back to "implement
`attributesInterface()` *and* put the level in the description" is
already the pre-authorized answer and costs nothing to do blind — the
probe just avoids carrying a redundant description forever. Log the
timeout and move on; do not burn a session on D-Bus plumbing.

### A1.1 — `CanvasAccessible` container + factory registration

New `src/Accessibility.h` / `.cpp` (private to the leaf, no public
header). `CanvasAccessible : QAccessibleWidget` per spec §4.1:
role `Document`, child count from `View::blockCount()`, `child(i)` via
`View::blockIdAt(i)`, `childAt(x,y)`, `indexOfChild()`. Factory
installed once (guard against re-registration across `View` instances
per spec §4.5).

Children can return a placeholder `CanvasBlockAccessible` that only
answers `role()`/`rect()` at this stage — A1.2 fills it in.

**Done when:** `QAccessible::queryAccessibleInterface(view)` returns a
`Document`-role interface whose `childCount()` tracks `blockCount()`
across insert/remove; new `tests/tst_canvas_accessibility.cpp` in the
suite; baseline up from 315.

### A1.2 — `CanvasBlockAccessible` skeleton + role/state mapping

Implement the spec §4.2 table, one test case per row. States:
`focusable`/`focused` (block holds the caret), `editable` (from
`View::isReadOnly()`), `checkable`/`checked` for task list items from
the `Checked` attr, `invisible` for hidden blocks.

Heading level per A1.0's verdict.

**Done when:** every `BlockKind` maps to its spec'd role, states
verified, and the two unreachable-role limitations (spec §4.6
finding 4) are recorded as comments at the mapping site so nobody
"fixes" them later.

### A1.3 — `accessibleDocumentName` public property

Per spec §9 Q2 (user-approved API extension). Add to `View` and
forward from `EditorWidget`:

```cpp
void    setAccessibleDocumentName(const QString &name);
QString accessibleDocumentName() const;
```

Container `QAccessible::Name` resolution order:
`accessibleDocumentName()` → `View::inlineTitle()` →
`tr("Markdown document")`.

**Done when:** all three fallback levels are tested; the property is
documented in the header with a one-line note that it exists because
only the embedder knows the document's identity.

### A1.4 — ⏸ phase close

Full suite. Constitution check. Update the canvas `CLAUDE.md` status
line. No code changes beyond what the close reveals.

---

## Phase A2 — text interface

### A2.1 — `QAccessibleTextInterface` core

`text(start, end)`, `characterCount()`, `textAtOffset` /
`textBeforeOffset` / `textAfterOffset` for `CharBoundary`,
`WordBoundary`, `ParagraphBoundary` (a block *is* a paragraph, so
paragraph boundary is the whole block).

**All offsets are within this block's own buffer.** Convert
byte↔QChar with `coords::` (`<markoff/core/TextUnits.h>`) — the same
helpers the leaf already uses. Never sum across blocks (C4).

**Test the UTF-8 boundary explicitly** — multi-byte characters
(accents, CJK, emoji with surrogate pairs) are the obvious break
point, and a QChar/byte mix-up here is silent.

**Runs the full suite** (shares `coords::` with every leaf).

**Done when:** all boundary types work on ASCII and multi-byte
fixtures; `characterCount()` is QChar count, not byte count.

### A2.2 — caret and selection

`cursorPosition()` returns a position only when
`View::caretBlock() == id`. `selectionCount()`/`selection()` return
this block's intersection with the view selection, so a cross-block
selection appears as a selection on each spanned block (spec §4.1).
`setCursorPosition()` routes to `View::setCaretPosition()`.

**Done when:** single-block and cross-block selections both present
correctly; a selection ending mid-block reports the right partial
range; `setCursorPosition` round-trips.

### A2.3 — geometry and line boundaries

`characterRect(offset)`, `offsetAtPoint(point)`, and
`textAtOffset(..., LineBoundary)`. These need a `QTextLayout`, so
they realize the queried block — that is expected and bounded
(spec §5). Map block-local rects to global coordinates via
`View::blockRect()` + viewport mapping.

**Done when:** round-trip `offsetAtPoint(characterRect(n).center()) == n`
holds for realized blocks; querying an unrealized block realizes just
that one.

### A2.4 — ⏸ phase close

Full suite + perf re-run (`build-perf`, E9's four budgets). A11y
should add nothing per-keystroke, so a regression here means an
`updateAccessibility()` call landed on a hot path — find it.

---

## Phase A3 — notifications

### A3.1 — event spy harness

`QAccessible::installUpdateHandler` spy collecting
`(object, event type, payload)`. Reusable by A3.2/A3.3.

**Note:** `installUpdateHandler` is global and returns the previous
handler — restore it in the test's cleanup or later tests see stale
events.

### A3.2 — caret, selection, focus events

Wire `QAccessibleTextCursorEvent`, `QAccessibleTextSelectionEvent`,
and focus events to `View`'s existing signal points (spec §4.4).
**No new deferral** (C2) — emit synchronously where the state
changes.

### A3.3 — text and structure events

`QAccessibleTextInsertEvent` / `RemoveEvent` per block on edit;
`ObjectCreated`/`ObjectDestroyed` on block insert/remove, driven by
`View::onDocumentChanged` diffing the id list — which is also the
eviction trigger for spec §9 Q1 (block-accessible lifetime under CRDT
churn). **Do both in this task**; they are the same diff.

Call `QAccessible::deleteAccessibleInterface` for removed blocks.

**Done when:** the §4.4 event table is fully covered by spy
assertions, including a remote/CRDT edit path, and no interface
outlives its block.

### A3.4 — ⏸ phase close

Full suite.

---

## Phase A4 — folding, actions, editable text

### A4.1 — folding state and actions

Hidden blocks stay in the child list with `state().invisible`
(spec §4.3 — removing them destabilizes child indices for AT clients
holding references). Folded heads get `expandable`/`expanded` plus
`QAccessibleActionInterface` with an expand/collapse action wired to
`View::toggleFold()`, and emit `QAccessibleStateChangeEvent`.

### A4.2 — `QAccessibleEditableTextInterface`

**Decide in-task and log the reasoning.** The interface
(`insertText`/`deleteText`/`replaceText`) lets an AT client edit
programmatically. Canvas is an editor, so implementing it is
defensible; but every method must route through the existing
`insertText()`/document command path — **never** touch buffers
directly — and read-only mode must reject all three.

If the routing turns out not to be clean, skipping it is acceptable:
role `ROLE_TEXT` plus `state().editable` already tells a screen reader
the text is editable, and normal typing works through the existing key
pipeline regardless. Log whichever way it goes.

### A4.3 — ⏸ phase close

Full suite.

---

## Phase A5 — acceptance

### A5.1 — realization-bound test

Per spec §5: walk every block's `text()`, `characterCount()`, role,
and state on a large fixture; assert `View::realizedBlockCount()` is
unchanged. This is the test that keeps the tree shape's main
advantage from silently rotting.

### A5.2 — audit pass

Read the whole a11y surface against spec §4 as written prose, not as
a diff. Confirm: role table matches §4.2, event table matches §4.4,
the §6 table limitation and the §4.6 finding-4 role gaps are recorded
in the code and in `docs/queue.md`. File the Qt upstream issues (level
attribute, missing roles) or log a decision not to.

### A-G1 — user gate

Run the Orca pass now, or defer it to a later dogfood session? Either
way it needs `--direct` permission. **Stop here and ask.**

### A5.3 — manual Orca pass (`--direct`, per-task permission)

Prerequisite: `sudo pacman -S orca` (spec §4.6 finding 2).

Drive the demo app with Orca running. Verify: document navigation
(`Orca+arrow`), block-by-block reading, heading announcement (and
level, if A1.0 said it works), caret announcement while typing,
selection announcement, list-item checked state, fold expand/collapse,
and that a table announces as a table.

**This is acceptance, not a ratchet.** Findings become follow-up
tasks or logged limitations — a disappointing result does not
retroactively fail the phases.

### A5.4 — ⏸ arc close

Full suite, perf, constitution. Update `docs/STATUS.md`, root
`CLAUDE.md`, canvas `CLAUDE.md`. Move this plan's status to CLOSED and
record the final baseline.

---

## Findings log

(One line minimum per task. Append; never rewrite.)

- **A1.0 (2026-08-19): `Attribute::Level` DOES reach AT-SPI — spec §4.6
  finding 3's suspicion was wrong.** Probe: scratch `QApplication`
  (Qt 6.11.1, Wayland session) with a `QLabel` wrapped by a
  `QAccessibleWidget` subclass implementing
  `QAccessibleAttributesInterface::attributeValue(Level) == 2`,
  factory-installed, run with `QT_ACCESSIBILITY=1` and
  `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1`. Dumped from a second process
  via `busctl --address="unix:path=$XDG_RUNTIME_DIR/at-spi/bus_1" call
  <app-bus-name> <object-path> org.a11y.atspi.Accessible
  GetAttributes` (the AT-SPI bus is separate from the session bus;
  its address comes from `org.a11y.Bus /org/a11y/bus GetAddress` on
  the session bus). Result: `a{ss} 1 "level" "2"` — present and
  correct. Decision: implement `attributesInterface()` in A1.2/A1.4
  as planned, and **skip the redundant description-text fallback**
  the spec pre-authorized for the no-answer case — it is not needed.
  File nothing upstream (no bug exists). Scratch probe lived at
  `/tmp/.../scratchpad/a11y_probe/`, not committed, deleted after the
  probe.
- **A1.1 (2026-08-19): `CanvasAccessible` container landed.** New
  `src/Accessibility.{h,cpp}`: `CanvasAccessible : QAccessibleWidget`
  (role `Document`, `childCount()`/`child()`/`childAt()`/
  `indexOfChild()` backed by `View::blockCount()`/`blockIdAt()`/
  `blockIndexOf()`/`blockRect()`, `state().editable` tracking
  `isReadOnly()`) and a `CanvasBlockAccessible` skeleton implementing
  bare `QAccessibleInterface` (real `rect()` — maps
  `View::blockRect()` through the scroll offset and viewport to
  global coords; placeholder `role()`/`state()`, replaced by A1.2).
  Factory installed once from `View`'s constructor via a static-bool
  guard. Block-accessible cache is `std::unordered_map<BlockId, …>`,
  not `QHash` — `QHash`'s copy-on-write `Node` requires a copyable
  value and rejects `std::unique_ptr` (compile error, not a runtime
  finding); `BlockId` needed a local `std::hash` shim (`BlockIdHash`)
  since it only defines Qt's `qHash`. `iterateBlocks()`/`blockCount()`
  already track the FULL document (not `realizedBlockCount()`) via
  `BlockLayoutCache::entries()` — confirmed by reading
  `BlockLayoutCache.h`, no core change needed, spec §8 holds.
  Falsification: broke `childCount()` to return a hardcoded `0`
  (`7117c023`), 3 of 4 new test cases failed as expected, reverted
  (`8c54a2e3`). Full suite 207/207 → **208/208**. Test-design note
  (not a product finding): `MarkoffDocument::loadFromMarkdown()`'s
  `d2DocumentChanged` is one-event-loop-spin deferred (core's own doc
  comment) — a *second* load on an already-attached `View` needs
  `QCoreApplication::processEvents()` before `childCount()` reflects
  it, since `View::onDocumentChanged()` is wired to that signal, not
  the synchronous `documentChanged()`.
- **A1.2 (2026-08-19): `CanvasBlockAccessible` role/state mapping
  landed.** `role()` switches on `MarkoffDocument::blockKind()` per
  spec §4.2's table, with two special cases: footnote-def paragraphs
  (`View::isFootnoteDefBlock()`) map to `Section`, and the two spec
  §4.6 finding-4 limitations (`BlockQuote` → `Section`, not
  `ROLE_BLOCK_QUOTE`; `Math` → `StaticText`, not `ROLE_MATH` — neither
  reachable from Qt) are recorded as comments at the mapping site.
  `state()` sets `focusable` (always true), `focused`
  (`View::caretBlock() == id`), `editable` (`!View::isReadOnly()`),
  `checkable`/`checked` (task `ListItem`s — `MarkerStyle == "task"` +
  the `Checked` attr), and `invisible`
  (`View::isBlockHidden()`). `CanvasBlockAccessible` gained
  `QAccessibleAttributesInterface` via `interface_cast`, exposing
  `Attribute::Level` for `Heading` blocks only (empty `attributeKeys()`
  otherwise) — A1.0's probe already confirmed this reaches AT-SPI, so
  no description-text fallback was implemented.
  **Fixture-design finding (not a product bug):** `BlockKind::Math` and
  `BlockKind::Mermaid` are **never** produced by `loadFromMarkdown` —
  confirmed by grep (`MarkoffDocument.cpp`'s `mapTopLevelKind` switch
  has no case for either; `BlockPresentation.cpp`'s own comment: "
  `BlockKind::Mermaid` itself is never assigned by the load path").
  `Math` is reachable only via live typed inference (`$$…$$`
  promotion); `Mermaid` is reachable through **no path in canvas at
  all** — a mermaid fence always loads as `CodeBlock` with
  `infoString == "mermaid"`. Both role-mapping test rows use
  `MarkoffDocument::testInsertBlock(kind, content)` (a document-level
  test helper, unconditionally compiled, not previously used outside
  `markoff-core`'s own D2 tests) followed by `View::setDocument()` —
  `setDocument()` primes the block-index cache synchronously at attach
  time (`onDocumentChanged()` called directly, not via the debounced
  `d2DocumentChanged` signal `testInsertBlock` never emits anyway), so
  no `processEvents()` spin is needed. `HtmlBlock`, by contrast,
  **does** load correctly via a plain `loadFromMarkdown("<div>\n…\n</div>\n")`
  fixture — confirmed empirically; `MarkoffDocument.cpp`'s top-level
  kind map has a direct, unconditional `Kind::HtmlBlock` →
  `BlockKind::HtmlBlock` case (unlike `Image`, which needs a load-time
  caret-block promotion pass — also exercised, and also just worked).
  16 new test cases added to `tst_canvas_accessibility.cpp` (13 role
  rows + 3 state groups), all passing on first run; no core change
  needed (spec §8 held). Falsification: broke the `Heading` role
  mapping to return `Paragraph` (`e4915b30`), 1 of 22 accessibility
  tests failed as expected, reverted (`7e11e4ec`). Canvas suite
  unaffected in target count (still 40 targets — A1.2 only grew an
  existing test binary); full suite target count holds at
  **208/208** (same as A1.1's ending baseline — A1.2 added no new
  test *executable*, only cases within the existing one).
- **A1.3 (2026-08-19): `accessibleDocumentName` property + container
  Name resolution landed.** `View::setAccessibleDocumentName(const
  QString &)`/`accessibleDocumentName()` — no-op-if-unchanged setter,
  same shape as `setInlineTitle`, but no repaint (pure a11y-name-
  resolution input, spec §9 Q2, never rendered). `EditorWidget` gets
  the same thin pass-through every other `View` forward already uses.
  `CanvasAccessible::text(QAccessible::Text)` implements the
  container's `QAccessible::Name` resolution chain:
  `accessibleDocumentName()` → `inlineTitle()` →
  `tr("Markdown document")`; every other `Text` value falls through
  to `QAccessibleWidget`'s default. Three new tests cover all three
  fallback levels plus a clear-falls-back-through round trip
  (22 → 25 accessibility test cases). No core change needed (spec §8
  held). Falsification: dropped the `accessibleDocumentName()` level
  from the resolution chain (`39e02ea6`), 1 of 25 tests failed as
  expected, reverted (`7a2a30b1`) — confirmed byte-identical to the
  real commit's file (`git diff 893f68ef 7a2a30b1` empty). Full suite
  target count holds at **208/208**.
  **Process note (not a product finding):** this task's own
  falsification step went wrong twice before landing cleanly — worth
  recording so a future session recognizes the failure mode instead
  of re-discovering it. First, the real implementation and the
  falsification break were committed together as a single commit
  (same mistake A1.2's session caught and fixed the same way: `git
  reset --soft` back to the prior real commit, then commit the real
  work and the break separately). Second, after `git revert --no-edit
  HEAD` correctly un-broke the file, a second, unintended `revert`
  landed on top (reflog shows a "Reapply" commit undoing the revert)
  — cause not fully isolated, but the working tree ended up back in
  the broken state despite `git status` reporting clean, because a
  `git reset --soft` earlier in the same recovery had moved HEAD
  without touching the working tree/index (soft reset's documented
  behavior, but easy to misapply when the goal is "make the files
  match commit X" — that needs `--hard` or `git checkout <commit> --
  <path>`, not `--soft`). Recovered by reverting via `git checkout
  893f68ef -- Accessibility.cpp` (content-addressed, not history-
  addressed) and diffing the two commits' trees directly to confirm
  byte-identity before trusting the test run.
- **A1.4 (2026-08-19): Phase A1 closed — verification only, no code
  changes.** Confirmed A1.0–A1.3 all ☑ with findings-log entries
  matching what's actually in `src/Accessibility.{h,cpp}` (container +
  per-block tree, role/state mapping incl. `Attribute::Level` on
  headings, `accessibleDocumentName` + three-level Name resolution).
  Clean rebuild (`cmake --build build-dev -j 4`, no warnings of note).
  Full suite via `scripts/run-tests.sh`: **208/208**, same target
  count as A1.1's ending baseline — A1.2/A1.3 grew test *cases* inside
  the existing `tst_canvas_accessibility` binary, not new executables,
  so the target count never moved across A1. `tst_realistic` (105.67s)
  and `tst_benchmark` (307.76s) both passed.
  `libs/markoff-canvas/tests/check-constitution.sh`: clean (C1–C4)
  over 79 files. Both remotes (`codeberg` primary, `origin` GitHub
  mirror) were already in sync with `HEAD` before this task started —
  nothing to pull. Nothing surfaced needing a fix; per the plan's task
  description ("no code changes beyond what the close reveals") this
  session made **docs-only** changes: canvas `CLAUDE.md` status
  section rewritten to describe A1 as closed and point at A2.1 as the
  next task; root `CLAUDE.md`'s "Current workfront" section updated
  the same way (was still saying "Start at A1.0", now stale).
