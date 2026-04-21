# Session brief — `Markoff::Editor` key-dispatch architectural fix

> **You are a fresh-context agent picking up a BLOCKING issue.** The v0.6.0 soak week surfaced an architectural flaw in `Markoff::Editor`'s key-dispatch that papered over with two bandages (alpha.6 `rectForPosition` clamp, alpha.8 `m_inKeyPressEvent` re-entrance guard). Both must come off as part of the real fix. `v0.6.1` does not tag until they do. Phase C7 is BLOCKED until this is closed.

> **The previous session authorising this work said, verbatim:** "I really want to get this architectural flaw fixed. Make sure that when I start a new session, the fresh context is prepared to go deep on analysis and find solutions which please *all* stakeholders in corbomite and markoff."

> **Go deep. Analyse thoroughly. Do not rush to implementation.** The architectural choice has cross-repo stakeholder reach; the three options listed in the main spec are an analysis, not a decision. Your job is to make a defended decision.

---

## Read in this order, then stop and think

1. **This brief** (you are here).
2. **[The architectural spec](2026-04-21-editor-key-dispatch-architecture.md)** — the flaw, the loop, the three options (A/B/C), the alpha.8 bandage costs, the D1/D2/D3 dogfood-surfaced test-pass criteria.
3. **[Phase C status board](../phase-c-status.md)** — the alpha.3 → alpha.8 activity-log entry for context on the six-crash soak-week arc that surfaced this. The crash table shows the progression from "symptom C3-integration bugs" to "pre-existing architectural flaw" that the bandages revealed.
4. **Corbomite-side `docs/PROJECT-STATE.md`** — the Current focus paragraph summarises the Corbomite-facing state.

After reading the four, do NOT touch code. Do NOT propose a fix. Move to the investigation section.

---

## Stakeholders — each bullet is a potential veto

### Markoff library stakeholders

| Concern | Constraint |
|---|---|
| **Standalone build green** | `cd /home/clinton/dev/Corbomite/libs/markoff-family && cmake -S . -B build-dev && cmake --build build-dev -j && cd build-dev && ctest` must pass with zero host-project deps. This is Markoff invariant #1 per the handoff doc. |
| **Public API stability** | Any public-API break (anything in `include/markoff/*.h`) requires a version-bump call and a migration note. `Editor::keyPressEvent` is not a public API but `Editor::event` is virtual and subclassable. |
| **markoff-testapp** | `./build-dev/bin/markoff-testapp` must behave identically post-fix — same keys, same shortcuts, same autocorrect, same selection, same IME. |
| **No regression to scene-graph features** | Per-block `TextControl`, `MarkdownTextItem`, `SelectionScene`, `SelectionManager`, `FoldGutter`, `FoldingModel`, `SearchBar` — every key-driven path through them must continue to work. |
| **SceneCoordinator reparse path** | The `m_reparseTimer` / `reparse()` mechanism was gated in canonical mode (alpha.5). Your fix should not re-enable it accidentally. Phase A non-canonical usage still needs it. |
| **CJK autocorrect** | `MarkdownTextItem::applyCjkBracketAutocorrect` uses local `QTextCursor` ops after `m_control->processEvent`. Is prime suspect for D2 cursor drift. May need to be rewritten as part of the fix or flagged as orthogonal follow-up — decide explicitly. |
| **Tab smart-indent dual-path** | `Editor::event` catches Tab/Backtab at line ~427 BEFORE dispatching to keyPressEvent. `Editor::keyPressEvent` also has `handleTabKey` at line ~808. This is two handlers for the same key. Understand the routing before refactoring it. |
| **Phase-E hedge preservation** | `CanonicalBuffer` and `CursorPosition` (from C3 spec §11) must remain intact. Anchor-handle semantics across multiple cursors still matters. |

### Corbomite integration stakeholders

