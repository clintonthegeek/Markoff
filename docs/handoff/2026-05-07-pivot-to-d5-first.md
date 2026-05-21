# Markoff — Unified Direction (D5-first)

**Date:** 2026-05-07
**Branch:** `exploration/new-foundation`
**Status:** D5-first remains the **product** posture (collab-first
architecture, single-user as a Capabilities subtraction). **Execution**
posture pivoted 2026-05-20 to **port-first** — Corbomite reintegration
drives evidence-based API micro-specs; the speculative `markoff-core`
freeze was withdrawn. Both stances coexist: D5-first describes *what*
the architecture is; port-first describes *how* the remaining API
surface gets finalized. For the current execution narrative read
[`2026-05-20-port-first-session-recap.md`](2026-05-20-port-first-session-recap.md)
first; this doc remains authoritative for the underlying design calls.
**Supersedes:**

- `docs/plans/2026-05-07-markoff-v1.0-overview.md` and Parts 1–5 (the
  v1.0 plan series).
- `docs/specs/2026-05-07-markoff-v1.0-design.md` (the v1.0 design spec).
- The Corbomite migration guide work item
  (`docs/handoff/2026-05-07-markoff-v1.0-corbomite-migration.md` does
  not exist and is not to be written under v1.0 framing).
- The C-restoration paper trail (already declared historical; this doc
  finishes that retirement explicitly).
- `master`-side `CLAUDE.md`'s description of Markoff as "Phase B
  bridge / Phase C retirement" — that vocabulary is dead on this branch
  and the master CLAUDE.md will be reconciled at merge time.

This document picks the winners and losers. It is not itself a plan.
It tells you what gets planned next, what gets pruned, and what stays
frozen.

---

## 1. The decision

**Collab is the product. D5 (collab activation) ships before any
public-API freeze, before any Corbomite-facing migration guide, and
before any further perf or facade work.**

The branch has loop-pivoted four times in two months because each pivot
hedged on whether collab was in scope. Every architectural decision in
the view layer was hedged against an undecided D5; the v1.0 plan was the
clearest example — written 2026-05-07, the same day D4 completed, with
D5 still stubbed at `docs/archive/2026-05-04-d5-collab-activation-STUB.md`.

That hedging stops here. The collabtext investment, the IdList work
(D1), the per-block CRDT foundation (D2), the view-layer adaptation
(D3), and the parser scope reduction (D4) only make sense as
investments toward a collaborative editor. We commit.

The single-user fast path (memory: `project_lightweight_non_crdt_codepath.md`)
remains a defensible *future* optimisation but is not a v1.0-class
deliverable. It does not gate D5.

---

## 2. What's retired

Each item below is to be moved out of `docs/` (into `docs/archive/`) or
deleted, in the cleanup work-unit at the head of §4. None of it
informs new design. None of it is to be cited in new specs except as
historical context. Nobody works on any of it.

### 2.1 The v1.0 plan series (entirely)

- `docs/plans/2026-05-07-markoff-v1.0-overview.md`
- `docs/plans/2026-05-07-markoff-v1.0-part1-foundation.md`
- `docs/plans/2026-05-07-markoff-v1.0-part2-document-source.md`
- `docs/plans/2026-05-07-markoff-v1.0-part3-live-facade-perf.md`
- `docs/plans/2026-05-07-markoff-v1.0-part4-live-links-testapp.md`
- `docs/plans/2026-05-07-markoff-v1.0-part5-migration-merge.md`
- `docs/specs/2026-05-07-markoff-v1.0-design.md`

These move to `docs/archive/v1.0-plan-pre-d5/` with a one-paragraph
header noting they were retired before any phase began executing
because they presumed D5 was either complete or out of scope. Neither
turned out to be true.

### 2.2 The C-restoration paper trail

- `docs/restoration-status.md`
- The R1–R5.5 plan series under `docs/plans/2026-05-02-live-render-r*.md`.
- `docs/specs/2026-05-03-live-render-r5-5-marker-paragraph.md` and
  any sibling marker-paragraph specs.
- The R5.5 SESSION-BRIEFs and bug-tracker docs in `docs/handoff/`.

The branch's worktree CLAUDE.md already declared these historical. The
move into `docs/archive/c-restoration-arc/` finalises the retirement so
there is nothing left in `docs/specs/` or `docs/plans/` that competes
with the D-arc as the live record.

The two D2-era commits that already partially landed Part 2 work
(`65c6397`, `c70e0ed`, `00c78d2`, `2f967f3`, `a632154`, `86109b3`,
`66db6f3`, `262214f`, `6313f77`, `5f5c1f2`, `9aaad12`, `7cf22c9`,
`e8177b1`, `f646c90`, `4711a92`, `6501b24`, `df7ad00`, `bbf36e3`,
`876382d`, `6dee7c5`, `1ad9e0c`) are *not* reverted. They were
mechanical renames + neutral additions; they survive because the new
D5-first plan will use them as-is. The plan papers retire, the code
stays.

