# Tier 2 — Cursor typing-authority + invariants

**Date:** 2026-05-15
**Branch:** `exploration/new-foundation`
**Predecessor:** `docs/specs/2026-05-11-focus-chokepoint-design.md` (tier 1, complete).
**Plan to follow:** `docs/plans/2026-05-15-tier-2-cursor-typing-authority.md` (to be written).

## 0. One-paragraph summary

Tier 2 of the cursor architecture cleanup. Closes queue #2 concerns **#1** (docstring honesty), **#2** (`TextCaret` field rename), and **#6** (per-keystroke sync invariant test). Concern **#9** (write-time variant validation) is **deferred** — `LiveCursorState::request()` and `syncFromTextEdit` already validate on write; the remaining bypass in `tryResolvePending` cites *"valid transient states during a structural cascade"* whose specification is missing from the paper trail. We do not touch it without understanding it. Tier 2 is pure documentation + a rename + new test coverage; **no production-behavior changes**, by design. The widget is working well at the start of this spec, and the spec is engineered to preserve that.

## 1. Motivation

Tier 1 (`docs/specs/2026-05-11-focus-chokepoint-design.md` — focus chokepoint), the follow-on block-only kinds work (`docs/specs/2026-05-13-block-only-kinds-design.md`), and the D-fc-5 fix (commit `249e7ef`) settled the *structural-event* side of the cursor seam. Click, arrow nav across blocks, kind transitions, BlockSelected for HR/Image, delegate-swap registration ordering — all dogfooded and stable.

The *typing* side is currently coherent in code (`syncFromTextEdit` was added during the S1/S2/S3 fix in commit `6c44a07`, `request()` already validates variants on write) but documented as if it were pre-tier-1. The docstring's "single canonical cursor value" claim invites future authors to reason about the seam against a model that no longer matches reality, and there is no invariant test pinning the keystroke-by-keystroke synchronisation that the working code already achieves.

Tier 2 closes that documentation/test gap. It does not add new authority and does not touch the call paths that are currently working.

## 2. Scope and explicit non-goals

### 2.1 In scope

- **#1 (full).** Replace the "single canonical" framing in `LiveCursorState`'s class header docblock with an honest dual-authority statement (TextEdit canonical for in-block typing; `m_cursor` canonical for structural events).
- **#2.** Rename `TextCaret::cachedByteOffset` (in `libs/markoff-live/include/markoff/live/Cursor.h`) to `cachedQtPos`. Update all 16 references (10 in `LiveCursorState.cpp`, 2 in `LiveListModelBinding.cpp`, 6 in `tst_live_render_cursor.cpp`). Field comment rewritten to name UTF-16 code units explicitly and point at `Coordinates::qtPosToByte` for the byte-conversion entry point.
- **#6 (full).** New test binary `tst_live_render_cursor_typing_invariant` on `LiveRealisticInputHarness` (per invariant 5), five slots covering ASCII / CJK / emoji typing, in-block arrow nav, and kind transition. Each slot asserts `m_cursor.focusedQtPos == focusedTextEdit.cursorPosition` after every keystroke.

### 2.2 Explicit non-goals (deferred)

- **#9 (write-time variant validation) — deferred to tier 3 or its own tier.** `request()` (line 68) and `syncFromTextEdit` (line 150) already validate on write and emit `qCWarning` on rejection (no longer silent). The remaining bypass in `tryResolvePending` (`LiveCursorState.cpp:459-487`) skips validation citing two reasons in its comment: (a) null-registry segfault in unit tests — fixable, (b) *"reject some valid transient states during a structural cascade"* — undocumented. Investigating those transient states is a half-day audit not in tier 2's budget. **Discipline-log entry to be filed naming this as the open thread.**
- **#3 / #4 / #5 / #12** — tier 3 (API consolidation), unchanged from tier-1 spec §10.
- **#10** — tier 4 (selection/cursor unification), unchanged from tier-1 spec §10.

## 3. The L4 / ownership decision (per invariant 2)

**Decision: dual-store with delegate as in-block authority, mirror reconciled by `syncFromTextEdit`.**

- For *in-block caret position during typing*, `QQuickTextEdit::cursorPosition` is canonical. Native handling of typing, IME composition, dead keys, and within-block arrow keys lives in TextEdit and is correct. The chokepoint does not interpose on every keystroke.
- For *structural events* (kind transitions, cross-block nav, BlockSelected, BlockInternalEdit), `m_cursor` is canonical. The chokepoint (tier 1) owns transitions; delegates consume `focusedAnchorRow` / `focusedQtPos`.
- **Reconciliation rule.** Every text-bearing delegate's `onCursorPositionChanged` calls `LiveCursorState::syncFromTextEdit(anchor, qtPos)`. `LiveEditBinding::onContentsChange` also calls it after each buffer edit. The reconciliation is *write-through from delegate to mirror*; the mirror is never the authority during typing.

