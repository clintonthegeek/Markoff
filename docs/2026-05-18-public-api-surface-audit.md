# Public API Surface Audit — Markoff for Consumers

**Date:** 2026-05-18
**Branch:** `exploration/new-foundation`
**Status:** input audit — questions enumerated, **awaiting port evidence**.

A 2026-05-20 attempt to write a sweeping `markoff-core` freeze spec answering every question here was re-statused as draft reference (`docs/specs/2026-05-20-markoff-core-freeze-shape-design.md`) after the user pushed back: writing a 19-decision contract before Corbomite reintegration begins is the *spec-review-between-two-agents* antipattern. The path forward is port-first — each API gap that surfaces during reintegration becomes a one-decision micro-spec, and the audit's questions get answered as evidence accumulates.

Two narrow follow-ons did land on 2026-05-20 against real bugs, not against this audit:
- `docs/specs/2026-05-20-find-session-scope-design.md` — find boundary, three commits (`634266b..30c4f57`).
- `docs/specs/2026-05-18-markoff-source-freeze-shape-design.md` and `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md` — leaf freezes drafted pre-pivot; landed D-rows are real, remaining D-rows are accepted-smell decisions.

This audit document is retained for the per-symbol partition tables — a snapshot of the surface at audit time. Diff against live headers as the port progresses to see what moved.

**Companion docs:** `docs/handoff/2026-04-20-phase-c-ownership-handoff.md` (port-first rationale), `docs/handoff/2026-05-07-pivot-to-d5-first.md` §4.6, `docs/handoff/2026-05-08-defer-46-to-e-arc.md`.

## Purpose

This document enumerates the public C++ surface of the three shipping leaves (`markoff-core`, `markoff-live`, `markoff-source`) and partitions each exported type into one of:

- **Consumer entry point** — a type a host app (e.g. Corbomite) instantiates or holds.
- **Service interface** — abstract; the host implements or registers a concrete.
- **Foundation type** — the host touches it only when doing Heavy CRDT integration.
- **Internal-but-public** — shipped in `include/` for technical reasons (sister leaves consume it) but not part of the consumer story.

The output feeds the §4.6 freeze plan: every consumer entry point and service interface must be name-stable before freeze; internal-but-public types are candidates for `src/` relocation or `Markoff::Detail::` namespacing.

## Policy refresher (see `[[project-crdt-api-policy]]`)

The "no CRDT in public API" rule is asymmetric: it applies to view leaves (`markoff-live`, `markoff-source`), not to `markoff-core` and not to consumers. Foundation-design §D3 commits to Heavy CRDT mode — `CollabText::Crdt::Anchor`, `Crdt::Operation`, `Crdt::Buffer` are intentional in `markoff-core` public headers, and consumers import collabtext directly. `markoff-core` additionally provides CRDT-free wrappers (`TextAnchor`, `BlockAnchor`, `editSequence`) so consumers who want a CRDT-free build path have one. **Audit A** (2026-05-18 same session) confirmed view leaves are CRDT-free in both their public headers and their implementations.

---

## markoff-source — public surface

**Headers shipped:** `Editor.h`, `FindBar.h`. Two files, both consumer-facing.

### Consumer entry points

| Type | Header | Notes |
|---|---|---|
| `Markoff::Source::Widget::Editor` | `Editor.h` | `QWidget` subclass; subclasses `Markoff::MarkdownView`. Host instantiates, attaches a `MarkoffDocument` via `setDocument()`. |
| `Markoff::Source::Widget::FindBar` | `FindBar.h` | Standalone QWidget; constructed with an `Editor *`. Optional companion — the `Editor::showFindBar()` override is the alternative entry point. |

### Shape questions for §4.6

