# C3 API shape: canonical buffer, external reload, and one future-phase hedge

Covers three things: (1) `textDocument()` under symmetric-B, (2) external-reload + undo, (3) a structural hedge that leaves a future CRDT integration path open without bloating C3 scope. Each section stands on its own; the third is the only one that adds new surface to C3 beyond what you asked about.

---

## 1. `textDocument()` under symmetric-B

Agreed on deprecate, and I'd push one step further on the shape of canonical before the spec lands.

### Deprecate, don't rename

"Optional view-projection helper" is a trap. Anyone who grabs the `QTextDocument*` can mutate it directly and bypass the `MarkdownDelta`-through-`QUndoStack` contract. You can't enforce "read-only, purely for projection" at the type system level — `QTextDocument*` is mutable, period. The moment one leaf wires `setDocument()` against it, you're back to privileged-Live coupling by the side door, and the symmetry you just locked in leaks out over six months of incremental commits.

Deprecate. Remove in C3. The C3 ownership-handoff is explicit that this is the window for API breakage; waste it and we carry the accessor until v1.0.

### One step further: drop the internal `QTextDocument` entirely

The implicit question behind deprecate-vs-rename is whether `MarkoffDocument` *internally* still owns a `QTextDocument`. You wrote "canonical content can be a `QString + Document *` pair; the QTextDocument accessor becomes vestigial." I'd make that "becomes" a "is" — **canonical is `QString` + `Document*`, no internal `QTextDocument` at all.**

Reasons:

1. **Footgun elimination by construction.** If there's no internal `QTextDocument`, there's nothing to expose, nothing to deprecate-then-accidentally-re-expose, nothing for a future contributor to plumb out "just this once."
2. **Weight.** A `QTextDocument` carries `QTextLayout`s, block structures, a root `QTextFrame`, format collections. None of that serves canonical if nothing renders from it. For a 100KB markdown file, the `QTextDocument` representation is several MB of layout state that's rebuilt on every structural edit.
3. **The existing `beginTransaction`/`endTransaction` API survives unchanged in shape.** Its semantics become "group the following `MarkdownDelta` pushes into one undo macro" instead of "group the following `QTextCursor` edits." The callers don't see the difference.
4. **Serialization**: we're markdown-native. We don't go through `QTextDocumentWriter`; we don't use `QTextDocument::toMarkdown()` (it has known fidelity gaps — the same round-trip bugs `libs/markoff-live/docs/05-options-and-tradeoffs.md` rejected Option B for). Our bytes are the source.
5. **Symmetry again.** Source's canonical-internal is Qutepart's buffer (not a `QTextDocument`). Reading's canonical-internal is the AST (not a `QTextDocument`). `MarkoffDocument`'s canonical-internal being `QString + Document*` completes the pattern: nobody in the system treats `QTextDocument` as a canonical representation; each leaf's `QTextDocument` (if it has one at all — Source and Reading don't) is private scratch view state.

### Migration path for existing `textDocument()` callers

Worth auditing in the spec. Likely consumers and their migration:

| Current use | Migration |
|---|---|
| `markoff->textDocument()->toPlainText()` | `markoff->toMarkdown()` returning the canonical `QString` |
| `markoff->textDocument()->find(...)` | Search against canonical `QString` (or via `SearchController` in `markoff-core`, which should be the sanctioned path anyway) |
| `editor->setDocument(markoff->textDocument())` | Delete. This is exactly the privileged-coupling pattern symmetric-B rules out. |
| `connect(markoff->textDocument(), &QTextDocument::contentsChanged, ...)` | `connect(markoff, &MarkoffDocument::contentsChanged, ...)` — canonical signal lives on `MarkoffDocument`, not on a leaked inner object |

If any of these have consumers we don't control (Corbomite app, test app, third parties under GPL), flag them in the handoff. I'd expect the count to be low — we can grep it out before the spec lands.

### Contract decisions worth recording in the C3 spec

