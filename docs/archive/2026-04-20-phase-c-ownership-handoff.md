# Phase C Ownership Handoff — Corbomite Agent

**Effective:** 2026-04-20, concurrent with Markoff `v0.2.0`.

Development on Markoff's Phase C work passes to the agent that owns `/home/clinton/dev/Corbomite/`. That agent works both sides of the submodule boundary: it commits to Markoff's `master` and to Corbomite's branches within the same session, treating the submodule pin as a coordination mechanism between its own commits rather than between two different teams.

The Markoff-side agent that owned Phase A and wrote the Phase B plan has finished its remit. Any Markoff-internal work from this point forward — specs, plans, implementation, releases — is Corbomite-agent territory unless explicitly routed back.

## Why

Phase C's four work-units (DI seam, Theme consolidation, shared-document adoption, renderer unification) each require judgement calls about interface shape that are expensive to get wrong and are best made by the same person who has to both (a) write the interface in Markoff and (b) write the adapter in Corbomite. The spec-review-between-two-agents approach described in `docs/specs/...` would've introduced round-trip latency on every design decision. Single-agent ownership removes that entirely at the cost of a standalone-Markoff-reusability risk (documented below as an invariant to preserve).

## Scope of authority

The Corbomite agent may, in the Markoff repo:

- Create branches, commit, merge, tag.
- Write new specs and plans under `docs/specs/` and `docs/plans/`.
- Refactor any library (`markoff-core`, `markoff-live`, `markoff-source`, `markoff-reading`) as long as the invariants below hold.
- Retire Phase B bridge code (the `MARKOFF_READING_USE_REAL_COREDEPS` option + `libs/markoff-reading/stubs/`) once Phase C's DI seam lands.
- Move Corbomite-shaped types into Markoff (e.g., absorbing `Corbomite::Core::EmbedRegistry` as `Markoff::EmbedRegistry` behind an interface) when the interface design requires it.

The Corbomite agent should not, without a user check-in:

- Change Markoff's public API surface (class names in `Markoff::`, `Markoff::Source::`, `Markoff::Reading::`, or public header paths under `include/markoff/`) in a way that isn't already documented by a spec.
- Break the `v0.2.0` surface that CorbomiteApp currently builds against without landing both sides in the same pin bump.
- Vendor the `mmdr` Rust crate into Markoff (this decision was made in Phase B and should only be reopened via user ok).

## Invariants to preserve

These exist because the Corbomite agent is, by nature, focused on CorbomiteApp correctness. The invariants exist to keep Markoff from becoming a thinly-disguised Corbomite subdirectory.

1. **The standalone Markoff build works and passes all tests.** `cd /home/clinton/dev/Markoff && rm -rf build-dev && cmake -S . -B build-dev && cmake --build build-dev -j && cd build-dev && ctest` on a fresh checkout must succeed with zero external projects present. That means `MARKOFF_READING_USE_REAL_COREDEPS=OFF` (the default) remains buildable and testable as long as it exists; once Phase C retires the option, the same invariant applies to whatever replaces it.

2. **Markoff's public interfaces are host-agnostic.** When a Markoff type has a name that includes `Corbomite`, that's a bug — either the type should move to Corbomite, or the type should be renamed to describe what it does, not who uses it. The one current exception is the Phase B stubs under `libs/markoff-reading/stubs/corbomite/`, which deliberately mirror Corbomite's namespace for backward-compat; they retire in Phase C.

3. **Markoff's test suite is owned by Markoff.** Tests live in `libs/*/tests/` and `tests/markoff/`. They must pass on the standalone build. Tests that require Corbomite-provided concrete types should either (a) live on the Corbomite side, or (b) gate on the CMake option or its Phase C successor.

4. **Every Phase C work-unit tags a new Markoff version.** `v0.2.0` was the Phase A + Phase B-bridge deliverable. Each work-unit lands as a minor version bump (`v0.3.0`, `v0.4.0`, …) so Corbomite can pin to a specific milestone and roll back if one regresses. Never force-push a tag.