1. **`Editor::plainTextEdit()` is public.** Returns the inner `QPlainTextEdit *`. The header comment scopes it to "Gutter, FindBar, and tests" — but a host can use it too. Pick a policy: (a) keep it public and document as escape hatch, or (b) restrict to `friend`/sister-class access and let the forwarding methods (`toPlainText`, `extraSelections`, `textCursor`, etc.) be the only consumer surface.
2. **Forwarding methods duplicate the escape hatch.** `toPlainText()`, `extraSelections()`, `textCursor()`, `setTextCursor()`, `ensureCursorVisible()` all pass through to `m_editor`. If `plainTextEdit()` stays public this surface is redundant; if `plainTextEdit()` becomes restricted these become the documented API.
3. **`FindBar`'s relationship to `Editor`.** Currently it's a standalone QWidget the host can place separately. But `Editor::showFindBar()` is part of the `MarkdownView` contract — implying the Editor owns the bar internally. Confirm: does the host instantiate a `FindBar` at all, or does it call `showFindBar()` and let `Editor` manage one? Right now both work, which is the ambiguity.
4. **Type name `Editor`.** Inside `Markoff::Source::Widget`, the unqualified `Editor` is unambiguous, but in consumer code `using namespace Markoff::Source::Widget;` would clash with other "Editors." Common workaround: alias to `Markoff::SourceEditor` in a public umbrella header. Decision deferred.
5. **`Gutter` is forward-declared in `Editor.h` and `friend`-ed.** The class itself isn't in the public include path. That's correct (it's an implementation child widget) but the forward-decl and `friend` line leak its existence. Acceptable; consider a private namespace.

### Internal-but-public

None — both files are clearly consumer-facing.

---

## markoff-live — public surface

**Headers shipped:** 24 files. The leaf has a wide internal-but-public surface because the QML delegate machinery needs many cooperating C++ classes.

### Consumer entry points

| Type | Header | Role |
|---|---|---|
| `LiveListModelBinding` | `LiveListModelBinding.h` | The primary integration point. Host instantiates with `Capabilities` flags, wires document/theme/session, accesses sub-controllers via Q_PROPERTYs. |
| `BlockKindRegistry` | `BlockKindRegistry.h` | Per-document registry of block-kind metadata. Plugin authors `register_()` a `BlockKindDescriptor` to add a custom kind. |
| `LiveStructuralKeyHandler` | `LiveStructuralKeyHandler.h` | Structural-key dispatcher (Enter/Backspace-edge/etc.). Host instantiates when the consumer needs structural editing; QML calls `tryHandle()`. |

### Integration glue (host wires; doesn't usually instantiate directly)

| Type | Header | Role |
|---|---|---|
| `LiveBlockModel` | `LiveBlockModel.h` | `QAbstractListModel` exposed via QML ListView. Provided by `LiveListModelBinding`. |
| `LiveCursorState` | `LiveCursorState.h` | Canonical cursor + selection store. Provided by binding; consumer reads via Q_PROPERTYs. |
| `LiveSelectionView` | `LiveSelectionView.h` | Q_OBJECT facade over `LiveCursorState` selection. **Phase-A transition; will collapse.** Don't depend on this directly. |
| `LiveEditBinding` | `LiveEditBinding.h` | Per-delegate edit binding. QML wires; C++ consumers typically don't instantiate. |
| `LiveNavigationController` | `LiveNavigationController.h` | Cross-block keyboard navigation. Provided by binding. |
| `LiveActionController` | `LiveActionController.h` | QActions for cut/copy/paste/undo/redo/bold/italic/zoom/dark-toggle. |
| `LiveClipboardController` | `LiveClipboardController.h` | Clipboard ops. |
| `LiveFormatController` | `LiveFormatController.h` | Per-block format wrapping (bold/italic/link). |
| `LiveContextMenuHandler` | `LiveContextMenuHandler.h` | Native `QMenu`-backed right-click. |
| `BlockHitTester` | `BlockHitTester.h` | Hit-test bridge between QML and C++. |

### Internal-but-public

| Type | Header | Why it ships |
|---|---|---|
| `BlockKindDescriptor` | `BlockKindDescriptor.h` | Data struct registered via `BlockKindRegistry::register_`. Consumer-facing if adding a custom kind. |
| `BlockRecord`, `BlockKey` | `BlockRecord.h` | Data carriers; `LiveBlockModel` stores them. Consumer reads via `recordAt(row)` or model roles. |
| `AstBlockDiff` | `AstBlockDiff.h` | Pure C++ Myers/LCS diff. Algorithm; internal to binding. Exposed for testability — candidate to move to `src/`. |
| `Cursor`, `LiveRenderSelection` | `Cursor.h` | Type aliases over the `markoff-core` discriminated union + a selection struct. Carrier only. |
| `Coordinates` namespace | `Coordinates.h` | Byte↔QtPos UTF-8 helpers. Used by `LiveEditBinding`. **Decision:** keep public for consumers extending with custom delegates, or move to `src/`? |
| `BlockId`, `BlockKind` namespace | `BlockId.h`, `BlockKind.h` | Type alias + string constants. The string-keyed BlockKind names live here; the enum is in `markoff-core`. |
| `InlineHighlighter`, `InlineHighlighterAttached` | `InlineHighlighter.h`, `InlineHighlighterAttached.h` | E1 per-delegate `QSyntaxHighlighter`. QML-wired. |
| `Version` | `Version.h` | `version() → quint32`. Library version query. |
| `ThemeForeign` | `ThemeForeign.h` | QML_FOREIGN shim re-exporting `Markoff::Theme` enum to QML. Test/QML-build infrastructure. |
| `MarkoffLiveExport` | `MarkoffLiveExport.h` | Export macro. |

### Shape questions for §4.6

1. **`LiveListModelBinding` is a long, generic name.** Doesn't self-document what it binds (a `MarkoffDocument`) or that it's the singular host entry point. Alternatives: `LiveViewBinding`, `MarkoffLiveBinding`, `LiveEditor`. E3a shipped against the current name, so renaming has migration cost.
2. **Selection API mid-transition (tier-4c phase A).** `LiveSelectionView` is a stateless facade today; canonical state lives in `LiveCursorState`. Phase C plans to remove `LiveSelectionView` entirely. Consumers must depend on `LiveCursorState`, not `LiveSelectionView`. **Decision for §4.6:** rename or remove `LiveSelectionView` before freeze, or commit to it as a permanent compatibility shim?
3. **`BlockKindRegistry::isBlockOnly` asymmetry.** Discipline-log entry 2026-05-13: Math is explicit-false despite no `TextCaret` variant. Will be removed when Math becomes text-bearing. The flag's existence is a transitional smell. Should the freeze spec document it as transitional or remove the flag in favor of deriving from `supportedCursorVariants`?
4. **`AstBlockDiff` and `Coordinates` are public but algorithm-like.** Candidates to move to `src/` and stop shipping. Worth confirming no consumer needs them.
5. **`BlockKindDescriptor::delegateUrl` is unused at runtime.** Built-in kinds hardcoded in `LiveView.qml`'s `DelegateChooser`; the field is wired only for hypothetical plugin kinds. Either commit to plugin-kind support as a §4.6 feature or remove the field.
6. **`BlockRecord::inlineSpans` comment is stale.** §A.7 of the developmental history says "excluded ... consumed by InlineFormatHighlighter in R6." E1 shipped; field is load-bearing. Update the comment as part of the freeze pass.
7. **Re-entrance guards live in production.** `m_applyingTextUpdate` (LiveEditBinding), `m_applyingSessionSelection` (LiveSelectionView). Invariant-7 smell, currently transient. Removed in §4.6? Or documented as supported?
8. **`Qt.callLater` site at `MathDelegate.qml:113`.** Invariant-6 smell. Not in the C++ surface but in the QML the host ships when consuming `markoff-live`. Worth resolving before freeze.

---

## markoff-core — public surface

**Headers shipped:** ~60. Largest leaf because it owns the foundation (CRDT, sessions, services, commands, undo log).

### Consumer UI types

| Type | Header | Role |
|---|---|---|
| `ActionId` | `ActionId.h` | Formatting action enum (`Bold`, `Italic`, `Link`, etc.). |
| `CursorPos` | `CursorPos.h` | `{line, column}`, 1-based. |
| `Theme` | `Theme.h` | Q_GADGET value type; color palette + per-slot fonts + code-token highlight. |
| `EditorContext`, `BlockKindNames` | `EditorContext.h` | Current-block context for action-enable logic. |
| `LinkActivation`, `LinkKind` | `LinkActivation.h`, `LinkKind.h` | Link click payload + classification. |
| `MarkdownView` | `MarkdownView.h` | QWidget polymorphic base for source/live/reading widgets. Host holds `MarkdownView *` and dispatches through the contract. |
| `Cursor` (variant) | `Cursor.h` | `NoCursor | TextCaret | BlockSelected | BlockInternalEdit` discriminated union. |

### Foundation types (CRDT-aware integration)

| Type | Header | Notes |
|---|---|---|
| `MarkoffDocument` | `MarkoffDocument.h` | The CRDT-backed document. Owns sessions; emits change signals; serializes for save. |
| `Session` | `Session.h` | Per-view session created by `MarkoffDocument::createSession()`. |
| `Selection` | `Selection.h` | `(anchor, active, kind)` with `TextAnchor`-typed anchors. CRDT-free header per spec. |
| `BlockAnchor` | `BlockAnchor.h` | Wraps `BlockId` for typed signatures. **Currently a type alias for `BlockId`** — flag for §4.6 (alias vs distinct type). |
| `TextAnchor` | `TextAnchor.h` | CRDT-free wrapper around `Crdt::Anchor`. Companion to `anchorAt(Crdt::Bias)`. |
| `BlockId`, `BlockKind` | `BlockId.h`, `BlockKind.h` | Opaque block identity + the kind enum. |
| `Origin` | `Origin.h` | Edit-origin enum (`UserEdit`, `ExternalReload`, `FirstOpen`, etc.). |
| `FoldRef` | `FoldRef.h` | Fold region; stores `Crdt::Anchor` directly. **No `TextAnchor` wrapper companion — gap.** |
| `PasteMeta` | `PasteMeta.h` | Structured-paste metadata. |
| `SessionParams` | `SessionParams.h` | Session constructor params. |

### Service interfaces (host registers/implements)

| Type | Header | Role |
|---|---|---|
| `LinkService` | `LinkService.h` | Abstract: classify/resolve/activate/notifyHover. |
| `DefaultLinkService` | `DefaultLinkService.h` | Concrete demo impl. |
| `SyntaxHighlightService`, `Kf6SyntaxHighlightService` | `SyntaxHighlightService.h`, `Kf6SyntaxHighlightService.h` | Abstract + KF6 concrete. |
| `CompletionProvider`, `CompletionRegistry` | `CompletionProvider.h`, `CompletionRegistry.h` | Abstract + registry. |
| `CodeBlockProcessor`, `CodeBlockProcessorRegistry` | `CodeBlockProcessor.h`, `CodeBlockProcessorRegistry.h` | Abstract + registry. |
| `BlockSerializer`, `BlockSerializerRegistry` | `BlockSerializer.h`, `BlockSerializerRegistry.h` | Function typedef + abstract registry. |
| `MarkoffServices` | `MarkoffServices.h` | Aggregate struct bundling syntax/codeProcessors/links/completion. |

### Internal-but-public (candidates for `src/` relocation or `Detail::` namespacing)

| Type | Header | Why shipped today |
|---|---|---|
| `Cmd::*` | `Cmd.h`, `Cmd/D2.h`, `Cmd/Edit.h` | Command functions used by `markoff-live`'s structural-key handler and edit binding. Header comments warn "do not call from view code" — that's the consumer policy, but the headers ship publicly. |
| `UndoLog` | `UndoLog.h` | D2 undo log; `markoff-live` opens transactions via `d2UndoLog()`. |
| `CrdtProxies` (`BufferProxy`, `IdListProxy`, `SiblingMapProxy`) | `CrdtProxies.h` | QObject signal proxies; per-block change notifications used by `markoff-live`'s model. |
| `KindTagMap`, `CausalLwwMap` | `KindTagMap.h`, `CausalLwwMap.h` | The internal CRDT primitives backing block-kind / attrs / etc. |
| `MarkoffOp`, `MarkoffBundleMeta`, `MarkoffSerializer` | `MarkoffOp.h`, `MarkoffSerializer.h` | Wire-format ops + binary serializer. The collab path uses these. |
| `RenderedBlock` | `RenderedBlock.h` | Rendered block output (image/svg/highlighted/empty). |
| `SiblingMapOpHeader` | `SiblingMapOpHeader.h` | Wire-format sibling-map op. |
| `WatermarkCoordinator` | `WatermarkCoordinator.h` | GC watermark. |
| `InlineParseCache` | `InlineParseCache.h` | Per-block inline-span cache. |
| `BlockEdit`, `StructuralOp` | `BlockEdit.h`, `StructuralOp.h` | Edit/op records the legacy `applyBlockEdit`/`applyStructural` use. `applyFlatEdit` is the modern entry point. |
| `AnchorJson` | `AnchorJson.h` | JSON serialization for `Crdt::Anchor`. |
| `SourceTextDocumentBinding` | `SourceTextDocumentBinding.h` | The binding `markoff-source/Editor` uses. **Open question:** is this part of the consumer story for hosts building custom source-style widgets, or strictly markoff-source's helper? |
| `CodeSpan`, `CodeTokenKind` | `CodeSpan.h`, `CodeTokenKind.h` | Syntax-highlight span + token enum. Consumer-facing if implementing a custom `SyntaxHighlightService`. |
| `AttrNames`, `BlockAttrsMap` | `AttrNames.h`, `BlockAttrsMap.h` | Block-attribute name constants + map. |
| `FootnoteDefMap`, `FrontmatterMap`, `LinkRefMap` | (same names) | Per-aspect storage maps. |
| `CompletionCandidate`, `CompletionContext`, `CompletionDetector`, `CompletionTrigger` | (same names) | Completion infrastructure. |
| `SearchEngine` | `SearchEngine.h` | Search/find with hit collection. **Open question:** consumer-facing for "find in markdown" features? |
| `MarkoffCoreExport` | `MarkoffCoreExport.h` | Export macro. |

### Wrapper-coverage gaps (Category B — see `[[project-crdt-api-policy]]`)

These CRDT-typed accessors lack a CRDT-free companion. A consumer wanting a CRDT-free build path can't currently get one without falling back to `Crdt::Anchor`.

| Method | Returns/Takes | Wrapper gap |
|---|---|---|
| `Session::topVisibleAnchor()` | `Crdt::Anchor` | No `TextAnchor` variant. |
| `Session::setTopVisible(Crdt::Anchor, qreal)` | `Crdt::Anchor` | No `TextAnchor` setter. |
| `Session::scrollChanged(Crdt::Anchor, qreal)` (signal) | `Crdt::Anchor` | No `TextAnchor` variant signal. |
| `FoldRef::start` | `Crdt::Anchor` member | No `TextAnchor` mirror. Fold JSON serialization currently bridges. |
| `MarkoffDocument::version()` | `Crdt::Global` | No CRDT-free version-vector accessor. `editSequence()` / `d2EditSequence()` exist for dirty-tracking but don't expose causality. |

**Decision for §4.6:** add wrappers, document scroll/fold as "Heavy CRDT only" sub-API, or remove these methods from the public surface entirely if no consumer uses them yet.

### Shape questions for §4.6

1. **`BlockAnchor` is a type alias for `BlockId` today** — but the docs and many signatures treat it as distinct. Decide: collapse to `BlockId` (saves a type) or promote to a distinct struct (clearer intent, breaks no current callers because the alias propagates). The current state is the worst of both.
2. **Move `Cmd::*` to `src/`?** Header comments say "do not call from view code," but `markoff-live`'s implementation calls them. If we want to enforce the policy, either inline them into `MarkoffDocument`'s public surface (`applyFlatEdit` + companions) or relocate to `Markoff::Detail` in `src/`.
3. **`CrdtProxies` shape stability.** `BufferProxy::notifyChanged()`, `IdListProxy`, `SiblingMapProxy` — are these signal interfaces stable, or do they expect to change as the view layer settles? `markoff-live` currently consumes them.
4. **`UndoLog` exposure.** `markoff-live` opens transactions directly via `doc.d2UndoLog()`. Consumers wanting to coalesce edits would need this. Document as "advanced; Heavy CRDT only" or hide entirely.
5. **`MarkoffServices` aggregate.** Bundles four service registries. Either the host wires services through this struct exclusively (then it's good API) or individually (then the bundle is a half-measure).
6. **`SearchEngine` consumer story.** Find-in-doc is a feature consumers want; is `SearchEngine` the public surface, or do they go through `LiveListModelBinding`'s clipboard/action controllers?
7. **`SourceTextDocumentBinding` consumer story.** Used by `markoff-source/Editor`. Is this the supported way for a host to build a source-style widget against `MarkoffDocument`, or strictly an internal helper? The freeze should pick.
8. **`Selection::toJson`/`fromJson` carry `TextAnchor` identity** — but `TextAnchor` is opaque, so round-tripping is via the embedded fields. Document the wire format if hosts will persist selections.
9. **`applyFlatEdit` coordinate semantics.** Documented (no-separator concatenation); the new `flatView()` introduced 2026-05-18 is the separator-bearing companion. Worth a freeze-pass docstring that names both and points to the conversion helper.
10. **Known consumer-visible gap.** The source-widget separator-zone-edit problem (queue.md Discipline Log, 2026-05-18) — backspace at start of a block doesn't merge into the previous block. Either fix before freeze or document as a known limitation in the migration guide.

---

## Cross-cutting open questions

Distinct from the per-leaf shape questions, these touch multiple leaves and need an arc-level decision:

1. **Type-alias hygiene.** `BlockAnchor` is aliased to `BlockId` in `markoff-core`; `markoff-live::BlockId` is aliased to `Markoff::BlockAnchor`. Two aliases pointing at the same thing across leaves means *every* call site is using one or the other inconsistently. Pick a canonical name; deprecate the others.

2. **Umbrella headers per leaf.** No `<markoff/core.h>`, `<markoff/live.h>`, `<markoff/source.h>` aggregate headers today. Consumers include individual headers à la carte. Decision: stay à la carte (current behavior is explicit; small compile-time wins) or ship umbrellas (one-line includes per leaf).

3. **Namespace consistency.** `Markoff::` (core), `Markoff::Live::` (live), `Markoff::Source::Widget::` (source — three levels). `Widget` in the source namespace is a leftover from when there might have been a `Source::Headless::` or similar. Decision: flatten to `Markoff::Source::Editor` for symmetry with `Markoff::Live::Editor`-like patterns?

4. **The asymmetric CRDT policy must be documented in the Corbomite migration guide.** Hosts need to know upfront that any Markoff leaf compile-depends on collabtext via transitive includes (Session.h → `<crdt/Anchor.h>`). Audit A established this; the migration guide makes it consumer-visible.

5. **Service-registration timing.** `LinkService`, `SyntaxHighlightService`, `CodeBlockProcessorRegistry`, `CompletionRegistry` — when must the host install these for `markoff-live` to render correctly? Pre-`setDocument`? Pre-`setSession`? After model rows are populated? The §4.6 spec should pin this.

6. **Q_INVOKABLE proliferation on `LiveListModelBinding`.** Theme proxies (`pixelSizeFor`, `familyFor`, `boldFor`, `italicFor`, `colorFor`) exist because of QML gadget dispatch limits. Document as part of the QML-integration story so consumers know the C++-side equivalents (read `Theme` directly from `binding.theme`).

---

## Recommended next steps

1. **Resolve the per-leaf shape questions** before drafting the freeze spec. Each requires a yes/no/different-shape user call. Group them: `markoff-source` (5 questions), `markoff-live` (8 questions), `markoff-core` (10 questions), cross-cutting (6 questions). Aim to clear them in a single brainstorm session per leaf, in the order Corbomite would encounter them.

2. **Audit B (wrapper coverage)** is now well-scoped: the gaps are `Session::topVisibleAnchor/setTopVisible/scrollChanged` and `FoldRef::start`. Audit B can become a small spec ("Add TextAnchor wrappers for scroll/fold; document Heavy CRDT companions") rather than the open-ended survey it might have been.

3. **Migration guide outline.** Once shape questions resolve, draft a per-leaf migration guide section: "What you instantiate," "Wires you register," "Signals you connect," "Heavy CRDT extras (optional)." Corbomite's actual code-to-port becomes the falsification harness for the migration guide.

4. **Concrete `markoff-source` decisions to make first.** Smallest surface (2 headers); resolving 5 shape questions there gives the freeze pattern that informs the bigger leaves.

## Status

| Audit | Status |
|---|---|
| Audit A — view leaves CRDT-free | ✅ pass (2026-05-18) |
| Audit C — naming/shape (this doc) | enumerated 2026-05-18; awaiting port evidence per gap |
| Audit B — wrapper coverage | scoped (Session scroll + FoldRef are the gaps); resolve when a real consumer pulls |
| Per-leaf freeze specs | source/live pre-pivot drafts (D-rows partially landed); find shipped 2026-05-20; core spec re-statused as draft reference |
| Corbomite migration guide | deferred until ~10 port-driven micro-specs accumulate |
