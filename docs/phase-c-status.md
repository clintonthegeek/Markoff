# Phase C Status

Living status board for the Phase C work-units. The Corbomite agent
updates this file in every Phase C commit touching either repo. Entries
are append-only in the "Activity log" section; the work-unit status table
gets edited in place.

## Scope agreement

Phase C delivers the final shape of the tri-view API and absorbs the
Corbomite-prescribed features the original four work-units didn't cover.
Seven work-units total:

| ID   | Work-unit                                              | Markoff tag on completion |
| ---- | ------------------------------------------------------ | ------------------------- |
| C1   | DI seam (retires `MARKOFF_READING_USE_REAL_COREDEPS`)  | `v0.3.0`                  |
| C5   | Reading-mode interaction parity (Cluster V Phase 4)    | `v0.4.0`                  |
| C6   | Editor state + context-menu contribution surface       | `v0.5.0`                  |
| C3   | `MarkoffDocument` becomes content-authoritative        | `v0.6.0`                  |
| C7   | Source feature completion (find/replace + fold-gutter) | `v0.7.0`                  |
| C2   | Theme / ResourceProvider / LinkResolver consolidation  | `v0.8.0`                  |
| C4   | Renderer unification (Code/Math/Mermaid)               | `v0.9.0`                  |

Original IDs (C1–C4) preserved. **New IDs: C5, C6, C7** — added
2026-04-20 when the Corbomite agent took the Phase C baton and folded in
Corbomite's outstanding Markoff-side prescriptions. The ID numbering is
deliberately non-monotonic: it reflects the original handoff order
(C1→C3→C2→C4) with C5/C6/C7 slotted where they fit topologically.

## Ordering rationale

**C1 first** — it closes the Phase B bridge cleanly and establishes
the interface-injection pattern the rest of Phase C reuses. Nothing
else can land cleanly until `MARKOFF_READING_USE_REAL_COREDEPS` is
retired because mid-flight refactors would have to serve both the
option'd and de-option'd worlds.

**C5 and C6 come next** — they are small, high-user-visibility
unlocks that finish Cluster V (the Corbomite-side UI cluster that was
deferred to let Phase C run). Doing them right after C1 lets Corbomite
ship user-visible progress while the larger C3 is drafted. C5 is
ReadingView polish; C6 is a pure signal-addition to `Markoff::Editor`.

**C3 before C2** — shared-document adoption is the biggest
user-visible unlock and is mostly orthogonal to Theme/Provider/Resolver
shapes. Doing it in isolation avoids cross-coupling two large
refactors. Also closes the door on Corbomite `NoteDocument` shaping
decisions that would otherwise drift while C2 is in flight.

**C7 before C2** — find/replace is the last Qutepart-fork Phase 3
deliverable and completes the Source-mode UI (Corbomite's Cluster R
menus have a disabled "Find…"/"Replace…" placeholder waiting on it).
Landing it before C2 means C2's Theme consolidation only has to touch
finished surfaces.

**C2 late** — Theme consolidation is the most likely source of
CorbomiteApp call-site breakage. Do it once the rest of Phase C is
stable so there's only one high-breakage coordination beat.

**C4 last** — smallest, mostly internal, no consumer-facing surface.
Polish. Absorbs any renderer-unification follow-ups that surfaced
during C1–C7.

## Work-unit status

| ID   | Status       | Spec                                   | Plan                                   | Markoff PR/branch    | Corbomite PR/branch  | Tag       |
| ---- | ------------ | -------------------------------------- | -------------------------------------- | -------------------- | -------------------- | --------- |
| C1   | markoff ready (v0.3.0) — corbomite adapter shipped; Phase B bridge retired | [C1 DI seam](docs/specs/2026-04-20-phase-c1-di-seam.md) | [C1 plan](docs/plans/2026-04-20-phase-c1-di-seam.md) | `master`             | Corbomite `59ecd5cb` | `v0.3.0`  |
| C5   | requirements — see inputs | —                         | —                                      | —                    | —                    | —         |
| C6   | requirements — see inputs | [consumer editor-state surface §1-8 + §9 context-menu](libs/markoff-live/docs/specs/2026-04-20-consumer-editor-state-surface.md) | —                                      | —                    | —                    | —         |
| C3   | not started  | —                                      | —                                      | —                    | —                    | —         |
| C7   | requirements — see inputs | [find/replace design](libs/markoff-live/docs/specs/2026-04-14-find-replace-design.md) + [code-folding Kate harvest](libs/markoff-live/docs/specs/2026-04-14-code-folding-kate-harvest.md) + [find/replace Kate harvest](libs/markoff-live/docs/specs/2026-04-14-find-replace-kate-harvest.md) | —                                      | —                    | —                    | —         |
| C2   | not started  | —                                      | —                                      | —                    | —                    | —         |
| C4   | not started  | —                                      | —                                      | —                    | —                    | —         |

