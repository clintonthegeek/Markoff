# D2 reset / reload: clear before rebuild

**Date:** 2026-05-25
**Status:** Design — approved 2026-05-25, awaiting plan.
**Driving consumer:** Corbomite (port-first micro-spec).
**Driving steer:** `~/dev/Corbomite/docs/handoff/2026-05-25-to-markoff-d2-clear-on-reset.md`.
**Severity:** P0/P1 — silent content corruption on save in wholesale-replace flows.
**Author:** port-driven; written against `master` tip `637c52b`.

---

## 1. Problem

`MarkoffDocument::resetContent()` and `loadFromMarkdown()` both rebuild
D2 block state via `buildD2FromBytes()` →
`materializeBlocksFromParsedDoc()`. That helper **appends** to the
underlying `IdList`, `kindTagMap`, `blockAttrsMap`, `blockBuffers`,
`blockLoadTimeBytes`, and `bufferProxies` with no pre-clear.

On a *fresh* document this is correct; on an *already-populated* document
it doubles the content. `serializeForSave()` then writes
**new content + stale old content** to disk.

The gap is documented in-code at `MarkoffDocument.cpp:741-746`:

> *"this populates D2 on top of any pre-existing D2 state without
> clearing first. […] For the wholesale-replace origins
> (ExternalReload*, UserRevertToSaved) on a non-fresh document, the
> right behavior is a full D2 wipe before rebuild; that requires IdList
> clear semantics the CRDT doesn't yet expose."*

Reproduced by three Corbomite headless integration tests (plain replace,
heading replace, unicode replace) — `original\nmodified\n` lands on disk
in every case.

### Affected origins

- `Origin::ExternalReloadClean`
- `Origin::ExternalReloadResolved`
- `Origin::UserRevertToSaved`
- `Origin::FirstOpen` / `Origin::TestFixture` *when called on a non-fresh
  document instance* (rare but observable).
- `loadFromMarkdown()` called more than once on the same document.

`Origin::UserEdit` is **not** affected (it goes through the incremental
`d2ApplyBufferEdit` path, not a wholesale rebuild).

---

## 2. Observable contract (the promise)

After `resetContent(B, origin)` *or* `loadFromMarkdown(B)` on a document
whose D2 currently holds content A:

1. `iterateBlocks()` returns only blocks parsed from B.
2. `serializeForSave()` writes exactly B, modulo the B1 inter-block
   separator (`"\n\n"`) and the canonical trailing `"\n"` from
   `finalDocumentTerminator()`.
3. `frontmatterValue()`, `iterateFootnoteDefs()`, link-ref entries, and
   `blockLoadTimeBytes()` reflect only B — no entries that originated
   from A survive.
4. Holds **regardless of whether the document was fresh** — `resetContent`
   and `loadFromMarkdown` are idempotent across N invocations.

The legacy `d->buffer` keeps its current behaviour (mega-edit replace for
`UserRevertToSaved`, full reset for the reload origins). That's
intentional — legacy undo of revert is preserved. Out of scope for this
fix.

---

## 3. Architecture

Two new primitives in collabtext; one new helper in markoff-core.

### 3.1 collabtext: `local_clear()` on the two CRDT classes

**`CollabText::Crdt::IdList::local_clear()`** — single-replica reset
primitive.

- Drops all entries (visible + tombstones).
- Clears the undo stack and the deferred-op queue.
- Leaves `replica_id` and the internal clock (lamport counter)
  **untouched**. Subsequent `insert_after` calls produce ops with
  monotonically-increasing stamps; no clock regression.
- Does **not** emit a `RemoteOp` and does **not** fire the
  `m_on_local_op` / `m_on_change` callbacks. (Callers that need to
  notify downstream listeners do so explicitly after the clear.)

**`CollabText::Crdt::CausalLwwMap<K,V>::local_clear()`** — same
semantics for the LWW map.

