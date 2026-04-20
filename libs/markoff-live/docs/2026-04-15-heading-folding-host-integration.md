# Heading Folding — Host Integration Note

> **Audience:** Corbomite developers integrating markoff's heading-folding
> feature. Markoff is encapsulated and deliberately doesn't decide where
> fold state lives on disk.
>
> **Companion spec:** [`specs/2026-04-15-heading-folding-design.md`](./specs/2026-04-15-heading-folding-design.md).

## Summary

Markoff's `FoldingModel` keeps fold state in memory, keyed by heading
hierarchy path (e.g. `["Intro", "Goals"]`). It exposes that state as a
`QJsonObject` for persistence, and a signal whenever the user changes
it. The host app decides where the JSON lives.

This note presents two options for Corbomite and shows the markoff API
contract both options plug into.

## The markoff contract

**Save trigger.** Markoff emits `Editor::foldStateChanged()` whenever
the folded set changes (user clicks arrow, runs Fold All, etc.). On
this signal, call `Editor::serializeFoldState()` to get a JSON object
and write it to your store.

```cpp
connect(editor, &Markoff::Editor::foldStateChanged,
        this, [this, editor]() {
            const QJsonObject folds = editor->serializeFoldState();
            m_store->writeFoldState(m_currentNotePath, folds);
        });
```

**Restore trigger.** After loading a note into the editor
(`Editor::setContent()` / equivalent), read the stored JSON for that
note path and call `Editor::restoreFoldState()`.

```cpp
editor->setContent(noteText);
const QJsonObject folds = m_store->readFoldState(notePath);
editor->restoreFoldState(folds);
```

**Serialization schema.**

```json
{
  "version": 1,
  "folds": [
    ["Intro", "Goals"],
    ["Reference", "API", "Query"]
  ]
}
```

Markoff tolerates missing keys, unknown keys (with warning), and paths
that no longer exist in the document (silently dropped). Bad JSON is a
no-op + warning — never a crash.

## Option A — store under `.obsidian/workspace.json`

Colocate fold state with other editor state in Corbomite's existing
`WorkspaceState` primitive (landed in commit `d681c03` —
`.obsidian/workspace.json`).

**Schema addition:** a top-level `"markoffFolds"` (or similar
Corbomite-reserved) key:

```json
{
  "main": { /* existing pane/tab state */ },
  "leftSplit": { /* ... */ },
  "markoffFolds": {
    "Notes/Introduction.md": {
      "version": 1,
      "folds": [["Intro", "Goals"]]
    },
    "Notes/API.md": {
      "version": 1,
      "folds": [["Reference", "Query"]]
    }
  }
}
```

**Pros:**

- One file for all per-vault editor state, following Obsidian's
  convention.
- No new file type for the vault to track.
- Atomic updates via the existing `WorkspaceState` primitive.
- Vault-scoped out of the box.

**Cons:**

- `markoffFolds` is a Corbomite-specific key Obsidian won't recognize.
  Obsidian ignores unknown keys, so this is safe — but it means this
  data doesn't round-trip if a user edits the vault with Obsidian.
  (That's acceptable; Obsidian's own fold state is line-based and
  line-based fold state gets invalidated on any external edit anyway.)
- Growing the workspace.json with per-note entries scales linearly
  with vault size; fine for thousands of notes, possibly a concern at
  hundreds of thousands.

## Option B — sidecar file `.obsidian/markoff-folds.json`

Put fold state in a dedicated file.

```json
{
  "version": 1,
  "notes": {
    "Notes/Introduction.md": {
      "folds": [["Intro", "Goals"]]
    },
    "Notes/API.md": {
      "folds": [["Reference", "Query"]]
    }
  }
}
```

**Pros:**

- Clean separation from Obsidian's schema; no risk of confusing third
  parties or future Obsidian updates.
- Easy to delete to reset fold state without nuking the whole
  workspace.
- Easier to shard if per-note scale ever becomes an issue (per-note
  files under a `.obsidian/markoff-folds/` directory).

**Cons:**

- A new file the host must create, read, write, and handle IO errors
  for. More surface area than A.
- One more file Obsidian users might see in their `.obsidian/` and
  wonder about.

## Recommendation

**Option A.** It reuses existing infrastructure (`WorkspaceState`),
follows Obsidian's own convention of keeping editor state in
`workspace.json`, and has the atomic-update story already solved. The
"Obsidian won't read it" concern is moot because Obsidian's own fold
format is brittle (line-number-keyed, dropped on any external edit per
the audit at `docs/obsidian-audit/01-markoff-gaps.md`) — we would not
want to round-trip through their format anyway.

If Corbomite hits a scale problem, migration to a sidecar (Option B) is
straightforward: add a reader for the old location, write to the new
one, clean up the old key on next save. Start with A.

## Wiring sketch (Option A)

```cpp
// In the NoteEditorWidget (or wherever Editor is instantiated):

void NoteEditorWidget::loadNote(const QString &notePath) {
    const QString text = m_vault->readNote(notePath);
    m_editor->setContent(text);

    const QJsonObject folds = m_workspaceState->value(
        QStringLiteral("markoffFolds.%1").arg(notePath)).toObject();
    m_editor->restoreFoldState(folds);
}

void NoteEditorWidget::onFoldStateChanged() {
    const QJsonObject folds = m_editor->serializeFoldState();
    m_workspaceState->setValue(
        QStringLiteral("markoffFolds.%1").arg(m_currentNotePath),
        folds);
}

// In the constructor:
connect(m_editor, &Markoff::Editor::foldStateChanged,
        this, &NoteEditorWidget::onFoldStateChanged);
```

Exact `WorkspaceState` API signatures may differ — adjust to match.

## What markoff guarantees

- `serializeFoldState()` is cheap (small JSON, proportional to folded
  count — usually <20 entries).
- `restoreFoldState()` is idempotent. Calling it with the current state
  is a no-op and emits no signals.
- Fold state is automatically pruned on every reparse. Stale paths
  never accumulate.
- `foldStateChanged()` fires only when the folded set actually changes.
  The host can safely write-through on every emission without
  debouncing.

## What the host must handle

- Mapping note identity to JSON blob (note path → QJsonObject). Markoff
  doesn't know about vaults or note paths.
- Vault-level coordination: when the user renames a note, the host
  must rename the corresponding fold entry.
- Deleting fold state when a note is deleted (optional — stale entries
  do no harm beyond a few bytes).
- Multi-editor coordination: if two `Editor` instances show the same
  note, the host decides which one's fold state wins. Markoff instances
  don't talk to each other.

## Optional UX hooks

Markoff emits `foldsAutoExpanded(QList<QStringList> paths)` when
navigation or find auto-unfolds ancestor headings. The host can use
this to show a transient notice ("3 sections expanded to show match")
or ignore it. No required handling.
