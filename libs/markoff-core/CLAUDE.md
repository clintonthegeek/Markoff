# markoff-core — library guide

`markoff_core` owns Markoff's document model, block serialization, sessions,
editing commands, undo log and view-facing signals. It requires the installed
provider-backed CollabText Core package. `MarkoffDocument::open()` binds a
document to a provider-owned scope; construction and publication return typed
results, and ordinary edits remain gated while provider recovery is unresolved.

The active migration authority is the
[A01–A16 execution roadmap](../../docs/plans/2026-10-12-collabtext-adoption-execution.md).
Corbomite remains paused until A16. `BlockId` is the complete opaque
actor/counter identity. Do not add numeric replica IDs, numeric conversions,
or alternate record formats.

## Current document API

Open documents with `MarkoffDocument::open(provider, scope, mode)`. The returned
document publishes block edits, structural operations, maps, undo/redo, remote
actions, Markdown import/replacement and revision-bound export through its
provider. Preserve the typed publication outcomes; a call that did not commit
must not be reported or treated as durable.

Use revision-bound export for persistence. Markdown is the import/export
format; accepted MAB actions are the durable editing history. `flatView()` and
`widgetFlatView()` are derived projections and never define operation identity.

Use the block-scoped edit, structural, and map APIs. Group related changes into
one accepted action and preserve the publication outcome through the view and
host boundary. Undo/redo publish compensating actions with durable history-group
associations; do not mutate provider-owned state through a second local engine.

## Identity and migration boundary

Block order and view positions use full `BlockId` plus provider-scoped opaque
text-position tokens. Resolve tokens against the current provider projection;
reject invalid or unresolved tokens instead of narrowing them to byte offsets.

## Block buffer convention

Buffers contain content without a trailing structural newline. Separators are
added by Markdown serialization or by the selected flat view. Marker handling is
per kind:

| Block kind | Buffer contents |
|---|---|
| Paragraph | Plain paragraph text |
| ATX Heading | Includes the `#` prefix |
| Setext Heading | Title only; serializer reconstructs underline |
| CodeBlock | Includes fences |
| ListItem | Marker removed; serializer reconstructs it from attrs |
| BlockQuote | `>` prefixes removed per depth; serializer rebuilds them from attrs |
| HorizontalRule | Buffer ignored; serializer emits the rule |
| Math, Image, Mermaid, HtmlBlock, Table | Passthrough bytes |

Use `listItemDisplayMarker()` and `blockQuoteDisplayMarker()` when a view needs
the removed marker. `inferBlockKind()` must not run on marker-stripped
ListItem/BlockQuote contents.

## Text units and tests

`Markoff::TextUnits` is the shared Core API. `checkedByteToQtPos()` and
`checkedQtPosToByte()` reject malformed UTF-8, out-of-range positions, and
positions inside a Unicode scalar. The clamping helpers remain in use by some
view paths; new editing and persistence code should use checked conversions and
handle refusal explicitly. Live and Canvas include `<markoff/core/TextUnits.h>`
directly; there is no Live coordinate alias.

Build the core with `cmake --build build-dev --target markoff_core -j 4`.
Run focused root tests with `QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev
--output-on-failure -R '<test-name>'`. The root build already is the
provider-backed engine. Session routine and the current task:
[`AGENTS.md`](../../AGENTS.md),
[`docs/STATUS.md`](../../docs/STATUS.md).

## Related contracts

- [Markoff adoption plan](../../docs/plans/2026-10-11-collabtext-vnext-adoption.md)
- [Execution roadmap](../../docs/plans/2026-10-12-collabtext-adoption-execution.md)
- [Document storage provider](../../docs/specs/2026-10-12-document-storage-provider.md)
- [Core D5 design](../../docs/specs/2026-05-07-d5-collab-activation-design.md)