- Drops all entries (live + tombstoned), clears the undo stack.
- Leaves `replica_id` / clock untouched.
- Does **not** fire `m_onChange`.

Both methods are documented as **for use when the canonical content is
replaced from outside the CRDT** (file reload, revert-to-saved,
programmatic content swap). Calling them on a connected collab session
is allowed but the downstream effect on remote peers is undefined — peers
will not see the clear. Reconciling reset with collab is a higher-layer
concern (see §6 out-of-scope).

### 3.2 markoff-core: `MarkoffDocument::wipeD2State()`

Private helper on `MarkoffDocument`. Performs the equivalent of "make
this document instance behave like a freshly-constructed one" without
disturbing replica id, signal connections, or QObject identity.

```cpp
// Private. Resets all D2 in-memory state to the post-construction
// shape, without disturbing replica id, the legacy buffer, the undo
// log, or external signal connections. Used by resetContent() and
// loadFromMarkdown() before they rebuild D2 from new bytes; safe to
// call on a fresh document (no-op-ish).
//
// Single-replica only: this does NOT emit remote ops. A connected
// collab peer would not see the wipe. See spec §6.
void wipeD2State();
```

Sequence:

1. `d->idList.local_clear()`.
2. `d->kindTagMap.local_clear()`, `d->blockAttrsMap.local_clear()`,
   `d->frontmatterMap.local_clear()`, `d->linkRefMap.local_clear()`,
   `d->footnoteDefMap.local_clear()`.
3. `d->blockBuffers.clear()` (destroys the per-block `Buffer`
   instances).
4. `d->blockLoadTimeBytes.clear()`.
5. `d->blockEditSequences.clear()`, `d->touchedSinceLoad.clear()`,
   `d->structuralEditSequence = 0`.
6. `if (d->inlineCache) d->inlineCache->clear()`.
7. For each `bufferProxies[id]`: `proxy->deleteLater()`. Then
   `d->bufferProxies.clear()`. (QObject children of `this`; safe to
   delete-later, references in QML get nulled by the QObject
   destructor.)
8. `d->nextBlockId` is **not** reset — block IDs continue monotonically
   across wipes, preserving uniqueness if any external reference (e.g.
   a stale `BlockId` held by a view) is ever resolved against the
   document.

### 3.3 Data flow after the fix

```
resetContent(B, origin):
    bump editSequence
    update legacy d->buffer (existing per-origin code, unchanged)
    wipeD2State()              ← NEW; unconditional, all origins
    buildD2FromBytes(B)        ← unchanged
    emit documentReloaded / documentChanged
    scheduleD2Changed()

loadFromMarkdown(B):
    wipeD2State()              ← NEW; unconditional
    buildD2FromBytes(B)        ← unchanged
    emit documentLoaded / documentChanged
    scheduleD2Changed()
```

Unconditional wipe — including for `FirstOpen` / `TestFixture`, which
are typically called on a fresh document. The wipe is cheap on a fresh
document (every container is already empty) and removing per-origin
branching simplifies the contract.

---

## 4. Acceptance tests

### 4.1 markoff-core integration (extends `tst_d2_reset_content.cpp`)

The Corbomite steer's acceptance test:

```cpp
void TstD2ResetContent::nonFreshReset_replacesContent_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("original content\n");
    doc.resetContent("modified content\n", Origin::TestFixture);
    QCOMPARE(doc.serializeForSave(), QByteArray("modified content\n"));
}
```

Plus, parametrised across the three reproduction shapes from the steer:

| Round | Initial load | Reset to | Expected `serializeForSave()` |
|-------|-------------|----------|-------------------------------|
| 1 | `original content\n` | `modified content\n` | `modified content\n` |
| 2 | `# Note 1\n` | `# Modified Note 1\n` | `# Modified Note 1\n` |
| 3 | `日本語 café 🎉 résumé\n` | `日本語 café 🎉 résumé\n\nMore text\n` | `…\n\nMore text\n` |

