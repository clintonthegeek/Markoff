# markoff-core freeze shape — DRAFT REFERENCE (not action plan)

> **2026-05-20 — re-statused after port-first pivot.**
>
> This document was originally drafted as a 19-decision freeze contract for `markoff-core`. On the same day, the user pushed back: drafting a 19-decision contract before any Corbomite reintegration begins is the *spec-review-between-two-agents* antipattern that the 2026-04-20 Phase C handoff explicitly warned against. Speculative interfaces (D5 embed, D6 mermaid, D7 vault) freeze signatures for features that don't exist and consumers that haven't pulled on them; wrapper-coverage (D8/D9) and `seal()` semantics (D19) add API surface for hypothetical needs.
>
> **What this document IS now:**
>
> - A snapshot of the 2026-05-18 audit's questions and ONE possible set of answers.
> - A reference for the type-identity decisions (**D1** BlockAnchor canonical, **D2** Cmd:: → Detail::, **D3** CrdtProxies → Detail::, **D10** broader Detail:: policy). These are standalone API hygiene improvements that may still be valid regardless of port order.
> - A grab-bag of design notes for the speculative items (D4–D9, D12–D19). Treat these as "options the audit suggested," not commitments.
>
> **What this document is NOT:**
>
> - An action plan. There is no 12-commit migration. There is no implementation plan to write against this.
> - A frozen API contract. Nothing here is committed.
> - The path to Corbomite reintegration. That path is: begin the port against HEAD; each API gap becomes a one-decision micro-spec; the freeze spec gets written from evidence AFTER ~10 such gaps land.
>
> See `docs/handoff/2026-04-20-phase-c-ownership-handoff.md` §"Why" for the rationale on port-first vs. spec-first.

---

**Original draft below preserved for reference. Decisions D1, D2, D3, D10 may survive a future evidence-driven freeze; the rest should be re-evaluated against actual port pressure.**

---

**Date:** 2026-05-20
**Branch:** `exploration/new-foundation`
**Status:** draft notes — NOT action plan, NOT committed API.
**Companion specs:**
- `docs/specs/2026-05-18-markoff-source-freeze-shape-design.md` — source-leaf freeze (partially superseded by find work; D1/D3/D4 stand).
- `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md` — live-leaf freeze (partially superseded by find work; receives D11/D12 amendments via this spec).
- `docs/specs/2026-05-20-find-session-scope-design.md` — find boundary (resolves the search/replace shape questions in the audit).
- `docs/2026-05-18-public-api-surface-audit.md` — input audit; every markoff-core shape question raised there is resolved by one of D1–D19 below.
**Supersedes:** none — first markoff-core-scoped freeze.

## Purpose

Three sibling specs (`markoff-source` freeze, `markoff-live` freeze, find-session-scope) pinned the leaf-facing surfaces. This spec finishes the job for `markoff-core` — by far the largest of the four leaves (62 tracked headers) and the surface a consumer like Corbomite touches most heavily.

The 2026-05-18 audit enumerated ten core-leaf shape questions, five wrapper-coverage gaps, and six cross-cutting questions. Today's Corbomite-side checklist surfaced four further gaps that were inherited from the old leaves' retirement: vault types are gone, `EmbedRegistry`/`MermaidRenderer` are gone, `MarkdownDelta` is gone, and `MarkoffDocument::wordCount()` is gone. This spec resolves all of them by one of three moves:

1. **Restore** the abstract interface (where the consumer needs the seam — vault, embed registry, mermaid renderer).
2. **Demote** to `Markoff::Detail::` (where the type is consumed only by sister leaves — Cmd, CrdtProxies, SearchEngine, UndoLog, MarkoffOp).
3. **Document and freeze** (where the shape is right but the contract is undocumented — applyFlatEdit semantics, Selection JSON wire format, service-registration timing).

The freeze does not add E3/E4/E5 *behaviour* (embed rendering, table editing, mermaid concretes). It pins the *shape* those phases will consume, so Corbomite can begin its port against a stable surface.

## Scope

In scope:

- Type-identity decisions for `markoff-core`'s public API (D1–D2).
- Service-interface restoration for embed / mermaid / vault (D3–D7).
- Wrapper-coverage gap closure (D8–D9).
- `Markoff::Detail::` namespace policy for sister-leaf-only types (D10–D11).
- Document API hygiene (D12–D14): applyFlatEdit semantics, source-widget separator-zone-edit fix, frontmatter access for property editors.
- Small additions Corbomite needs (D15–D17): wordCount, Selection JSON pin, CRDT policy docstring.
- Cross-cutting (D18–D19): no umbrella headers, service-registration timing.
- Pointers to `markoff-live` freeze amendments (E1–E2 in §"Cross-leaf coordination") — implemented in a companion amendment, not in this spec.

Out of scope (tracked separately):

