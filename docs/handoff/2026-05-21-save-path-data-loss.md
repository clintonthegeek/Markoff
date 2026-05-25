# 2026-05-21 — Save path data loss: `toMarkdown()` reads the wrong store

**Discovered by:** Corbomite Vault save path against Markoff
`exploration/new-foundation` HEAD `bb2e5c0`, after the smoke-test
dogfood of the 2026-05-20 port-first session's pin bump.

**Symptom:** a testvault file (`testvaults/starter-vault/PKM LM/Keyboard
Hotkeys and The Command Pallette.md`) was found emptied — 60 lines →
0 lines — in the Corbomite working tree after a single manual session
exercising the new EditorWidget + Find UI. No crash, no warning, no
indication anything was wrong. The user noticed only because git status
showed the modification.

## Root cause — dual-store split that the save path falls into

`MarkoffDocument` carries two parallel content stores:

1. **`d->buffer`** — `CollabText::Crdt::Buffer`. The legacy whole-document
   flat-text store. Populated by `resetContent()` (since `861196c`'s fix
   it also rebuilds D2). Mutated by legacy `undo()`/`redo()`. Read by
   `version()`. Read by `toMarkdownUtf8()` / `toMarkdown()`.
2. **D2 per-block state** — `idList` + per-block `blockBuffers` +
   sibling causal-LWW maps (`blockKindMap`, `blockAttrsMap`,
   `frontmatterMap`, `footnoteDefMap`). The new canonical store.
   Populated by `loadFromMarkdown()` (via `buildD2FromBytes`). Mutated
   by every D2 edit path (`d2ApplyBufferEdit`, `applyFlatEdit`,
   block-level `Cmd::*`). Read by `iterateBlocks()`, `blockText()`,
   the view bindings (`LiveListModelBinding`, `LiveBlockModel`), and
   `serializeForSave()`.

**`loadFromMarkdown()` does NOT touch `d->buffer`.** It only calls
`buildD2FromBytes()`. So after a load via `loadFromMarkdown`, `d->buffer`
is empty. All editing flows mutate D2, never `d->buffer`. So `d->buffer`
stays empty for the document's lifetime.

The save path goes:

```
Vault::saveDocument(doc)
  → doc->markdown()                              // QString
    → NoteDocument::markdown()
      → d->markoff->toMarkdown()
        → QString::fromUtf8(toMarkdownUtf8())
          → d->buffer.text()                     // empty
  → modify(file, markdown.toUtf8())              // writes "" to disk
```

This is the deterministic data-loss path for any D2-loaded document.

A second Vault callsite hits the same trap at `Vault.cpp:833` — the
disk-compare used for external-change detection compares
`doc->markoff()->toMarkdown().toUtf8()` (empty) against disk bytes
(real), so any save will look like the doc has external-changes to
reconcile against, which it doesn't.

## Why the pre-`875f852c` Corbomite path "didn't" lose data

The recap doc records that Corbomite's first integration used
`resetContent` for first-open; that left D2 empty and rendered nothing.
The workaround in `875f852c` switched to `loadFromMarkdown`, which fixed
the render but switched on the data-loss trap.

After `861196c` (resetContent populates D2): both load paths produce a
fully populated D2. But neither path keeps `d->buffer` in sync with
subsequent edits — the dual-store split is permanent for any document
that has been edited.

## The right answer already exists — `serializeForSave()`

`MarkoffDocument::serializeForSave()` is the canonical D2-aware save
serializer. It walks `iterateBlocks()`, uses per-kind serializers,
applies the B1 inter-block separator convention
(`interBlockSeparator() == "\n\n"`, `finalDocumentTerminator() == "\n"`),
handles frontmatter and listitem markers, and uses `blockLoadTimeBytes`
for byte-identical round-trip on untouched blocks.

Tests already use it (e.g. `tst_live_render_setext_e2e.cpp:101`).
Corbomite just never migrated.

## Fix landing in this commit

**Markoff side (this commit):**

1. `[[deprecated]]` annotation on `toMarkdown()` and `toMarkdownUtf8()`
   in `MarkoffDocument.h`, with a diagnostic pointing to
   `serializeForSave()`. Compile-time signal at every callsite.
2. CLAUDE.md "Canonical text egress" subsection under
   `libs/markoff-core/CLAUDE.md` explaining the split and pointing
   readers at the right method.
3. This handoff doc.

No internal-callsite migrations in this commit. The deprecation surfaces
the gap; following commits clean up the internal callers
(`SourceTextDocumentBinding.cpp:205`, `SearchController.cpp:55`, the
legacy attach tests).

## Fix landing in Corbomite (paired commit, separate repo)

`NoteDocument::markdown()` routes through `serializeForSave()`:

```cpp
QString NoteDocument::markdown() const {
    return QString::fromUtf8(d->markoff->serializeForSave());
}
```

This fixes the `Vault::saveDocument` path. `Vault.cpp:833`
(disk-compare) is bypassed by `NoteDocument::markdown()`, so it needs
its own touch to call `doc->markoff()->serializeForSave()` directly.

Smoke-test plan: open a vault doc in Live mode, type into it, close
the app, check that the file on disk reflects the edit (and is not
empty).

## Open questions for the next session

1. Should `toMarkdown()` and `toMarkdownUtf8()` be redirected to
   `serializeForSave()` (semantic correction) rather than just
   deprecated? Currently they keep legacy semantics so internal users
   like `SearchController` and `SourceTextDocumentBinding`'s legacy
   fallback still get the old behavior. Migrating those callers and
   then folding the deprecated accessors into wrappers around
   `serializeForSave()` (or deleting them) is the eventual cleanup.

2. Is `d->buffer` still earning its keep? Its remaining real users are
   legacy `undo()`/`redo()` and `version()`. The D2 path has its own
   undo (`undoD2`/`redoD2`) and its own sequence (`d2EditSequence`).
   If consumers fully migrate, the legacy buffer can be deleted —
   removing a whole dual-source-of-truth seam.

3. Are there any other API methods that read `d->buffer` directly and
   leak the stale data outwards? A quick pass: `visibleLength()` (yes
   — reports buffer length, not D2 length), `anchorAt`/`resolveAnchor`
   (yes, but only consumed via the `TextAnchor` wrappers, which are
   D2-aware via composition). `version()` (CRDT internal). The
   `visibleLength()` reading is a candidate footgun similar to
   `toMarkdown`.
