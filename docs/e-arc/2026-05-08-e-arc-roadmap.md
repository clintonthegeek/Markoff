# E-arc roadmap — orientation for fresh contexts

**Date:** 2026-05-08
**Branch:** `exploration/new-foundation`
**Purpose:** Canonical orientation doc for the E-arc work. An empty agent context picking up any phase of E should read this first to understand what each phase delivers, the ordering constraints, and the invariants binding the arc.

---

## 1. What E-arc is

E-arc completes the QML live-render view as the **maximalist Markoff prototype**: feature-complete for an Obsidian-equivalent live-preview pane, with inline-format rendering, cursor-aware delimiter visibility, the Obsidian-flavoured affordances (wikilinks, embeds, tags, callouts), tables, frontmatter, footnotes, and math/mermaid parity with Reading mode.

**Framing premise:** the live-render view is the ice-breaker experimental prototype. Every other view Markoff will ship — Source, Reading, future widget shapes (compose, comment, slide, outline, etc.) — is a *structural subset* of live-render. E-arc builds the full superset; the post-E distillation phase (E6) extracts the recipe for subtracting from it.

**Authoritative framing spec:** `docs/specs/2026-05-08-e-arc-framing.md`. **Read it before starting any E-phase.**

---

## 2. Phase summary

| Phase | Status | Deliverable | Companion |
|---|---|---|---|
| **E1** | `plan-approved` (ready to execute) | Inline-format highlighter in QML delegates (bold/italic/strike/inline-code/highlight/link/wikilink/tag) reading `BlockRecord::inlineSpans`. Tag: `v0.7.0-e1`. | Spec: [`2026-05-08-e1-inline-highlighter-design.md`](../specs/2026-05-08-e1-inline-highlighter-design.md). Plan: [`2026-05-08-e1-inline-highlighter.md`](../plans/2026-05-08-e1-inline-highlighter.md). |
| **E2** | `pending` | Cursor-aware delimiter visibility — markers hide unless caret enters the span | `docs/specs/2026-XX-XX-e2-delimiter-visibility-design.md` (TBW) |
| **E3** | `pending` | Wikilinks, embeds, tags, callouts — Obsidian-flavoured affordances | `docs/specs/2026-XX-XX-e3-obsidian-affordances-design.md` (TBW) |
| **E4** | `pending` | Tables, frontmatter, footnote rendering | `docs/specs/2026-XX-XX-e4-tables-frontmatter-footnotes-design.md` (TBW) |
| **E5** | `pending` | Math / Mermaid Live-mode parity with Reading mode | `docs/specs/2026-XX-XX-e5-math-mermaid-parity-design.md` (TBW) |
| **E6** | `pending` | Distillation — extract the foolproof view-construction recipe | `docs/recipe/` (TBW) |

Status legend: `pending` (not yet started) · `spec-in-brainstorm` · `spec-approved` · `plan-approved` · `in-progress` · `dogfood` · `complete`.

---

## 3. Where to start (by purpose)

| If you want to … | Read |
|---|---|
| Understand why E-arc is shaped the way it is | `docs/specs/2026-05-08-e-arc-framing.md` (the constitutional doc) |
| Understand E-arc's place in the larger arc lineage | `docs/d-arc/2026-05-04-d-arc-roadmap.md` §"What's next" + this file's §1 |
| Pick up E1 (inline-format highlighter) | `docs/specs/2026-05-08-e-arc-framing.md` §2.E1 + the design pre-cursors listed there |
| Pick up E2 (delimiter visibility) | E1 must be complete first; then `docs/specs/2026-05-08-e-arc-framing.md` §2.E2 |
| Understand the post-E new-widget posture | `docs/specs/2026-05-08-e-arc-framing.md` §3 |
| Track E-arc progress in real time | `docs/e-arc/e-arc-status.md` (created when E1 begins) |

---

## 4. Cross-arc binding constraints

### 4.1 Prerequisite: D-arc bookend

D5 + §4.5 must complete before E-arc begins. **Both landed 2026-05-08.**

> **2026-05-08 amendment.** Pivot-doc §4.6 (public-API freeze + Corbomite
> migration) is **no longer a prerequisite**. §4.6 is deferred until
> Corbomite is ready to consume the freeze; E-arc begins in §4.6's slot
> instead. See `docs/handoff/2026-05-08-defer-46-to-e-arc.md` and the
> framing-doc §0.1 addendum.

Originally this section also listed *"§4.6 public-API freeze ships"* with
the rationale *"building E1 against a moving public API risks throwing
work away."* That rationale is replaced by the inverse: freezing without a
ready Corbomite consumer is freezing in the dark; E1–E5 will expose seams
the freeze should account for; build first, freeze post-E-arc.

### 4.2 The view-construction discipline

E-arc adds one invariant inherited by all post-E work:

> Every E-phase ships with a "subtractability note" — how would a view that doesn't need this capability avoid linking it, instantiating it, paying its runtime cost? If the answer is "you can't," that's a recipe-violation flag.

The distillation pass at E6 audits subtractability across all phases. Post-E view development applies the recipe distillation produces — every new view starts by stating which capabilities it subtracts.

### 4.3 D-arc invariants carried forward

E-arc inherits and respects:

- Single-user is the default — collab is layered, never assumed.
- No `Corbomite`-named types in Markoff public API.
- `master` is append-only; phase milestones tag versions.
- The collabtext scope-line (`docs/d-arc/collabtext-scope-line.md`) — E-arc never adds requirements that violate the six "won't do" items.

---

## 5. Document organization

When E-arc begins:

```
docs/
├─ e-arc/
│  ├─ 2026-05-08-e-arc-roadmap.md            ← this file (orientation)
│  └─ e-arc-status.md                         ← live status board (created at E1 kick-off)
├─ specs/
│  ├─ 2026-05-08-e-arc-framing.md            ← constitutional framing
│  └─ 2026-XX-XX-eN-*-design.md              ← per-phase specs (TBW)
├─ plans/
│  └─ 2026-XX-XX-eN-*.md                     ← per-phase plans (TBW)
├─ recipe/                                    ← created at E6
│  ├─ 2026-XX-XX-view-construction-recipe.md ← the load-bearing post-E doc
│  ├─ capability-matrix.md
│  └─ delegate-authoring-template.md
└─ ...
```

---

## 6. Update protocol

When a phase advances:

- The phase's status badge above moves: `pending` → `spec-in-brainstorm` → `spec-approved` → `plan-approved` → `in-progress` → `complete` (with `dogfood` between `in-progress` and `complete` where appropriate).
- A new line in `docs/e-arc/e-arc-status.md` records the change with date and one-sentence summary.
- The D-arc roadmap's "What's next" pointer updates if E-arc's leading phase changes.
- The worktree `CLAUDE.md` banner gets a one-line update if the active phase changes.

When E-arc completes (E6 done): this roadmap stays in tree as historical context; `docs/recipe/` becomes the active reference for ongoing development; new-widget arcs begin under their own roadmaps that cite the recipe as authoritative.