1. **Signal vocabulary on `MarkoffDocument`.** At minimum: `contentsChanged(offset, removed, inserted)` (bytewise delta) and `parseUpdated(Document*)` (AST ready). Plus `documentReloaded()` — see §2.
2. **Edit entrypoints.** Do leaves call `markoff->pushDelta(offset, removed, inserted)` directly, or does every leaf route through `QUndoStack::push(new MarkdownDelta(...))`? Prefer the command-push form — it's harder to call wrongly, and `beginTransaction` becomes `QUndoStack::beginMacro` verbatim.
3. **Read-vs-write boundary.** Leaves get `const QString& toMarkdown() const` and `const Document* parsedDocument() const` for reads. Writes are *only* through the command push API. Enforces the contract at the type system level — which `textDocument()` specifically can't.
4. **Parser debouncing ownership.** Whose concern is it that reparse doesn't fire on every keystroke? `MarkoffDocument`'s (one debouncer, fires `parseUpdated` on settle) or the leaves' (each re-renders when it wants)? The former is symmetric; the latter distributes the debounce policy across three places. Prefer the former.

---

## 2. External-reload + undo

(c) is the right base, but the non-dirty branch needs a sharper argument than "louder but safer" and the dirty branch has a post-modal loose end worth nailing down now.

### Vote: (c), reasoning reframed

The framing "undo is my-edit history, not external-event history" is correct but under-sells itself. Here's the stronger version.

**(a) quietly corrupts the on-disk relationship.** Consider: user has the file open, clean. External writer changes disk. We push a mega-delta onto the stack. User hits Ctrl+Z — in-memory content reverts to pre-external state. In-memory ≠ disk. User keeps working, eventually Ctrl+S. We now write the pre-external content back, silently stomping the external change. The user has no signal that they just clobbered someone else's edit; the undo button was presented as "revert my mistake," not "restore the document to what it used to be before `git pull`." Autosave makes this worse — the stomp happens on a debounce timer without user action at all.

**(b) is a footgun for the dirty case.** Wiping the stack of dirty edits loses user work silently. Non-starter.