Repeat each case for `Origin::ExternalReloadClean`,
`Origin::ExternalReloadResolved`, `Origin::UserRevertToSaved`,
`Origin::TestFixture`, and `Origin::FirstOpen`.

### 4.2 markoff-core: idempotent `loadFromMarkdown`

```cpp
void TstD2ResetContent::loadFromMarkdown_calledTwice_replacesNotAppends()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("first\n");
    doc.loadFromMarkdown("second\n");
    QCOMPARE(doc.serializeForSave(), QByteArray("second\n"));
    QCOMPARE(doc.iterateBlocks().size(), size_t{1});
}
```

### 4.3 markoff-core: sidecar maps cleared

For frontmatter, footnotes, and link-refs — confirm each is gone after
the wipe. Frontmatter has a public accessor; footnotes and link-refs do
not (the maps are private), so they're verified indirectly via
`serializeForSave()`, which is the consumer-relevant signal anyway.

```cpp
void TstD2ResetContent::reset_clearsFrontmatterFromPrior()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("---\ntitle: A\n---\n\nBody A\n");
    doc.resetContent("Body B with no frontmatter\n",
                     Origin::ExternalReloadClean);
    QVERIFY(!doc.frontmatterValue("raw").has_value());
    QCOMPARE(doc.serializeForSave(),
             QByteArray("Body B with no frontmatter\n"));
}

void TstD2ResetContent::reset_clearsFootnotesFromPrior()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Text[^1]\n\n[^1]: footnote A\n");
    doc.resetContent("Plain text\n", Origin::ExternalReloadClean);
    // Stale footnote def must not survive into the serialized output.
    QCOMPARE(doc.serializeForSave(), QByteArray("Plain text\n"));
}
```

### 4.4 collabtext: `local_clear` unit tests

For each CRDT, two tests:

1. **Wipe + reuse:** After `local_clear()`, the structure reports
   empty; subsequent `insert_after` / `setWithNextStamp` succeeds and
   produces an op whose stamp is strictly greater than any pre-clear
   stamp (no clock regression).
2. **Non-emitting:** Hook the `set_on_local_op` / `setOnChange`
   callback; verify it does **not** fire during `local_clear()`.

These prove the falsifiable invariant.

### 4.5 Falsification

Before landing the fix, run the new tests against unmodified
`buildD2FromBytes` to confirm they fail with the doubling signature.
Per `docs/INVARIANTS.md` invariant 4.

---

## 5. Implementation notes

### 5.1 `bufferProxies` lifetime

Each `BufferProxy` is a `QObject` with `this` as parent. QML may hold
references via `bufferProxy(id)` getter. `deleteLater()` is the safe
disposal — it lets the event loop unwind any in-flight signal emissions
before the QObject vanishes. Direct `delete` risks dangling pointers if a
proxy is mid-emit during the wipe.

### 5.2 Replica id and clock preservation

Both `local_clear()` primitives leave the lamport counter untouched.
This matters for hybrid scenarios — e.g. a document gets briefly
collab-synced, then disconnects, then the user does
`ExternalReloadClean`. A future re-connect needs the clock to still be
monotonic relative to the peers' record of this replica. Resetting the
clock would risk stamp collisions on reconnect.

### 5.3 `editSequence` vs `d2EditSequence`

The existing `resetContent` bumps `editSequence` (legacy). `wipeD2State`
should **not** bump either sequence — the immediately-following
`buildD2FromBytes` does its work, and `scheduleD2Changed()` fires the
deferred signal. Bumping mid-wipe would create a spurious "in-between"
state that no consumer should see.

### 5.4 Comment removal

Once landed, delete the four-paragraph caveat at
`MarkoffDocument.cpp:734-746` and the analogous note in
`tst_d2_reset_content.cpp:15-19` — they're now historical, not active
caveats.