| Concern | Constraint |
|---|---|
| **`NoteEditorWidget` mode-swap** | The QStackedWidget-based Source/Live/Reading swap calls `Editor::setDocument(doc)` + `Editor::setDocument(nullptr)` across swaps. Must keep working per C3 Task 23. |
| **`MainWindow` + `KActionCollection`** | Cluster V Phase 2+3 wired ~40 KDE actions to Markoff::Editor (Ctrl+Z, Ctrl+F, Format toolbar, Heading radio, Edit/View/Format/Insert/Table menu entries, Ctrl+E toggle mode). Every one is a potential veto. |
| **KF6 `xmlgui`** | `corbomiteui.rc.in` v10 lives alongside; shortcuts are registered through there. The `kf.xmlgui: Index 18 is not within range` noise in the dogfood log is ORTHOGONAL to this fix (pre-existing KF6 issue) but note it so you don't chase it. |
| **Cluster R "Format" toolbar** | The Format/Heading/Insert/Table toolbar actions fire via C6 `EditorContext::contextChanged`. Key events that mutate the cursor must still trigger context refresh. |
| **AutosaveReactor** | Fires on `NoteDocument::modificationChanged(true)`. If your fix changes when edits actually reach canonical, autosave timing changes. Verify dogfood behaviour — the 500ms autosave debounce must still start after the first edit lands. |
| **Plugin surface (`VaultProxy`, `FileManagerProxy`)** | These don't observe key events directly but do observe `NoteDocument::textChanged`. Indirect; but trace through to confirm nothing subscribes to anything keyboard-shaped on the Editor. |
| **`HoverPopover`** | Currently snapshots content on wiki-link hover. Post-C3 follow-up for live binding still pending (Corbomite-side, not Markoff-side). Your fix should not block that follow-up. |
| **Full Corbomite ctest green** | 260/264 baseline. 4 pre-existing flakes (`tst_markoff_undo_grouping`, `tst_markoff_table_operations`, `tst_completion_popup`, `tst_benchmark_layout`). No NEW failures allowed. |

### Cross-cutting / external stakeholders

| Concern | Constraint |
|---|---|
| **Qt6 event-system contracts** | Whatever you change must play by Qt's rules for focus propagation, event acceptance, and shortcut override. Reading `QWidget::event`, `QApplication::notify`, `QShortcutMap` source is non-optional. |
| **Wayland / X11 platform quirks** | The initial soak crashes surfaced in `QXkbCommon::possibleKeys` on Wayland. Your fix should work on both (test on whatever `xdg_session_type` your env uses). |
| **Future Phase E (CRDT)** | `CanonicalBuffer` is the swap-point. Your fix should not introduce anything that couples to `InMemoryCanonicalBuffer` specifically. |
| **Non-Corbomite third-party consumers** | None known today, but `markoff-live` is a library consumable by anyone. If your fix changes the focus-proxy contract, third-party hosts embedding `Markoff::Editor` see the same change. Document in the v0.6.1 release-notes entry on phase-c-status. |

---

## Required investigation (before proposing a fix)

