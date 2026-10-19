# Reply to the consolidated open asks (2026-10-18)

**From:** Markoff family maintainers
**To:** Corbomite
**Date:** 2026-10-19
**Re:** `Corbomite/docs/handoff/2026-10-18-to-markoff-consolidated-open-asks.md`

Status of every numbered ask, verified against the tree we ship. Where a row is
closed we say what closed it; where it is scheduled we name the order. The
internal triage with line-level evidence is
[`../plans/2026-10-19-corbomite-open-asks-triage.md`](../plans/2026-10-19-corbomite-open-asks-triage.md).

**Please re-verify the markoff-core rows.** Your evidence cites
`MarkoffDocument.cpp`, `MarkoffDocumentPrivate.h` and
`SourceTextDocumentBinding.cpp`, which do not exist at the pin `dca64686` —
the A13 rewrite replaced them with `MarkoffDocumentVNext.cpp` and
`SourceTextDocumentBindingVNext.cpp`. Several conclusions change as a result
(notably M2, M10, M12, M22). The collabtext rows were also taken from
collabtext `master` rather than the pinned alpha.5 commit; M1 and M29 are
already fixed in what we ship.

## Closed — no action needed on your side

| Ask | What settled it |
|---|---|
| M2 | The per-block pending-payload map no longer exists. One action is one MAB envelope carrying every primitive, routed by `routeRemoteAction`; the key collision you describe cannot occur. |
| M10 | `latestParse`/`parsedDocument`/`blockAnchorAt`/`blockByteRange`/`blockAt` were deleted with the rewrite. No consumer can get a confident wrong answer because the API is gone. |
| M22 | The process-global block-id counters went with the rewrite; `BlockId` is the engine's actor/counter identity. |
| M12 | `onSaveComplete`/`wantsAcksAtWatermark`/`ackedWatermark`/`collectGarbage` are gone; `save()` is `snapshotForExport` + `acknowledgeExport`, which records the export watermark without compacting. Physical collection is deliberately disabled. If you still measure growth, file it as a retention-policy question, not this mechanism. |
| M33 | Delivered as per-key `setFrontmatterValue`/`removeFrontmatterValue` over the frontmatter map, staged as a durable action. We added your acceptance test (`f7fe14f6`): one `undoD2()` restores the frontmatter bytes exactly, and redo re-applies them. |
| M1 | Fixed at the pinned alpha.5 commit: `try_remove_at`/`try_insert_after` return a typed `unresolvable_anchor`, `apply_concrete(RemoveOp)` returns false before observing the version, and the miss/out-of-order/legacy-no-mutation tests exist. |
| M29 | Replaced at the pinned commit: `Global::coverage_intersection()` is the watermark API, `Buffer::compact(watermark)` is deleted, `NetworkSim::compact_all` uses `collect_garbage`, and the 3-replica coverage tests exist. `Global::meet` remains the documented summary fold. |
| M38 | **Option 1 confirmed.** One card document, shared by paint and edit. The paint pass may hold a non-owning `const MarkoffDocument *`; the `renderInto` pointer overload is `const` and is the intended shape. A second `open()` for the same key still returns `writer_conflict` — that limit is per document key and by design. |
| M39 | **Deliberate.** Mid-block paste splits at the caret, trims the payload, and appends the tail to the last pasted block; `Cmd/D2.h` documents it and `smart_paste_inserts_typed_blocks_as_one_action` asserts it. Keep your flat-splice emulation. If the leading-whitespace trim blocks a concrete card-editor case, file that narrowly and we will treat it separately. |
| M37 (render half) | Agreed and already delivered: both `renderInto` overloads are public and `const`; no change requested. The embeddable-card half stays open below. |

## Fixed in this cycle