Status values: `not started` → `requirements — see inputs` → `spec drafted` → `spec approved` → `markoff implementing` → `markoff ready` (tag exists) → `corbomite adapting` → `corbomite shipped` → `markoff cleanup` → `done`.

## Work-unit input prescriptions (Corbomite side)

The Corbomite app team prescribed these features before the Phase C
handoff. Each bullet is a must-land requirement — specs get drafted to
satisfy them, not to re-negotiate them.

### C1 — DI seam

- Replace the `MARKOFF_READING_USE_REAL_COREDEPS` CMake option with
  interface injection. Interfaces Markoff defines: `IEmbedRegistry`,
  `ICodeBlockProcessorRegistry`, `IPostProcessorRegistry`,
  `IVaultResourceProvider`, `IEmbedDepthGuard`,
  `IMarkdownRenderChildFactory`, `IMermaidRenderer`, `ILinkResolver`,
  `IMetadataCache` (Corbomite implements each).
- Retire the `libs/markoff-reading/stubs/corbomite/` shim tree.
- Host plugs interfaces in at runtime (setter on `ReadingView` / a
  `Context` object consumed at construction — shape to be designed).
- Non-host consumers get default no-op implementations that work
  standalone (`MermaidRenderer` without mmdr → placeholder; `EmbedRegistry`
  empty → no embeds resolve).

### C5 — Reading-mode interaction parity (Cluster V Phase 4 absorption)

From `Corbomite/docs/superpowers/plans/2026-04-20-cluster-v-editor-workspace-ui-surfacing.md` Phase 4:

- **`ReadingView::linkHovered(QString href, QPoint globalPos)` signal.**
  Today `ReadingView` emits `wikiLinkHovered` which is too narrow; the
  rename/alias covers regular links too. Preserve existing emit sites;
  add a unified signal. Corbomite's `HoverPopover` wires to the new
  signal.
- **Click-to-fold on `HeadingItem`.** Clicking a heading's visual
  marker toggles its fold. Uses the existing fold-state infrastructure
  (`foldedHeadings()` / `setFoldedHeadings()`).
- **`codeBlockProcessorRegistry` routing in `ReadingView`'s pipeline.**
  ReadingView's `SectionLayout` must consult the registry when a code
  block's language matches a registered processor, same as Markoff
  Live already does. Today code blocks always fall through to the
  default syntax-highlighting path.
- **`ReadingView::zoomIn() / zoomOut() / resetZoom()` virtuals.**
  Override the `MarkdownView` default no-op implementations. ReadingView
  already has internal zoom state; the virtuals expose it. Corbomite's
  `View::zoomIn/Out/Reset` dispatches to this.

### C6 — Editor state + context-menu contribution surface

Full spec already drafted — see
[`libs/markoff-live/docs/specs/2026-04-20-consumer-editor-state-surface.md`](libs/markoff-live/docs/specs/2026-04-20-consumer-editor-state-surface.md)
(recovered from Corbomite's submodule at v0.2.8 after being stranded
on the local ahead-master for three weeks).

- `EditorContext` struct covering: inline-span membership
  (bold/italic/strike/inline-code/highlight/link/wiki-link/tag/embed/
  footnote/math), block kind (paragraph/heading-N/code/blockquote/
  callout/list-item/task-item), heading level (0–6), table coords
  (row/col, header flag), and link/tag/footnote URIs.
- `Q_SIGNAL void contextChanged(const EditorContext &)` — debounced
  to ~50 ms or "cursor-settled" equivalent per the spec's §4.
- Pull accessor `EditorContext Editor::context() const` — for host
  code that can't use the signal (e.g. menu-open callbacks).
- Default impls are fine to ship first; full classification is the
  main engineering cost and can be built incrementally.
- **Context-menu contribution point (§9):**
  `Q_SIGNAL void aboutToShowContextMenu(QMenu *menu,
  const EditorContext &ctx, QPoint globalPos)` emitted
  mid-`contextMenuEvent` after Markoff's built-ins are inserted and
  before `menu->exec()`. Corbomite's `MainWindow` wraps the menu in
  `MenuSectionHelper` and injects Format/Heading/Insert/Table
  entries. Same signal is the natural hook for third-party plugin
  `editor-menu` contributions later.