1. **Read `libs/markoff-live/src/Editor.cpp` end to end.** Not just the key-dispatch methods — every `connect`, every event filter, every `setFocus` call. There are ~20 `setFocus()` call sites in Editor.cpp; understand when each fires and whether it's relevant.
2. **`git blame` on `libs/markoff-live/src/Editor.cpp` lines 85–92** (focus-proxy + event-filter setup). When was this pattern introduced? Was it discussed? Any commit message that explains intent?
3. **Check `markoff-testapp` behaviour.** Build and run `./build-dev/bin/markoff-testapp path/to/some.md`. Does bare-Shift press crash it? Does typing produce cursor-drift warnings (D2) with `QT_LOGGING_RULES="markoff.live.*=true"`? If markoff-testapp exhibits the same bugs, the fix is pre-C3 scope and shouldn't depend on canonical-mode state.
4. **Reference implementations — read these and note design differences:**
   - `QTextEdit` / `QPlainTextEdit` (Qt's own) — how do they handle key dispatch without the bubble-back issue? They don't use focus proxy; keys come to them directly.
   - `KTextEditor` (KDE Frameworks) — how does the Kate `View` handle key routing through its embedded view?
   - `QScintilla` if installed (may not be; optional).
5. **Read the two bandage commits in full**:
   - alpha.6: `TextControl::rectForPosition` clamp + `markoff.live.text_control.cursor_drift` category.
   - alpha.8: `Editor::m_inKeyPressEvent` guard.
6. **Read Cluster V Phase 2+3 plan** at `/home/clinton/dev/Corbomite/docs/superpowers/plans/2026-04-20-cluster-v-editor-workspace-ui-surfacing.md`. Editor actions and shortcuts were wired up there. Any concerns from that era affect Corbomite-side stakeholders.
7. **Survey every `connect` to a `QTextDocument::contentsChange` signal in markoff-live/**. There are at least three: the adjustSpanOffsets connection, the Task 15 outbound-delta lambda, and the per-item TextControl internal `_q_contentsChanged` slot. These all fire on every keystroke. Understand interaction order.
8. **Check the Markoff family commit log since `v0.5.0`** for any commit that touched key dispatch, focus, or event filtering. The soak-week ones are tagged alpha.3–alpha.8; anything else in between?

---

## Think out loud before acting

Run `superpowers:brainstorming` after the investigation. The brainstorm's output should be a spec.

Topics the brainstorm MUST cover:

- Each of the three options (A/B/C) from the main spec — pro, con, impact on each stakeholder category above.
- Hybrid options or options not yet enumerated. Example: "split Editor into a host-facing wrapper + an internal widget where the wrapper doesn't set focus proxy." Evaluate.
- Whether the CJK autocorrect local-cursor issue (D2 prime suspect) is in scope or a separate follow-up.
- Whether the D3 font warning is in scope or a separate follow-up.
- Test-strategy for the full test-pass matrix in the main spec.
- Migration path for markoff-testapp and any hypothetical third-party consumer.
- Whether `v0.6.0-alpha.9/.10/...` makes sense before `v0.6.1` (probably yes — one alpha for the refactor with bandages ON, then one for bandages OFF + D1/D2/D3 verified).

Think hard about second-order effects. The spec's "stakeholder breadth" paragraph lists ~12 key-driven features in one class; multiply by the cross-repo stakeholders and you have something like 30+ behaviours that must continue working exactly as they do today.

---

## Deliverables

1. **A spec** (new file, not overwriting the existing architectural spec). Names the chosen solution. Defends it against every stakeholder concern above. Annotates which bandages go away and when.
2. **A plan** (new file). Phase-by-phase: spec → code → bandage removal → test pass → release tag. Each phase has commit-level tasks.
3. **The implementation.** TDD where possible; for UI-level behaviours, manual test matrix with explicit sign-off.
4. **Test additions.** `tst_markoff_live_key_dispatch` or similar. At minimum covers D1 (focus survives paragraph boundary), D2 (no drift warnings), D3 (no font warnings on open), and every behaviour in the test-pass summary table.
5. **phase-c-status.md activity log entry** closing out the architectural debt. Note the bandage commits that were reverted, the test-pass results, and the v0.6.1 tag.
6. **`v0.6.1` tag** once D1/D2/D3 and the full summary table all clear with both bandages REMOVED.

---

## Hard warnings

- **Option A is the recommended choice in the main spec. It is NOT the decided choice.** Treat the spec's recommendation as input, not output.
- **Both bandages come off as part of this fix.** Not "we'll remove them later." Both OFF before the test pass runs, or the test pass is testing the bandages not the fix.
- **Do NOT tag `v0.6.1` with a bandage in place.** Promoting a bandage to a milestone cements architectural debt. Previous session was explicit about this.
- **Do NOT skip brainstorming.** The skill is mandatory for work of this scope per superpowers convention.
- **Do NOT treat Corbomite ctest green as sufficient acceptance.** Several of the soak-week bugs never showed up in the test suite — the tests `QSignalSpy::wait` on parseUpdated before exercising keys, which masks the timing-dependent paths. Manual dogfood of the full test-pass matrix is REQUIRED.
- **Do NOT assume the fix is "small."** It touches every key-driven Live feature. Cluster V + Cluster H + Cluster R all have overlapping stake. Re-reading their plans is part of the investigation.
- **If you get stuck, surface it.** The previous-session agent was told: "If unexpected problems emerge requiring my input, stop and escalate." Same rule applies. Better to ask than to ship more bandages.

---

## Session-start ritual

Per Corbomite CLAUDE.md section "Session-start ritual (TL;DR — full version in CONTRIBUTING-OPS.md)":

1. Read `docs/PROJECT-STATE.md` top-to-bottom. (Already done above as step 4.)
2. Since you're continuing specific in-flight work (not picking up new), skip the backlog skim.
3. Read this brief + the architectural spec + the phase-c-status activity log.
4. Glance at `git log --oneline -15` on both repos for most-recent state.
5. State the situation back: "Per the session brief, current focus is the Editor key-dispatch architectural fix; six alpha tags landed, bandages in place, v0.6.1 blocked on Option-A-or-variant decision. Next step is investigation + brainstorming before any implementation. Confirm or redirect?" Wait for confirmation before starting investigation.

Cross-repo working convention (Ritual 5) applies: commits on both Markoff master and Corbomite master; no feature branches; submodule pin bumps via ff-only; v-tags append-only.

---

## Read this paragraph before closing the brief

The previous-session agent shipped six alpha bandages in one afternoon. Each was a reasonable response to the crash in front of it. Each taught something about the actual bug. None of them — individually or collectively — is the fix. The user's explicit ask ("go deep on analysis and find solutions which please *all* stakeholders") is a request to stop the bandage pattern and do the architectural work properly. If you find yourself reaching for a quick fix, re-read this paragraph and stop.

The code has been broken in this way since Phase A. Another week won't hurt.
