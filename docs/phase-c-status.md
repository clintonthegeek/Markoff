# Phase C Status

> **HISTORICAL — do not update.** This board tracks the master-side
> Phase C narrative (C1–C7 work-units against the four-leaves
> architecture). That architecture was retired on
> `exploration/new-foundation` by the D-arc + E-arc rebuild. Phase C is
> not the active sequence on this branch.
>
> Authoritative posture for this branch:
> [`handoff/2026-05-20-port-first-session-recap.md`](handoff/2026-05-20-port-first-session-recap.md).
> Live status: [`e-arc/e-arc-status.md`](e-arc/e-arc-status.md).
>
> Kept in tree so historical commit references (C3 soak fixes, the
> editor key-dispatch SESSION-BRIEF, etc.) remain interpretable.
> At merge-to-master the file moves to `docs/archive/phase-c/`.

Living status board for the Phase C work-units. The Corbomite agent
updates this file in every Phase C commit touching either repo. Entries
are append-only in the "Activity log" section; the work-unit status table
gets edited in place.

## Scope agreement

Phase C delivers the final shape of the tri-view API and absorbs the
Corbomite-prescribed features the original four work-units didn't cover.
Seven work-units total:

| ID   | Work-unit                                              | Markoff tag on completion |
| ---- | ------------------------------------------------------ | ------------------------- |
| C1   | DI seam (retires `MARKOFF_READING_USE_REAL_COREDEPS`)  | `v0.3.0`                  |
| C5   | Reading-mode interaction parity (Cluster V Phase 4)    | `v0.4.0`                  |
| C6   | Editor state + context-menu contribution surface       | `v0.5.0`                  |
| C3   | `MarkoffDocument` becomes content-authoritative        | `v0.6.0`                  |
| C7   | Source feature completion (find/replace + fold-gutter) | `v0.7.0`                  |
| C2   | Theme / ResourceProvider / LinkResolver consolidation  | `v0.8.0`                  |
| C4   | Renderer unification (Code/Math/Mermaid)               | `v0.9.0`                  |

Original IDs (C1–C4) preserved. **New IDs: C5, C6, C7** — added
2026-04-20 when the Corbomite agent took the Phase C baton and folded in
Corbomite's outstanding Markoff-side prescriptions. The ID numbering is
deliberately non-monotonic: it reflects the original handoff order
(C1→C3→C2→C4) with C5/C6/C7 slotted where they fit topologically.

## Ordering rationale

**C1 first** — it closes the Phase B bridge cleanly and establishes
the interface-injection pattern the rest of Phase C reuses. Nothing
else can land cleanly until `MARKOFF_READING_USE_REAL_COREDEPS` is
retired because mid-flight refactors would have to serve both the
option'd and de-option'd worlds.

**C5 and C6 come next** — they are small, high-user-visibility
unlocks that finish Cluster V (the Corbomite-side UI cluster that was
deferred to let Phase C run). Doing them right after C1 lets Corbomite
ship user-visible progress while the larger C3 is drafted. C5 is
ReadingView polish; C6 is a pure signal-addition to `Markoff::Editor`.

**C3 before C2** — shared-document adoption is the biggest
user-visible unlock and is mostly orthogonal to Theme/Provider/Resolver
shapes. Doing it in isolation avoids cross-coupling two large
refactors. Also closes the door on Corbomite `NoteDocument` shaping
decisions that would otherwise drift while C2 is in flight.

**C7 before C2** — find/replace is the last Qutepart-fork Phase 3
deliverable and completes the Source-mode UI (Corbomite's Cluster R
menus have a disabled "Find…"/"Replace…" placeholder waiting on it).
Landing it before C2 means C2's Theme consolidation only has to touch
finished surfaces.

**C2 late** — Theme consolidation is the most likely source of
CorbomiteApp call-site breakage. Do it once the rest of Phase C is
stable so there's only one high-breakage coordination beat.

**C4 last** — smallest, mostly internal, no consumer-facing surface.
Polish. Absorbs any renderer-unification follow-ups that surfaced
during C1–C7.

## Work-unit status

| ID   | Status       | Spec                                   | Plan                                   | Markoff PR/branch    | Corbomite PR/branch  | Tag       |
| ---- | ------------ | -------------------------------------- | -------------------------------------- | -------------------- | -------------------- | --------- |
| C1   | markoff ready (v0.3.0) — corbomite adapter shipped; Phase B bridge retired | [C1 DI seam](docs/specs/2026-04-20-phase-c1-di-seam.md) | [C1 plan](docs/plans/2026-04-20-phase-c1-di-seam.md) | `master`             | Corbomite `59ecd5cb` | `v0.3.0`  |
| C5   | markoff ready (v0.4.0) | [C5 spec](specs/2026-04-20-phase-c5-reading-interaction-parity.md) | [C5 plan](plans/2026-04-20-phase-c5-reading-interaction-parity.md) | `master`             | —                    | `v0.4.0`  |
| C6   | done | [C6 wrapper spec](specs/2026-04-20-phase-c6-editor-state-context-menu.md) + [consumer-spec §1-8 + §9](libs/markoff-live/docs/specs/2026-04-20-consumer-editor-state-surface.md) | [C6 plan](plans/2026-04-20-phase-c6-editor-state-context-menu.md) | `master`             | Corbomite `a893c88d` | `v0.5.0`  |
| C3   | **in soak** — `v0.6.0` tagged premature per 2026-04-21 C3 landing review; re-tagged `v0.6.0-alpha.2` at same SHA pending dogfood week. Fix commits tag `v0.6.0-alpha.3/.4/...`; stability re-tags as `v0.6.1`. | [C3 spec](specs/2026-04-20-phase-c3-markoff-document-content-authoritative.md) | [C3 plan](plans/2026-04-20-phase-c3-markoff-document-content-authoritative.md) | `master`             | Corbomite `c6c4f446`..`23bc5094` | `v0.6.0` + `v0.6.0-alpha.2` |
| C7   | **BLOCKED** — editor key-dispatch architectural flaw must be resolved first ([spec](specs/2026-04-21-editor-key-dispatch-architecture.md)) ⟵ **fresh-context agents: start with [session brief](specs/2026-04-21-editor-key-dispatch-SESSION-BRIEF.md) ⟵** . See activity log 2026-04-21. Design inputs ready but implementation deferred: [find/replace design](libs/markoff-live/docs/specs/2026-04-14-find-replace-design.md) + [code-folding Kate harvest](libs/markoff-live/docs/specs/2026-04-14-code-folding-kate-harvest.md) + [find/replace Kate harvest](libs/markoff-live/docs/specs/2026-04-14-find-replace-kate-harvest.md) | —                                      | —                    | —                    | —         |
| C2   | not started  | —                                      | —                                      | —                    | —                    | —         |
| C4   | not started  | —                                      | —                                      | —                    | —                    | —         |

Status values: `not started` → `requirements — see inputs` → `spec drafted` → `spec approved` → `markoff implementing` → `markoff ready` (tag exists) → `corbomite adapting` → `corbomite shipped` → `markoff cleanup` → `done`.

## Work-unit input prescriptions (Corbomite side)

The Corbomite app team prescribed these features before the Phase C
handoff. Each bullet is a must-land requirement — specs get drafted to
satisfy them, not to re-negotiate them.

