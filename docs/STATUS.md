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
and a seven-wave order. No wave has started. First actions: send the
answer-and-close reply (Wave 0), then the parser and build-hygiene fixes
(Wave 1).

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

**Publishing gap (found 2026-10-19).** That commit is the tip of
`feat/fuguemax-placement`; collabtext `master` does not contain it (329
commits short, plus 2 packaging commits it does not have). Neither the tag
nor the branch is on `origin` (GitHub lists only `master` and
`roadmap/reliability-v2`; Codeberg holds `master` at `046d375`). Until it
is pushed or merged and retagged, the pinned commit is reachable only from
a local clone. Publish it before treating the prefix as reproducible.

## Latest verification

2026-10-19: read-only audit of the Corbomite asks against Markoff
`dca64686`, Graffodil `011b342f`, and collabtext `e4779644`. No build and
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
