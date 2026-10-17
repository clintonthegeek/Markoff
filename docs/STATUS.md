# Markoff — live status

Updated 2026-10-17. One screen. Task detail lives on the
[CollabText task board](plans/2026-10-12-collabtext-adoption-execution.md).
The previous board text is
[`archive/collabtext-adoption/2026-10-16-status-board.md`](archive/collabtext-adoption/2026-10-16-status-board.md).

## Now

CollabText vNext adoption. The document and the Source, Styled, and Canvas
views already use the installed provider-backed engine. Corbomite stays
paused until A16.

**Current task: A14.** Acceptance record. Then A15 transition package and
the A16 signal. A12 sends remote carets as ephemeral `MPR/1` messages.
A10 keeps 100 undoable actions per actor. A13 removed the vendored engine,
the live leaf, and the unbuilt legacy branches.

## Pin

`collabtext-markoff-core-2026-10-13-alpha5` at
`e8a8e7d1a95dd50e23f38d6727921b47b814e44b` (0.2.0-alpha.5, manifest v17).
Root CMake requires that installed package. Recreate a missing prefix from
the candidate's `docs/USING_VNEXT.md`. The last machine-local prefix was
`/tmp/collabtext-alpha5-prefix-b`.

## Latest verification

2026-10-17, A12, parent `0a0b500f`: offscreen
`./build-dev/bin/tst_vnext_action_consumer` (6 passed) and
`./build-dev/bin/tst_canvas_vnext_session_positions` (12 passed).
Package `0.2.0-alpha.5`, manifest v17, prefix
`build-collabtext-alpha5/install`. The full suite and the installed-consumer
2/2 run are A14.

## Leaves

- `markoff-canvas` — primary editor. Open for this workfront.
- `markoff-core` — provider-backed `MarkoffDocument`. Open for this workfront.
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