| Ask | Commit | What changed |
|---|---|---|
| M3 | `0d974f1b` | `parsedFrontmatter()` computes under `std::call_once`; concurrent readers can no longer see the empty window. |
| M4 | `2e3b54cf` | `parse()` discards every tree before touching the byte map; a failed parse is fail-closed. |
| M8 | `a910338d` | `MarkdownView` retires its document pointer on `destroyed()` and emits `documentChanged(nullptr)`; all eight holders are `QPointer`s. Deleting a document under a live view is now safe. |
| M9 | `08fa50b2` | `canUndoD2()`/`canRedoD2()`/`d2UndoDepth()` plus virtual `canUndo()`/`canRedo()` — drop your `canEdit()` proxy. |
| M11 | `253f1889` | Unsupported find flags are refused loudly and `supports(FindFlag)` queries them. Removing the enum values remains a decision, so this is additive. |
| M20 | `0a5425be` | `session()`/`setSession()` are on `MarkdownView`. |
| M21 | `750b2236` | The Source leaf destroys its owned session; the Styled leaf emits `sessionChanged` when the session dies. |
| M23 | `15093905` | Styled context reports Table + exact row/column inside a frame instead of a mis-mapped block. |
| M24 | `0cbd6deb` | Source copy restores list/quote markers via a shared helper. |
| M25 | `34c7e00c` | The Styled find adapter re-points on document swap. |
| M28 | `16e7bfe4` | Every test is labelled `unit`/`integration`. The manual `perf_load_bench` stays out of ctest by design (needs an input file, prints numbers, cannot fail). |
| M30 | `bc776efd` | Byte-to-char mapping validates UTF-8 sequences; Latin-1/truncated input no longer skews offsets. |
| M32 | `344288a7` | `PerfProbe` is compiled out unless `MARKOFF_PERF`; the in-loop site is gone; keys are literal addresses. |
| M34 | `49a12c0e` | The dead `Origin` enum is deleted, so the stale undo comment cannot mislead again. |
| M35 | `5c0e5c6a` | The closing fence must be a complete `---` line; `----`, `--- `, `---foo` no longer truncate the body; round-trip and empty-block cases are tested. |
| M36 | `76c44019` | `MARKOFF_BUILD_TESTS` is the single switch, seeded into every leaf gate, defaulting OFF for subdirectory consumers. |
| M16 (offset) | `3739dd7f` | Case-insensitive search no longer re-serialises the block prefix per match. |
| M27 (1–3) | earlier | Demo apps are gated by `MARKOFF_BUILD_APPS` (OFF for subdirectory consumers) and headers are installed. Item 4 (PUBLIC engine dependency) remains open. |

## Scheduled / open

- **Wave 3 (hot path):** M17's remaining throttle, M13 (cached flat view + block index), M18 (row index). These need before/after probe numbers, so they are not landing as drive-by changes.
- **M31:** incremental reparse; the largest item, after the map builder settles.
- **M7:** the largest visible gap we agree with — theme plumbed into Styled and ignored. It is a feature inside a leaf we keep bug-fix-only, so it needs an explicit exception task before code. Expect `Theme::operator==`, theme reads at every colour site, and theme in `computeBlockHash` as one change.
- **M37 (card half):** open; gated on a documented embedded mode and scroll ownership. M38/M39 answers remove the architectural blockers.
- **M26, M27 item 4:** packaging. Both are latent on this toolchain; M26 contradicts a recorded deliberate choice. Deferred until a non-submodule or MSVC consumer exists.
- **M14, M15, M19 and the Graffodil punch list:** separate repo. M5 (`79357fc`) and M6 (`fc778db`) are fixed with bug reports and regressions; the rest follows.
- **M28's doc-destroyed test:** landed with M8.

## Note for your pin

collabtext `master` now carries the engine line and every candidate tag is
published, including `collabtext-integration-2026-10-19` (`4d0177cd`). Markoff
keeps `collabtext-markoff-core-2026-10-13-alpha5` (`e8a8e7d1`): the integration
commit is a descendant, and the only `libs/` delta is capability-manifest
metadata, so our acceptance record still applies. Your
`build-collabtext-alpha5/install` prefix is unchanged and now recreatable.
