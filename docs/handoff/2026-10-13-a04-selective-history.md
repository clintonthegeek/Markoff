# A04 selective-history Core proposal

The Markoff A04 audit found that `UndoLog::undoForBlock` selects a historical
operation, while the alpha.4 consumer API can only undo or redo the latest local
group. Markoff needs to compensate a selected Core group while leaving unrelated
newer groups active.

The accompanying `2026-10-13-a04-selective-history.patch` is a focused prototype
against CollabText commit
`c0a0ef55ea9b6a3e448b9ef5c258f09933ebb127`. It adds `try_undo_group` and
`try_redo_group` to text and IdList engines. The API moves a selected group
between the active and redo regions of the existing local-history snapshot;
normal undo/redo continue to select the boundary group. It does not change wire
operations or snapshot encoding.

The API is committed and pinned by local tag
`collabtext-markoff-core-2026-10-13-alpha5` at
`e8a8e7d1a95dd50e23f38d6727921b47b814e44b`. The public API contract and
alpha.5/manifest v17 capability manifest document the change. Its focused
Debug and Release tests pass. The full CollabText Debug and Release suites pass
100/100 each. A relocated alpha.5 Core installation builds Markoff's current
consumer baseline, which passes 3/3. The affected tests also pass under ASan/UBSan
with leak detection disabled. LeakSanitizer aborts in this ptrace-restricted runner;
the full instrumented build also hit the temporary filesystem's 7.7 GB limit.

A Markoff consumer regression covers an isolated cross-stream action trace:
action A inserts block A and edits its text; action B inserts block B and edits
its text through the shared order stream. It selectively undoes A, verifies B stays
visible, replays all operations into fresh projections, restores per-stream owner
history and redoes A then B with the original identities. It passes as part of the
relocated consumer suite (3/3).

The absence-recovery trace also injects a definite pre-rename append failure.
The coordinator classifies it as absent, retires the exact preparation, rebuilds
the projection from the accepted prefix and commits the terminal map gap. This
trace passes in the same suite.

A second durable-provider regression publishes action A and action B as separate
MAB actions over one shared order stream, two block-text streams and a shared
Markoff map. It selectively compensates A across all three primitives in one undo
MAB action, including a fresh-stamp map tombstone. After provider reopen, it
replays all three accepted bundles into fresh projections, restores owner-private
list/text histories, and verifies block B and its map value remain while A's
effects are absent. MAI/2 preserves map prior state for undo after reopen, and the
coordinator checks every MAB operation against that action's history
associations. The undo append injects uncertainty immediately after its atomic
rename; the session is then poisoned, and after closing and reopening the
provider, recovery inspects that exact undo transaction and finishes
private-history persistence before replay. The coordinator also refuses a
subsequent write while the undo outcome is unresolved. This passes in the
relocated consumer suite (3/3). The trace now continues after recovery: it
rebuilds mutable adapters and owner history, selectively redoes A across list,
text and map with fresh identities, commits the redo MAB, then closes and
reopens a second time. Replay preserves both original block identities and
order, both text values, both map values and the redo owner history. Retirement
is recorded by the redo action, leaving the undone action eligible for redo.
The expanded relocated consumer suite passes 3/3.

A04 remains partial: the regression is adapter-level; MarkoffDocument's
production dispatcher still needs to capture the groups and construct the same
single undo action. A provider-level pre-rename prepare failure preserves the
exact reservation and allows the same transaction to retry after reopen;
coordinator-level allocation and prepare recovery remain open. The relocated
local package matches the pinned commit
and has archive hash
`a89dc8cf1b26831e8830af4fa1335abd3d3f6c1fb9e5f48b3d25455a6dac5612`; the tag
remains local and unpublished. A08/A10 depend on that acceptance.
