# Markoff answer — hide matching first heading as title

**From:** Markoff maintainers.
**Date:** 2026-08-30.
**In reply to:** `2026-08-27-to-markoff-hide-first-heading-as-title.md`.
**Verdict:** shipped. `View::setHideMatchingFirstHeadingAsTitle(bool)`
(and the `EditorWidget` pass-through) is on Markoff master in commit
`9b0138f8`, with the plan
[`../plans/2026-08-30-hide-first-heading-as-title.md`](../plans/2026-08-30-hide-first-heading-as-title.md)
(CLOSED). Wire it on your next re-pin:

```cpp
// attach path, after setInlineTitle(...)
m_editor->setHideMatchingFirstHeadingAsTitle(isFirstHeadingTitleMatch());
```

The view re-derives the match continuously (document edit, title change,
flag flip), so one call at attach is enough — you do not need to re-sync
it after a rename or a heading edit; the hide toggles as the
title/heading relationship changes.

## Decisions you asked us to make (D1–D5 of the plan)

- **D1 — flag, not `setHiddenBlockIds`.** The match predicate lives
  inside the leaf (it already owns `inlineTitle()` and the one document-
  change chokepoint); a consumer-supplied set would go stale on every
  edit. Your option A.
- **D3 — match rule:** first block is a **level-1** `Heading` whose
  **plain text** equals `inlineTitle()`. Level-1 is deliberate: it
  mirrors your own `HologramDocument::firstHeading()` so the two
  predicates can never disagree (a `## title` stays body content for
  both of us). The plain-text strip mirrors your code bit-for-bit —
  ATX strips leading `#`s + one space, **no trailing-hash strip**;
  setext uses the content-only buffer.
- **D4 — never hides the document's only block.** A file whose entire
  content is a lone matching `# title` keeps it visible; a fully
  invisible document would have no caret target.
- **D5 — independent of band visibility.** Hiding without
  `setInlineTitleVisible(true)` renders the title nowhere; your call.

## The one divergence from your handoff — read this before wiring (D2)

Your handoff asked for the hidden title to be **excluded from the
block-index space** (`blockCount`/`blockIdAt`/`blockIndexOf`/`blockRect`)
so the a11y tree matches what's rendered. **We kept the block in the
index space instead**, hidden via the same projection folding already
uses. Concretely:

- `blockCount()` includes the title; `blockIdAt(0)` is still the title;
  `blockIndexOf()` stays document order — indices of everything after
  the title never shift.
- The a11y tree keeps the title as child 0 with `state().invisible`
  (same convention fold bodies already use — hidden blocks stay in the
  tree so AT child indices are stable). The document name (your band,
  via `inlineTitle`/`accessibleDocumentName`) is announced once at the
  container; Orca's normal navigation skips the invisible heading.
- `blockRect(titleId)` is a zero-height rect at the right y.
- Caret/find/select all avoid it exactly as you specified: find matches
  inside it still exist (`FindController` is document-side) but
  `navigationRequested` lands on the first visible block; Home/Ctrl+Home
  skip it; Ctrl+A/Copy exclude it.

Why: excluding it would have broken `blockIndexOf`'s documented
"document-order index" contract and shifted your (and our) persisted
ephemeral-state indices with the title-match state; and "index
membership == rendered" is already false in this leaf (folds), so the
a11y design's answer to a hidden block is `invisible`, not removal. The
handoff's stated goal — a tree consistent with what's rendered — is
reached: the tree truthfully reports an invisible title. If literal
exclusion is ever a hard requirement, that is a larger, separate change;
the current shape is the stable one and matches fold precedent.

## No Corbomite impact

The change is additive and off by default; no existing contract changed
(`blockCount`/`blockIdAt`/`blockIndexOf`/`blockRect` semantics, a11y
child count/order, default rendering all byte-identical to before).
Corbomite can re-pin without reading anything.

## Baselines

Markoff holds its measured default-config suite **213/213** (canvas
41/41) at this commit. Your handoff cited "canvas 208/208 / full
315/315" — those predate the rich-clipboard merge at the pin and the
G3 live-retirement; the 213/213 figure is the current measured count.
