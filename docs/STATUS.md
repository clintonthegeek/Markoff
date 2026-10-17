# Markoff — live status

Updated 2026-10-17. One screen. Task detail lives on the
[CollabText task board](plans/2026-10-12-collabtext-adoption-execution.md).
The previous board text is
[`archive/collabtext-adoption/2026-10-16-status-board.md`](archive/collabtext-adoption/2026-10-16-status-board.md).

## Now

CollabText vNext adoption. The document and the Source, Styled, and Canvas
views already use the installed provider-backed engine. Corbomite stays
paused until A16.

**Current task: A13.** Remove dead legacy engine branches from the libraries
the root build compiles. Next is A10, local-history retirement. Then A12
presence, A14 acceptance, A15 transition package, A16 signal.

## Pin

`collabtext-markoff-core-2026-10-13-alpha5` at
`e8a8e7d1a95dd50e23f38d6727921b47b814e44b` (0.2.0-alpha.5, manifest v17).
Root CMake requires that installed package. Recreate a missing prefix from
the candidate's `docs/USING_VNEXT.md`. The last machine-local prefix was
`/tmp/collabtext-alpha5-prefix-b`.

## Latest verification

2026-10-16, at `6fbe26f0`: candidate Debug build, offscreen CTest 93/93;
installed Core and view consumers 2/2. A 2026-10-17 source read confirmed
A01–A09 against the tree and did not re-run the suite. The next code change
records a new result on the task board.

## Leaves

- `markoff-canvas` — primary editor. Open for this workfront.
- `markoff-core` — provider-backed `MarkoffDocument`. Open for this workfront.
- `markoff-parser` — load-time and per-block parsing.
- `markoff-source` — permanent, bug-fix only outside this migration.
- `markoff-styled` — bug-fix only; backs Corbomite reading mode.
- `markoff-live` — retired 2026-08-19, excluded from the root build. Ignore it.

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