5. **Markoff's `master` is append-only.** No destructive operations. No force-push. If a merge goes wrong, a revert commit is the fix.

6. **The Corbomite agent commits to Markoff under the same user identity it commits to Corbomite under.** Co-author trailers identify the agent as the author; no need to pretend to be a different entity for the Markoff repo.

## Handoff artifacts

These documents constitute the complete picture. Read in this order:

1. **Phase A design + plan** — what was built and why:
   - `docs/specs/2026-04-20-tri-view-unified-api-design.md`
   - `docs/plans/2026-04-20-tri-view-phase-a.md`

2. **Phase B design + plan** — the bridge you just executed:
   - `docs/specs/2026-04-20-phase-b-corbomite-migration.md`
   - `docs/plans/2026-04-20-phase-b-corbomite-migration.md`

3. **This handoff doc** — scope of authority + invariants.

4. **`docs/phase-c-status.md`** — living status board for the four Phase C work-units. Update in every Phase C commit on either side.

5. **Per-library CLAUDE.md files**:
   - `libs/markoff/CLAUDE.md` (the live library; will be renamed `libs/markoff-live/CLAUDE.md` on its next touch — Phase A renamed the directory but the CLAUDE.md inside it still says "markoff")
   - `libs/markoff-core/CLAUDE.md`
   - `libs/markoff-source/CLAUDE.md` (Qutepart-specific; imported from Corbomite, keep the Qutepart conventions)
   - `libs/markoff-reading/CLAUDE.md` (you probably know this one already)
   - Markoff root has no top-level CLAUDE.md; the per-library ones + this handoff doc are the canon.

6. **The Phase C scope agreement** (a decision record, not a plan): four work-units in the order C1 → C3 → C2 → C4, each landing as a Markoff tag + a Corbomite pin bump. Rationale for the order is in the status doc.

## What's new about operating in Markoff

Differences from Corbomite's conventions:

- **CLAUDE.md scope rule**: `libs/markoff/CLAUDE.md` says "do not reason about Corbomite concerns; stay within this directory." That rule was written for a Markoff-only agent. As the Corbomite agent holding both repos, you override it for the specific case of designing interfaces that Corbomite will implement — but don't let that override creep into the Markoff library sources themselves (see invariant #2).
- **tr() usage**: Markoff uses `tr()` for user-visible strings and is translation-ready independent of any parent. Keep that when adding UI strings.
- **Test app**: `./build/bin/markoff-testapp [path/to/file.md]` runs the live editor standalone. Useful for visually verifying Phase C changes to `Markoff::Live` without launching CorbomiteApp.
- **Build dir**: Markoff uses `build-dev/` at the top level (legacy no-preset layout). The Phase A worktree at `.worktrees/tri-view-phase-a/` is preserved as a reference; don't delete it, but don't commit to it either.

## First actions recommended

1. Open `docs/phase-c-status.md` (just created), read the C1 → C3 → C2 → C4 ordering.
2. Draft the C1 spec in Markoff: interface shapes for `IEmbedRegistry`, `ICodeBlockProcessorRegistry`, `IVaultResourceProvider`, `IMermaidRenderer`, `IEmbedDepthGuard`, `IMarkdownRenderChildFactory`. Cross-check each against the real type it'll replace in `Corbomite::Core`. Flag any awkwardness in the spec before writing code.
3. Implement C1 in Markoff behind a new opt-in flag (`MARKOFF_READING_USE_DI_SEAM`, say) that leaves Phase B's `MARKOFF_READING_USE_REAL_COREDEPS` working. Tag `v0.3.0-alpha.1`.
4. Implement C1 adapters in Corbomite, bump pin, switch CorbomiteApp to the new flag.
5. Retire the Phase B flag + stubs in Markoff. Tag `v0.3.0`.

Phase C status, versioning, and the work-unit recipe live in `docs/phase-c-status.md` from here on.
