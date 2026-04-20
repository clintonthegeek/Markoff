# Phase C5 — Reading-mode interaction parity

**Status:** drafted 2026-04-20
**Markoff tag on completion:** `v0.4.0`
**Absorbs:** Corbomite Cluster V Phase 4 (deferred pending this work).
**Ordering:** second in Phase C, immediately after C1 (DI seam, `v0.3.0`, done).

## 1. Scope

C5 closes the four "Reading-mode interaction parity" items listed in
[`phase-c-status.md`](../phase-c-status.md) §"C5 — Reading-mode
interaction parity". Code inspection after C1 landed narrowed the
shape of the work: two items are real Markoff-side feature additions,
two were already landed during earlier phases and need only status
documentation plus (for zoom) a Corbomite-side dispatch beat.

| Item                                    | Prescription                                    | State before C5                                                             | C5 delivers                                         |
| --------------------------------------- | ----------------------------------------------- | --------------------------------------------------------------------------- | --------------------------------------------------- |
| Unified `linkHovered`                   | new signal with `(href, globalPos)`             | `ReadingView` emits `wikiLinkHovered(target)`; `Editor` emits one-arg form  | **Replace** on Reading; **widen** on Editor         |
| Click-to-fold on heading                | heading fold toggle from click                  | `eventFilter` already toggles via fold-arrow hit-test                       | Documented as done; regression test added           |
| `codeBlockProcessorRegistry` routing    | `SectionLayout` consults registry               | Registry is a `bool`-returning feature-flag map; render output discarded; Live doesn't consume it either | **Deferred** — revisit under C3 or C4 with a redesigned registry contract |
| `zoomIn / zoomOut / resetZoom` virtuals | override `MarkdownView` defaults on ReadingView | Overrides already present + `zoomChanged` signal (QGraphicsView xform scale) | Documented as done; Corbomite dispatch beat lands in adaptation commit |

**Why `codeBlockProcessorRegistry` routing deferred:** post-C1 inspection
showed the registry's `CodeBlockProcessor` type is
`std::function<bool(source, node, ctx)>` — a handler returns `bool`
indicating "I handled this language" but produces no render payload
the layout can mount. Markoff Live, which the original prescription
cited as precedent, doesn't consume the registry at all (zero
`dispatch()` call sites). Today `SectionLayout` hardcodes mermaid
rendering via `ctx.mermaidRenderer` and plain-code rendering via
`CodeBlockHighlighter`; math/latex fenced blocks fall through to the
default path and render as inert code. The intended design — "registry-
driven fenced-block dispatch" — requires a signature redesign
(processor returns a mount payload, or SectionLayout gets an
out-parameter protocol) plus migrations of the existing hardcoded
branches. That belongs under C3 (`MarkoffDocument` content-
authoritative, which reshapes the render pipeline anyway) or C4
(renderer unification). No user currently relies on math fenced-block
rendering in Corbomite; the deferral has no observable regression.

### Out of scope (deferred)

- **`codeBlockProcessorRegistry` render-pipeline routing.** See the
  "Why … deferred" paragraph above. Revisit under C3 or C4 with a
  redesigned processor contract.
- **Math / LaTeX fenced-block rendering.** Symptom of the registry
  deferral — `math` / `latex` fenced blocks remain inert until the
  registry redesign ships. Nobody currently relies on this.
- **Zoom-policy harmonization** across leaves (Live/Source font-size
  vs. Reading transform-scale). Cosmetic; revisit post-Phase-C if
  demand surfaces.
- **Click-on-heading-text folding** beyond the fold-arrow hit-zone.
  Current arrow-only behavior matches the user-chosen scope.
- **Zoom dispatch on non-markdown views** (graph, canvas). The
  default-no-op virtual from Cluster V already satisfies their
  contract.

## 2. Markoff-side API changes

### 2.1 `ReadingView` — `wikiLinkHovered` → `linkHovered`

**Remove** (breaking):

```cpp
void wikiLinkHovered(const QString &target);
```

**Add:**

```cpp
void linkHovered(const QString &href, const QPoint &globalPos);
```

**Emit-site changes:**

- The existing `m_hoverTimer` timeout path in `ReadingView.cpp` emits
  the unified signal. Store the viewport position captured when
  `m_pendingHoverTarget` was set (new member
  `QPoint m_pendingHoverViewportPos`); at timeout fire, translate via
  `m_graphicsView->viewport()->mapToGlobal(m_pendingHoverViewportPos)`.
- Hover-leave case (empty target): emit `linkHovered(QString(),
  QPoint())`. Consumers treat an empty `href` as "popover should hide"
  regardless of `globalPos`.

