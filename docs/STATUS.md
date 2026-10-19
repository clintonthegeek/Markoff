# Markoff — live status

Updated 2026-10-19. One screen. The current task list is the
[Corbomite open-asks board](plans/2026-10-19-corbomite-open-asks-triage.md).
The closed CollabText board is
[`plans/2026-10-12-collabtext-adoption-execution.md`](plans/2026-10-12-collabtext-adoption-execution.md).

## Now

CollabText vNext adoption is closed; its readiness signal stands at
[`docs/handoff/2026-10-17-to-corbomite-ready-for-collabtext-transition.md`](handoff/2026-10-17-to-corbomite-ready-for-collabtext-transition.md),
and host storage, the lease, application wiring, and production acceptance
stay in Corbomite.

The new workfront is Corbomite's consolidated asks of 2026-10-18. The triage
board records a per-ask verdict against this tree, the already-closed rows,
and a seven-wave order. **Waves 1 and 2 are complete** (Wave 1: M3, M4, M35, M33, M36, M32, M34,
M28 — `0d974f1b`..`16e7bfe4`; Wave 2: M8, M21, M20, M9, M23, M25, M24,
M11 — `a910338d`..`253f1889`); the pin publication (Wave 0b) is done and the
pin stays `e8a8e7d1`. M30 (parser UTF-8 validation) landed ahead of Wave 3, and M16's offset half
landed. Wave 0's answer-and-close reply, the rest of Wave 3, M31 and Waves 5–6
have not started. Graffodil's M5 and M6 are done in that repo. M11's enum disposition (implement or remove the three unimplemented
flags) stays a decision.

The triage corrected the ask document's own evidence: its markoff-core
`file:line` citations predate A13 and name files that do not exist at
`dca64686`. M2, M10, M22 and M12's mechanism are already gone; M17, M18,
M27, M28 and M33 are partly or differently delivered.

Physical collection stays disabled. A14's probe budget misses stay recorded
and are not waived.

## Pin

`collabtext-markoff-core-2026-10-13-alpha5` at
`e8a8e7d1a95dd50e23f38d6727921b47b814e44b` (0.2.0-alpha.5, manifest v17).
Root CMake requires that installed package. The prefix used for the
acceptance runs is `build-collabtext-alpha5/install`. Recreate a missing
prefix from the candidate's `docs/USING_VNEXT.md`.

**Published 2026-10-19.** The tag and its branch are now on `origin`, and
`origin/master` contains the commit too, so the prefix is recreatable on a
fresh clone. `master` also carries the annotated integration tag
`collabtext-integration-2026-10-19` = `4d0177cd6c3f8802ec031327d0f7d9f314a97e6f`.
The pin stays at `e8a8e7d1`: the integration commit is a descendant of it, and
the only `libs/` difference is three lines of capability-manifest metadata, so
the A14/A15 acceptance record still applies. Move the pin to the integration
tag when a Core change needs a fresh acceptance run. Inbound handoff:
[`docs/handoff/2026-10-19-collabtext-master-integration-pin.md`](handoff/2026-10-19-collabtext-master-integration-pin.md).

## Latest verification

2026-10-19, through M16: offscreen `scripts/run-tests.sh` passed **95/95** in
68.69 s on `3739dd7f` (wire `build-dev`, prefix `build-collabtext-alpha5/install`).

2026-10-19, Waves 1–2 and M30: offscreen `scripts/run-tests.sh` passed **95/95** in
39.95 s on `bc776efd` (wire `build-dev`, prefix `build-collabtext-alpha5/install`).
Also checked: `-DMARKOFF_BUILD_TESTS=OFF` configures with zero ctest entries;
a `-DMARKOFF_PERF` build compiles parser and core.

2026-10-19: collabtext refs re-checked after the integration push —
`origin/master` contains `e8a8e7d1`, and the alpha.5 and integration tags
are on `origin` as annotated tags. Read-only: no build, no test run.

2026-10-19: read-only audit of the Corbomite asks against Markoff
`dca64686`, Graffodil `011b342f`, and collabtext at the pin `e8a8e7d1`
plus the then-local `master` `e4779644`. No build and
no test run; the triage makes source claims only, and each fix wave carries
its own verification.

2026-10-17, A16: documentation only. The signal above is the delivery.
The suite was not re-run.

2026-10-17, A15: offscreen `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
on `build-dev` passed 94/94 in 28.09 s, including
`markoff_provider_interface_conformance`. Package `0.2.0-alpha.5`,
manifest v17, prefix `build-collabtext-alpha5/install`. A14's Debug,
Release, ASan/UBSan 93/93 and installed-consumer 2/2 runs stand. The
probe budget misses stand with them.

## Leaves

- `markoff-canvas` — primary editor. No open adoption task.
- `markoff-core` — provider-backed `MarkoffDocument`. No open adoption task.
- `markoff-parser` — load-time and per-block parsing.
- `markoff-source` — permanent, bug-fix only outside this migration.
- `markoff-styled` — bug-fix only; backs Corbomite reading mode.
- `markoff-live` — removed in A13. Tag `archive/markoff-live-final` keeps the
  last supported snapshot.

## Not this workfront

Closed arcs and their plans: canvas production (2026-08-15), G1 accessibility
(2026-09-30), Hologram first-heading hide (`9b0138f8`). The manual Orca pass
stays deferred until the user asks for it. Older dormant canvas notes are in
the archived status board and in [`queue.md`](queue.md). `queue.md` is not
the active task list.

## When you stop

Update the task row, the "Now" section above, and the verification line if
you ran tests. Commit that doc update with the code. Session routine:
[`../AGENTS.md`](../AGENTS.md).