### C3 — `MarkoffDocument` becomes content-authoritative

- `MarkoffDocument` owns the text buffer. All three leaves subscribe
  to changes via Qt signals and update their views.
- Corbomite's `NoteDocument` becomes a thin wrapper over
  `MarkoffDocument` (or pools — the right shape to be decided in the
  C3 spec).
- Per-leaf ephemeral state (cursor, selection, scroll, fold) stays
  with the leaf — only text + frontmatter + parse tree are shared.
- Closes out the deferred items in
  `Markoff/docs/plans/2026-04-20-tri-view-phase-a.md` §"Deferred to
  Phase C": async parse worker sharing, precise pixel↔visual-line
  scroll conversion, source-offset↔per-block cursor translation.

### C7 — Source feature completion

- **Public find/replace API** on `Markoff::Source::SourceEditor`.
  Absorbs what was planned as "Qutepart-fork Phase 3" on the
  Corbomite side. See
  [`libs/markoff-live/docs/specs/2026-04-14-find-replace-design.md`](libs/markoff-live/docs/specs/2026-04-14-find-replace-design.md)
  for the UI shape Markoff Live already targets; Source mode shares
  most of it modulo Qutepart's peculiarities.
- **Fold-gutter coordinator** — complete `FoldGutter::paint()`
  (the Cluster V.2 scouting flagged this is not a theme concern;
  the Qutepart-fork Phase 6 reference was a misread) and wire the
  Markoff-internal fold coordinator across Source and Live.
- Corbomite's Cluster R "Find…" / "Replace…" menu slots (today
  disabled placeholders with a tooltip pointing at Qutepart-fork P3)
  light up when this lands.

### C2 — Theme / ResourceProvider / LinkResolver consolidation

- One `Theme` type in `Markoff::Core`. Replaces the three in
  `Markoff::Live`, `Markoff::Reading`, and `Markoff::Source`.
  Corbomite's `KColorSchemeManager` (Cluster V Phase 1) drives it.
- `IVaultResourceProvider` from C1 is the abstract surface;
  Corbomite's concrete `VaultResourceProvider` (promoted to
  `libs/core/` in Cluster J) implements it.
- `ILinkResolver` — same pattern; Corbomite's concrete resolver
  stays in `libs/storage/`.
- **Breaking:** Corbomite call sites using per-library Theme types
  migrate to the unified one. Most are in `NoteEditorWidget` and
  `MainWindow`.

### C4 — Renderer unification