### 2.3 `markoff-view-qml`

The legacy QML view leaf. Source mode still nominally ships; live mode
was retired in D4. It is redundant with `markoff-source` (canonical
source widget) and will not have any role under D5.

It dies in the cleanup work-unit. If a regression surfaces in
`markoff-source` that view-qml currently masks, that regression is a
real bug to fix in `markoff-source` — view-qml is not a fallback.

### 2.4 The Corbomite migration guide work item

The migration guide was a v1.0 deliverable. With v1.0 retired and D5
ahead, no migration guide gets written until D5 is shipped, the live
pipeline is audited (§4.5), and a public API is genuinely stable.

Corbomite's view of Markoff continues to operate against the current
shape (the `MARKOFF_READING_USE_REAL_COREDEPS=ON` bridge from Phase B,
where applicable). The handoff doc at
`docs/handoff/2026-04-20-phase-c-ownership-handoff.md` is also retired
under this pivot; the joint-ownership posture from Phase C does not
apply to the D-arc.

### 2.5 The R5.5 vestigial workarounds (flagged, not yet pruned)

`docs/handoff/2026-05-07-live-binding-developmental-history.md` §A.3 and
§A.4 documented two pieces of machinery in the live binding that exist
to work around pre-D2 `BlockAnchor` instability:

- `AstBlockDiff`'s `Delete+Insert → Equal` collapse pass.
- `LiveBlockModel::anchorRenumbered` and the `LiveCursorState`
  consumer.

D2 made `BlockId == IdList element id`, which removes the underlying
instability. These workarounds are dead weight, but pruning them now is
*side work on an unfinished codebase* — D5 design may rebuild this
layer anyway, in which case the prune is automatic. They stay until
the §4.5 audit, then go.

---

## 3. What's frozen

These pieces are *load-bearing right now* and will remain load-bearing
through D5 design. **No work happens on them outside §4. No perf work,
no rewrites, no opportunistic touches.**

- The current `LiveListModelBinding` ↔ `AstBlockDiff` ↔
  `LiveBlockModel` pipeline (the full-walk in `onD2Changed`, the Myers
  diff, the kind-transition inline pass, the structural row signals,
  `anchorRenumbered`).
- `MarkoffDocument`'s D2 surface (`d2ApplyBufferEdit`,
  `d2InsertBlock`/`d2RemoveBlock`, `d2SetBlockKind`/`d2SetBlockAttr`,
  `applyStructural`, `applyFlatEdit`, the `d2DocumentChanged` debounce,
  the targeted signals added in `00c78d2`).
- `markoff-source` (the canonical QPlainTextEdit-based source widget).
- `MarkoffParser::Parser` namespace and the on-demand parsing
  contract (`Document::fromMarkdown`, `inlineSpansFor` cache).
- `Markoff::CursorPos` / `Theme` / `EditorContext` / `ActionId` /
  `BlockKindNames` / `MarkdownView` (the v1.0-introduced consumer
  primitives that landed before this pivot). They are kept *as
  internal types*; the public-API freeze that Part 1 of the v1.0 plan
  was leading toward is deferred until §4.6.

The *only* approved touches to any of the above between now and §4.5
are bug-fixes for bugs that surface during D5 design or implementation
and that block progress. Speculative cleanup is forbidden.

The single open correctness item in `docs/TODO.md` (ListItem
Tab/Shift-Tab parent-existence guard) is documented as cosmetic / non-
corrupting; it stays in TODO and is fixed during §4.5 unless D5
design needs the surrounding code touched first.

---

## 4. What gets completed, in this order

This is the ordered list. **Nothing is done out of order. Nothing is
done in parallel.** Each work-unit ends with a green tree and an
explicit completion check before the next begins.

### 4.1 Retirement cleanup (one work-unit)

Move the §2.1 and §2.2 paper out of `docs/` into `docs/archive/`,
with the README headers described above. Update `docs/d-arc/d-arc-
status.md` to reference this pivot doc as authoritative.

Update the worktree `CLAUDE.md` and `libs/markoff-live/CLAUDE.md`,
`libs/markoff-core/CLAUDE.md`, `libs/markoff-source/CLAUDE.md` to point
to this doc as the current posture. Remove references to the v1.0 plan
and the Phase B/C handoff.

No code touched. Output: a single PR-shaped commit series titled
`docs: retire v1.0 plan, C-restoration paper, Phase B/C handoff`.

### 4.2 view-qml deletion (CLOSED — already done in `f646c90`)