---

## 6. Out of scope

### 6.1 D2 undo of revert

Current `Origin::UserRevertToSaved` is undoable in the **legacy
buffer** (the mega-edit is on the buffer undo stack). After this fix it
is **not** undoable in D2 — `wipeD2State` discards D2 undo state. This
matches typical editor "revert from disk" semantics (treated as a
checkpoint, not an undoable edit). If D2-undoable revert is later
desired, that's a separate spec needing an undoable-clear primitive on
the CRDTs.

### 6.2 Collab broadcast of resets

A connected peer that does `ExternalReloadClean` will not propagate the
reset to other peers — `local_clear()` is by definition non-emitting,
and the subsequent `buildD2FromBytes` produces local-only ops the peer
already has (or doesn't, if they diverge). Reconciling reset with collab
is a future "session reset" signal at a higher layer.

### 6.3 Tombstone compaction during normal editing

`local_clear()` happens to drop tombstones, but this fix doesn't address
tombstone bloat from normal editing. Separate concern.

### 6.4 Origin enum simplification

Now that all origins do the same wipe-then-rebuild, the per-origin
`switch` in `resetContent` is solely about the **legacy buffer**. Could
be tidied later; not part of this fix.

### 6.5 Stale comment at `MarkoffDocument.cpp:1851`

The comment block above `loadFromMarkdown` doesn't mention the
single-call-only constraint that the implementation actually has today.
After this fix the constraint goes away, so the comment update is
trivial — fold into the implementation work.

---

## 7. Cross-repo PR sequence

The fix spans collabtext (new CRDT primitives) and Markoff (new helper +
acceptance tests). Submodule pin discipline:

1. **collabtext:** branch, add `local_clear()` to `IdList` and
   `CausalLwwMap`, add unit tests, run collabtext's own test suite,
   commit, push to `master` on Codeberg.
2. **Markoff:** bump `libs/collabtext` submodule pin to the new
   collabtext tip in a dedicated commit (`chore(submodule): bump
   collabtext to <sha> for local_clear`).
3. **Markoff:** in a second commit, add `wipeD2State()` + acceptance
   tests + comment cleanup.
4. **Corbomite:** re-pins Markoff per its own steer; updates three
   integration tests for the now-correct (un-doubled) round-trip.

The Markoff side is two commits, not squashed — the submodule bump is
isolated so a bisect can identify the collabtext API change cleanly.

---

## 8. Invariants check (per `docs/INVARIANTS.md`)

- **Invariant 4 (falsifiable tests come first):** §4.5 explicitly
  requires running the new tests against unmodified code to confirm
  doubling-signature failure.
- **Invariant 3 (new authority retires old):** `wipeD2State` is the new
  authority for "make D2 forget everything". The old in-code TODO
  caveat at `MarkoffDocument.cpp:741-746` is retired in the same plan
  (§5.4).
- **Other invariants (1, 2, 5, 6, 7):** not implicated — this is not
  a focus/caret/block-content authority change.

---

## 9. References

- Corbomite steer: `~/dev/Corbomite/docs/handoff/2026-05-25-to-markoff-d2-clear-on-reset.md`
- Port-first session recap: `docs/handoff/2026-05-20-port-first-session-recap.md`
- B1 buffer convention: `docs/specs/2026-05-18-b1-buffer-convention-design.md`
- Save-path data loss handoff: `docs/handoff/2026-05-21-save-path-data-loss.md`
- Invariants: `docs/INVARIANTS.md`
- Existing test: `libs/markoff-core/tests/d2/tst_d2_reset_content.cpp`
- Affected code: `libs/markoff-core/src/MarkoffDocument.cpp` lines
  692-752 (`resetContent`), 1732-1820
  (`materializeBlocksFromParsedDoc`), 1822-1849 (`buildD2FromBytes`),
  1851-1862 (`loadFromMarkdown`).
