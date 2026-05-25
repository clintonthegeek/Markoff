# Archived: Markoff v1.0 plan (pre-D5)

**Retired:** 2026-05-07
**Retired by:** `docs/handoff/2026-05-07-pivot-to-d5-first.md` §2.1.

These documents constitute the v1.0 plan as written 2026-05-07 — the
same day D4 completed and before D5 had substantive design. The plan
presumed D5 was either complete or out of scope. Neither turned out to
be true: the unified-direction pivot of 2026-05-07 commits the branch
to designing and shipping D5 (collab activation) before any v1.0-class
work happens.

These plans are kept as historical context. They are **not
authoritative**, are **not to be executed**, and are **not to be cited
in new specs or plans except as historical context**. Any future
v1.0-shaped work will be planned from scratch in light of what D5
actually delivers.

## Contents

- `2026-05-07-markoff-v1.0-overview.md` — overview of the five-part
  v1.0 plan series.
- `2026-05-07-markoff-v1.0-part1-foundation.md` — library renames,
  parser namespace rename, public include path reorganisation, shared
  consumer types (`CursorPos`, `Theme`, `EditorContext`, `ActionId`,
  `BlockKindNames`), `MarkdownView` base class.
- `2026-05-07-markoff-v1.0-part2-document-source.md` — `MarkoffDocument`
  signal contract, `Source::Editor` inherits `MarkdownView`.
- `2026-05-07-markoff-v1.0-part3-live-facade-perf.md` — `Live::Editor`
  QWidget facade hosting QQuickWidget, replace `LiveListModelBinding::onD2Changed`
  full-walk with targeted handler, perf benchmark. **This is the part
  whose merits triggered the pivot review** — see
  `docs/handoff/2026-05-07-live-binding-developmental-history.md` for
  why.
- `2026-05-07-markoff-v1.0-part4-live-links-testapp.md` — inline-runs
  scaffold (links only), tri-mode test app rebuild.
- `2026-05-07-markoff-v1.0-part5-migration-merge.md` — Corbomite
  migration guide, merge to main, tag `v1.0.0`.
- `2026-05-07-markoff-v1.0-design.md` — the v1.0 design spec these
  plans executed.

## Code that landed on the branch before retirement

Several mechanical-rename commits and neutral additions from Part 1 and
the head of Part 2 had already landed before the plan was retired
(library renames, public include path reorganisation, the
`MarkdownView` base class, `Markoff::CursorPos` / `Theme` /
`EditorContext` / `ActionId` / `BlockKindNames`, the targeted block-
update signals on `MarkoffDocument`, `Source::Editor` inheriting
`MarkdownView`).

Those commits are *not* reverted. The pivot doc treats them as kept-as-
is contributions that survive into the post-D5 codebase. The
**plans** retire; the **code** stays.

## See also

- `docs/handoff/2026-05-07-pivot-to-d5-first.md` — the pivot doc.
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` —
  developmental history that informed the retirement.