> **Status:** closed 2026-05-07. The deletion was already landed in
> commit `f646c90` ("markoff-view-qml: delete (retired in D4;
> superseded by markoff-source for v1.0)") as part of v1.0 Part 1
> prep, before this pivot doc was written. It survives under the §2.1
> "kept commits, plans retire, code stays" rule. The work-unit was
> redundant; closed without further action beyond updating CLAUDE.md
> to remove the stale "flagged for deletion" entry.

Original prescription (kept for the record):

> Delete `libs/markoff-view-qml/` entirely, plus its CMake registration,
> plus any references in the test app and root CMakeLists. If any tests
> move from view-qml to markoff-source as part of the deletion, they move
> intact (no rewrites). Tests pass at the end of the commit.

### 4.3 D5 substantive design (CLOSED — spec-approved 2026-05-08)

> **Status:** closed 2026-05-08. The substantive D5 design was
> brainstormed in-conversation, written, and user-approved.
>
> **Delivered:**
> - `docs/specs/2026-05-07-d5-collab-activation-design.md` — the D5
>   spec (boundary API, sibling-map sync, watermark/ack mechanics,
>   consumer wiring sketch, out-of-scope list, test strategy,
>   implementation sequencing).
> - `docs/handoff/2026-05-07-collabtext-d5-negotiation-opener.md` — a
>   maintainer-facing companion document (not initially scoped here;
>   added during brainstorm). Independent track; D5 implementation
>   does not gate on its outcome.
> - Stub archived at `docs/archive/2026-05-04-d5-collab-activation-STUB.md`.
>
> **Reframing surfaced during brainstorm.** The original prescription
> below assumed Markoff sat *on top of* collabtext at runtime. The
> brainstorm rejected that framing: Markoff and collabtext are peer
> widgets; a host (Corbomite, eventually) owns transport/identity/
> multi-user/side-channels. The spec reflects this three-layer model.
> The negotiation opener captures the asks of collabtext maintainers
> that follow from it.
>
> **§4.4 (writing-plans for D5 implementation plan) is the next
> work-unit.**

Original prescription (kept for the record):

> Replace `docs/archive/2026-05-04-d5-collab-activation-STUB.md` with
> a real D5 design spec. The stub names the inputs but not the design.
>
> The design must answer at minimum: network shape, identity model,
> conflict semantics for structural ops, persistence, undo across
> replicas, view-layer contract impact, scope boundaries. Output: a
> spec under `docs/specs/2026-05-XX-d5-collab-activation-design.md`
> and a brainstorm trail in `docs/d-arc/`. The design pass is
> co-owned with the collabtext side, mirroring the D0 joint-design
> model.
>
> **No code is written until the spec is user-approved.**

### 4.4 D5 implementation

A plan file under `docs/plans/`, then phase-by-phase implementation
following the same cadence as D2/D3/D4 (per-phase commit, per-phase
tests, dogfood pass at the end). Length and shape are determined by
the spec from §4.3.

Acceptance: the spec's acceptance criteria are met, the tests pass,
the user signs off on a dogfood pass.

### 4.5 Live-pipeline audit and prune

With D5 landed, audit and prune the live pipeline. Targets:

- The R5.5 vestigial workarounds (§2.5).
- The full-walk vs targeted-signal question, *now* informed by what D5
  needs from the doc-signal contract rather than by speculation.
- Open correctness items in `docs/TODO.md`.

> **Narrowed 2026-05-08.** A prior bullet — *"any other dead code
> surfaced by D5 (e.g. `inlineSpansFor` in the hot path)"* — is
> retracted. `inlineSpansFor` is load-bearing infrastructure for the
> post-D5 E-arc (inline-format styling + cursor-aware delimiter
> visibility), not dead code. Removal would tear out the upstream half
> of an in-flight foundational feature. See
> `docs/specs/2026-05-08-e-arc-framing.md` and the §A.7 erratum in
> `docs/handoff/2026-05-07-live-binding-developmental-history.md`.

This is the work the v1.0 Part 3 plan tried to do prematurely. It is
done correctly here because by §4.5 the foundation is genuinely
complete and the pipeline's invariants are settled.

Output: one or more commits under a single audit work-unit, possibly
its own short plan if the surface is large.

### 4.6 Public API freeze + (only then) Corbomite migration

> **Status (2026-05-08): deferred until Corbomite is ready to consume the
> freeze.** E-arc begins in §4.6's slot in the meantime. §4.6 is *deferred*,
> not *cancelled*; it resumes when Corbomite signals readiness, or
> post-E-arc, whichever lands first. Decision record + resumption criteria:
> `docs/handoff/2026-05-08-defer-46-to-e-arc.md`. Inversion rationale: a
> migration guide written without a ready consumer goes stale before it
> ships, and freezing primitives without an exercising consumer risks
> freezing the wrong shape — exactly the thrash this pivot doc was written
> to prevent.