**Scope widen — regular-link hover:**

Today only wiki-links drive the hover timer. `LinkRenderer::linkHovered`
already emits for regular URLs too but ReadingView ignores it.
Subscribe `ReadingView`'s hover pipeline to `LinkRenderer::linkHovered`
and funnel it through the same `m_hoverTimer` debounce + emit path.
One new connection in the pipeline-wiring code. No new rendering work.

**Href normalization** stays what it is today:

- Wiki-links: resolved wiki-target string (e.g. `"MyNote"` or
  `"MyNote#Anchor"`) — unchanged.
- Regular URLs: raw href string from the Markdown AST — unchanged.

Consumers distinguish by prefix heuristics as they already do.

### 2.2 `Markoff::Editor` — widen `linkHovered`

**Before:**

```cpp
void linkHovered(const QString &target);
```

**After:**

```cpp
void linkHovered(const QString &href, const QPoint &globalPos);
```

**Emit-site changes** (`Editor.cpp` lines near 2301, 2312, 2316):
pass `QCursor::pos()` at emit time. `TextControl::linkHovered`
fires synchronously inside mouse-move handling, so `QCursor::pos()`
is current. Hover-leave case: pass `QPoint()`.

### 2.3 Click-to-fold — no API change

Current flow stays:

1. `eventFilter` captures `MouseButtonPress` on `m_graphicsView->viewport()`.
2. `sectionIndexAt(pos)` looks up fold-arrow items tagged with
   `kFoldArrowSectionIdxProperty`.
3. `toggleFold(idx)` runs if the arrow was hit.

C5 adds only a regression test (§4.2). No code change.

### 2.4 Zoom virtuals — no API change

Already present at `ReadingView.{h,cpp}` — see C1-era lines:

```cpp
void zoomIn()    override;  // header
void zoomOut()   override;
void resetZoom() override;
```

with QGraphicsView transform scaling and `zoomChanged()` emission
(see `ReadingView.cpp` ~825-850). C5 documents the contract Corbomite
dispatches to.

## 3. Corbomite-side adaptation

Runs in the submodule-bump commit(s) that move Corbomite's pin to
Markoff `v0.4.0`.

### 3.1 HoverPopover rewire

- **Reading side:** reconnect `ReadingView::wikiLinkHovered` →
  `ReadingView::linkHovered`; drop any `QCursor::pos()` synth in the
  popover callback and consume the provided `globalPos`.
- **Editor side:** reconnect the existing one-arg `linkHovered` slot
  to the two-arg form; drop synth.
- **Behavior widen:** regular-URL hover now shows the popover (wiki-
  only today). Per Cluster J, `EmbedRenderer` / the popover renderer
  already handles non-wiki hrefs gracefully, so no renderer change.
  Grep-sweep `HoverPopover.cpp` for any wiki-specific guard and drop.

### 3.2 `View::zoomIn/Out/Reset` dispatch (Cluster V Phase 4)

Add to `Corbomite::View`:

```cpp
virtual void zoomIn()    {}
virtual void zoomOut()   {}
virtual void resetZoom() {}
```

Default-no-op per the Cluster V spec (so graph/canvas views implicitly
ignore). Corbomite's `MarkdownView` (the CorbomiteApp wrapper around
the active Markoff leaf) overrides by delegating to the active
`Markoff::MarkdownView *`:

```cpp
void MarkdownView::zoomIn()    override { if (auto *m = activeLeaf()) m->zoomIn(); }
void MarkdownView::zoomOut()   override { if (auto *m = activeLeaf()) m->zoomOut(); }
void MarkdownView::resetZoom() override { if (auto *m = activeLeaf()) m->resetZoom(); }
```

Ctrl+= / Ctrl+− / Ctrl+0 shortcut wiring already exists (Cluster V
Phase 2+3). After this beat the chain reaches real per-leaf
implementations in Reading + Live + Source.

### 3.3 Four gated tests stay gated

The four tests still sitting behind
`if(FALSE AND MARKOFF_READING_USE_REAL_COREDEPS)` in
`libs/markoff-reading/tests/CMakeLists.txt` (`tst_sectionlayout_mermaid`,
`tst_readingview_embedrenderer`, `tst_readingview_mermaid_registered`,
`tst_readingview_embed_builtins`) assert on real processor-dispatch
behavior that the deferred §2.3 redesign would unlock. They stay
gated through C5 and un-gate when the registry routing lands (C3 or
C4).