**Retirement (per invariant 3).** Nothing is being retired in tier 2. The pre-tier-2 docstring claim of "single canonical" was always aspirational; tier 1 made the dual-store explicit by adding `syncFromTextEdit` (in commit `6c44a07`'s S1/S2/S3 fix) without rewriting the docstring. Tier 2 admits the truth in writing. This is **a documentation correction, not a model change** — and it is named as such here per the invariant 3 "name what you're retiring" rule (in this case: nothing).

## 4. Architecture

No architectural change. The cursor seam at end-of-tier-2 has the same call graph as at start-of-tier-2:

```
TextEdit::cursorPositionChanged
    └─→ Connections.onCursorPositionChanged (in each text-bearing delegate)
            └─→ LiveCursorState::syncFromTextEdit(anchor, qtPos)
                    ├─→ validateVariant() ── reject if invalid (existing)
                    └─→ m_cursor = TextCaret{anchor, qtPos}; emit cursorChanged()

structural events (Enter, Backspace, kind transition, click, arrow cross-block)
    └─→ LiveStructuralKeyHandler → LiveCursorState::establishFocus(anchor, qtPos)
            └─→ tryResolvePending() ── chokepoint, picks variant per registry
```

Tier 2 changes (a) the docstring describing the left-hand column, (b) the field name `TextCaret::cachedByteOffset` → `cachedQtPos`, and (c) adds an invariant test exercising the top path. Nothing else moves.

## 5. Components

### 5.1 `LiveCursorState` class-header docblock

`libs/markoff-live/include/markoff/live/LiveCursorState.h:21-37` currently opens with:

> Owns the single canonical cursor value for the live view.

This is wrong as of commit `6c44a07`. Replace with:

> Owns the canonical cursor value for **structural events** (kind transitions, cross-block navigation, `BlockSelected`, `BlockInternalEdit`). For **in-block caret position during typing**, `QQuickTextEdit::cursorPosition` is canonical; `m_cursor` mirrors it via `syncFromTextEdit`, called from each text-bearing delegate's `onCursorPositionChanged` and from `LiveEditBinding::onContentsChange` after each buffer edit. The authority split is documented in `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md` §3.

The existing descriptions of `cursorKind`, `focusedAnchorRow`, `focusedQtPos`, and `requestTextCaretAtRow` remain intact.

### 5.2 `TextCaret::cachedByteOffset` → `cachedQtPos`

`libs/markoff-live/include/markoff/live/Cursor.h` — the field, its type (`quint32`), and the surrounding `TextCaret` struct shape are unchanged; only the name and comment change.

New field comment:

> qtPos: UTF-16 code-unit offset within the block, as reported by `QQuickTextEdit::cursorPosition`. Producer: `TextEdit::cursorPositionChanged` → `LiveCursorState::syncFromTextEdit`. Consumer: focus dispatch (`focusEditAt(qtPos)`), `focusedQtPos` Q_PROPERTY. **NOT a byte offset** — for conversion to CRDT byte space, see `Coordinates::qtPosToByte` (`LiveEditBinding.cpp:151`).

All 16 reference sites updated. Verification: `git grep cachedByteOffset` returns zero hits post-rename.

### 5.3 `tst_live_render_cursor_typing_invariant` (new)

New CMake target. Five slots, each using `LiveRealisticInputHarness` to type real keystrokes into a populated `LiveView`. The harness already exists (tier 1 wired it up); we only add a fixture file.

| Slot | Scenario | Invariant |
|---|---|---|
| `cursor_mirrors_textedit_through_ascii_typing` | Type `"Hello, World!"` one char at a time into an empty paragraph. | After each `QTest::keyClick`, `m_cursor.focusedQtPos == focusedTextEdit.cursorPosition`. |
| `cursor_mirrors_textedit_through_cjk_typing` | Type `"これはテスト"` one char at a time. | Same. Catches the latent UTF-16-vs-bytes confusion concern #2 named (CJK chars are single UTF-16 units, no surprise expected — but pin it). |
| `cursor_mirrors_textedit_through_emoji_typing` | Type `"🎉🚀✨"` one char at a time. | Same. Each emoji is a UTF-16 surrogate pair; `qtPos` advances by 2 per character. Asserts mirror reflects that. |
| `cursor_mirrors_textedit_through_arrow_within_block` | Type `"abcdef"`, then `Left` three times. | After each `Left` keystroke, mirror matches TextEdit. Covers the in-block arrow path (no structural event). |
| `cursor_mirrors_textedit_through_kind_transition` | In an empty paragraph, type `"# "` (space triggers kind→Heading). | After the post-transition `syncFromTextEdit` fires on the new HeadingDelegate, `m_cursor.focusedQtPos` matches the new delegate's `TextEdit::cursorPosition`. Pins tier-1's S1/S2/S3 re-anchor fix. |

Test policy: assertion via `QTRY_COMPARE` with the standard 5s timeout (matches the rest of the live-render suite). The focused TextEdit is found via the `QQuickItem` returned by `m_delegates[focusedAnchor].root` (queried through a test-only accessor — see §7).

### 5.4 Falsifiability proof (per invariant 4)

Before declaring the suite green, prove the invariant is falsifiable:

1. Commit: `markoff-live: stub — syncFromTextEdit no-op (FALSIFIABILITY PROOF, REVERTS NEXT)`. Body: `void syncFromTextEdit(...) { /* stub */ }` in `LiveCursorState.cpp`.
2. Run the five slots. **All five must fail.** If any pass, the invariant is too weak; fix the test, not the stub.
3. Commit: `Revert "markoff-live: stub — syncFromTextEdit no-op (FALSIFIABILITY PROOF, REVERTS NEXT)"`.

Pattern follows `0aef9f3` / `6b32482` from the block-only kinds plan.

## 6. Data flow

No new data paths. Tier 2 documents and tests existing paths.

## 7. Testing — supporting work

The chokepoint invariant test (`tst_live_render_focus_chokepoint_invariant`) already exposes a test-only accessor for the delegate registry: `LiveCursorState::isDelegateRegistered(anchor)` and the QML harness's `delegateAt(row)`. The new test needs one additional accessor:

```cpp
// LiveCursorState.h, test-only section
QQuickItem *delegateRootFor(Markoff::BlockAnchor anchor) const {
    auto it = m_delegates.find(anchor);
    return it == m_delegates.end() ? nullptr : it->root.data();
}
```

Within the test, the focused TextEdit is the delegate root's `textEdit` named child (every text-bearing delegate exposes one — see `ParagraphDelegate.qml`'s `objectName: "textEdit"`).