### C1 — DI seam

- Replace the `MARKOFF_READING_USE_REAL_COREDEPS` CMake option with
  interface injection. Interfaces Markoff defines: `IEmbedRegistry`,
  `ICodeBlockProcessorRegistry`, `IPostProcessorRegistry`,
  `IVaultResourceProvider`, `IEmbedDepthGuard`,
  `IMarkdownRenderChildFactory`, `IMermaidRenderer`, `ILinkResolver`,
  `IMetadataCache` (Corbomite implements each).
- Retire the `libs/markoff-reading/stubs/corbomite/` shim tree.
- Host plugs interfaces in at runtime (setter on `ReadingView` / a
  `Context` object consumed at construction — shape to be designed).
- Non-host consumers get default no-op implementations that work
  standalone (`MermaidRenderer` without mmdr → placeholder; `EmbedRegistry`
  empty → no embeds resolve).

### C5 — Reading-mode interaction parity (Cluster V Phase 4 absorption)

From `Corbomite/docs/superpowers/plans/2026-04-20-cluster-v-editor-workspace-ui-surfacing.md` Phase 4:

- **`ReadingView::linkHovered(QString href, QPoint globalPos)` signal.**
  Today `ReadingView` emits `wikiLinkHovered` which is too narrow; the
  rename/alias covers regular links too. Preserve existing emit sites;
  add a unified signal. Corbomite's `HoverPopover` wires to the new
  signal.
- **Click-to-fold on `HeadingItem`.** Clicking a heading's visual
  marker toggles its fold. Uses the existing fold-state infrastructure
  (`foldedHeadings()` / `setFoldedHeadings()`).
- **`codeBlockProcessorRegistry` routing in `ReadingView`'s pipeline.**
  ReadingView's `SectionLayout` must consult the registry when a code
  block's language matches a registered processor, same as Markoff
  Live already does. Today code blocks always fall through to the
  default syntax-highlighting path.
- **`ReadingView::zoomIn() / zoomOut() / resetZoom()` virtuals.**
  Override the `MarkdownView` default no-op implementations. ReadingView
  already has internal zoom state; the virtuals expose it. Corbomite's
  `View::zoomIn/Out/Reset` dispatches to this.

### C6 — Editor state + context-menu contribution surface

Full spec already drafted — see
[`libs/markoff-live/docs/specs/2026-04-20-consumer-editor-state-surface.md`](libs/markoff-live/docs/specs/2026-04-20-consumer-editor-state-surface.md)
(recovered from Corbomite's submodule at v0.2.8 after being stranded
on the local ahead-master for three weeks).

- `EditorContext` struct covering: inline-span membership
  (bold/italic/strike/inline-code/highlight/link/wiki-link/tag/embed/
  footnote/math), block kind (paragraph/heading-N/code/blockquote/
  callout/list-item/task-item), heading level (0–6), table coords
  (row/col, header flag), and link/tag/footnote URIs.
- `Q_SIGNAL void contextChanged(const EditorContext &)` — debounced
  to ~50 ms or "cursor-settled" equivalent per the spec's §4.
- Pull accessor `EditorContext Editor::context() const` — for host
  code that can't use the signal (e.g. menu-open callbacks).
- Default impls are fine to ship first; full classification is the
  main engineering cost and can be built incrementally.
- **Context-menu contribution point (§9):**
  `Q_SIGNAL void aboutToShowContextMenu(QMenu *menu,
  const EditorContext &ctx, QPoint globalPos)` emitted
  mid-`contextMenuEvent` after Markoff's built-ins are inserted and
  before `menu->exec()`. Corbomite's `MainWindow` wraps the menu in
  `MenuSectionHelper` and injects Format/Heading/Insert/Table
  entries. Same signal is the natural hook for third-party plugin
  `editor-menu` contributions later.

### C3 — `MarkoffDocument` becomes content-authoritative

- `MarkoffDocument` owns the text buffer. All three leaves subscribe
  to changes via Qt signals and update their views.
- Corbomite's `NoteDocument` becomes a thin wrapper over
  `MarkoffDocument` (or pools — the right shape to be decided in the
  C3 spec).
- Per-leaf ephemeral state (cursor, selection, scroll, fold) stays
  with the leaf — only text + frontmatter + parse tree are shared.
- Closes out the deferred items in
  `Markoff/docs/plans/2026-04-20-tri-view-phase-a.md` §"Deferred to
  Phase C": async parse worker sharing, precise pixel↔visual-line
  scroll conversion, source-offset↔per-block cursor translation.

### C7 — Source feature completion

- **Public find/replace API** on `Markoff::Source::SourceEditor`.
  Absorbs what was planned as "Qutepart-fork Phase 3" on the
  Corbomite side. See
  [`libs/markoff-live/docs/specs/2026-04-14-find-replace-design.md`](libs/markoff-live/docs/specs/2026-04-14-find-replace-design.md)
  for the UI shape Markoff Live already targets; Source mode shares
  most of it modulo Qutepart's peculiarities.
- **Fold-gutter coordinator** — complete `FoldGutter::paint()`
  (the Cluster V.2 scouting flagged this is not a theme concern;
  the Qutepart-fork Phase 6 reference was a misread) and wire the
  Markoff-internal fold coordinator across Source and Live.
- Corbomite's Cluster R "Find…" / "Replace…" menu slots (today
  disabled placeholders with a tooltip pointing at Qutepart-fork P3)
  light up when this lands.

### C2 — Theme / ResourceProvider / LinkResolver consolidation

- One `Theme` type in `Markoff::Core`. Replaces the three in
  `Markoff::Live`, `Markoff::Reading`, and `Markoff::Source`.
  Corbomite's `KColorSchemeManager` (Cluster V Phase 1) drives it.
- `IVaultResourceProvider` from C1 is the abstract surface;
  Corbomite's concrete `VaultResourceProvider` (promoted to
  `libs/core/` in Cluster J) implements it.
- `ILinkResolver` — same pattern; Corbomite's concrete resolver
  stays in `libs/storage/`.
- **Breaking:** Corbomite call sites using per-library Theme types
  migrate to the unified one. Most are in `NoteEditorWidget` and
  `MainWindow`.

### C4 — Renderer unification

