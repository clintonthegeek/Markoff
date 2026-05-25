# E-arc framing — Live-render completion as the maximalist prototype

**Date:** 2026-05-08
**Branch:** `exploration/new-foundation`
**Status:** approved (this doc establishes the arc; phase-level specs follow)
**Predecessors (the inputs this synthesises):**

- `docs/specs/2026-04-29-live-render-design.md` — walking-skeleton design; §7 "Approach 3" is the destination shape
- `docs/specs/2026-04-30-live-editing-design.md` — names cursor-aware delimiter hiding as "additive" future work
- `docs/specs/2026-05-01-live-projection-layer.md` — `InlineFormatHighlighter`-as-`InlinePrediction`-producer architecture
- `docs/specs/2026-04-29-footnote-cleanup-design.md` §"Deferred Obsidian quirks" — footnote rendering gap
- `docs/specs/2026-04-20-tri-view-unified-api-design.md` — the Obsidian-equivalent three-view goal
- `docs/handoff/2026-05-07-pivot-to-d5-first.md` — collab-first posture; this doc complements §4.6
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` (with 2026-05-08 erratum) — `inlineSpansFor` is load-bearing infrastructure, not dead code

**Companion (live status):** `docs/e-arc/e-arc-status.md`.

---

## 0.1 Amendment (2026-05-08): §4.6 prerequisite removed; E-arc begins now

The §4 prerequisite list called out three D-arc bookend items as required
before E-arc could begin: D5 implementation, §4.5 audit, and pivot-doc
§4.6 (public-API freeze + Corbomite migration). The first two are done.
**§4.6 is removed from the prerequisite list.** Pivot-doc §4.6 is deferred
until Corbomite is ready to consume the freeze; E-arc begins now in §4.6's
slot.

The §4 argument *"if §4.6's freeze re-shapes a primitive E1 was about to
use, E1 wastes work"* is replaced by the inverse: freezing without a ready
Corbomite consumer is freezing in the dark, and the seams E1–E5 will
surface are exactly the input §4.6 needs to do its job. Decision record:
`docs/handoff/2026-05-08-defer-46-to-e-arc.md`.

§4 below is left as-written for the historical record; this addendum
overrides the §4.6 bullet there. §1.2's statement that E-arc internals are
out-of-scope for the §4.6 freeze still stands and is unaffected by this
amendment.

---

## 0. TL;DR

Phase E is the arc that turns Markoff's live-render view from "block-broken plaintext editor" into a **WYSIWYG-leaning Obsidian-equivalent live-preview widget**. It is the work that makes "live render" mean what the term implies: rendered inline formatting with cursor-aware reveal of source delimiters, rich block kinds (math, mermaid, tables, callouts), and the wikilink/embed/tag affordances that distinguish a notes-app markdown editor from a generic markdown editor.

E-arc is treated as the **maximalist case**: the QML live-render view is the ice-breaker experimental prototype, the most feature-rich consumer of `MarkoffDocument`. **Every other view Markoff will ever ship is a structural subset of what live-render does.** Source mode is "live-render minus rendering minus delimiter-hide." Reading mode is "live-render minus inline editing minus auto-hide minus speculative state, plus virtualisation." Custom embedded-render-only views (e.g. a Reading-style preview pane inside a chat client) are progressive simplifications.

E-arc bookends with a **distillation phase** (post-E) that extracts the foolproof recipe for generalising Markoff into new view shapes. After distillation, new widget development is no longer architectural pioneering — it's recipe-following.

This framing decision is made now, before E begins, because every E-phase decision benefits from being made under the maximalist-prototype constraint. Decisions that look "live-render-specific" in isolation become "the master template for view N" when read against this framing.

---

## 1. Frame

### 1.1 Why this framing

Markoff's prior design momentum was driven by *consumer-pull*: Corbomite needed a Reading view, so `markoff-reading` got built. Corbomite needed a Source widget, so `markoff-source` got built. Live-render was treated as parallel to those, when the architectural reality is that live-render is the **superset** of the others.

A live-preview view that renders inline formatting *with cursor-aware delimiter visibility* must:

- Own the raw block source (because hiding `**` requires knowing where it is in source)
- Render block-kind-specific styling (the same way Reading does — same Theme, same block delegates)
- Run inline syntax highlighting (the same way Source might, but applied to rendered text rather than monospace source)
- Apply per-character formatting at the same speed as a typing keystroke (because typing is the dominant operation)
- Coexist with collaborative remote-cursor presence (the same way collab-aware Source might)
- Handle structural editing (Enter / Backspace-merge / Tab-promote — the same as Source structural keys)
- Survive remote ops landing mid-edit (the same as Source under collab)

Every one of those capabilities is required by *some other* view in the family. Live-render needs *all* of them simultaneously. So building live-render correctly produces the full toolkit; building any other view correctly produces a subset of that toolkit.

The ice-breaker framing follows: live-render is where the architectural unknowns are confronted, where the test surface is densest, where the user-visible bugs surface fastest, and where the lessons compound. Every architectural choice made under E-arc becomes either part of the recipe (if it generalises) or an explicit live-render-specific override (if it doesn't).

### 1.2 What this is not

Phase E is **not** a feature wishlist or a Corbomite-driven roadmap. The phases below are ordered by foundational dependency, not by user-visible priority. E1 (inline-format highlighter) precedes E2 (cursor-aware delimiter hide) because the highlighter is the carrier the hide-flag rides on. E5 (math/mermaid Live-mode parity) comes last because by then every supporting primitive (delegate registration, theme integration, embed contract) is settled.

Phase E is also **not** a lock-step plan. Each E-phase is its own spec → plan → implementation cycle, mirroring D2/D3/D4. Phase ordering is suggestive; if E3 (wikilinks/embeds/tags/callouts) surfaces a primitive that E2 (delimiter hide) needed, the dependency rearranges. Re-orderings get a single recent-changes log entry in `e-arc-status.md` and don't need a new framing doc.

Phase E is **not** in scope for the v0.X public-API freeze (pivot-doc §4.6). The freeze targets `MarkdownView`, `MarkoffDocument`, and the consumer primitives that already exist. E-arc internal types (`InlineFormatHighlighter`, projection-layer types, delegate registries) are widget-internal and stay flexible until distillation extracts the stable contracts.

**Out of scope for E-arc** (mentioned here so phase-spec authors don't smuggle them in):

- **Mobile platforms.** Touch event handling (long-press menus, tap-and-hold), virtual-keyboard interactions, gesture recognisers. Markoff is a desktop Qt-Widgets/QML widget at E-arc; mobile is a candidate post-distillation arc.
- **Accessibility beyond Qt defaults.** Screen-reader custom semantics, high-contrast theme overrides, motion-reduction preferences. `Qt::AccessibleText` defaults are inherited; deeper a11y is a future arc.
- **I18n beyond `tr()`.** RTL layout in delegates, locale-driven date/number formatting inside frontmatter or embed renderings, bidi text in inline spans. `tr()` for user-visible strings is the existing convention; deeper i18n is out.

These are candidate scopes for future arcs. They are explicitly not E-arc concerns; phase specs that touch them must scope-trim or reject the touch.

### 1.3 What this enables

By the time E-arc closes:

- Live-render is feature-complete for an Obsidian-equivalent live-preview pane
- The full inventory of features any view might need has been built once, with tests, in production
- The architectural invariants that make live-render correct under collab are proven on the most complex case
- The performance envelope is measured under realistic load (long documents, many remote peers, all delegate kinds active)
- The pluggability hooks (code-block processors, block-kind transformers, custom kinds, themes) have a real consumer exercising them

Distillation then produces:

- A view-construction recipe: "to build a new view, start with the live-render scaffold and explicitly subtract these capabilities"
- A capability matrix: which features have hidden dependencies on which others
- A test-template library: each layer's test contract reusable across views
- A delegate-authoring recipe: how to write a new block-kind delegate from scratch

After distillation, building "Markoff Email Compose," "Markoff Comment Thread," "Markoff Slide View," etc., is a multi-week project, not a multi-quarter project.

---

## 2. The phase sketch

E-arc has six phases. E1–E5 are feature work; E6 is the distillation bookend.

### E1 — Inline-format highlighter in QML delegates

**Goal.** Every text-bearing delegate (paragraph, heading, list-item, blockquote, code-block info-string, table cell — when tables land) renders inline markdown formatting via a per-delegate `QSyntaxHighlighter` consuming `BlockRecord::inlineSpans` from `LiveBlockModel::spansAtRow(row)`.

**Inputs.**

- The data path is built (D2/D3/D4): `MarkoffDocument::inlineSpansFor(BlockId)` returns `QList<SourceSpan>`, cached per `(BlockId, blockEditSequence)` by `InlineParseCache`.
- The view-qml-era `InlineFormatHighlighter` was the prior consumer. Deleted in `f646c90` along with view-qml. Its design (per-delegate `QSyntaxHighlighter` painting `QTextCharFormat` ranges; bold/italic/strikethrough/inline-code/highlight/link/wikilink/tag) is the reference.
- Theme integration via `Markoff::Theme` — colors, fonts, weights all routed through Theme.

**Acceptance.**

- All current text-bearing delegates render bold, italic, strikethrough, inline-code, link, wikilink, tag, and highlight via styled glyphs.
- Markers themselves render in source form; auto-hide is E2's work.
- Tests cover one fixture per inline kind across each delegate kind (matrix of inline-kind × delegate-kind).
- Performance: per-keystroke cost stays bounded by `InlineParseCache` (cache hit on unchanged blocks; one parse per changed block per keystroke).

### E2 — Cursor-aware delimiter visibility (auto-hide)

**Goal.** Inline-format markers are visually hidden by default; they reappear when the caret enters the span. Typing `**bold**` shows as **bold** with the asterisks invisible until the caret moves into the span, at which point all four asterisks reveal.

**Architectural shape.**

- The highlighter from E1 already knows where the markers are (start/end byte offsets per span).
- Adding hide-mode is a per-span flag on the `QTextCharFormat`: zero-width markers, or a transparency override, or `setProperty(QTextFormat::FullWidthSelection, false)` with `foreground=transparent`.
- Per-delegate cursor watcher: when `cursorPositionChanged` fires, recompute which spans contain the cursor, refresh those spans' formatting (markers visible) and previous-cursor-spans' formatting (markers hidden again).

**Inputs.**

- `2026-04-30-live-editing-design.md` calls this "additive — a cosmetic flag on the highlighter."
- `2026-04-29-live-render-design.md §7` confirms the delegate-owns-raw-source contract is required (Approach 3). D2 made `model.text` source-faithful — Approach 3's data shape is already in place.

**Acceptance.**

- Caret-out: markers invisible, content rendered with formatting applied.
- Caret-in (within the span, anywhere): markers visible.
- Caret-on-marker (e.g. between the two asterisks of `**`): markers visible.
- Caret-adjacent (immediately before opening or after closing marker): policy decision — initial implementation hides; revisit if user feedback indicates otherwise.
- Selection partially covering a span: span markers visible (selection should never lie about source).
- Performance: cursor moves are O(1) — only the spans containing the previous and current cursor positions need refresh.

### E3 — Wikilinks, embeds, tags, callouts

**Goal.** Obsidian-flavoured features that distinguish a notes-app markdown editor from a generic markdown editor.

**Sub-deliverables.**

- **Wikilinks** (`[[Page Name]]`, `[[Page Name|alias]]`, `[[Page#Section]]`, `[[Page#^block-id]]`) — inline kind, link-styled, navigation hook for consumer (`linkActivated(url)` on the document or a sibling signal).
- **Embeds** (`![[Page Name]]`, `![[Page#Section]]`, `![[image.png]]`) — block-level for image/file embeds, inline-render for content embeds. Content-embed rendering is consumer-resolved (consumer provides the embedded content; the delegate renders it). Image embed renders inline if image; renders as block-level for non-image attachments.
- **Tags** (`#tag`, `#nested/tag`) — inline kind, distinct visual treatment, consumer signal on click.
- **Callouts** (`> [!note]`, `> [!warning] Title`, foldable — Obsidian-specific blockquote variant) — block-kind. Adds `CalloutDelegate` with type-driven icon/colour and folded-state.

**Inputs.**

- `2026-04-29-footnote-cleanup-design.md` lists wikilinks/tags/embeds as "Obsidian quirks deferred to Option B" pending live-preview existing — that gating clears at E1 landing.
- Tree-sitter grammar likely needs extension for some of these (depending on what the current `markoff-parser` grammar covers). Grammar work is bundled into the relevant sub-deliverable, not a separate phase.

**Acceptance.**

- Each sub-deliverable: parser support, model-side data, delegate render (block-level) or highlighter span (inline), tests for fixtures, integration with consumer signals.
- Wikilink resolution is consumer-policy: Markoff exposes `linkActivated(url, modifiers)`; the consumer decides what to do with `[[Page]]`.

### E4 — Tables, frontmatter, footnote rendering

**Goal.** Block kinds that exist in the source format but currently have no Live-mode rendering.

**Sub-deliverables.**

- **Tables** — `TableDelegate.qml` renders pipe-syntax tables as a Qt Quick `TableView` or grid layout. Cell editing under E-arc's structural-key contract (Tab navigates cell-to-cell; Enter creates new row; Shift-Enter for line-in-cell). Markdown round-trip is exact.
- **Frontmatter** — YAML frontmatter (already parsed by `markoff-parser`) renders as a foldable header block in Live mode. Default: collapsed; click to expand to source view.
- **Footnote rendering** — the inline `[^foo]` reference renders as a superscript number, hover shows definition, click scrolls to the footnote-definition block at document end. The `[^foo]: definition` block at the end gets a footnote-block delegate with backreference-arrow link.

**Inputs.**

- Footnote design is mostly in `2026-04-29-footnote-cleanup-design.md` Option A (shipped); E4 builds the rendering layer.
- Tables: design pending; spec when E4 begins.
- Frontmatter parsing exists in `markoff-parser`; E4 wires the rendering.

**Acceptance.**

- Each sub-deliverable: parser support (verified existing or extended), delegate, tests for fixtures including round-trip-to-source tests, integration with existing structural-key contract where editing is involved.

### E5 — Math / Mermaid Live-mode parity

**Goal.** Block-level math and mermaid diagram rendering work in Live mode at parity with Reading mode.

**Sub-deliverables.**

- **Math.** `MathDelegate` already exists in `markoff-live` (per the `BlockKindRegistry` registration); E5 confirms it renders inline math (`$x^2$` within a paragraph) as well as block math (`$$\nx^2\n$$` as its own block). Inline math is an inline span in E1's highlighter, replaced by a JKQTMathText render at paint time.
- **Mermaid.** Block-level only (no inline mermaid). `markoff-reading` uses a WebEngineView-based path; E5 either reuses that or builds a Live-mode-specific path. Decision deferred to E5 spec; either approach is valid.

**Inputs.**

- `markoff-reading` already has math + mermaid for Reading mode.
- `JKQTMathText` is a vendored sibling library.
- The plugin extension point for code-block processors (`CodeBlockProcessorRegistry`, foundation-level) is the natural home for Mermaid as a registered processor for `mermaid` info-string.

**Acceptance.**

- A markdown file using inline math, block math, and a mermaid diagram renders correctly in Live mode.
- The Reading-mode and Live-mode renderings are visually consistent (same JKQTMathText settings, same mermaid rendering pipeline if shared).
- Tests: fixture-driven render-comparison tests.

### E6 — Distillation (post-feature bookend)

**Goal.** Extract the foolproof recipe for generalising Markoff into new view shapes from the lessons of E1–E5.

**Deliverables.**

- **`docs/recipe/2026-XX-XX-view-construction-recipe.md`** — the load-bearing doc. Names the live-render scaffold as the master template; lists every capability as a feature-axis with subtraction guidance. Sample subtractions: "to build a Source view, subtract E1 (highlighter — but reuse for source-mode syntax highlighting), subtract E2 (auto-hide — invert: always show markers), subtract delegate variety (one delegate, not many), keep the structural-key contract." Each subtraction is annotated with which capabilities it depends on and which it replaces.
- **`docs/recipe/capability-matrix.md`** — the capabilities × views grid. Rows: every capability in the live-render prototype. Columns: live-render, source, reading, hypothetical-views (compose, comment, slide). Each cell: required / optional / inverted / replaced.
- **`docs/recipe/delegate-authoring-template.md`** — the recipe for writing a new block-kind delegate. The template includes: kind registration, theme integration, structural-key handling, cursor-state participation, selection-view participation, inline-highlighter integration. Each section of the template is annotated with which other delegates it's based on and what it adds.
- **Refactor pass on `markoff-core`:** identify which capabilities surfaced as widget-internal during E1–E5 but are recipe-stable enough to promote to `markoff-core` for cross-view reuse. Examples likely include: a base `BlockDelegate` QML type, a base inline-highlighter pattern, the `InlineParseCache` (already in core), the `BlockKindRegistry` (currently per-document — possibly promotion-eligible).

**Acceptance.**

- A documentation milestone: the recipe doc exists and is reviewable by an outside engineer.
- The recipe is exercised: at least one new view (or a non-trivial existing view rebuild) produced from the recipe, in scope for the next arc.
- The capability matrix is complete: every cell filled.

E6 has no code-deletion goal; it's a documentation-and-lesson-extraction phase. If E6 reveals architectural cleanup that should land in `markoff-core` or `markoff-live`, that cleanup ships as part of E6.

---

## 3. After E6 — the post-E framing

The post-E posture, as approved by the user 2026-05-08:

> *"After phase E, we will likely develop new widgets, but the QML live render view is the ice-breaker experimental prototype which will guide all subsequent development: basically any other view is a subset of what it will be capable of doing once phase E is complete. Post-E, we document and distil ALL our lessons into a foolproof recipe for generalizing markoff into many different views."*

**Operating implication for any post-E new-view arc:**

- Every new-view arc starts with: which subtractions from the recipe define this view? Which capabilities are required? Which inverted? Which replaced?
- New-view specs cite the recipe by section; they don't re-derive architecture from first principles.
- The view-construction recipe is the foundation invariant of post-E development. Updating it requires a recipe-update spec (analogous to a constitutional amendment); ad-hoc deviations in new-view code are flagged as cleanup debt.
- The capability matrix is updated additively: new views add a column; if a new view requires a capability the recipe didn't anticipate, that capability is added as a new row, with a note on whether it should be back-ported to live-render.

**What this rules out:**

- New-view arcs that re-pioneer architecture instead of subtracting from the prototype.
- Views built outside Markoff (in consumer code) that re-implement what the recipe should yield.
- Per-view forks of `markoff-core` primitives. The recipe's promotion pass at E6 settles which primitives are core-eligible; new-view arcs use them as-is.

**What this enables:**

- A library of widgets like "Markoff Compose," "Markoff Comment," "Markoff Slide," "Markoff Outline," etc., shipped at multi-week cadence, each rebuilt cleanly from the same primitives.
- Markoff as a *family* of widgets sharing a single document, theme, undo stack, search stack, and collab boundary — the goal `2026-04-20-tri-view-unified-api-design.md` opened with, scaled beyond three views.
- Consumer projects (Corbomite first, others later) selecting any subset of the Markoff widget family with predictable integration cost.

---

## 4. Sequencing and prerequisites

**E-arc cannot begin until D-arc's bookend completes.** Specifically:

- D5 implementation phases 1–9 must be done.
- §4.5 audit must run (the live-binding cycle-guards removal; the §4.5 scope is now narrower per the 2026-05-08 framing — see `pivot-to-d5-first.md` updates).
- ~~§4.6 public-API freeze must land.~~ **— overridden by §0.1 amendment (2026-05-08)**: pivot-doc §4.6 is deferred until Corbomite is ready; E-arc begins in §4.6's slot. See `docs/handoff/2026-05-08-defer-46-to-e-arc.md`.

This sequencing is not a queue; it's a dependency. E-arc work on the inline highlighter is harmless to schedule against an evolving public API — but if §4.6's freeze re-shapes a primitive E1 was about to use, E1 wastes work. The clean order is: collab is right, foundation is frozen, then features are built against a stable surface. **(§0.1 inverts this argument: without a ready Corbomite consumer, freezing first is freezing in the dark; build E-arc, surface the seams, freeze post-E-arc.)**

**Within E-arc:** E1 must precede E2 (highlighter is the carrier of the hide-flag). E1 should precede E3 (wikilinks/tags want the highlighter pattern). E5 should be last (math/mermaid is the most isolated and benefits from settled primitives). E4 ordering is flexible.

**E6 begins after E5 ships.** E6 cannot be parallel with E1–E5 because the recipe needs all five phases' lessons in hand.

---

## 5. Cross-arc invariants

E-arc inherits and respects all D-arc invariants:

- **Single-user is the default.** New delegates and the highlighter must work without any consumer wired in. Collab is layered on top, not assumed by render.
- **Collab correctness is preserved.** D5's remote-cursor + structural-op machinery must keep working as features land. Concretely: every E-phase that touches cursor or selection state distinguishes *local caret* from *peer cursor* (e.g., E2's auto-hide reveals on local caret entry only, not on peer cursor entry); every phase that adds a new editing path emits the corresponding `Cmd::*` so collab convergence holds; every phase that adds a new block-kind extends the per-kind sibling-map ops if it stores per-block state outside the buffer. This is implicit in "single-user is the default" but stated separately to forestall slips at phase-spec time.
- **No `Corbomite`-named types in Markoff public API.**
- **`master` is append-only.**
- **Phase milestones tag versions.**

E-arc adds one new invariant:

- **Every E-phase ships with a "subtractability note"** in its spec. The note answers: how would a view that doesn't need this capability avoid linking it / instantiating it / paying its runtime cost? If the answer is "you can't, it's wired in everywhere," that's a recipe-violation flag and the implementation must be reshaped before the phase closes. The distillation pass at E6 audits these notes. (Template, worked example, and violation cadence in §5.1.)

### 5.1 Subtractability note: template, example, cadence

The §5 invariant requires each E-phase to ship a "subtractability note." Its shape:

**Section in the spec.** The note lives in the phase spec under a top-level `## N. Subtractability note` heading, where N is the spec's last numbered section + 1. Conventionally the second-to-last section, with §N+1 reserved for "Acceptance criteria."

**Body shape.** One paragraph per row in the eventual capability matrix (granularity rule in §5.2). Each paragraph answers, for the capability the row represents:

- **What the capability is** in one sentence (the *what* a future view either needs or doesn't).
- **What a view that needs this capability links / instantiates / pays for** — the dependency chain a consumer cannot avoid.
- **What a view that doesn't need this capability can do instead** — the subtraction path. Choose from: *omit entirely* (capability is opt-in via registration), *invert* (e.g. inline highlighter: live-render auto-hides delimiters, source-mode never auto-hides), *replace* (use a different rendering for the same data), *no-op* (default implementation that satisfies the contract without exercising it).

**Worked example (E1 inline-format highlighter).**

> **What.** Per-delegate `QSyntaxHighlighter` painting `QTextCharFormat` ranges from `BlockRecord::inlineSpans` over the rendered text in each text-bearing delegate.
>
> **A view that needs it links** the inline-span data path (`MarkoffDocument::inlineSpansFor` + `InlineParseCache`), the highlighter class itself, the `Markoff::Theme` token table for inline-kind styling, and any per-delegate cursor watcher that drives reveal/hide (E2-tier).
>
> **A view that doesn't need it can:**
>
> - **Omit entirely** for a plain-text-style view that renders raw markdown source unstyled — the highlighter is opt-in via delegate-side registration; a delegate that doesn't construct a highlighter pays nothing.
> - **Invert** for source mode — the highlighter pattern is reused but bound to monospace source rendering, with all delimiter spans always-visible instead of auto-hidden. Inline-kind styling tokens differ (source-style coloration vs. live-render-style typographic emphasis) but the data path is shared.
> - **Replace** for a Reading-style preview pane — the highlighter is replaced by a one-pass HTML/QML render that bakes spans into static formatting at parse time; the trade-off is no incremental update and no cursor-aware reveal.

**Recipe-violation flag.** If the answer to "what can a view that doesn't need this do?" is *"you can't, it's wired in everywhere"*, the implementation has captured the capability into a load-bearing assumption that the recipe cannot subtract from. The phase **pauses** at green-tree before its acceptance check; the offending capture is reshaped (new injection seam, opt-in registration, default-no-op subclass, etc.) until the subtraction path is real. The arc continues normally once the violation clears. E6 audits the corrected note along with everything else.

A "cannot subtract" answer is not always a violation — fundamental capabilities (e.g., document binding, basic block iteration) genuinely have no subtraction. The judgment is whether the capability is fundamental to *Markoff-the-family-of-views*, in which case `markoff-core` is its right home and the note records that, or fundamental only to *live-render*, in which case the recipe needs an alternate path.

### 5.2 Capability granularity: phase-deliverable rows

E6's capability matrix has one row per **major sub-deliverable** of an E-phase. Concretely:

- E1 → 1 row: *inline-format rendering* (with span-kind list as a note-internal detail, not separate rows).
- E2 → 1 row: *cursor-aware delimiter visibility*.
- E3 → 4 rows: *wikilinks*, *embeds*, *tags*, *callouts*. Each row is its own capability because each has a distinct subtraction path (a view that wants tags but not wikilinks is plausible; a view that wants wikilinks but not the auto-hide is plausible).
- E4 → 3 rows: *tables*, *frontmatter rendering*, *footnote rendering*.
- E5 → 2 rows: *math rendering*, *Mermaid rendering*. (Inline + block math share a row; the inline carrier rides on E1's row, not its own.)
- E6 itself → no rows (E6 produces the matrix, doesn't fill a cell in it).

Total at arc close: ~11 rows.

E6 reserves the right to **merge** rows whose subtraction paths turn out identical (e.g., if *frontmatter rendering* and *footnote rendering* both subtract via "render as plain block, no special delegate," they may collapse to one row), or **split** rows whose internal sub-deliverables turn out to have distinct subtraction paths (e.g., if E1's inline kinds turn out to subtract differently — say link/wikilink/tag share a navigation contract that bold/italic/strike/inline-code/highlight don't — the row may split).

The granularity rule lets phases write their subtractability note without waiting on E6. The risk of late merge/split is low: subtraction paths that turn out identical merge cheaply (one-paragraph rewrite); subtraction paths that turn out distinct were going to surface as distinct anyway.

### 5.3 Recipe deliverable: docs-only, with one canonical worked example

E6's deliverables in §2.E6 are docs-only:

- `docs/recipe/2026-XX-XX-view-construction-recipe.md`
- `docs/recipe/capability-matrix.md`
- `docs/recipe/delegate-authoring-template.md`

**Not delivered** at E6: a code scaffold (e.g., a `libs/markoff-view-template/` pre-built skeleton). A scaffold is a *consumption-side* artefact that a future arc may produce if-and-when needed. Arguments against scaffolding at E6: the recipe is exercised through documentation, not boilerplate; new-view arcs starting from a scaffold tend to inherit unexamined defaults; the worked-example pattern (below) gives consumers a tested reference without a maintained scaffold.

**The worked example.** `delegate-authoring-template.md` is built around *one canonical worked example* — the best-instantiated existing block-kind delegate from `libs/markoff-live` at E5-close. Every section of the template (kind registration, theme integration, structural-key handling, cursor-state participation, selection-view participation, inline-highlighter integration) annotates how that delegate satisfies the section. A new-view author reads the section, reads the example's matching code, and reproduces the pattern.

E6's "exercise the recipe" acceptance criterion (§2.E6) is satisfied by **rebuilding one existing delegate from the recipe** as a non-trivial verification — either a fresh implementation that round-trips equivalent behaviour, or a new view that subtracts from the recipe in a way no current view does (e.g., a "Markoff Outline" view: subtract delegate variety, subtract inline-format rendering, keep block-kind navigation, keep structural-key contract). The "one new view in scope for the next arc" phrasing in §2.E6 stands; the choice of which view is deferred to the user at E6 time.

---

## 6. Companion docs and cross-references

When E-arc begins:

- `docs/e-arc/e-arc-status.md` — live status board. Created at E1 kick-off.
- `docs/e-arc/2026-05-08-e-arc-roadmap.md` — orientation doc, mirrors D-arc roadmap.
- Per-phase specs under `docs/specs/2026-XX-XX-eN-*-design.md`.
- Per-phase plans under `docs/plans/2026-XX-XX-eN-*.md`.

This framing doc stays as the constitutional document for the arc — it doesn't get superseded by phase-level specs. If E-arc's framing itself changes, this doc gets a §0.1-style addendum noting the change, the way the D5 spec carries §0.1 negotiation outcome.
