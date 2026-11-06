# To Corbomite — Markoff per-keystroke perf pass + the SessionMode API change

**From:** Markoff maintainers
**Date:** 2026-11-06
**Re:** fast-typing performance for single-user sessions, and one new public API surface

## Why

Markoff users typing at ~100 WPM on slower machines saw per-keystroke lag that
grew over a session. The engine pin move (2026-10-21) fixed the engine's share
(18.56→2.96 ms/keystroke fresh, slope ~4× shallower). This pass attacked the
Markoff-side wrapping, which had its own O(action-history) and O(document)
per-keystroke work. No behavior contracts moved; one new API surface was added.

## What changed on the typing path (no API movement)

| Commit | Change | Per-keystroke effect |
|---|---|---|
| `e5c953f3` | Dropped the post-edit `flushPendingD2Changed()` calls in canvas (printable, undo/redo, paste, insertText, IME) | Every keystroke re-ran the full `onDocumentChanged` pass — an O(blocks) cache sync, viewport re-realize and caret resolve — a second time. The flush is a plain re-emit (`c3316f9`); the core debounce it drained no longer exists. `flushPendingD2Changed` itself is unchanged and remains the supported attach/population call. |
| `0690b3e7` | Delivery sequence cached in the lifecycle | Every staged action decoded every accepted MAB/3 bundle to compute `max(deliverySequence)+1` — O(actions) per keystroke. Now a count-keyed cache, advanced in memory on the verified fast path, rescanned after remote accepts/recovery. |
| `fc84f97f` | `applyBlockEdit` reads recovery state in memory | One fewer full accepted-prefix copy per keystroke. The coordinator's own pre/post-commit re-reads are durability protocol and unchanged. |
| `01c6dda9` | O(1) visible-block membership | `applyBlockEdit` and `inlineSpansFor` rebuilt the whole visible list per keystroke. A `visibilityGeneration()` counter (bumped only on idList-bearing commits, remote accepts, recovery rebuilds) keys a cached set. |
| `ad329de9` | `BlockLayoutCache::sync` single-block fast path | Content-only edits restyled one block but rebuilt the entire entry vector, index hash and position prefix-sum. Fast path restyles in place and re-prefix-sums from the first changed entry. |
| `888a3626` | Caret-invariant inline rebuild skip | The edited block's inline layout was rebuilt twice per keystroke (realize + setCaret's restyle) with identical inputs. Blocks with no delimiter spans, no caret-substituted render (code fence/mermaid/display math) and no preedit now skip the second rebuild. Delimiter/code/math/preedit blocks rebuild exactly as before. |

Measured expectation: the O(history) decode and set-building terms are gone from
the per-keystroke path; the remaining per-action O(history) work is the owner
history re-encode and the provider's durable commit — both are publication
protocol (see "Still open" below).

## The API change: `SessionMode`

```cpp
enum class Markoff::VNext::SessionMode { collaborative, local };

auto doc = MarkoffDocument::open(provider, scope, StorageMode::durable,
                                 /*registry=*/nullptr,
                                 /*sessionMode=*/VNext::SessionMode::local);
doc->sessionMode();  // accessor
```

- **Default is `collaborative`** — every existing caller is source-compatible
  unchanged. The installed API moves (new enum + symbols + a defaulted
  parameter), so a re-pin to the next Markoff tag is a recompile, not an
  adoption task. Per the release-tag convention, the next tag must carry
  `API: moved (new Markoff::VNext::SessionMode, MarkoffDocument::open gains a
  defaulted sessionMode parameter, new sessionMode() accessor)`.
- **`local` declares a single-writer session** that never receives remote
  actions. What it skips: the per-action replication self-validation in the
  coordinator — the staged-MAB-vs-owner-history decode comparison and the
  accepted-prefix revision-chain re-validation (O(history) decode plus two
  `std::set` builds per keystroke). What it does **not** change: durability,
  undo, block identity, edit sequences, crash recovery — all identical. Undo
  after reopen still works; M06-style recovery semantics are untouched.
- Remote actions offered to a local session are refused outright
  (`unsupported_operation`) at both `acceptRemoteAction` and
  `routeRemoteAction` — nothing is quarantined or deferred. To collaborate,
  reopen the document in collaborative mode.
- **Recommendation for single-user fast typing:** open with
  `SessionMode::local`. The pin stays `collabtext-perf-checkpoint-2026-10-21`
  unless the acceptance in flight (below) passes and is recorded.

## Still open (deliberately not done here)

1. **Per-action owner-history re-encode + durable persist.** The largest
   remaining per-keystroke O(history) term. Holding the owner-history index in
   memory and persisting it incrementally touches the durable publication
   protocol — it needs collabtext-side work (an incremental/append-only owner
   history) and its own acceptance record. Ask filed upstream with this handoff.
2. **`try_anchor_of` indexed lookup** (carried from the 2026-10-21 note):
   the engine still enumerates the whole visible list before every
   insert-path anchor lookup.
3. **Presence tokens**: two `boundary_at` calls per caret move survive in all
   modes; gating them needs a plain-position fallback in `Selection` and is
   not safe to slip in untested.
4. **Hidden source/styled views**: `SourceTextDocumentBinding` and
   `StyleApplier` do full-document work per keystroke with no visibility
   gate. A naive `isVisible()` gate desyncs hidden panes (their QTextDocument
   is the editing model); the correct design is a dirty flag + resync-on-show,
   which changes host-observed behavior and wants a joint decision.

## Verification

Offscreen fast suite `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
passed **101/101** after every commit (wire `build-dev`), including the two new
local-mode tests (`local_session_edits_undoes_and_recovers`,
`open_defaults_to_collaborative_session_mode` in `tst_foundation_markoff_document`).
The canvas gate (C1–C4) is unchanged.
