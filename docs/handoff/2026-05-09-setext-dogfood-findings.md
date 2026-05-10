# Setext Dogfood Findings — 2026-05-09

Dogfood pass against `/tmp/setext-dogfood.md` (fixture from setext plan Phase 5B).

## What passed

- **Visual render (load):** Setext H1 and H2 blocks render with correct heading typography — indistinguishable from ATX equivalents. All four setext headings in the fixture loaded correctly.
- **Save round-trip (intact blocks):** Loading and re-saving without editing preserves setext form verbatim.
- **ATX headings:** Unaffected; ATX H2, H3 render and edit normally.

## Bugs found (cursor/focus after kind-transition)

All three bugs are cursor placement / focus loss issues triggered at kind-transition boundaries. Root cause is likely that `Cmd::changeKind` fires and the pipeline `return`s without issuing a cursor request to re-anchor the caret.

### Bug S1 — Setext demote: focus lost when last underline char is deleted

**Steps:** Click into a setext heading block. Backspace the `===` or `---` underline character by character. When the last character is removed, the caret disappears and keyboard focus is lost — the widget no longer responds to keystrokes.

**Expected:** Cursor stays in the (now-Paragraph) block at the point of deletion.

**Likely site:** `LiveListModelBinding::onD2Changed` setext-demote branch — `Cmd::changeKind` fires and the function `return`s with no subsequent `requestTextCaretAtRow`. Compare the ATX-demote path and the existing promote path to see how cursor re-anchoring is done there.

### Bug S2 — Setext creation: focus lost when first underline char is typed

**Steps:** In a plain paragraph, type some text, then Shift+Enter (soft break), then type the first `-` or `=`. Focus is lost (same symptom as S1). Clicking back into the widget restores focus; subsequent `-`/`=` characters can be typed normally.

**Expected:** Focus stays continuously; the first underline char should behave exactly like the second.

**Likely site:** The heading-promotion path in `onD2Changed` — when the block transitions from Paragraph to Heading (after the first underline char is confirmed), the promotion path may not re-anchor the cursor. Alternatively, something about the text-bearing delegate being swapped (Paragraph → Heading delegate) during the QML `DelegateChooser` refresh drops focus.

### Bug S3 — ATX demote: cursor jumps to line start when space after `#` is deleted

**Steps:** Click into an ATX heading block (e.g. `## My Heading`). Position caret between `##` and ` My Heading`. Delete the space (so buffer becomes `##My Heading`). Kind-transition fires, demoting the block to Paragraph. The cursor jumps to position 0 (start of the line) rather than staying at position 2 (between the hashes and the text).

**Expected:** Cursor remains at (approximately) the deletion point — position 2 in the resulting paragraph text `##My Heading`.

**Likely site:** Same as S1 — ATX-demote branch in `onD2Changed` issues `Cmd::changeKind` and `return`s without requesting a cursor position.

## Recommendation

Fix S1, S2, S3 before tagging `v0.7.0-e2.5`. The theme is clear: any `return` in the kind-transition pipeline after a `Cmd::changeKind` call needs to be followed by a `requestTextCaretAtRow` (or equivalent) to re-anchor the caret. A single targeted fix pass should close all three.

Write a regression test for each (kind-demote keeps cursor in same block; heading-creation keeps focus). Add to `tst_live_render_kind_transition` or a new `tst_live_render_setext_e2e` extension.