- Collapse the two-path code-block rendering (Markoff Live's
  registry vs. Reading's direct dispatch) into a single registry
  pattern.
- Collapse the two-path math rendering (JKQTMathText direct
  embed vs. registry-routed).
- Audit Mermaid dispatch — today it's registry-routed in Reading
  only; evaluate whether Live should also gain mermaid support.
- Smallest work-unit; final polish.

## Recipe per work-unit

1. **Draft spec** in `docs/specs/YYYY-MM-DD-phase-c<N>-<topic>.md`. Include:
   - Interface shape(s) proposed
   - Migration path for any Markoff internals that call the same surface
   - Breaking-change manifest for CorbomiteApp
   - Acceptance criteria
2. **Update the table above** to `spec drafted`. Commit the spec.
3. **User or a parallel reviewer reads the spec**, either approves or sends back. (For unit-sized changes with no ambiguity, the Corbomite agent may self-approve and proceed — note in the activity log.)
4. **Draft plan** in `docs/plans/YYYY-MM-DD-phase-c<N>-<topic>.md`. Task-by-task with explicit files + commit messages, per the Phase A/B convention.
5. **Implement on Markoff `master`** (per Corbomite's no-feature-branches convention; commits land directly). Green tests after each commit. Set status to `markoff implementing`.
6. **Tag** the Markoff SHA once all Phase-C-<N> tasks are done. Set status to `markoff ready`.
7. **Bump Corbomite's submodule pin** and write the CorbomiteApp-side adaptation. Set status to `corbomite adapting`.
8. **Ship on the Corbomite side**, run CorbomiteApp end-to-end smoke. Set status to `corbomite shipped`.
9. **Return to Markoff** to retire any bridge code left over from the previous phase. Set status to `markoff cleanup`. Tag a cleanup release if the diff is non-trivial.
10. **Close** to `done`. Activity-log a brief retrospective: what broke, what we'd do differently.

## Invariants (copy from handoff doc — repeat for visibility)

1. Standalone Markoff build + ctest always green. `MARKOFF_READING_USE_REAL_COREDEPS=OFF` is the standalone default; it retires in C1 and whatever replaces it inherits the invariant.
2. No `Corbomite`-named types in Markoff public interfaces (Phase B stubs excepted; retire in C1).
3. Tests that need Corbomite concretes gate on the appropriate CMake option / injection mechanism.
4. Every work-unit tags a Markoff version.
5. `master` is append-only. No force-push.
6. Commit identity stays unified across both repos (Corbomite agent, co-author trailer).

## Activity log

Append in reverse-chronological order (newest first).

### 2026-04-21 — Dogfood paused; fresh-context session brief for architectural fix

User has paused dogfooding. Next work unit is the architectural fix for `Markoff::Editor` key-dispatch. Fresh-context agents picking this up MUST start with the **[session brief](specs/2026-04-21-editor-key-dispatch-SESSION-BRIEF.md)** — it lists the full reading path, cross-repo stakeholder enumeration, required investigation, brainstorm topics, deliverables, and hard warnings. The user's explicit ask: "go deep on analysis and find solutions which please *all* stakeholders in corbomite and markoff."

The brief contains a hard-warning block: do NOT rush to Option A (the main architectural spec's recommended choice) without evaluating all options, hybrids, and stakeholder vetoes. Do NOT skip brainstorming. Do NOT tag `v0.6.1` with bandages in place. Do NOT treat Corbomite ctest as sufficient — manual dogfood required for key-dispatch behaviours.

### 2026-04-21 — Soak-week fix series alpha.3 → alpha.8; C7 BLOCKED on architectural spec

Six consecutive typing-triggered SEGVs during user dogfooding surfaced a mix of C3-integration bugs and one pre-existing `Markoff::Editor` architectural flaw. Six fix tags pushed; the final one (alpha.8) is explicitly a **bandage** and the architectural issue **blocks C7 until resolved**.

**Soak crashes + fixes:**

| Tag | Crash trigger | Root cause | Fix |
|---|---|---|---|
| `v0.6.0-alpha.3` | Typing right after opening a note | Scene had no focusItem; key events bubbled from m_view back to Editor in infinite recursion | Focus-loop at tail of `onCanonicalParseUpdated` |
| `v0.6.0-alpha.4` | Cursor crash after typing a short while | `onCanonicalParseUpdated` called `loadMarkdown` unconditionally on every debounced parseUpdated, tearing down TextControl state mid-typing | Gate rebuild on `consumeRebuildFlag()` or scene-out-of-sync |
| `v0.6.0-alpha.5` | Cursor crash (same symptom, different path) | `SceneCoordinator`'s internal 150ms reparse timer ran strip/refresh Inline Substitutions under `doc->blockSignals(true)`, desync'ing TextControl's internal tracking | Skip internal reparse when canonical-bound |
| `v0.6.0-alpha.6` | Same cursor-drift symptom | Root still un-localized; pre-emptive defense | Defensive clamp in `rectForPosition` + qCWarning on drift |
| `v0.6.0-alpha.7` | Crash before typing a single character | Race: `Vault::openDocument` schedules async parse; `Editor::setDocument` only built scene when `parsedDocument()` was non-null. Between setDocument and first parseUpdated (~150ms), no focus target → bubble-recursion | Build scene synchronously in `setDocument` from `doc->toMarkdown()`; don't wait for parseUpdated |
| `v0.6.0-alpha.8` | Bare Shift press mid-typing (thousands of recursive keyPressEvent lines) | **Architectural flaw in `Markoff::Editor`** (pre-existing, masked by pre-C3 tests). `setFocusProxy(m_view)` + manual `sendEvent(m_view, e)` in `keyPressEvent` contradict each other; Qt's natural unaccepted-event bubbling from m_view's QWidget parent (= Editor) completes the loop | **Bandage**: re-entrance guard in `Editor::keyPressEvent`. Real fix deferred to dedicated spec |

**Architectural followup spec:** [`docs/specs/2026-04-21-editor-key-dispatch-architecture.md`](specs/2026-04-21-editor-key-dispatch-architecture.md) — details the flaw, three fix options (A: drop manual sendEvent + rely on focus proxy; B: reparent m_view; C: drop focus proxy), recommends Option A, and enumerates the stakeholders (every key-driven Live feature: Tab, Ctrl+Home, PageUp/Down, character input, IME, selection, shortcuts, context menu, CJK autocorrect, read-only mode, event filtering).

**C7 is BLOCKED** on this spec's resolution. C7 (Source find/replace + fold-gutter) adds many new key bindings — each one would have to navigate the re-entrance guard, and every new "unaccepted bubbles back" edge case would be silently swallowed (quiet regression) rather than crashing (loud regression). Fixing the architecture before C7 means the test pass is smaller and the behavioral contract is coherent.

**Current soak state:** User dogfooding v0.6.0-alpha.8; further crashes funnel into new alpha tags. `v0.6.1` will NOT be tagged while the bandage is in place — promoting the bandage to a milestone release would cement the architectural debt.

**Not proposed for this PR or tagging:** immediate Option A implementation. The architectural fix touches every key-driven Live feature; it deserves spec + reviewer + test-pass scope, not a rushed soak-week commit.



### 2026-04-21 — C3 landing review received; 5 asks actioned; v0.6.0 re-tagged alpha.2 pending soak week

Markoff dev team posted a pre-C7 review at `/home/clinton/dev/Markoff/2026-04-21-c3-landing-review.md` (~150 lines). Summary: "the design direction is right; the build is not yet earned." Five concerns named:

1. **`m_sceneNeedsFullRebuildOnNextParse` is an escape hatch at the critical seam** — fires on multi-paragraph paste, multi-line delete across heading, edit touching image block. Each full rebuild costs focus loss, scroll jump, ephemeral-state reset. The original A-vs-B pushback explicitly called out this as "the hardest engineering problem in C3" warranting several iteration alphas.

2. **Two test quarantines without rewrite plan** — `1c33098` + `1205a38` during Task 5/7. On inspection Task 8 at `3682de0` rewrote + un-quarantined all three (`tst_markoff_document`, `tst_markoff_search_controller`, `tst_markoff_replace_controller` — all three registered + passing). Reviewer was reading chronologically.

3. **Anchor-math bug fixed 7min after interface** — right-bias-at-pure-insert (Task 1 code quality review caught it). Concern: the bug family — right-bias at SOT/EOT, left-bias under collapse-vs-span delete, bias under replace (one-delta vs two), macro-grouped edits, undo→redo stability, reset invariance — may have uncovered edge cases.

4. **Velocity vs. soak** — 2h37m spec-to-tag; alpha.1 existed for 34 minutes; zero dogfooding. Original expected cadence was "several iteration commits between alpha.1 and stable" per the Task 2 A-vs-B response; actual was "zero iteration commits."

5. **Two clones diverged** — 45 commits + 4 tags ahead of canonical `~/dev/Markoff/` (at `v0.3.0-alpha.2`). Library state depends on which checkout you read.

User approved all five asks + committed to personal dogfooding. Actions (all landed this session):

- **Ask 1 (push to canonical):** 4 tags pushed + canonical `master` fast-forwarded to `55f4807` via `git pull --ff-only` from the canonical side (direct push rejected because canonical has `master` checked out). Library state now consistent.

- **Ask 3a (un-quarantine verify):** verified all three previously-quarantined tests currently registered + passing — nothing to do, Task 8 already handled.

- **Ask 3b (anchor edge-case pass):** commit `a75424b`. 11 new test slots across `tst_canonical_buffer.cpp` + `tst_cursor_anchor.cpp` covering right/left bias at SOT + EOT, anchor at deleteStart (unaffected both biases), anchor at deleteEnd (non-straddle follow), replace with mismatched removed/inserted lengths, macro-grouped multi-delta edits, and undo→redo stability. **All pass, no bugs exposed** — the implementation holds up under expanded coverage.

- **Concern 1 soak instrumentation:** commit `c661bf1`. `Q_LOGGING_CATEGORY("markoff.live.scene.rebuild")` in `SceneCoordinator.cpp` logs both the single-item splice happy-path (offset/removed/inserted/itemIdx) and every `m_sceneNeedsFullRebuildOnNextParse = true` site (multi-item span, non-text item, out-of-range) with reason + context. Off by default; enable at runtime with `QT_LOGGING_RULES="markoff.live.scene.rebuild=true"`. Dogfooding can measure fallback-fire rate.

- **Ask 4 (re-tag):** `v0.6.0-alpha.2` created at commit `cf37d0e` (same SHA as `v0.6.0`; append-only — `v0.6.0` stays in place). Honesty-level annotation: the original `v0.6.0` landed too fast for the A-vs-B cadence the lock-in response promised.

- **Ask 2 (soak state annotation):** work-unit status table C3 row updated from `done` to `in soak`. Fix commits during the week tag as `v0.6.0-alpha.3/.4/...`; genuine stability after dogfooding re-tags as `v0.6.1`.

**Soak week starts now.** User dogfoods `v0.6.0` in CorbomiteApp with realistic docs + cross-mode editing + the scene-rebuild logging enabled. Any fallback-fire patterns that emerge drive either (a) extending the single-item splice to cover the case, or (b) accepting full-rebuild as the model and optimising it. **C7 does not start until the week closes.** Corbomite-side polish or C7 spec-drafting (non-library work) are valid uses of the interim.

How to apply for future multi-task plans with multi-repo reach: build an explicit dogfooding checkpoint between alpha.N and release tag; don't treat passing tests as the sole release gate.



### 2026-04-21 — C3 done (Corbomite-side adaptation shipped)

Tasks 20-23 Corbomite-side adaptation landed. Total C3 commit
count:
- Markoff side: ~19 commits on master across v0.6.0-alpha.1
  (Task 9) and v0.6.0 (Task 19) tag lines; core primitives +
  MarkoffDocument rewrite + three leaves bound + tri-view
  interop + cross-mode undo tests.
- Corbomite side: ~5 commits (20-23 adaptation + pin bumps).

Key Corbomite-side deliverables:

- Task 20 (NoteDocument wrapper): NoteDocument owns
  Markoff::MarkoffDocument via pimpl. markdown() / setMarkdown()
  delegate to toMarkdown() / resetContent(TestFixture).
  markoff() accessor exposes the MarkoffDocument for leaves to
  bind via setDocument(note->markoff()). contentsChanged /
  documentReloaded relay to textChanged + modified=true.

- Task 21 (Vault ParsePool + raw-byte saveDocument):
  Vault owns one Markoff::ParsePool for its lifetime.
  openDocument hydrates via resetContent(FirstOpen) + explicit
  setModified(false). saveDocument writes canonical bytes via
  raw QFile::write (no QTextDocumentWriter coercion). Byte-
  equality defense-in-depth in onExternalModified suppresses
  reloads when disk bytes equal canonical.

- Task 22 (External-reload Origin dispatch):
  onExternalModified dispatches to Origin::ExternalReloadClean
  (auto-apply, clear stack) or emits externalReloadConflict
  signal (dirty case). resolveExternalReload applies
  Origin::ExternalReloadResolved after UI merge-modal outcome.

- Task 23 (NoteEditorWidget flush/restore retired): four pre-C3
  call sites deleted. Mode swap: outgoing leaf
  setDocument(nullptr), stacked-page switch, incoming leaf
  setDocument(m_doc->markoff()) + ephemeralState restore.
  Canonical content never round-trips through leaves during
  swap. Replaced Corbomite::SourceEditor (Phase A adapter) with
  Markoff::Source::SourceEditor throughout the editor stack.

Test state (Corbomite build at Task 24 validation):
- Markoff-core tests: all C3 tests (tst_canonical_buffer,
  tst_cursor_position, tst_markdown_delta, tst_parse_pool,
  tst_origin_reset, tst_cursor_anchor, tst_markoff_document,
  tst_markoff_search_controller, tst_markoff_replace_controller)
  pass.
- Markoff-leaf tests: tst_source_canonical_attach,
  tst_reading_canonical_attach, tst_live_canonical_attach,
  tst_scene_offset_map all pass.
- Top-level markoff: tst_canonical_interop, tst_cross_mode_undo
  both pass. tst_tri_view_smoke + C6-era markoff editor tests
  continue green.
- Corbomite adaptation: tst_notedocument, tst_vault_save_reload,
  tst_note_editor_widget_ephemeral,
  tst_note_editor_widget_mode_transition all pass.
- Full Corbomite ctest: 260/264 pass. 4 pre-existing failures:
  tst_markoff_undo_grouping, tst_markoff_table_operations
  (documented pre-C3), tst_completion_popup (Wayland-teardown
  SEGFAULT, environmental), tst_benchmark_layout (documented
  timeout).

Follow-ups tracked (per spec §9):
- HoverPopover live-binding (post-C3 Corbomite follow-up).
- Sync-chattiness undo-clear mitigation (Phase-E motivator).
- libs/markoff-live/CLAUDE.md rename (cosmetic).
- Four MARKOFF_READING_USE_REAL_COREDEPS-gated-then-retired
  tests from C1b become revivable (C3 makes injection concrete).

C3 work-unit complete. Next work-unit in Markoff Phase C
sequence: C7 (Source feature completion — find/replace +
fold-gutter).

### 2026-04-21 — C3 landed at v0.6.0

Tasks 9-18 of the Phase C3 plan landed on `master`. Leaves
(Source, Reading, Live) now all subscribe canonical via
MarkdownView::setDocument. Local edits in any leaf route
through MarkdownDelta commands pushed onto MarkoffDocument's
shared QUndoStack. External deltas from other sources splice
into the affected leaf's private view-state via guard-protected
signal handlers.

Leaf adaptations:

- Source (Tasks 10-11): Qutepart inner doc subscribes to
  contentsChanged (splices) + documentReloaded (wholesale
  reload). QTextDocument::contentsChange → onLocalContentsChange
  → push MarkdownDelta. m_applyingCanonicalDelta guard
  symmetric across inbound/outbound.
- Reading (Task 12): subscribes to parseUpdated + documentReloaded
  (read-only). Section layout rebuilds on every parseUpdated.
  No outbound.
- Live (Tasks 13-16): Editor subscribes to contentsChanged +
  parseUpdated + documentReloaded. SceneCoordinator::m_itemMap
  carries per-item (canonicalStart, canonicalEnd) built from
  MarkdownSplitter segments with \\n-join fixup for contiguous
  coverage. Per-item QTextDocument::contentsChange translates
  to canonical offset via map[idx].canonicalStart + localPos
  and pushes MarkdownDelta. Inbound splice fast-path for
  single-item deltas; multi-item deltas flag
  m_sceneNeedsFullRebuildOnNextParse.

Top-level tests (tests/markoff/):
- tst_canonical_interop: all three leaves bound to one doc
  observe each other's edits; 100-edit burst collapses to
  ≤3 parseUpdated emissions.
- tst_cross_mode_undo: edit-in-Source → swap-to-Live →
  edit-in-Live → Ctrl+Z reverses LIFO regardless of active
  leaf; four edits + four undos return to original.

Corbomite build 100% green modulo documented pre-existing
flakes (tst_benchmark_layout timeout, tst_editorsuggest bounds,
tst_markoff_undo_grouping, tst_markoff_table_operations). All
C3-introduced tests pass.

Next: Corbomite-side adaptation (Tasks 20-25) — NoteDocument
wrapper rewrite, Vault ParsePool + byte-equality echo
suppression, external-reload Origin dispatch, NoteEditorWidget
flush/restore retirement, full ctest + manual smoke, close
the C3 work-unit.

### 2026-04-21 — C3 alpha.1: core primitives + MarkoffDocument rewrite

Tasks 1-8 of the Phase C3 plan landed on `master` across commits
`aaaf57d` → `3682de0`. Markoff-core now has:

- `CanonicalBuffer` pure-virtual interface + `InMemoryCanonicalBuffer`
  concrete (QString + anchor table with bias semantics).
- `CursorPosition` move-only RAII handle over anchor handles.
- `MarkdownDelta` QUndoCommand — single command type with
  mergeWith coalescing for adjacent pure-insert / pure-delete
  sequences.
- `ParsePool` + `ParsePoolWorker` — single-worker-thread async
  parse queue with per-sender generation counter; auto-constructed
  if caller passes `nullptr`.
- `MarkoffDocument` public API per spec §4.4: toMarkdown / length /
  substring / parsedDocument / parseIsPending / undoStack /
  resetContent(Origin) / trackCursor / resolveCursor + 3-arg
  contentsChanged + parseUpdated + documentReloaded signals. All
  5 Origin enum values implemented; UserRevertToSaved pushes a
  MarkdownDelta so Ctrl+Z reverses the revert.
- Pool debounce via per-doc QTimer; jobCompleted lambda routes
  parsed Document back to parseUpdated on the main thread with
  sender filtering.

Test coverage (9 test targets, all green):

- tst_canonical_buffer (11 slots — includes pure-insert-at-anchor
  bias behaviors and reset-clears-anchors assertion after a
  code-review-caught gap)
- tst_cursor_position (3 slots — move semantics)
- tst_markdown_delta (5 slots — redo/undo/merge/reject non-adjacent/
  reject cross-type)
- tst_parse_pool (3 slots — produces, coalesces burst, cancels on
  destroy)
- tst_origin_reset (5 slots — one per Origin enum value)
- tst_cursor_anchor (4 slots — preceding/following/left-straddle/
  right-straddle bias)
- tst_markoff_document (7 slots, all rewritten from Phase-A API)
- tst_markoff_search_controller (rewritten against resetContent)
- tst_markoff_replace_controller (rewritten against resetContent +
  MarkdownDelta undoStack)

Leaves (Markoff::Source / Markoff::Editor Live / Markoff::Reading)
still hold their Phase-A text independence; symmetric-B leaf
adaptation begins at Task 10 (Source), then Task 12 (Reading),
then Tasks 13-16 (Live). v0.6.0 proper lands at Task 19 after
the leaves bind + tri-view interop + cross-mode undo tests.

Next: Task 10 (Source::SourceEditor binds canonical via
setDocument).

### 2026-04-20 — C3 plan drafted

`docs/plans/2026-04-20-phase-c3-markoff-document-content-authoritative.md` — 3240-line 25-task plan. §0 orientation covers build commands, invariants, submodule pin protocol, commit conventions, SPDX header, and five troubleshooting entries. Tasks cleave into 12 phases:

- §A Tasks 1–5: markoff-core primitives (`CanonicalBuffer` + `InMemoryCanonicalBuffer`, `CursorPosition`, `MarkdownDelta`, `ParsePool` + `DefaultParsePool`, test quarantine of legacy `tst_markoff_document` slots).
- §B Tasks 6–8: `MarkoffDocument` rewrite (header replacement, reads + anchor-handle API, writes + `contentsChanged` emission + `resetContent(Origin)` branch behavior, `Origin` + anchor tests + tst_markoff_document rewrite).
- §C Task 9: interim tag `v0.6.0-alpha.1`.
- §D Tasks 10–11: `Source::SourceEditor` attach/detach + inbound splicing + outbound local-edit → `MarkdownDelta` + IME macro.
- §E Task 12: `Reading::ReadingView` attach/detach + `parseUpdated` + `documentReloaded`.
- §F Tasks 13–16: Live (`Markoff::Editor`) attach + `SceneCoordinator` per-block offset map + outbound local-edit translation + inbound splicing + multi-block-delta full-rebuild flag.
- §G Tasks 17–18: tri-view `tst_canonical_interop` + `tst_cross_mode_undo`.
- §H Task 19: tag `v0.6.0`.
- §I Task 20: Corbomite `NoteDocument` wrapper rewrite (delegates `markdown()/setMarkdown` to owned `MarkoffDocument`; new `markoff()` accessor).
- §J Tasks 21–22: Corbomite `Vault` ownership of `ParsePool` + raw-byte `saveDocument` + byte-equality echo suppression + external-reload `Origin` dispatch (clean vs. merge-modal resolved).
- §K Task 23: `NoteEditorWidget` four flush/restore call sites delete; mode-swap becomes `setDocument(nullptr)` / `setDocument(markoff)` + ephemeralState preservation.
- §L Tasks 24–25: submodule pin bump to `v0.6.0`, full Corbomite ctest, manual `./build/Corbomite` smoke per spec §7.2 #16, closeout updates to `phase-c-status` + Corbomite `PROJECT-STATE` + `decisions-archive`.

§M self-review maps every spec section to its covering task(s) — no gaps. Cross-task type-consistency cleared: `applyCanonicalDelta`/`canonicalSubstring`/`releaseAnchorHandle` (pkg-private helpers in Task 6 called from Tasks 3/6/2 respectively), `m_applyingCanonicalDelta` guard convention across all three leaves, `simulateBlockEdit` test helper defined in Task 15 and reused in 17/18. Task 9 interim tag `v0.6.0-alpha.1` is the first bisect-friendly checkpoint after markoff-core primitives land but before leaves are adapted — catches core-vs-leaf regressions cheaply. Per-leaf commits are test-first: every leaf task's Step 1 writes a failing test naming the slot(s), Step 2+ implements, Step N runs + commits.

Ritual 5 compliance: all Markoff-side commits use `<library>: <description>` subjects (no cluster footer). Corbomite-side adapter commits use `feat(markoff): Phase C3 adaptation — …` conventional shape. `Co-Authored-By: Claude Opus 4.7 (1M context) …` trailer both sides.

User pre-approved plan; next: Task 1 in a fresh execution session (or this one — work authorized to proceed autonomously unless unexpected problems emerge).

### 2026-04-20 — C3 spec drafted

`docs/specs/2026-04-20-phase-c3-markoff-document-content-authoritative.md` — 574-line spec for the largest Phase C work-unit. Delivers content-authoritative `MarkoffDocument` as the symmetric-B design: canonical = markdown bytes (`QString` behind `CanonicalBuffer` interface); one `QUndoStack` on `MarkoffDocument`; all three leaves subscribe to bytewise deltas (`contentsChanged`) + AST (`parseUpdated`) + wholesale-reload (`documentReloaded`); every edit routes through `MarkdownDelta` commands; native Qt per-leaf undo disabled everywhere.

Key calls recorded in §10 (Decisions recorded), eight total:

1. Wrapper (1:1), not pool — Vault's existing NoteDocument cache is the de-facto pool.
2. Symmetric-B undo — rejected A (Live scene-graph rewrite on single `QTextEdit`) as a separately-scoped future phase; Qt-cliff cost on A was prohibitive.
3. Shared single-worker `ParsePool` — Cluster I `MetadataWorker` precedent.
4. No internal `QTextDocument` on `MarkoffDocument` — footgun by type-leak; removal in C3 is the fix.
5. `Origin` enum on `resetContent` covers FirstOpen / ExternalReloadClean / ExternalReloadResolved / UserRevertToSaved / TestFixture with differing stack semantics.
6. `documentReloaded` signal distinct from `contentsChanged`.
7. Byte-equality defense-in-depth in echo suppression (enabled by raw-byte save path).
8. HoverPopover live-binding deferred as post-C3 Corbomite follow-up.

Phase-E hedge (§11): `CanonicalBuffer` interface + `CursorPosition` opaque handle ship in C3 at small real cost; keep a future CRDT swap (`~/dev/collabtext/`) as a clean internal refactor, not a re-architecture. Scouting doc landing on the Corbomite side at `docs/superpowers/plans/2026-04-20-phase-e-crdt-canonical-SCOUTING.md`.

Scope explicitly excludes: Live scene-graph rewrite (future phase), renderer unification (C4), Theme/ResourceProvider/LinkResolver consolidation (C2), find/replace API (C7), fold-gutter coordinator (C7), plugin-visible `MarkoffDocument`. 14 acceptance criteria (7 Markoff-side, 7 Corbomite-side) + 12-file signatures-at-a-glance manifest + 7-item breaking-changes list for the removed `textDocument()` / `setPlainText` / `plainText` / `replace` / `insert` / `remove` / `beginTransaction` / `endTransaction` / `parsed()` API.

**Not self-approved** — design had substantive pushback from the Markoff agent during brainstorm (correctly caught a `QTextCursor`-as-view framing error and a B-as-dual-stacks strawman in the A/B options writeup). Post-iteration the user explicitly pre-approved the final direction, so spec is approved on that basis. Next: plan file.

### 2026-04-20 — C6 corbomite-shipped; done

Corbomite submodule bumped to `v0.5.0-1-ga9cbc9a` (one commit past
the tag — the phase-c-status update). Corbomite adapter commit
`a893c88d`:

- `MainWindow::connectEditorContext` + `onEditorContextChanged`
  (public Q_SLOT for test dispatch) drives Format toolbar
  check-state, Heading radio, Table delete-row/col gating (refined
  via the new `EditorContext::table` row/col fields), and
  `toggle_fold` enable-state. `refreshEditorActions` remains as the
  initial-state primer for non-context-driven actions.
- `MainWindow::connectEditorContextMenu` + `onAboutToShowContextMenu`
  (public Q_SLOT) wire the new `aboutToShowContextMenu` signal to
  `MenuSectionHelper`-driven contributions: Format / Heading / Insert
  / Table entries land in the `"action"` section of the right-click
  editor menu via `addToSection(QAction *, QString)` + `finalize()`
  (plan had assumed `addItem` + `flush` — real API confirmed during
  adaptation).
- Latent UX fix surfaced during adapter wiring: `format_bold`,
  `format_italic`, `format_strikethrough`, `format_inline_code`
  actions were not previously `setCheckable(true)`; `QAction::setChecked`
  was silently no-oping on them. Adding `setCheckable(true)` in
  `MainWindow::setupActions` is why the Format toolbar now shows
  correct visual check state.
- `tst_mainwindow_action_wiring` extended with a slot-dispatch test
  that calls `onEditorContextChanged` with a synthetic
  `EditorContext` and asserts the `format_bold` action's
  `isChecked()` flips.

A stale-vault artifact briefly looked like a `tst_e2e_gui` regression
during execution (global-search returned 0 hits + SEGV in
teardown) — verified on the pre-C6 parent commit with a clean vault
state: both issues were pre-existing state-dependent flakes, not
caused by C6. Full Corbomite ctest green modulo the two documented
flakes (`tst_benchmark_layout` timeout, `tst_editorsuggest` bounds
bug). Cluster V.2 remains open.

Next: C3 (`MarkoffDocument` content-authoritative).

### 2026-04-20 — C6 landed at `v0.5.0`

6 commits on `master` (`19f41c7` → `9f500e1` + this test-extension
commit). Deliverables:

- `Markoff::EditorContext` struct shipped as public API surface in
  `<markoff/EditorContext.h>` — full shape (BlockKind/ListMarker/
  TaskState enums + TableContext/LinkContext/TagContext/FootnoteContext
  nested structs) stable; initial classifier populates the Option-C
  field set (blockKind, headingLevel, table, inBold/Italic/
  Strikethrough/InlineCode, hasSelection, atBlockStart, atBlockEnd,
  readOnly).
- `Markoff::Editor::context() const` pull accessor.
- `Markoff::Editor::contextChanged(const EditorContext &)` signal
  with 16ms QTimer::singleShot debounce; kicks on per-item TextControl
  cursorPositionChanged + selectionChanged + QTextDocument
  contentsChanged + SceneCoordinator::reparsed + setReadOnly.
- `Markoff::Editor::aboutToShowContextMenu(QMenu *, const EditorContext &, const QPoint &)`
  signal fired in both branches of contextMenuEvent (table + general)
  after built-ins, before menu.exec(). Read-only editors emit nothing
  (early-return preserved).
- Internal classifier at `src/EditorContextClassifier.{h,cpp}` in
  `Markoff::Internal::` namespace — not public API.
- `kInlineCodeProperty = QTextFormat::UserProperty + 100` on the
  highlighter's inline-code runs; classifier reads via
  `QTextCharFormat::boolProperty`. Bold/italic/strike read standard
  Qt flags (fontWeight/fontItalic/fontStrikeOut).

~15 `EditorContext` fields (callout/blockquote/code-block sub-context,
link/tag/footnote context, math/highlight inline, task-state detail,
list-marker detail, column-alignment) stay default-valued; struct
shape stable so consumer code is forward-compatible. Fill in future
C-phases when a consumer materializes.

Test coverage: 17 classifier unit tests + 4 contextChanged tests + 3
context-menu signal tests + 3 end-to-end snapshot tests = 27 new
test slots. Full markoff ctest green.

Next: Corbomite submodule bump to `v0.5.0` + `MainWindow` consumer
wiring (onEditorContextChanged + onAboutToShowContextMenu via
MenuSectionHelper) + Cluster V debt cleanup.

### 2026-04-20 — C6 spec drafted

`docs/specs/2026-04-20-phase-c6-editor-state-context-menu.md` — a
150-line wrapper over the recovered 640-line consumer-spec (authored
by the Corbomite app team, stranded on submodule master before Phase C
ownership transferred). Wrapper narrows scope via brainstorming to
Option C (stub + defer unused fields): ships the full `EditorContext`
struct shape stable, but initial classifier only populates the fields
Corbomite's `MainWindow::refreshEditorActions` needs today
(`blockKind`, `headingLevel`, `table`, `inBold/Italic/Strike/InlineCode`,
`hasSelection`, `atBlockStart`, `atBlockEnd`, `readOnly`). Unpopulated
fields (~15 — callout/blockquote/code-block sub-context, link/tag/
footnote context, math/highlight inline, task-state detail, list-marker
detail) stay default-zero; future C-phases fill them as consumers
materialize. Ships the §9 `aboutToShowContextMenu` contribution signal
alongside (after-built-ins shape, not section-tags-before-built-ins —
keeps Markoff out of section-ordering business). Signal fires in both
branches of `contextMenuEvent` (table + general). Eight decisions
recorded. Self-approved per the recipe's step-3 clause (all design
calls unambiguous; consumer-spec pre-specifies the struct shape and API
contract).

Next: C6 plan, then staged implementation (commits A-F), tag `v0.5.0`,
Corbomite adapter beat.

### 2026-04-20 — C5 landed at `v0.4.0`

5 commits on `master` (b8d24ec → b2514ac). Deliverables:

- `ReadingView::wikiLinkHovered(QString)` replaced by unified
  `linkHovered(QString href, QPoint globalPos)`. Breaking to
  Corbomite's HoverPopover consumer — rewires in the submodule-bump
  commit.
- `Markoff::Editor::linkHovered(QString)` widened to the same
  two-arg shape. `TextControl::linkHovered` deliberately unchanged
  (internal leaf signal, not part of the tri-view contract).
- New `tst_readingview_click_to_fold` regression guard exercising
  the full click-dispatch path (eventFilter → sectionIndexAt →
  toggleFold) via `QTest::mouseClick` against the graphicsView's
  viewport at the fold-arrow's scene-mapped center.
- New `tst_readingview_hover_signal` locks the two-arg signal shape
  + empty-href leave semantics.

Scope narrowed during execution: **LinkRenderer forwarding for
regular-URL hover** (original spec §2.1 widen) dropped after T3
implementer discovered `Markoff::Reading::LinkRenderer` is orphaned
(constructed nowhere in production). The real gap is a URL-span
hit-test equivalent to `wikiLinkTargetAt` or LinkRenderer
integration into the section pipeline; both belong under C3/C4, not
a signal-unification work-unit. Captured in commit `4b95f3d`. Reading
mode still has no regular-URL hover — same coverage as before C5 —
but Reading mode also has no hover popover wired at all today, so
no user-visible regression.

`codeBlockProcessorRegistry` routing remains deferred per the
earlier C5 spec revision (`d445345`).

Standalone `ctest` green: 64/64 (`markoff|reading` suite in the
Corbomite-tree build).

Next: Corbomite submodule bump to `v0.4.0` + HoverPopover rewire
(consuming the two-arg signal; also wiring ReadingView::linkHovered
→ HoverPopover for the first time — Reading-mode hover popover is
new behavior) + Cluster V Phase 4 closeout.

### 2026-04-20 — C5 spec drafted

`docs/specs/2026-04-20-phase-c5-reading-interaction-parity.md` landed.
Post-C1 code inspection narrowed the four prescribed items to two
real Markoff-side features (unified `linkHovered(href, globalPos)`
replacing `wikiLinkHovered` + widening `Markoff::Editor::linkHovered`
to match; `SectionLayout` dispatch through
`codeBlockProcessorRegistry` — today the registry is populated but
never consulted during layout) plus two already-landed items
(click-to-fold via fold-arrow hit-test; `zoomIn/Out/resetZoom`
virtuals on `ReadingView` with `zoomChanged` emission). Zoom-policy
harmonization across leaves and click-on-heading-text folding are
explicitly deferred. Corbomite-side adaptation beat absorbs Cluster V
Phase 4: HoverPopover rewire (now hovers regular URLs too, not just
wiki-links), `View::zoomIn/Out/Reset` default-no-op virtuals +
`MarkdownView` delegation, and un-gating the four
`MARKOFF_READING_USE_REAL_COREDEPS`-gated tests that now exercise real
processor dispatch. Seven decisions recorded in §5; self-approved per
the recipe's step-3 clause (unambiguous design; signatures follow
Markoff Live precedent).

Next: plan file, then C5a implementation (signal rename/widen + tests,
tag `v0.4.0-alpha.1`), then C5b (SectionLayout routing, tag
`v0.4.0`), then Corbomite adaptation.

### 2026-04-20 — C1b landed at `v0.3.0`; Phase B bridge retired

Stubs tree (`libs/markoff-reading/stubs/corbomite/` — 9 header files
plus `mmdr_ffi.h`) deleted. `MARKOFF_READING_USE_REAL_COREDEPS` option
+ its conditional block removed from `libs/markoff-reading/CMakeLists.txt`.
Standalone Markoff builds in a fresh checkout with 76/76 ctest green
and zero external-dep links. CorbomiteApp builds via
`libs/core/include/corbomite/markoff_adapters/` (composition-based
adapters — see Corbomite `59ecd5cb`).

Between C1a (alpha.1) and C1b (v0.3.0), an intermediate
`v0.3.0-alpha.2` gated the 4 Phase-B-style tests
(tst_sectionlayout_mermaid, tst_readingview_embedrenderer,
tst_readingview_mermaid_registered, tst_readingview_embed_builtins)
behind `if(FALSE AND MARKOFF_READING_USE_REAL_COREDEPS)`. They
compile against the new Markoff types but assert real mmdr + real
MetadataParser output at runtime; un-gate pending a Corbomite-side
test harness that injects the adapter concretes
(`Corbomite::Core::MermaidRenderer`,
`Corbomite::MarkoffAdapters::MetadataParserImpl`). Tracked as a
Phase C1 follow-up.

The CLAUDE.md docs at the Markoff repo root still describe the
Phase B option in a few places; left for a doc-only follow-up touch
to avoid mixing that with the API retire.

C1 work-unit closed. Next: C5 spec (Reading-mode interaction parity).

### 2026-04-20 — C1a landed at `v0.3.0-alpha.1`

Sub-phase C1a shipped in 5 commits on `master` (`fe655b0` → `2956ee7`)
and tagged `v0.3.0-alpha.1`. Concrete deliverables:

- `markoff-core`: 10 new public headers (5 primitives
  `EmbedRegistry` / `CodeBlockProcessorRegistry` / `MarkdownRenderChild` /
  `EmbedDepthGuard` / `MermaidRenderer` + 5 vault abstracts under
  `Markoff::Vault::` namespace), 5 Default* no-op concretes, and 4 new
  unit tests. `MarkdownRenderChild.cpp` is the sole new .cpp.
- `markoff-reading`: every `<corbomite/...>` include dropped from src/
  and include/; types rename to `Markoff::*` / `Markoff::Vault::*`.
  `ReadingView` gains 5 new setters (`setEmbedRegistry`,
  `setVaultLinkResolver`, `setVaultMetadataCache`,
  `setVaultMetadataParser`, `setMermaidRenderer`) plus lazy-default
  fallbacks via a fwd-declared `LazyDefaults` struct. `EmbedRenderer`
  gains `setMetadataParser`. `SectionLayout::Context` gains
  `mermaidRenderer *`; the two Mermaid callers route through it. Old
  internal `src/MermaidRenderer.{h,cpp}` deleted (mmdr-coupled path is
  now host-side only, via the `Markoff::MermaidRenderer` abstract).
- Tests: 9 test files retyped via mechanical sed (Corbomite::Core::X →
  Markoff::X; Corbomite::Storage::X → Markoff::Vault::X). Includes
  switched to angle-bracket `<markoff/...>` form.

The Phase B CMake option `MARKOFF_READING_USE_REAL_COREDEPS` stays in
place but is now a no-op for link shape (markoff-reading no longer
includes any corbomite/ headers from its sources). The stubs tree
under `libs/markoff-reading/stubs/corbomite/` is unused by
markoff-reading but still compiles; C1b (Tasks 17-21) deletes it.

Task 8 (un-gate Phase-B tests) is a conscious no-op in C1a: the 4
gated tests all assert on real SVG output or real metadata slicing,
which `Default*` impls cannot provide. Un-gating blocked on Task 13
writing Corbomite-side adapter tests, or on the host injecting real
parser/renderer concretes.

Standalone `ctest` green: 76/76. Tag append-only.

Next: Corbomite-side adapter commit (Tasks 10-16) bumps submodule pin
to `v0.3.0-alpha.1` and retypes Corbomite's Core/Storage primitives to
inherit the new Markoff interfaces.

### 2026-04-20 — C1 plan drafted

`docs/plans/2026-04-20-phase-c1-di-seam.md` — 26-task self-contained
execution plan written for a fresh-context agent. Covers C1a
(Tasks 1–9, Markoff side, introduce types + default impls +
retargeted markoff-reading, tag `v0.3.0-alpha.1`), Corbomite adapter
commit (Tasks 10–16, inheritance-based adapters + MainWindow wiring),
C1b (Tasks 17–22, delete stubs + retire CMake option, tag `v0.3.0`),
and Corbomite cleanup (Tasks 23–26). Includes orientation §0 with
build commands, invariants, submodule pin-bump protocol, header
template, and commit-message convention. Troubleshooting appendix
covers the four known thorns (registerExtension name collision,
MetadataParser static-vs-instance, mermaid test assertions,
MarkdownRenderChild MI).

Timeline estimate: 2.5 days across both repos.

Ready to execute. Next: Task 1 in a fresh-context session.

### 2026-04-20 — C1 spec drafted

`docs/specs/2026-04-20-phase-c1-di-seam.md` landed. Lays out the two-
sub-phase migration (`v0.3.0-alpha.1` introduces new Markoff types
alongside Phase B's CMake option; `v0.3.0` retires the option + stubs
after Corbomite ships the adapter layer). Interfaces split across
`Markoff::` (rendering primitives: EmbedRegistry, CodeBlockProcessor-
Registry, MarkdownRenderChild, EmbedDepthGuard, MermaidRenderer) and
`Markoff::Vault::` (vault-level abstractions: ResourceProvider,
LinkResolver, MetadataCache, MetadataParser). Setter-style injection
on `ReadingView` with lazy-default fallbacks in `markoff-core`. Ten
decisions recorded in §9. Self-approved per the recipe's step-3
clause for unambiguous design calls — signatures are 1:1 with existing
Corbomite::Core types.

Next action: C1a implementation on Markoff master, then Corbomite
adapter, then C1b cleanup. Plan file next.

### 2026-04-20 — scope expanded: C5, C6, C7 added by Corbomite agent

Corbomite agent took the Phase C baton per the handoff. Scope expanded
from four work-units to seven by folding in:

- **Cluster V Phase 4** (Corbomite's deferred UI cluster — ReadingView
  interactions that need Markoff-side signal and virtual additions).
  Now C5.
- **The consumer editor-state surface spec** (443 + 203 lines of
  Corbomite-prescribed requirements that had been stranded on Corbomite's
  submodule ahead-master; recovered in Markoff commits `8d92247` and
  `245bd56`; tagged `v0.2.8`). Now C6.
- **Qutepart-fork Phase 3** (public find/replace API — was tracked on
  Corbomite's "Parallel long-term internal refactors" table under the
  Qutepart-Corbomite fork banner; now absorbed into Markoff since
  `Markoff::Source` owns the widget). Now C7.
- **Cluster V.2 fold-gutter coordinator** — also absorbed into C7
  after the V.2 scouting doc flagged the Qutepart-fork-Phase-6 reference
  as a misread.

Ordering revised: `C1 → C5 → C6 → C3 → C7 → C2 → C4`. C5 and C6 are
small user-visible unlocks that complete Cluster V and can ship as
back-to-back v0.4.0 / v0.5.0 releases after C1 lands. C7 gets slotted
before C2 so find/replace and fold-gutter don't suffer Theme-refactor
churn. C3/C2/C4 retain their original C1→C3→C2→C4 sequencing.

Markoff `master` at `v0.2.8`. C1 is the next work-unit to spec.

### 2026-04-20 — handoff

Phase C ownership transferred to the Corbomite agent. Status board
created. Scope, ordering, and invariants recorded. C1 is the next
work-unit to spec.

Markoff state at handoff: `v0.2.0` on `master` at `476e389`. Feature
branch `feature/tri-view-phase-a` preserved at the same SHA for
reference. Worktree at `.worktrees/tri-view-phase-a/` preserved.

Phase A docs (both sides) at `docs/specs/2026-04-20-tri-view-*.md` and
`docs/plans/2026-04-20-tri-view-phase-a.md`.

Phase B docs (both sides) at `docs/specs/2026-04-20-phase-b-*.md` and
`docs/plans/2026-04-20-phase-b-*.md`.

Handoff doc at `docs/handoff/2026-04-20-phase-c-ownership-handoff.md`.