- Implementation of E3 embed delegates / E4 table editor / E5 math+mermaid. This spec freezes the abstract interfaces; concrete rendering lands in those phases.
- The QWidget-hosting wrapper `Markoff::Live::EditorWidget` (Corbomite-checklist push-back #2). Receives its own mini-spec, referenced in §"Cross-leaf coordination".
- Read-only mode for Live (`Capabilities::Editable`). Amends `markoff-live` freeze D-row; reference in §"Cross-leaf coordination".
- Replace flow (`Markoff::ReplaceController`). Deferred per find-session-scope §"Out of scope".
- HoverPopover strategy. Three options surfaced in the Corbomite checklist; pick one once embeds land.
- Reading-mode leaf restoration. User confirmed 2026-05-20 that Live-with-editing-disabled is acceptable for now.

## Policy refresher

The 2026-05-18 audit recorded an asymmetric CRDT policy (`[[project-crdt-api-policy]]`): view leaves stay CRDT-free in their public surface; `markoff-core` and consumers may import collabtext directly. Audit A (same day) confirmed view leaves comply. This spec maintains that policy — every restoration below preserves the rule that **a host can build a CRDT-free view leaf against `markoff-core`'s public surface alone**, even when the host also chooses to consume CRDT-typed accessors directly. The two surfaces co-exist; the freeze does not retire either.

---

## Decisions

### D1. `BlockAnchor` is the canonical public spelling; `BlockId` becomes internal

**Decision:** `Markoff::BlockAnchor` is the single name appearing in public signatures (function parameters, return types, struct members, signal payloads). `Markoff::BlockId` is kept as an internal alias for terse use inside `src/` and inside `markoff-live::Detail::`, but is removed from public signatures across `markoff-core`.

**Why:** The audit named the current shape "worst of both worlds" — two names, one type, used inconsistently. `BlockAnchor` reads as a public-API word (mirrors `TextAnchor`, signals identity-of-block); `BlockId` reads as an implementation detail (CRDT-shaped). Picking `BlockAnchor` for the public layer commits to the naming that already governs `BlockAnchor.h` + `TextAnchor.h` and frees `BlockId` to denote the internal CRDT shape without further confusion. The alias propagates, so no callsite breaks; the freeze pass is purely a documentation + signature-rewrite job.

**How to apply:**

- Audit every public header under `libs/markoff-core/include/markoff/core/` and rewrite `BlockId` → `BlockAnchor` in signatures, member names, signal payloads.
- `BlockId.h` stays as `using BlockId = BlockAnchor;` (alias retained for internal use); add a `// Internal alias` comment.
- Sister-leaf headers (`markoff-live/include/markoff/live/BlockId.h`, etc.) keep their own internal aliases; they may continue to spell `BlockId` in `src/` but their public surface migrates.
- Add a section to the migration guide: "consumers spell block identity as `Markoff::BlockAnchor`."

### D2. `Cmd::*` moves under `Markoff::Detail::Cmd`; header path stays at `markoff/core/Cmd/`

**Decision:** The `Markoff::Cmd::changeKind`, `Cmd::insertBlock`, `Cmd::splitBlock`, etc. functions move to namespace `Markoff::Detail::Cmd`. Their headers remain in `libs/markoff-core/include/markoff/core/Cmd/` and `Cmd.h` for sister-leaf consumption (markoff-live's `LiveStructuralKeyHandler` uses them). Consumers (Corbomite) MUST NOT call them; the public path for structural edits is `applyFlatEdit` + companions.

**Why:** Header comments already say "do not call from view code" — but `markoff-live` is *exactly* a view that calls them, and headers ship publicly. The current state lies. Moving to `Markoff::Detail::` puts the boundary in the symbol name, where it can't be missed. Header paths stay because (a) markoff-live `#include`s them, and (b) renaming the include path is churn without benefit.

**How to apply:**

- Move `namespace Markoff::Cmd { ... }` → `namespace Markoff::Detail::Cmd { ... }` across all `Cmd*.h` and `Cmd*.cpp` files.
- Migrate the ~30 call sites in `markoff-live/src/` (mostly `LiveStructuralKeyHandler.cpp` + `LiveEditBinding.cpp`) to the new namespace.
- Add header-level docstring on `Cmd.h`: *"Internal. Sister-leaf consumption only. Consumers use `MarkoffDocument::applyFlatEdit` and the companions in §Public Document API."*

### D3. `CrdtProxies` moves under `Markoff::Detail::`

**Decision:** `Markoff::BufferProxy`, `IdListProxy`, `SiblingMapProxy` move to namespace `Markoff::Detail::`. Header remains at `markoff/core/CrdtProxies.h` for sister-leaf use.

**Why:** Same reasoning as D2. Markoff-live's `LiveBlockModel` and `LiveListModelBinding` consume these for per-block change notification; consumers don't.

**How to apply:** Namespace migration across `CrdtProxies.h/.cpp`, `MarkoffDocument.h` accessors (`bufferProxy()`, `idListProxy()`, etc.) update to return `Detail::*Proxy *`, and the ~12 sister-leaf call sites follow.

### D4. `MarkoffServices` is the canonical service-wiring entry point

**Decision:** Hosts wire all four services (`LinkService`, `SyntaxHighlightService`, `CodeBlockProcessorRegistry`, `CompletionRegistry`) through a single `MarkoffServices` aggregate, installed by `MarkoffDocument::setServices(const MarkoffServices &)`. Individual `setLinkService` / `setSyntaxHighlightService` / etc. setters on `MarkoffDocument` move to `Markoff::Detail::` (or are removed if no test uses them).

**Why:** The audit observed this is "either good API or a half-measure." Picking the aggregate gives a single point where service-registration timing is enforced (see D19) and a single point Corbomite has to wire. The individual setters survive only to support fixture-style tests that swap one service at a time; those usages are internal.

**How to apply:**

- Audit every `MarkoffDocument::set*Service` / `set*Registry` call. Migrate host-side callers to `setServices(MarkoffServices{...})`. Tests that swap a single service keep the granular setter but route through `Markoff::Detail::` (or rebuild a `MarkoffServices` per test).
- Docstring on `MarkoffServices.h` defines the wiring order: services first, *then* `setDocument`/`setSession`, *then* model rows populate.

### D5. `EmbedRegistry` abstract interface restored

**Decision:** Restore `Markoff::EmbedRegistry` as an abstract interface at `libs/markoff-core/include/markoff/core/EmbedRegistry.h`. Shape follows the Phase C1 design (factory-keyed by extension, dispatched by `EmbedRequest`). No default concrete in `markoff-core` — host registers factories. Live-leaf consumes via the registry's `dispatch(EmbedRequest)`; renderer wiring lands in E3.

**Why:** Corbomite needs embeds (necessary per 2026-05-20 user checklist). Restoring the abstract now means the freeze contract names how Corbomite plugs in its `Corbomite::EmbedRegistry` concrete. Implementation behaviour comes with E3; *interface* shape is the freeze deliverable.

**How to apply:**

- Restore `EmbedRegistry.h` from the master-branch shape (`EmbedRequest` struct: `targetPath`, `sourcePath`, `displayText`, `depth`; `EmbedFactory = function<unique_ptr<MarkdownRenderChild>(const EmbedRequest &)>`; `register_(QString ext, EmbedFactory)`; `dispatch(EmbedRequest) const`).
- Restore `MarkdownRenderChild` value-typed result holder.
- Restore `EmbedDepthGuard` (small RAII helper).
- Add to `MarkoffServices`: `EmbedRegistry *embeds = nullptr;`.

### D6. `MermaidRenderer` abstract interface restored

**Decision:** Restore `Markoff::MermaidRenderer` abstract at `libs/markoff-core/include/markoff/core/MermaidRenderer.h`. Pure virtual `renderToSvg(const QString &diagram, const QSize &) const`. No default concrete in core; host provides via `MarkoffServices::mermaid`.

**Why:** Mermaid is user-flagged nice-to-have, but the renderer pointer was the host-injection seam Corbomite already used. Restoring it now means E5 doesn't need to invent a new seam.

**How to apply:** Restore the abstract from master-shape; add `MermaidRenderer *mermaid` to `MarkoffServices`.

### D7. Vault seam restored as abstract interfaces; no default concretes in core

**Decision:** Restore the `Markoff::Vault::` namespace with five public types, all abstract or value-typed:

- `Vault::CachedMetadata` — value type. Fields: `headings`, `blocks`, `sections` (per the master shape used by Corbomite's `MarkoffAdapters.cpp`). Optionals so unfilled fields don't carry empty containers.
- `Vault::LinkResolver` — abstract: `virtual QString resolve(const QString &linkText, const QString &fromPath) const = 0`.
- `Vault::MetadataCache` — abstract: `virtual const CachedMetadata *getFileCache(const QString &path) const = 0`.
- `Vault::MetadataParser` — abstract: parses raw markdown to `CachedMetadata`.
- `Vault::ResourceProvider` — abstract: `virtual std::optional<QString> resolveEmbed(const QString &name) const = 0`; image/asset lookup.

No `DefaultLinkResolver` / `DefaultMetadataCache` ship in `markoff-core`; hosts provide concretes. (A `DefaultResourceProvider` returning `std::nullopt` ships purely to keep the standalone test app buildable without a vault.)

**Why:** Corbomite has these concretes already; the old vault types are what `MarkoffAdapters.cpp` converted into. Restoring the abstracts lets Corbomite drop the conversion layer entirely and implement the interfaces directly. The asymmetry vs. D5/D6 (no default concretes) is intentional: vault behaviour is host-specific by definition (filesystem layout, link-resolution policy, metadata schema), so a "default" would only ever be a useless stub.

**How to apply:**

- Restore the five types under `libs/markoff-core/include/markoff/core/vault/`.
- Add to `MarkoffServices`: `Vault::LinkResolver *linkResolver = nullptr; Vault::MetadataCache *metadataCache = nullptr; Vault::ResourceProvider *resourceProvider = nullptr;`.
- `LinkService` (the existing abstract that handles classify/resolve/activate/notifyHover for clicks) stays — it's the UI-event seam. `Vault::LinkResolver` is the path-resolution seam. They cooperate: a default `LinkService` impl can call `LinkResolver::resolve` to turn `[[Page]]` into a filesystem path.

### D8. `Session` scroll API gains `TextAnchor` companions

**Decision:** Add three CRDT-free wrappers to `Session`:

- `TextAnchor topVisibleTextAnchor() const`
- `void setTopVisibleTextAnchor(const TextAnchor &, qreal yOffset)`
- `void scrollChangedTextAnchor(const TextAnchor &, qreal yOffset)` (signal)

The existing `topVisibleAnchor()` / `setTopVisible(Crdt::Anchor, qreal)` / `scrollChanged(Crdt::Anchor, qreal)` stay; they are now "Heavy CRDT companion" overloads. Hosts choose which they consume.

**Why:** Audit B's wrapper-coverage gap. A consumer choosing the CRDT-free path can't currently scroll-restore without falling back to `Crdt::Anchor`.

**How to apply:** Add three thin methods to `Session.cpp` that bridge via `Crdt::Anchor` internally. Add a docstring on `Session.h` naming both surfaces.

### D9. `FoldRef::start` gains a `TextAnchor` mirror; JSON wire format pinned

**Decision:** Add `TextAnchor startAnchor() const` and `void setStartAnchor(const TextAnchor &)` to `FoldRef`. The underlying `Crdt::Anchor` field remains; `startAnchor()` is a non-storing accessor. Document the JSON serialization schema in a header docstring on `FoldRef.h` so hosts that persist folds know the wire format.

**Why:** Audit B's second wrapper gap. Same pattern as D8.

**How to apply:** Add the two accessors; docstring lists the JSON keys (`start`, `end`, `kind`, `attrs`) and references `AnchorJson` for `TextAnchor` round-tripping.

### D10. `Markoff::Detail::` namespace policy

**Decision:** The following types move under `Markoff::Detail::`. Their headers stay at their current paths under `markoff/core/` so sister leaves can include them; their *namespace* changes. Public consumers (Corbomite) MUST NOT reference them by name in production code.

| Type | Current namespace | Becomes |
|---|---|---|
| `Cmd::*` (D2) | `Markoff::Cmd` | `Markoff::Detail::Cmd` |
| `BufferProxy`, `IdListProxy`, `SiblingMapProxy` (D3) | `Markoff` | `Markoff::Detail` |
| `SearchEngine` | `Markoff` | `Markoff::Detail` |
| `UndoLog` | `Markoff` | `Markoff::Detail` |
| `MarkoffOp`, `MarkoffBundleMeta`, `MarkoffSerializer` | `Markoff` | `Markoff::Detail` |
| `BlockEdit`, `StructuralOp` | `Markoff` | `Markoff::Detail` |
| `KindTagMap`, `CausalLwwMap` | `Markoff` | `Markoff::Detail` |
| `WatermarkCoordinator` | `Markoff` | `Markoff::Detail` |
| `InlineParseCache` | `Markoff` | `Markoff::Detail` |
| `RenderedBlock` | `Markoff` | `Markoff::Detail` |
| `SiblingMapOpHeader` | `Markoff` | `Markoff::Detail` |
| `AnchorJson` (functions) | `Markoff::AnchorJson` | `Markoff::Detail::AnchorJson` |
| `SourceTextDocumentBinding` | `Markoff` | `Markoff::Detail` |

**Why:** Audit observation: each of these is shipped publicly only because a sister leaf needs it. None is part of the Corbomite-facing contract. Moving the *namespace* (without moving the header) preserves sister-leaf access while marking the symbol as internal. Future agents reading the code see `Markoff::Detail::Cmd::changeKind` and know "this is sister-leaf plumbing."

**Open exceptions:** `SearchEngine` was the candidate seam for find-in-doc until `FindController` shipped; with the controller in place, `SearchEngine` is purely the controller's worker — clearly Detail. `UndoLog` is a borderline case (a host that wants to coalesce edits across multiple `applyFlatEdit` calls would want it); the freeze treats it as Detail and adds `MarkoffDocument::beginTransaction()` / `endTransaction()` *if* a real Corbomite need surfaces during reintegration. Until then, `UndoLog` stays Detail.

**How to apply:** Namespace migration per the table. ~40 type renames; mechanical. Sister-leaf `#include`s do not change.

### D11. Sister-leaf privilege documented

**Decision:** Add a top-of-`Markoff::Detail::` docstring (in a new `libs/markoff-core/include/markoff/core/Detail.h` umbrella that's *not* shipped via `MarkoffServices` — just a documentation anchor) stating:

> *`Markoff::Detail::` is reserved for sister-leaf consumption. `markoff-live`, `markoff-source`, and any future view leaf may include and call these symbols. Hosts (Corbomite, third-party consumers) MUST NOT. The names will move/rename freely between minor versions; only `Markoff::` (non-Detail) is the stable contract.*

**Why:** Without an explicit policy doc, "Detail" is just a name. Naming the privilege makes the boundary load-bearing.

**How to apply:** One new header (~30 lines of comment, no code), referenced from the migration guide.

### D12. `applyFlatEdit` coordinate semantics docstring + companion naming

**Decision:** Add a multi-paragraph docstring to `MarkoffDocument::applyFlatEdit` naming both views:

- `flatView()` — separator-bearing, the QPlainTextEdit-compatible string with `"\n\n"` between blocks.
- `applyFlatEdit(int qtPosStart, int qtPosEnd, const QString &insertedText, Transaction &)` — operates on `flatView()` coordinates. Source-widget contract.

The docstring names the conversion helpers (`Coordinates::qtPosToByte`, `Coordinates::byteToQtPos`, both currently in `Markoff::Live::Detail::` per the live-freeze) and explicitly states that `applyFlatEdit` is the supported path for structural edits originating from text-flat surfaces.

**Why:** Audit observation #9. The current docstring undersells the entry point — `applyFlatEdit` is *the* structural-edit primitive, but reads like one method among many. Pinning the semantics with a paragraph of prose makes the contract durable.

**How to apply:** Edit `MarkoffDocument.h` docstring; cross-reference `flatView()` and the conversion helpers.

### D13. Source-widget separator-zone-edit bug — fixed before freeze

**Decision:** The latent limitation logged in the Discipline Log on 2026-05-18 (`SourceTextDocumentBinding.cpp:onQtContentsChange`, inv #3) — separator-zone deletes (backspace at start of block, removing the `\n\n` between blocks) translate to a zero-length cursor edit and the subsequent `onD2DocumentChanged` reverts the user edit — is **fixed before freeze**. This is not documented-as-limitation; it is fixed.

**Why:** A markdown source widget that can't backspace across block boundaries is not v1.0 quality. Corbomite's existing source widget supports this; the regression would block reintegration. Two technical paths:

1. **Sep-bearing `applyFlatEdit` variant**: a new `applyFlatEditSepAware(int qtPosStart, int qtPosEnd, const QString &insertedText)` that operates on `flatView()` coordinates *with* the separator zones; routes separator-only deletes to a structural merge command.
2. **Per-block edit routing**: `SourceTextDocumentBinding::onQtContentsChange` detects separator-zone edits before forwarding and issues `Cmd::merge` directly.

Recommendation: path 2. Keeps `applyFlatEdit` semantically clean (it operates on content, not structure) and confines the structural detection to the binding layer where it belongs. Path 1 conflates concerns.

**How to apply:** Implementation plan separately; the freeze spec commits to the fix landing as part of the freeze-pass commit batch (not deferred).

### D14. `MarkoffDocument::frontmatter()` exposes `FrontmatterMap` for property-editor consumers

**Decision:** Add a public accessor `const Detail::FrontmatterMap &frontmatter() const` returning the per-document YAML frontmatter map, plus a structured-edit entry point `void applyFrontmatterEdit(const QString &key, const QVariant &value, Transaction &)`. The `FrontmatterMap` itself moves to `Detail::` per D10, but the accessor is part of the public surface — consumers read keys, write through the structured entry.

**Why:** Corbomite's `PropertyEditorWidget` edits frontmatter as a structured side panel. Without a public accessor, the host has to reach through `Markoff::Detail::` symbols to read keys, violating D11.

**How to apply:** Two methods on `MarkoffDocument`. The signal `frontmatterChanged(const QString &key)` already exists internally; promote to public.

### D15. `MarkoffDocument::wordCount()`

**Decision:** Add `int wordCount() const` and signal `wordCountChanged(int)`. Computed lazily on `d2DocumentChanged`; counts whitespace-separated tokens across all block buffers excluding frontmatter and HR / Image / Math / CodeBlock-with-no-text blocks.

**Why:** Corbomite's status bar exposes word count today via `Editor::wordCountChanged`. Without it, every consumer either does the computation themselves (duplication) or shows no count. Two-method add. Tiny.

**How to apply:** Implement on `MarkoffDocument`. Cache the value behind a "needs-recompute" flag set by `d2DocumentChanged`; emit only on cross-threshold change to avoid signal spam on every keystroke.

### D16. `Selection::toJson`/`fromJson` wire format pinned

**Decision:** Document the JSON schema for round-tripped selections. Schema:

```json
{
  "anchor": { "block": "<BlockAnchor JSON>", "byte": 42, "qtPos": 5 },
  "active": { "block": "<BlockAnchor JSON>", "byte": 53, "qtPos": 8 },
  "kind": "TextRange"
}
```

`<BlockAnchor JSON>` is `AnchorJson::toJson(blockAnchor)` (now under `Detail::AnchorJson` per D10, but the wire format is public). Hosts that persist selections (session restore, undo/redo across reload) consume this schema.

**Why:** Audit observation #8. Without a pinned schema, hosts that persist selections do so against an implicit contract that breaks silently on internal refactors.

**How to apply:** Docstring on `Selection.h` + `Selection.cpp` validates incoming JSON against the schema and rejects malformed input. Add `tst_selection_json_roundtrip` to pin the schema with a fixture corpus.

### D17. CRDT-asymmetric policy documented at header level

**Decision:** Add a header-level docstring to `MarkoffDocument.h` and `Session.h` stating the asymmetric CRDT policy from `[[project-crdt-api-policy]]`:

> *This header transitively includes `<collabtext/...>` types. Consumers compile against collabtext via this include. View leaves (`markoff-live`, `markoff-source`) deliberately avoid CRDT-typed accessors in their public headers; consumers who want a CRDT-free build path should consume those leaves only and use the CRDT-free wrappers (`TextAnchor`, `BlockAnchor`, `editSequence()`, scroll-text-anchor variants per D8) when consuming `markoff-core`.*

**Why:** Hosts (Corbomite) need to know upfront that linking `markoff-core` pulls collabtext into their compile graph. Documenting it at header-level catches the surprise before the build error does.

**How to apply:** Three-paragraph docstring on the two headers. Migration guide reproduces.

### D18. No umbrella headers

**Decision:** No `markoff/core.h` aggregate header. Consumers include individual headers à la carte, matching `markoff-live` and `markoff-source`.

**Why:** Umbrella headers grow without bound, pay compile time for code the consumer doesn't use, and re-export internal symbols if anything Detail-namespaced lands in the umbrella. The audit named this as a deferred decision; the freeze answers "no."

**How to apply:** No action. Migration guide documents the convention.

### D19. Service-registration timing pinned

**Decision:** Services (`MarkoffServices` aggregate, per D4) MUST be installed via `MarkoffDocument::setServices` *before* `setDocument` is called on any view leaf. Hosts that wire a service after `setDocument` get a runtime warning (qWarning) and the late service is ignored — no silent partial wiring.

**Why:** Audit cross-cutting #5. The current implicit order is "whatever happens to work" — fragile. Pinning the order makes the contract testable and the failure mode visible.

**How to apply:** `MarkoffDocument::setDocument` (currently a no-op called from view leaves) is renamed `MarkoffDocument::seal()` semantically — once called, `setServices` becomes a warning-and-no-op. Add a `tst_service_registration_order` slot that asserts the warning fires.

---

## Cross-leaf coordination

This freeze touches surfaces that other freezes must echo. Two amendments to existing specs are required; one new mini-spec is recommended.

### E1. `markoff-live` freeze amendment: `Capabilities::Editable`

The `LiveListModelBinding::Capabilities` flags today are `Clipboard | Format | Actions | Session`. None of them disables typing into the inline `TextEdit`s of the delegates. To reuse Live as a reading view (per 2026-05-20 user direction), add `Editable` as a fifth capability flag (default ON for `AllCapabilities`). When OFF:

- Every text-bearing delegate's `TextEdit.readOnly` binds `!binding.editable`.
- `LiveStructuralKeyHandler::tryHandle` returns false unconditionally.
- The action controller's editing actions (Cut/Paste/Bold/Italic/Link/etc.) report disabled.
- The find adapter's `setCaretWithoutFocus` path stays enabled (read-only views still place a caret on find navigation).

This belongs in an amendment to `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md` as a new D-row.

### E2. `Markoff::Live::EditorWidget` QQuickWidget wrapper

Today's Live entry point is `LiveListModelBinding` (a QObject) + `LiveView.qml` driven by `QQmlApplicationEngine`. The test app does this with full QML hosting; Corbomite is a QWidget app that needs a `QQuickWidget` containing `LiveView.qml`.

Ship `Markoff::Live::EditorWidget` — a `QQuickWidget` subclass that owns a `LiveListModelBinding`, loads `LiveView.qml`, and subclasses `Markoff::MarkdownView` so the host's `MarkdownView *activeLeaf()` polymorphism stays intact.

Belongs in its own mini-spec (`docs/specs/2026-05-20-live-editor-widget.md`), referenced from this freeze. The freeze names it as required for Corbomite reintegration; the mini-spec pins its shape.

### E3. Source freeze: still stands

The source-leaf freeze spec D1 (forwarders deletion), D3 (namespace flatten), D4 (Gutter to Detail) are landed. D2 (FindBar) and D5 (showFindBar ordering) were superseded by the find work. No further amendments needed.

---

## New public surface (post-freeze)

This table is the freeze contract. After the freeze pass, every consumer-facing symbol in `markoff-core` appears here, and nothing else does.

### Consumer UI types

| Symbol | Header | Role |
|---|---|---|
| `Markoff::ActionId` | `core/ActionId.h` | Formatting action enum. |
| `Markoff::CursorPos` | `core/CursorPos.h` | `{line, column}`, 1-based. |
| `Markoff::Theme` | `core/Theme.h` | Q_GADGET. Color palette + per-slot fonts + callout accents. |
| `Markoff::EditorContext` | `core/EditorContext.h` | Current-block context for action-enable logic. |
| `Markoff::BlockKindNames` | `core/EditorContext.h` | String constants. |
| `Markoff::LinkActivation` | `core/LinkActivation.h` | Link click payload. |
| `Markoff::LinkKind` | `core/LinkKind.h` | Link classification enum. |
| `Markoff::MarkdownView` | `core/MarkdownView.h` | Abstract base for view leaves. |
| `Markoff::Cursor` | `core/Cursor.h` | `NoCursor | TextCaret | BlockSelected | BlockInternalEdit` variant. |

### Document API

| Symbol | Header | Role |
|---|---|---|
| `Markoff::MarkoffDocument` | `core/MarkoffDocument.h` | The CRDT-backed document. Add: `wordCount()` (D15), `frontmatter()`/`applyFrontmatterEdit()` (D14), `seal()`/`setServices()` ordering (D19), `applyFlatEdit` docstring (D12). |
| `Markoff::Session` | `core/Session.h` | Per-view session. Add: `topVisibleTextAnchor()`, `setTopVisibleTextAnchor()`, `scrollChangedTextAnchor()` (D8). |
| `Markoff::Selection` | `core/Selection.h` | `(anchor, active, kind)`. Add: pinned JSON schema (D16). |
| `Markoff::TextAnchor` | `core/TextAnchor.h` | CRDT-free wrapper around `Crdt::Anchor`. |
| `Markoff::BlockAnchor` | `core/BlockAnchor.h` | Block identity (D1 — canonical public spelling). |
| `Markoff::Origin` | `core/Origin.h` | Edit-origin enum. |
| `Markoff::FoldRef` | `core/FoldRef.h` | Fold region. Add: `startAnchor()` `TextAnchor` mirror + JSON docstring (D9). |
| `Markoff::PasteMeta` | `core/PasteMeta.h` | Structured-paste metadata. |
| `Markoff::SessionParams` | `core/SessionParams.h` | Session ctor params. |

### Service interfaces (host registers concretes via `MarkoffServices`)

| Symbol | Header | Role |
|---|---|---|
| `Markoff::MarkoffServices` | `core/MarkoffServices.h` | Aggregate (D4). Adds: `embeds`, `mermaid`, `linkResolver`, `metadataCache`, `resourceProvider`. |
| `Markoff::LinkService` | `core/LinkService.h` | UI-event seam (classify/resolve/activate/notifyHover). |
| `Markoff::DefaultLinkService` | `core/DefaultLinkService.h` | Concrete demo. |
| `Markoff::SyntaxHighlightService` | `core/SyntaxHighlightService.h` | Abstract. |
| `Markoff::Kf6SyntaxHighlightService` | `core/Kf6SyntaxHighlightService.h` | KF6 concrete. |
| `Markoff::CompletionProvider` | `core/CompletionProvider.h` | Abstract. |
| `Markoff::CompletionRegistry` | `core/CompletionRegistry.h` | Registry. |
| `Markoff::CompletionTrigger`, `CompletionCandidate`, `CompletionContext`, `CompletionDetector` | (same) | Completion infra. |
| `Markoff::CodeBlockProcessor` | `core/CodeBlockProcessor.h` | Abstract. |
| `Markoff::CodeBlockProcessorRegistry` | `core/CodeBlockProcessorRegistry.h` | Registry. |
| `Markoff::CodeSpan`, `CodeTokenKind` | (same) | Syntax span + token enum. |
| `Markoff::BlockSerializer` | `core/BlockSerializer.h` | Function typedef. |
| `Markoff::BlockSerializerRegistry` | `core/BlockSerializerRegistry.h` | Registry. |
| **`Markoff::EmbedRegistry`** (D5) | `core/EmbedRegistry.h` | Abstract. Restored. |
| **`Markoff::EmbedRequest`** (D5) | `core/EmbedRegistry.h` | Value type. Restored. |
| **`Markoff::EmbedDepthGuard`** (D5) | `core/EmbedDepthGuard.h` | RAII helper. Restored. |
| **`Markoff::MarkdownRenderChild`** (D5) | `core/MarkdownRenderChild.h` | Value-typed result holder. Restored. |
| **`Markoff::MermaidRenderer`** (D6) | `core/MermaidRenderer.h` | Abstract. Restored. |

### Vault interfaces (D7)

| Symbol | Header | Role |
|---|---|---|
| `Markoff::Vault::CachedMetadata` | `core/vault/CachedMetadata.h` | Value type (`headings`, `blocks`, `sections`). |
| `Markoff::Vault::LinkResolver` | `core/vault/LinkResolver.h` | Abstract: link-text → path. |
| `Markoff::Vault::MetadataCache` | `core/vault/MetadataCache.h` | Abstract: path → CachedMetadata. |
| `Markoff::Vault::MetadataParser` | `core/vault/MetadataParser.h` | Abstract: markdown → CachedMetadata. |
| `Markoff::Vault::ResourceProvider` | `core/vault/ResourceProvider.h` | Abstract: name → resolved path. |
| `Markoff::Vault::DefaultResourceProvider` | `core/vault/DefaultResourceProvider.h` | Stub concrete (returns `std::nullopt`); standalone-test buildability only. |

### Find (already landed)

| Symbol | Header | Role |
|---|---|---|
| `Markoff::FindController` | `core/FindController.h` | Session-scope find loop (landed 2026-05-20). |

### Detail (sister-leaf-only)

The symbols listed in D10's table become `Markoff::Detail::*`. Header paths unchanged; consumers MUST NOT reference. Migration guide names this rule.

---

## Migration / commit plan

The freeze pass is large but parallelizable. Recommended ordering across ~12 commits behind tag `v0.7.0-e3a`:

### Pass 1 — Detail-namespace migration (D2, D3, D10) — 1 commit

Mechanical namespace move for ~13 type families. Headers stay in place. Sister-leaf call sites update in the same commit. Test count unchanged.

### Pass 2 — `BlockAnchor` canonical spelling (D1) — 1 commit

Rewrite public signatures across `markoff-core`. Test count unchanged.

### Pass 3 — Vault restoration (D7) — 2 commits

1. Add the five abstract headers + value type + stub `DefaultResourceProvider`. Add to `MarkoffServices`.
2. Add `tst_vault_seam` covering the abstract contracts (mock concretes, assert call sequencing). Test count +1.

### Pass 4 — Embed + mermaid restoration (D5, D6) — 1 commit

Add `EmbedRegistry`/`EmbedRequest`/`EmbedDepthGuard`/`MarkdownRenderChild` + `MermaidRenderer` abstracts. Add to `MarkoffServices`. Add `tst_embed_registry_dispatch` (mock factory; assert dispatch routes by extension). Test count +1.

### Pass 5 — Wrapper coverage (D8, D9) — 1 commit

`Session::topVisibleTextAnchor` etc. + `FoldRef::startAnchor`. Add `tst_session_text_anchor_roundtrip` + `tst_fold_ref_text_anchor`. Test count +2.

### Pass 6 — `MarkoffServices` canonicalization (D4) — 1 commit

Migrate the four-way wiring to aggregate-first. Update test fixtures. Granular setters move to `Detail::`. Test count unchanged.

### Pass 7 — Document API hygiene (D12, D14, D15, D16, D19) — 2 commits

1. `wordCount()` + `frontmatter()`/`applyFrontmatterEdit()` + `seal()` ordering + service-late warning. Tests: `tst_word_count`, `tst_frontmatter_property_editor`, `tst_service_registration_order`. Test count +3.
2. `applyFlatEdit` docstring (no code change) + `Selection::toJson` schema fixture corpus. Test: `tst_selection_json_roundtrip`. Test count +1.

### Pass 8 — Source separator-zone-edit fix (D13) — 1 commit

Per-block edit routing in `SourceTextDocumentBinding::onQtContentsChange`. Tests: `tst_source_separator_zone_backspace` (the bug-class fixture), `tst_source_separator_zone_paste` (cross-block paste through separator). Test count +2.

### Pass 9 — Header-level docstrings (D11, D17, D18) — 1 commit

Add `Detail.h` policy header (D11), CRDT-asymmetry docstrings on `MarkoffDocument.h` + `Session.h` (D17), à-la-carte note in migration guide (D18). No code changes; doc only.

### Pass 10 — Migration guide draft — 1 commit

`docs/handoff/2026-05-XX-markoff-core-migration-guide.md` for Corbomite. Per-symbol porting table: old name (master) → new name (foundation-exploration) → consumer policy. Cross-cutting sections: CRDT policy, service registration, Detail namespace, vault implementation pattern.

**Expected test delta:** +10 tests across the freeze pass. Existing 215 → 225 fast-suite, pre-existing 3 failures unchanged.

**Cross-leaf commits (separate spec/plan):**
- E1 (Live `Capabilities::Editable`) — companion plan against the live freeze amendment.
- E2 (`Markoff::Live::EditorWidget`) — companion mini-spec + plan.

### Tag

After all 12 commits land and Corbomite has run a smoke port against the new headers, tag `v0.7.0-freeze`. Migration guide hyperlinks the tag.

---

## Testing strategy

Four layers:

1. **Unit — restored types.** Each restored interface (`EmbedRegistry`, `MermaidRenderer`, the five vault types) gets a `tst_*_seam` test pinning the abstract contract via a mock concrete.
2. **Unit — wrapper coverage.** `tst_session_text_anchor_roundtrip` + `tst_fold_ref_text_anchor` pin the CRDT-free path round-trips through the same canonical state as the CRDT-typed path.
3. **Unit — small additions.** `tst_word_count`, `tst_frontmatter_property_editor`, `tst_selection_json_roundtrip`, `tst_service_registration_order`.
4. **Integration — sister-leaf compile-check.** No new test; the existing sister-leaf tests must build under the renamed `Markoff::Detail::` namespaces. Build failure during Pass 1 is the test.

Falsifiability check, per `docs/INVARIANTS.md` invariant 4:

- `tst_service_registration_order` MUST be proven falsifiable by temporarily allowing late `setServices` and confirming the test fails.
- `tst_source_separator_zone_backspace` MUST be proven falsifiable by reintroducing the no-op zero-length-cursor path and confirming the test fails.

---

## Risks

- **`MarkoffDocument` is the most-included header in the project.** Any signature change there (D1 `BlockAnchor` rewrite, D14 frontmatter accessors, D15 wordCount, D17 docstring, D19 seal-semantics) cascades to every leaf. Mitigation: Pass 1 (Detail namespace) and Pass 2 (BlockAnchor) are mechanical and compile-checked; the substantive additions (Passes 3–8) happen against a stable post-rename baseline.
- **Vault restoration tests in Corbomite, not Markoff.** No standalone Markoff test exercises a real vault concrete — the tests use mock implementations. The first real falsifier is Corbomite's reintegration. Mitigation: keep the vault abstracts narrow and version them in `v0.7.0-freeze`; if Corbomite reintegration surfaces a missing accessor (e.g. headings-by-line range query), version-bump to `v0.7.1-freeze` rather than rework.
- **`Markoff::Detail::` policy depends on consumer discipline.** A consumer that ignores D11 and reaches through `Markoff::Detail::Cmd::changeKind` gets a working build today and a broken one tomorrow. Mitigation: `Detail.h` policy header is the only mechanism; complement with a `[[deprecated("Markoff::Detail:: is sister-leaf-only — see <core/Detail.h>")]]` attribute on each Detail-namespaced type? Decision deferred to spec review.
- **`MarkoffServices::seal()` semantic is new authority.** Per invariant 3 (a new authority retires the old one), removing the per-service late setters must happen in the same pass. Pass 6 covers this; the post-mortem will land in the discipline log if anything slips.
- **Source separator-zone-edit fix has cross-block-undo implications.** `Cmd::merge` inside `SourceTextDocumentBinding::onQtContentsChange` opens a new transaction; consecutive separator-zone backspaces should coalesce. Mitigation: the existing `UndoLog` transaction-batching path covers this; the test `tst_source_separator_zone_backspace_undo_coalesces` (Pass 8) pins it.
- **`MarkoffDocument::wordCount()` cache invalidation is per-edit, not per-keystroke.** The signal threshold (D15) avoids per-keystroke emission; the per-block computation re-runs every `d2DocumentChanged`. For very large documents this may be measurable. Mitigation: profile against the 73-kB foundation-design.md fixture used by `perf_load_bench`; if measurable, add a per-block cached count keyed by block edit-sequence.

## Open questions

1. **`MarkoffDocument::beginTransaction()` / `endTransaction()` as a public API?** D10 keeps `UndoLog` Detail-namespaced but acknowledges a borderline case for hosts wanting to coalesce edits across multiple `applyFlatEdit` calls. Decision: defer until a real Corbomite need surfaces. If it does, add the methods in a `v0.7.1-freeze` minor bump.
2. **`[[deprecated]]` attribute on Detail types?** See Risks. Decision: open for spec-review feedback.
3. **`Vault::MetadataCache` lifetime contract.** Audit-defined return type is `const CachedMetadata *` (raw pointer). Corbomite today uses `std::shared_ptr` internally and converts. Should the abstract require shared ownership, or stay raw-pointer (host responsible for lifetime)? Decision: raw pointer with a docstring contract that the cache owns the returned `CachedMetadata` and may invalidate it on `notifyFileChanged()` (which the abstract does not currently expose). Adding `notifyFileChanged` is a Corbomite request; punt to reintegration.
4. **`EmbedRegistry::register_` vs `registerExtension`.** Master shape used `registerExtension(QString, EmbedFactory)`. Phase C1 design used `register_` (trailing underscore — `register` is a reserved word). Pick: `registerExtension` reads naturally; `register_` is the C++-language-safe spelling. Decision: `registerExtension`. The trailing-underscore convention is a misfeature.
5. **Reading-leaf restoration.** User confirmed 2026-05-20 that reusing Live-with-editing-disabled is acceptable for now. If reintegration reveals real perf issues with that strategy (HoverPopover allocating a full LiveListModelBinding per hover, etc.), revisit by drafting a `markoff-reading-lite` mini-spec at that point. The freeze does not preclude.