- Collapse the two-path code-block rendering (Markoff Live's
  registry vs. Reading's direct dispatch) into a single registry
  pattern.
- Collapse the two-path math rendering (JKQTMathText direct
  embed vs. registry-routed).
- Audit Mermaid dispatch — today it's registry-routed in Reading
  only; evaluate whether Live should also gain mermaid support.
- Smallest work-unit; final polish.

## Recipe per work-unit

1. **Draft spec** in `docs/specs/YYYY-MM-DD-phase-c<N>-<topic>.md`. Include:
   - Interface shape(s) proposed
   - Migration path for any Markoff internals that call the same surface
   - Breaking-change manifest for CorbomiteApp
   - Acceptance criteria
2. **Update the table above** to `spec drafted`. Commit the spec.
3. **User or a parallel reviewer reads the spec**, either approves or sends back. (For unit-sized changes with no ambiguity, the Corbomite agent may self-approve and proceed — note in the activity log.)
4. **Draft plan** in `docs/plans/YYYY-MM-DD-phase-c<N>-<topic>.md`. Task-by-task with explicit files + commit messages, per the Phase A/B convention.
5. **Implement on Markoff `master`** (per Corbomite's no-feature-branches convention; commits land directly). Green tests after each commit. Set status to `markoff implementing`.
6. **Tag** the Markoff SHA once all Phase-C-<N> tasks are done. Set status to `markoff ready`.
7. **Bump Corbomite's submodule pin** and write the CorbomiteApp-side adaptation. Set status to `corbomite adapting`.
8. **Ship on the Corbomite side**, run CorbomiteApp end-to-end smoke. Set status to `corbomite shipped`.
9. **Return to Markoff** to retire any bridge code left over from the previous phase. Set status to `markoff cleanup`. Tag a cleanup release if the diff is non-trivial.
10. **Close** to `done`. Activity-log a brief retrospective: what broke, what we'd do differently.

## Invariants (copy from handoff doc — repeat for visibility)

1. Standalone Markoff build + ctest always green. `MARKOFF_READING_USE_REAL_COREDEPS=OFF` is the standalone default; it retires in C1 and whatever replaces it inherits the invariant.
2. No `Corbomite`-named types in Markoff public interfaces (Phase B stubs excepted; retire in C1).
3. Tests that need Corbomite concretes gate on the appropriate CMake option / injection mechanism.
4. Every work-unit tags a Markoff version.
5. `master` is append-only. No force-push.
6. Commit identity stays unified across both repos (Corbomite agent, co-author trailer).

## Activity log

Append in reverse-chronological order (newest first).

### 2026-04-20 — C1b landed at `v0.3.0`; Phase B bridge retired

Stubs tree (`libs/markoff-reading/stubs/corbomite/` — 9 header files
plus `mmdr_ffi.h`) deleted. `MARKOFF_READING_USE_REAL_COREDEPS` option
+ its conditional block removed from `libs/markoff-reading/CMakeLists.txt`.
Standalone Markoff builds in a fresh checkout with 76/76 ctest green
and zero external-dep links. CorbomiteApp builds via
`libs/core/include/corbomite/markoff_adapters/` (composition-based
adapters — see Corbomite `59ecd5cb`).

Between C1a (alpha.1) and C1b (v0.3.0), an intermediate
`v0.3.0-alpha.2` gated the 4 Phase-B-style tests
(tst_sectionlayout_mermaid, tst_readingview_embedrenderer,
tst_readingview_mermaid_registered, tst_readingview_embed_builtins)
behind `if(FALSE AND MARKOFF_READING_USE_REAL_COREDEPS)`. They
compile against the new Markoff types but assert real mmdr + real
MetadataParser output at runtime; un-gate pending a Corbomite-side
test harness that injects the adapter concretes
(`Corbomite::Core::MermaidRenderer`,
`Corbomite::MarkoffAdapters::MetadataParserImpl`). Tracked as a
Phase C1 follow-up.

The CLAUDE.md docs at the Markoff repo root still describe the
Phase B option in a few places; left for a doc-only follow-up touch
to avoid mixing that with the API retire.

C1 work-unit closed. Next: C5 spec (Reading-mode interaction parity).

### 2026-04-20 — C1a landed at `v0.3.0-alpha.1`

Sub-phase C1a shipped in 5 commits on `master` (`fe655b0` → `2956ee7`)
and tagged `v0.3.0-alpha.1`. Concrete deliverables:

- `markoff-core`: 10 new public headers (5 primitives
  `EmbedRegistry` / `CodeBlockProcessorRegistry` / `MarkdownRenderChild` /
  `EmbedDepthGuard` / `MermaidRenderer` + 5 vault abstracts under
  `Markoff::Vault::` namespace), 5 Default* no-op concretes, and 4 new
  unit tests. `MarkdownRenderChild.cpp` is the sole new .cpp.
- `markoff-reading`: every `<corbomite/...>` include dropped from src/
  and include/; types rename to `Markoff::*` / `Markoff::Vault::*`.
  `ReadingView` gains 5 new setters (`setEmbedRegistry`,
  `setVaultLinkResolver`, `setVaultMetadataCache`,
  `setVaultMetadataParser`, `setMermaidRenderer`) plus lazy-default
  fallbacks via a fwd-declared `LazyDefaults` struct. `EmbedRenderer`
  gains `setMetadataParser`. `SectionLayout::Context` gains
  `mermaidRenderer *`; the two Mermaid callers route through it. Old
  internal `src/MermaidRenderer.{h,cpp}` deleted (mmdr-coupled path is
  now host-side only, via the `Markoff::MermaidRenderer` abstract).
- Tests: 9 test files retyped via mechanical sed (Corbomite::Core::X →
  Markoff::X; Corbomite::Storage::X → Markoff::Vault::X). Includes
  switched to angle-bracket `<markoff/...>` form.

The Phase B CMake option `MARKOFF_READING_USE_REAL_COREDEPS` stays in
place but is now a no-op for link shape (markoff-reading no longer
includes any corbomite/ headers from its sources). The stubs tree
under `libs/markoff-reading/stubs/corbomite/` is unused by
markoff-reading but still compiles; C1b (Tasks 17-21) deletes it.

Task 8 (un-gate Phase-B tests) is a conscious no-op in C1a: the 4
gated tests all assert on real SVG output or real metadata slicing,
which `Default*` impls cannot provide. Un-gating blocked on Task 13
writing Corbomite-side adapter tests, or on the host injecting real
parser/renderer concretes.

Standalone `ctest` green: 76/76. Tag append-only.

Next: Corbomite-side adapter commit (Tasks 10-16) bumps submodule pin
to `v0.3.0-alpha.1` and retypes Corbomite's Core/Storage primitives to
inherit the new Markoff interfaces.

### 2026-04-20 — C1 plan drafted

`docs/plans/2026-04-20-phase-c1-di-seam.md` — 26-task self-contained
execution plan written for a fresh-context agent. Covers C1a
(Tasks 1–9, Markoff side, introduce types + default impls +
retargeted markoff-reading, tag `v0.3.0-alpha.1`), Corbomite adapter
commit (Tasks 10–16, inheritance-based adapters + MainWindow wiring),
C1b (Tasks 17–22, delete stubs + retire CMake option, tag `v0.3.0`),
and Corbomite cleanup (Tasks 23–26). Includes orientation §0 with
build commands, invariants, submodule pin-bump protocol, header
template, and commit-message convention. Troubleshooting appendix
covers the four known thorns (registerExtension name collision,
MetadataParser static-vs-instance, mermaid test assertions,
MarkdownRenderChild MI).

Timeline estimate: 2.5 days across both repos.

Ready to execute. Next: Task 1 in a fresh-context session.

### 2026-04-20 — C1 spec drafted

`docs/specs/2026-04-20-phase-c1-di-seam.md` landed. Lays out the two-
sub-phase migration (`v0.3.0-alpha.1` introduces new Markoff types
alongside Phase B's CMake option; `v0.3.0` retires the option + stubs
after Corbomite ships the adapter layer). Interfaces split across
`Markoff::` (rendering primitives: EmbedRegistry, CodeBlockProcessor-
Registry, MarkdownRenderChild, EmbedDepthGuard, MermaidRenderer) and
`Markoff::Vault::` (vault-level abstractions: ResourceProvider,
LinkResolver, MetadataCache, MetadataParser). Setter-style injection
on `ReadingView` with lazy-default fallbacks in `markoff-core`. Ten
decisions recorded in §9. Self-approved per the recipe's step-3
clause for unambiguous design calls — signatures are 1:1 with existing
Corbomite::Core types.

Next action: C1a implementation on Markoff master, then Corbomite
adapter, then C1b cleanup. Plan file next.

### 2026-04-20 — scope expanded: C5, C6, C7 added by Corbomite agent

Corbomite agent took the Phase C baton per the handoff. Scope expanded
from four work-units to seven by folding in:

- **Cluster V Phase 4** (Corbomite's deferred UI cluster — ReadingView
  interactions that need Markoff-side signal and virtual additions).
  Now C5.
- **The consumer editor-state surface spec** (443 + 203 lines of
  Corbomite-prescribed requirements that had been stranded on Corbomite's
  submodule ahead-master; recovered in Markoff commits `8d92247` and
  `245bd56`; tagged `v0.2.8`). Now C6.
- **Qutepart-fork Phase 3** (public find/replace API — was tracked on
  Corbomite's "Parallel long-term internal refactors" table under the
  Qutepart-Corbomite fork banner; now absorbed into Markoff since
  `Markoff::Source` owns the widget). Now C7.
- **Cluster V.2 fold-gutter coordinator** — also absorbed into C7
  after the V.2 scouting doc flagged the Qutepart-fork-Phase-6 reference
  as a misread.

Ordering revised: `C1 → C5 → C6 → C3 → C7 → C2 → C4`. C5 and C6 are
small user-visible unlocks that complete Cluster V and can ship as
back-to-back v0.4.0 / v0.5.0 releases after C1 lands. C7 gets slotted
before C2 so find/replace and fold-gutter don't suffer Theme-refactor
churn. C3/C2/C4 retain their original C1→C3→C2→C4 sequencing.

Markoff `master` at `v0.2.8`. C1 is the next work-unit to spec.

### 2026-04-20 — handoff

Phase C ownership transferred to the Corbomite agent. Status board
created. Scope, ordering, and invariants recorded. C1 is the next
work-unit to spec.

Markoff state at handoff: `v0.2.0` on `master` at `476e389`. Feature
branch `feature/tri-view-phase-a` preserved at the same SHA for
reference. Worktree at `.worktrees/tri-view-phase-a/` preserved.

Phase A docs (both sides) at `docs/specs/2026-04-20-tri-view-*.md` and
`docs/plans/2026-04-20-tri-view-phase-a.md`.

Phase B docs (both sides) at `docs/specs/2026-04-20-phase-b-*.md` and
`docs/plans/2026-04-20-phase-b-*.md`.

Handoff doc at `docs/handoff/2026-04-20-phase-c-ownership-handoff.md`.