## 8. Definition of done

- [ ] `LiveCursorState` class-header docblock rewritten per §5.1.
- [ ] `TextCaret::cachedByteOffset` renamed to `cachedQtPos`; field comment updated per §5.2; `git grep cachedByteOffset` returns zero.
- [ ] `tst_live_render_cursor_typing_invariant` target builds and runs; all five slots pass.
- [ ] Falsifiability proof committed and reverted per §5.4. All five slots demonstrably fail under the stub.
- [ ] Discipline-log entry filed in `docs/queue.md` naming the unspecified-transients in `tryResolvePending` (concern #9 deferred).
- [ ] Tier-1 chokepoint suite (`tst_live_render_focus_chokepoint_invariant`) and the existing live-render suite show no new failures vs. the baseline at start-of-tier-2.
- [ ] No new `Qt.callLater` or re-entrance guards introduced. Verify by grep.

## 9. Future work — tier 3, tier 4

Unchanged from tier-1 spec §10:

- **Tier 3 — API consolidation** (concerns #3, #4, #5, #12). Open thread folded in: investigate the "valid transient states during a structural cascade" cited by `tryResolvePending`'s validation bypass, then decide whether concern #9 is closed by removing the bypass or whether the transients require a different shape. Default order: investigation first, then API consolidation.
- **Tier 4 — selection/cursor unification** (concern #10).

Discipline rule (from tier 1 spec §10) unchanged: interactive dogfood between tiers, tag held pending sign-off. Tier 2's dogfood gate is light because there are no production-behavior changes; the gate is *"the user types into the widget for a session and nothing feels different"*, plus the new invariant slots passing.

## 10. Citations

- `docs/INVARIANTS.md` — invariants 1, 2, 3, 4, 5, 8 enforced here.
- `docs/specs/2026-05-11-focus-chokepoint-design.md` — tier-1 spec; §10 frames tier 2/3/4; §3 documents the L4 precedent this spec inherits.
- `docs/specs/2026-05-13-block-only-kinds-design.md` — block-only kinds (follow-on to tier 1).
- Commit `6c44a07` — S1/S2/S3 cursor fix; added `syncFromTextEdit` as the reconciliation hook.
- Commit `249e7ef` — D-fc-5 delegate-swap registration race fix; closed the block-only kinds dogfood loop.
- `docs/handoff/2026-05-09-setext-dogfood-findings.md` — dogfood pass that surfaced S1/S2/S3.
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` — developmental record (per invariant 1).
- `docs/queue.md` §#2 — twelve concerns; this spec resolves #1, #2, #6 (full).

## 11. Open questions deferred to the plan

- Exact CMake wiring for `tst_live_render_cursor_typing_invariant` — follow the `tst_live_render_focus_chokepoint_invariant` precedent.
- Whether the test-only `delegateRootFor` accessor goes in `LiveCursorState.h`'s public test-only section or a separate `LiveCursorStateTestAccess` header. Default: public test-only section, matching `isDelegateRegistered`.
- Should test 4 (`arrow_within_block`) also cover `Right` + `Home` + `End`? Default: no — keep the slot focused on the one path that matters; the in-block paths share the same `cursorPositionChanged` signal.