Per-note `NoteEditorWidget` injection of `setMermaidRenderer` /
`setVault*Parser` is still a good idea (hovered mermaid in wiki-links
works today because HoverPopover owns its own `EmbedRenderer`; the
per-note ReadingView doesn't render mermaid). Track as a C5
nice-to-have: land if trivial, defer otherwise.

### 3.4 Cluster V Phase 4 closeout

After §3.1-3.3 ship:

- `PROJECT-STATE.md` Cluster V row moves from "In progress (phase 4
  next)" to **Done**.
- `backlog.md` strike the Cluster V Phase 4 entry (if present).
- `cluster-retros/cluster-v.md` appends a short "Phase 4 absorbed by
  Markoff C5" note.
- Cluster V.2 stays open.

## 4. Migration + testing

### 4.1 Breaking-change manifest for CorbomiteApp

| Site                                    | Before                      | After                               |
| --------------------------------------- | --------------------------- | ----------------------------------- |
| HoverPopover connect (Reading)          | `wikiLinkHovered(QString)`  | `linkHovered(QString, QPoint)`      |
| HoverPopover connect (Editor)           | `linkHovered(QString)`      | `linkHovered(QString, QPoint)`      |
| Any other `wikiLinkHovered` consumer    | —                           | rewire or delete                    |

**Grep-sweep** on the Corbomite side: `wikiLinkHovered` should return
zero production hits post-bump; `linkHovered` connections should all
be two-arg.

### 4.2 Markoff-side tests

| Test                                    | Delta                                                                                             |
| --------------------------------------- | ------------------------------------------------------------------------------------------------- |
| `tst_readingview_linkrenderer`          | extend to assert `ReadingView::linkHovered(href, globalPos)` emits with non-null globalPos on hover and empty href on leave |
| `tst_markoff_wikilink_clickable`        | retype spies for two-arg `linkHovered`                                                            |
| `tst_textcontrol_links`                 | retype spy + global-pos assertion                                                                 |
| Editor signal tests (any observers)     | retype as above                                                                                   |
| **New:** `tst_readingview_click_to_fold` | simulate click on a fold-arrow item; assert `foldedHeadingsChanged()` emits + `foldedHeadings()` reflects the toggle |

### 4.3 Acceptance criteria

1. Standalone Markoff build green (`MARKOFF_READING_USE_REAL_COREDEPS=OFF`),
   full ctest pass including the three new/widened tests above.
2. `grep -rn wikiLinkHovered libs/` returns zero source hits across
   Markoff; only spec-doc mentions remain.
3. Corbomite submodule bumped to `v0.4.0`; build green (modulo the
   two pre-existing flakes); smoke-test: click-hover a wiki-link and
   a regular URL in both Reading and Live, popover appears for both.
4. Ctrl+= / Ctrl+− / Ctrl+0 work end-to-end in all three leaves.
5. Cluster V Phase 4 row = Done in `PROJECT-STATE.md`.

### 4.4 Sequencing

1. Draft C5 plan in `docs/plans/2026-04-20-phase-c5-reading-interaction-parity.md`.
2. Implement on Markoff `master`:
   - Single commit series — §2.1 (`linkHovered` on Reading) + §2.2
     (Editor widen) + §2.3 click-to-fold regression test + test
     updates. Tag `v0.4.0` when standalone ctest is green.
3. Bump Corbomite submodule to `v0.4.0`; ship §3 in one or two commits.
4. Return to Markoff only if cleanup is non-trivial (none expected —
   no bridge code here).

## 5. Decisions recorded

- **D1** `wikiLinkHovered` is replaced, not coexisted. Rationale:
  single consumer (HoverPopover) rewires in the same commit as the
  bump; coexistence has no call-sites worth preserving.
- **D2** `Markoff::Editor::linkHovered` widens to match. Rationale:
  consumers want a single signal shape across leaves; piecemeal
  widening later would be strictly more work.
- **D3** Click-to-fold stays arrow-only (not heading-text). Rationale:
  user-chosen scope; Obsidian-text-click behavior can be added later
  without breaking contract.
- **D4** Zoom policy is per-leaf. Rationale: harmonization is
  cosmetic; deferred.
- **D5** `codeBlockProcessorRegistry` routing deferred to C3/C4.
  Rationale: the registry's current `bool`-returning `CodeBlockProcessor`
  signature can't produce mount payloads; Markoff Live doesn't consume
  the registry either (contradicting the original prescription);
  redesign belongs under the render-pipeline-touching work-units
  (C3 `MarkoffDocument` or C4 renderer unification). No user depends
  on math/latex fenced rendering today, so deferral has no observable
  regression.
- **D6** Cluster V Phase 4 closeout happens in the Corbomite
  adaptation commit, not on the Markoff side. Rationale: the phase
  is a Corbomite UI cluster; Markoff only provides the primitives.
