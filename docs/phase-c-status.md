# Phase C Status

Living status board for the four Phase C work-units. The Corbomite agent
updates this file in every Phase C commit touching either repo. Entries
are append-only in the "Activity log" section; the work-unit status table
gets edited in place.

## Scope agreement

Phase C replaces Phase B's bridge code and delivers the final shape of
the tri-view API. Four work-units:

| ID   | Work-unit                                     | Markoff tag on completion |
| ---- | --------------------------------------------- | ------------------------- |
| C1   | DI seam (retires `MARKOFF_READING_USE_REAL_COREDEPS`) | `v0.3.0`          |
| C2   | Theme / ResourceProvider / LinkResolver consolidation | `v0.5.0`          |
| C3   | MarkoffDocument becomes content-authoritative | `v0.4.0`                  |
| C4   | Renderer unification (Code/Math/Mermaid)      | `v0.6.0`                  |

## Ordering rationale

**C1 first** — it closes the Phase B bridge cleanly and establishes
the interface-injection pattern the rest of Phase C reuses.

**C3 before C2** — shared-document adoption is the biggest user-visible
unlock and is mostly orthogonal to Theme/Provider/Resolver shapes. Doing
it in isolation avoids cross-coupling two large refactors.

**C2 late** — Theme consolidation is the most likely source of
CorbomiteApp call-site breakage. Do it once the rest of Phase C is
stable so there's only one high-breakage coordination beat.

**C4 last** — smallest, mostly internal, no consumer-facing surface.
Polish.

## Work-unit status

| ID   | Status       | Spec                                   | Plan                                   | Markoff PR/branch    | Corbomite PR/branch  | Tag       |
| ---- | ------------ | -------------------------------------- | -------------------------------------- | -------------------- | -------------------- | --------- |
| C1   | not started  | —                                      | —                                      | —                    | —                    | —         |
| C2   | not started  | —                                      | —                                      | —                    | —                    | —         |
| C3   | not started  | —                                      | —                                      | —                    | —                    | —         |
| C4   | not started  | —                                      | —                                      | —                    | —                    | —         |

Status values: `not started` → `spec drafted` → `spec approved` → `markoff implementing` → `markoff ready` (tag exists) → `corbomite adapting` → `corbomite shipped` → `markoff cleanup` → `done`.

## Recipe per work-unit

1. **Draft spec** in `docs/specs/YYYY-MM-DD-phase-c<N>-<topic>.md`. Include:
   - Interface shape(s) proposed
   - Migration path for any Markoff internals that call the same surface
   - Breaking-change manifest for CorbomiteApp
   - Acceptance criteria
2. **Update the table above** to `spec drafted`. Commit the spec.
3. **User or a parallel reviewer reads the spec**, either approves or sends back. (For unit-sized changes with no ambiguity, the Corbomite agent may self-approve and proceed — note in the activity log.)
4. **Draft plan** in `docs/plans/YYYY-MM-DD-phase-c<N>-<topic>.md`. Task-by-task with explicit files + commit messages, per the Phase A/B convention.
5. **Implement on a Markoff branch**; land commits one at a time with green tests after each. Set status to `markoff implementing`.
6. **Tag** the Markoff SHA once all Phase-C-<N> tasks are done. Set status to `markoff ready`.
7. **Bump Corbomite's submodule pin** and write the CorbomiteApp-side adaptation. Set status to `corbomite adapting`.
8. **Ship on the Corbomite side**, run CorbomiteApp end-to-end smoke. Set status to `corbomite shipped`.
9. **Return to Markoff** to retire any bridge code left over from the previous phase. Set status to `markoff cleanup`. Tag a cleanup release if the diff is non-trivial.
10. **Close** to `done`. Activity-log a brief retrospective: what broke, what we'd do differently.

## Invariants (copy from handoff doc — repeat for visibility)

1. Standalone Markoff build + ctest always green.
2. No `Corbomite`-named types in Markoff public interfaces (Phase B stubs excepted; retire in C1).
3. Tests that need Corbomite concretes gate on the appropriate CMake option.
4. Every work-unit tags a Markoff version.
5. `master` is append-only. No force-push.

## Activity log

Append in reverse-chronological order (newest first).

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