The work the v1.0 Parts 1, 2, 4, 5 were leading toward — locking down
`Markoff::MarkdownView`, `Markoff::MarkoffDocument`, `Markoff::CursorPos`
etc. as a stable consumer surface, writing a Corbomite migration guide,
merging to `main`, tagging a release.

Whether this happens at all, and on what timeline, is decided after
§4.5 lands. It is not pre-decided here. The v1.0 plan is retired; the
post-D5 equivalent will be written from scratch in light of what D5
actually delivered.

### 4.7 What follows the foundation-bookend (E-arc)

> **Status (2026-05-08): E-arc has begun.** §4.6 is deferred (see banner
> above); E-arc takes §4.6's slot in the work order. The "E-arc work does
> not start until §4.6 ships" sentence below is overridden by the decision
> record at `docs/handoff/2026-05-08-defer-46-to-e-arc.md`. Live status:
> `docs/e-arc/e-arc-status.md`.

After §4.5 + §4.6 close out the D-arc, the next arc is **E-arc** —
live-render completion, treated as the maximalist Markoff prototype.
E-arc completes inline-format rendering, cursor-aware delimiter
visibility, Obsidian-flavoured affordances (wikilinks/embeds/tags/
callouts), tables, frontmatter, footnote rendering, and math/mermaid
Live-mode parity. E-arc bookends with a distillation phase (E6) that
extracts the foolproof recipe for generalising Markoff into new view
shapes.

**Authoritative E-arc framing:**
`docs/specs/2026-05-08-e-arc-framing.md`. **Roadmap:**
`docs/e-arc/2026-05-08-e-arc-roadmap.md`.

E-arc is named here so that a fresh agent context arriving at §4.6
knows what comes next without re-deriving it. E-arc work does not
start until §4.6 ships — building features against an unfrozen public
surface throws work away.

---

## 5. Operating principles

These are the rules going forward. They apply to me (the agent) and
to any future agent or contributor on this branch.

1. **One arc at a time.** D5 is the active arc. No work happens on
   anything that is not §4.1 → §4.6 in order. "While I'm here I'll
   also …" is forbidden.

2. **Retirement is explicit.** When a piece of work is dead, it is
   either deleted or moved to `docs/archive/`. It does not stay alive
   in `docs/specs/` or `docs/plans/`. There is no third state.

3. **Speculative architectural change requires a measured premise.**
   Perf work requires a benchmark first. Architectural change requires
   a correctness or shipped-feature justification. "It would be cleaner"
   is not a justification on its own.

4. **Every architectural change cites the developmental record.**
   `docs/handoff/2026-05-07-live-binding-developmental-history.md` is
   the current record for the live pipeline. New specs that propose to
   change the pipeline must reference where the affected machinery came
   from and what it replaces. Loops happen when the record is ignored.

5. **No side work on an unfinished codebase.** If a cleanup is not
   blocking the active arc, it waits until §4.5 (or its post-D5
   equivalent). The branch carries dead weight intentionally; it gets
   pruned in one pass at a known checkpoint.

6. **The d-arc-status board stays current.** Every commit on this
   branch updates `docs/d-arc/d-arc-status.md`'s recent-changes log.
   That board is the single source of "where are we right now."

---

## 6. What this document is NOT

- **Not a D5 design.** §4.3 produces that. This doc only commits the
  branch to writing it.
- **Not a deletion PR.** §4.1 and §4.2 are the deletion PRs. This doc
  authorises them but doesn't perform them.
- **Not a freeze on bug-fixing.** Bugs that block D5 design or
  implementation get fixed under §3's "load-bearing" exception.
- **Not a commitment to a v1.0 release timeline.** §4.6 is conditional
  on §4.5; §4.5 is conditional on §4.4; etc. Timelines are decided as
  each work-unit completes, not pre-baked.

---

## 7. See also

- `docs/handoff/2026-05-07-live-binding-developmental-history.md` —
  pipeline-feature provenance and arc context. Cited by §2.5, §3, and
  rule 4.
- `docs/d-arc/d-arc-status.md` — live status board. Becomes the live
  tracker for §4 once §4.1 lands.
- `docs/d-arc/2026-05-04-d-arc-roadmap.md` — D-arc orientation.
- `docs/d-arc/collabtext-scope-line.md` — the six "won't do" items;
  reaffirmed by §4.3.
- `docs/archive/2026-05-04-d5-collab-activation-STUB.md` — the stub that
  §4.3 replaces.

---

## 8. Authority

This document is authoritative for the `exploration/new-foundation`
branch as of 2026-05-07. It is overridden only by:

- An explicit user instruction.
- A successor pivot doc that names this one as superseded and gives
  reasons.

Agents picking up work on this branch read this document first, then
the d-arc status board, then the active spec for the work-unit they're
on. If the work doesn't fit into §4's ordered list, it doesn't happen.