**(c) is the only option that preserves the invariant "undo reverses user-intent actions performed inside this editor."** Dirty → modal → explicit user resolution (which might itself be destructive, but it's *chosen*). Clean → stack clear, content swap. Undo after reload reverses nothing the user did; it reverses nothing at all. Consistent with the mental model.

### Refinements worth adding to the spec

**Post-merge-modal resolution also clears the undo stack.** The dirty-reload path goes: detect external change, detect dirty, open merge modal, user picks {keep-mine, take-theirs, show-diff-and-merge}. Whatever they pick, the resolved in-memory content is not a delta-reversible state. "Keep mine" means the in-memory buffer is the dirty content but disk is now theirs. "Take theirs" means we discarded the user's work. "Merge" means the buffer is a bespoke blend. After any of these, clearing the undo stack is the only coherent option. Otherwise Ctrl+Z post-resolution replays pre-modal state transitions that no longer make sense relative to disk.

**Add a `MarkoffDocument::documentReloaded()` signal.** Regardless of (c)-clean or (c)-dirty-resolved, leaves need to know the buffer just got replaced wholesale — not edit-by-edit — so they can:

- Discard any in-flight edit batches (Live's 150ms reparse debounce, Source's IME composition, any partial `beginTransaction` scope).
- Reset search/replace state (selection highlights, current-match cursor).
- Reset fold state or reconcile it against new content.
- Reset scroll position (or preserve it at visual-line level, per existing contract).

This signal is distinct from `contentsChanged(offset, removed, inserted)`. A plain-content-change signal with the delta being "replace everything" technically carries the information, but leaves would have to pattern-match on "delta spans whole document" to distinguish reload from a very large single-edit — fragile. A dedicated signal says what it means. Cost: trivial. Fires from exactly one place: the external-reload codepath inside whatever replaces `NoteDocument::setMarkdown`.

**Be explicit that "external reload" means "canonical replaced from outside the undo stack".** The spec will almost certainly grow a programmatic `MarkoffDocument::setMarkdown(QString, Origin)` used by:

- File load on first open
- External reload (the Q3 case)
- Possibly "discard and revert to last saved" (a user action)
- Possibly test fixtures

Each of those wants different stack semantics:

| Origin | Stack behaviour |
|---|---|
| First-open load | Stack is already empty; load populates; no command pushed |
| External reload (clean) | Clear stack (Q3 answer) |
| External reload (dirty, post-modal) | Clear stack (refinement above) |
| User-initiated "revert to saved" | **Open question** — (a)-like mega-command seems right here because the user *asked for it* and expects Ctrl+Z to undo their own revert |
| Test fixture | Clear stack |

So the `Origin` enum or equivalent should be on the API from day one. Hardcoding "setMarkdown always clears" closes off the revert-to-saved case, which will come up.

**Echo suppression under symmetric-B is actually simpler than today.** You already noted this: the flag flips at `Vault::saveDocument(doc)` boundary, which doesn't move. But under symmetric-B it gets *cleaner* — canonical is a `QString`, so `saveDocument` is a trivial `QFile::write(canonicalMarkdown)`. No `QTextDocumentWriter`, no format coercion, no "did the writer normalise anything." The bytes-in-memory equal the bytes-on-disk byte-for-byte, which makes the echo-suppression check sharper: if the post-write watcher event reads disk and the bytes equal canonical, suppress even without the flag (defence in depth against flag-leak bugs). Worth a one-line note in the spec; not load-bearing.

### Sync-chattiness note

Sync services (Obsidian Sync, Dropbox, iCloud, git auto-sync) fire external-reload events *chattily*. Under (c)-clean, every sync-down wipes undo history even when the reload was a no-op diff (or a diff in a part of the doc the user wasn't editing). For an Obsidian clone this might be more disruptive than (c) suggests on paper. Not a Q3 blocker — (c) is still right as the baseline behaviour for Phase C. See §3 for the longer answer.

---

## 3. Structural hedge: leave the door open for a CRDT-backed canonical

This is the one section that adds C3 surface beyond what you asked about. Skip if you're happy with §1 + §2 and want C3 to stay strictly scoped; read if you want the sync-chattiness problem solvable later without re-architecting.

### Why

`~/dev/collabtext/` is a plain-text offline-first CRDT engine (C++20, Qt-free core) specifically designed for the Syncthing/Dropbox/NAS case. Plain text only — which is exactly what our canonical is. The *engine* is production-ready; the *editor integration* is specified-not-implemented, which means we would be first consumer but nobody has baked integration opinions in.

Concretely: it solves the problem where two Corbomite clients edit the same note on two machines while offline, and Syncthing later reconciles them without creating `.sync-conflict` files or silent overwrites. Obsidian's answer to this problem is the $8/month Obsidian Sync service; a file-sync-transport CRDT is the kind of feature that could form a real wedge against the incumbent rather than competing on plugin parity. Future Phase E, not C3 scope.

**What we want in C3: a shape that doesn't preclude the swap.** The substitution is small in principle — canonical goes from `QString` to `collabtext::Buffer`, `MarkdownDelta::redo()` goes from `canonical.replace(...)` to `buffer.apply_local_edit(...)`, cursor positions go from ints to anchors — but it's only small if the C3 API is shaped to accommodate it. Two specific adjustments do most of the work.

### Adjustment 1: `CanonicalBuffer` interface, not a `QString` field

`MarkoffDocument` internally owns a `std::unique_ptr<CanonicalBuffer>`, not a `QString`:

```cpp
namespace Markoff {

enum class Bias { Left, Right };

class CanonicalBuffer {
public:
    virtual ~CanonicalBuffer() = default;

    // Read
    virtual QString   toMarkdown() const = 0;
    virtual qsizetype length() const = 0;
    virtual QString   substring(qsizetype offset, qsizetype length) const = 0;

    // Write: a single delta, from MarkdownDelta::redo/undo or reset()
    virtual void applyDelta(qsizetype offset,
                            qsizetype removedLength,
                            const QString& inserted) = 0;
    virtual void reset(const QString& newContent) = 0;

    // Anchors (see adjustment 2)
    virtual quint64   createAnchor(qsizetype offset, Bias bias) = 0;
    virtual qsizetype resolveAnchor(quint64 handle) const = 0;
    virtual void      releaseAnchor(quint64 handle) = 0;
};

} // namespace Markoff
```

C3 ships one concrete implementation: `InMemoryCanonicalBuffer`, backed by `QString` and a small anchor-bookkeeping table. Phase E (if we take it) ships a second: `CrdtCanonicalBuffer`, backed by `collabtext::Buffer`, with the same interface. `MarkoffDocument`'s constructor takes an `std::unique_ptr<CanonicalBuffer>`; the default factory returns an `InMemoryCanonicalBuffer`.

No leaf sees either concrete. `MarkoffDocument`'s public API (`toMarkdown()`, `pushDelta(...)` via `QUndoStack`, signals) is unchanged. The interface is small enough that writing it costs effectively nothing in C3, and it erases the "rip out canonical storage" class of migration work from a future phase.

### Adjustment 2: `CursorPosition` as an opaque handle, not an int

```cpp
namespace Markoff {

class CursorPosition {
public:
    CursorPosition() = default;
    bool isValid() const;
private:
    friend class MarkoffDocument;
    std::shared_ptr<void> m_holder;   // RAII; releases anchor on dtor
    quint64 m_handle = 0;
};

} // namespace Markoff
```

`MarkoffDocument` gains:
- `trackCursor(qsizetype offset, Bias bias) -> CursorPosition`
- `resolveCursor(CursorPosition) const -> qsizetype`

Scope guidance: **leaves that need to preserve cursor state across edits use `CursorPosition`; transient positions stay as `int`.** Persistent sites are things like `SearchController`'s current-match position, the "remembered scroll position at visual-line granularity," any fold-state cursor anchoring, any batched-edit cursor that survives a debounce window. Click-cursor-here-now is a transient int. Public API on `Editor` (`cursorLine()`, `goToLine(int)`, etc.) is unchanged — those are point-in-time queries.

**This is not Phase-E-only work.** `InMemoryCanonicalBuffer::applyDelta` has to adjust outstanding anchors on every edit regardless — that's the per-leaf offset translation the Phase-A-deferred plumbing needs anyway. Centralising it in `CanonicalBuffer` as "anchor management" is the same code with a better shape, AND it's Phase-E-compatible. Net new work: the `CursorPosition` struct and two document methods. Net avoided work: the offset-translation tables we'd otherwise distribute across the leaves.

### What this adjustment does NOT commit us to

- **Not committing to Phase E.** We can ship C3 with `InMemoryCanonicalBuffer` and never write another implementation. The interface is reusable even without CRDT (e.g., a mmap-backed buffer for large documents is another hypothetical concrete).
- **Not committing to CRDT on disk.** Even if Phase E happens, the storage format is a separate decision (full-CRDT-folder vs. markdown-plus-sidecar vs. CRDT-in-memory-only). C3 doesn't need to preempt it.
- **Not committing to changing the leaf contract.** Leaves still get `contentsChanged(offset, removed, inserted)` and `parseUpdated(Document*)`. The CRDT wrapper, if we build it, emits these signals synthesised from its internal state transitions; leaves don't know.
- **Not committing to real-time collaboration.** Live multi-user editing is a possible Phase-F product-line decision on top of Phase E; nothing about the C3 adjustment forces a network stack into the roadmap.

### What it DOES commit to

Two new types in `markoff-core` (`CanonicalBuffer`, `CursorPosition`), one concrete implementation (`InMemoryCanonicalBuffer`), and a discipline in the leaves about when to use `CursorPosition` vs. raw int offsets. That's it. None of this bloats C3 — the anchor-management code was going to get written in C3 anyway, as the "offset translation" half of the Phase-A-deferred work. This just puts it behind an interface that accepts a future swap.

### Decision

Three asks, in decreasing order of confidence that they're right:

1. **Adopt `CanonicalBuffer`.** Low cost, clean shape, door-open. I'd take this regardless of whether Phase E is ever actually on the roadmap.
2. **Adopt `CursorPosition` for persistent cursor state.** Zero net cost — the machinery exists regardless — and preserves optionality.
3. **Put a one-page "Phase E: CRDT-backed canonical" teaser in `docs/specs/` or the status board.** Just enough to hold the idea without committing. Blocks nothing, records the design intent so a future contributor doesn't accidentally unpick the abstractions thinking they're gratuitous.

Happy to turn any of this into spec text when you're ready to draft, or hold until you've got the C3 spec skeleton and slot it in. Say the word.
