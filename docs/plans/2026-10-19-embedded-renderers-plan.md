# Embedded renderers in the live view — reconnaissance and plan

Date: 2026-10-19. Status: proposal; no phase started. Driver: the survey in
[`../../mermade-and-others.md`](../../mermade-and-others.md) (native renderers
for extended Markdown; should be filed under `docs/research/` next time it is
touched). Goal: one general embedding mechanism in Markoff's editing view, every
engine family reachable through it, tables first.

**The live view is the canvas leaf.** Corbomite's `LivePreview` *is*
`Markoff::Canvas::EditorWidget`; Reading is `Styled::Editor`, Source is
`Source::Editor`, and card/hover previews go through
`Styled::DocumentRenderer::renderInto`. Canvas is the only leaf that renders
non-text content today, so it is where this work lands; the headless renderer
follows so cards and hovers get the same content.

## 1. What already exists (grounded)

### 1.1 The renderer seams, today

| Seam | Where | Contract | Used by |
|---|---|---|---|
| `MermaidRenderer` | `libs/markoff-canvas/include/markoff/canvas/MediaSeams.h:28-42` | `QPixmap render(const QString &source) const` | canvas, hardcoded to the info string `mermaid` (`BlockLayoutCache.cpp:371-380`) |
| `ImageResourceLookup` | `MediaSeams.h:20` | `QPixmap(const QString &target)` | canvas image/embed blocks (`BlockLayoutCache.cpp:387-410`) |
| `EmbedRegistry` | `libs/markoff-core/include/markoff/core/EmbedRegistry.h` | extension → `MarkdownRenderChild` factory | canvas calls `hasExtension()` only; `dispatch()` never called |
| `CodeBlockProcessor` + `CodeBlockProcessorRegistry` | `libs/markoff-core/include/markoff/core/CodeBlockProcessor.h`, `...Registry.h` | `language()`, `RenderedBlock render(bytes, Theme)`; registry keyed by language, with signals | **no leaf consults it** — no `processorFor()` call exists outside its own test |
| `RenderedBlock` | `libs/markoff-core/include/markoff/core/RenderedBlock.h` | `Kind{Image,Svg,Highlighted,Empty}`, `QImage`, `QString svg`, `spans`, `preferredSize`, `fallbackText` | **no reader in the tree** |
| `OpaqueBlockRenderer` | `libs/markoff-core/include/markoff/core/OpaqueBlockRenderer.h` | `isOpaque(BlockId, BlockKind)`, `renderOpaque(QTextCursor&, BlockId)` | styled's table frames only |
| `MarkoffServices` | `libs/markoff-core/include/markoff/core/MarkoffServices.h` | aggregate of syntax/code-processors/links/completion | never instantiated by any leaf (`BlockLayoutCache.h:261-265` says so) |

So the family already has an info-string-keyed renderer registry and a result
type designed for exactly this job. They are **dead code with tests**. The plan
below wires them rather than inventing a parallel API.

### 1.2 How canvas renders a block (the shape any embed must join)

There is no polymorphic item class. A block is a `BlockLayoutCache::Entry`;
`BlockPresentation::presentationFor()` is the single per-kind switch
(`BlockPresentation.h:14-20`); realized content is a union of fields on `Entry`
(`QTextLayout`, `mathPixmap`, `mermaidPixmap`, `imagePixmap`, `tableCells`);
`View::paintEvent` is a flag chain that branches on those fields
(`View.cpp:5742-5823`). Adding a renderer today touches ~6 files and adds a
seventh special case. That is the architectural problem this plan solves.

Relevant properties of that pipeline:

- **Synchronous, on the GUI thread.** Renderer calls happen inside
  `rebuildInline()` → `realizeRange()` → `paintEvent`. No threads, futures or
  timers anywhere in the leaf (constitution C2 forbids view-side deferral).
- **Cache key** is `(BlockId, blockEditSequence)` plus a structural-sequence
  check (`BlockLayoutCache.cpp:76-113`). Pixmaps live on the entry: any restyle
  re-renders; theme/scale change clears everything.
- **Caret-gated reveal.** Display math and mermaid show source while the caret is
  inside the block; images do not. Every rendered block needs this rule.
- **Measurement is post-hoc.** Height is overridden from the pixmap
  (`:489-508`); nothing can report a preferred size before rendering, and the
  renderer is never told the available column width.
- **No SVG.** Every artifact is a `QPixmap`. Qt6::Svg is installed (6.11.2) but
  nothing in Markoff links it.

### 1.3 Tables today

Canvas has a real grid engine, not an image: byte-level GFM tokenizer
(`TableGeometry.cpp`), two-pass measure (`BlockLayoutCache::realizeTable`),
per-cell `QTextLayout`, per-cell paint and hit-test, cell-granularity selection,
and block-relative byte ranges used directly as edit offsets
(`View.cpp:2214-2268`, `5568-5625`). Styled renders tables as `QTextTable`
frames through `OpaqueBlockRenderer` (`StyledTableRenderer`, `TableFrame.cpp`).

Known table gaps, from the recon (all fixable and all relevant as the first
acceptance case): header measurement uses the non-bold font while paint bolds it
(overflow risk); `alignment` is parsed but never applied at paint; column width
is capped at 240 DIP with no horizontal scroll; find/occurrence highlights are
not drawn in tables; cell selection is whole-cell only.

### 1.4 Mermaid already works end-to-end — through the host

Corbomite vendors a prebuilt Rust static library in `libs/mmdr/`
(`1jehuang/mermaid-rs-renderer`, MIT) whose C ABI is
`mmdr_render_svg(const char*, char**)` → SVG. Their
`Corbomite::Core::MermaidRenderer` wraps it, and
`src/editor/CanvasMermaidAdapter.cpp` rasterises the SVG with `QSvgRenderer`
into a `QPixmap` for the canvas seam. Two problems with leaving it there:

1. **Provenance.** The 34.8 MB archive is checked in with no pinned upstream
   commit, no `Cargo.lock`, no toolchain record — Corbomite's own README says it
   is not reproducible. Markoff must not inherit that.
2. **The adapter is the framework, badly.** It hardcodes an 800 px width cap,
   DPR 1, no theme, no cache, no cancellation — the same code every host would
   write for every engine.

### 1.5 What is missing for "everything"

1. No generic seam in canvas (only per-media callbacks).
2. Renderer inputs are too thin: source only. No theme, font scale, column
   width, DPR, or block identity.
3. No SVG path in the family; every surveyed engine emits SVG.
4. No async/cancellation: a subprocess or FFI render blocks paint.
5. No render cache keyed by source/theme/width/scale; no invalidation signal.
6. Attribute-only writes bump **no** sequence, so checkbox/display-mode changes
   can stay stale. Verified: `setBlockAttribute`/`removeBlockAttribute` emit
   `blocksChanged`/`d2DocumentChanged` but touch neither edit sequence
   (`MarkoffDocumentVNext.cpp:3632-3688`), and canvas neither subscribes to
   `blocksChanged` nor keys staleness on attrs (`BlockLayoutCache.cpp:76-113`,
   `View.cpp:392-399`). This is the same prerequisite that blocks the M13/M18
   caches.
7. No hit-test/selection contract for rendered content (non-table hit-test uses
   the source text layout; the pixmap branch paints nothing for selections).
8. No accessibility story for rendered content (role switch is per `BlockKind`).
9. No canvas rendering tests: the suite that covered math/table/ime/render was
   removed in A13; the inspection predicates (`isMathPixmapActive`,
   `isMermaidPixmapActive`, `isImagePlaceholderActive`, …) have no callers.
10. No fidelity corpus; every engine is a "looks right" judgement.

## 2. Architecture

### 2.1 One contract, in core

Extend the existing, currently unused types instead of adding a parallel API.
`RenderedBlock` becomes the artifact type; `CodeBlockProcessor` grows the inputs
it always needed; `CodeBlockProcessorRegistry` becomes the registry the canvas
actually consults. Keep the names (they are public and already exported) and
document the new semantics.

```cpp
// RenderedBlock additions (core)
struct RenderRequest {
    QString language;          // fenced info string, or a synth key ("math")
    QString source;            // block content, fences/delimiters excluded
    QSizeF  availableWidth;    // column width in DIP, or -1 for unbounded
    qreal   devicePixelRatio;
    qreal   fontScale;
    QColor  foreground, background;   // resolved Theme roles
    QString themeKey;          // stable hash of the colours above
};

struct RenderedBlock {
    enum class Kind { Image, Svg, Scene, Highlighted, Empty, Error };
    Kind kind = Kind::Empty;
    QImage image;              // Image: ready to paint
    QByteArray svg;            // Svg: rasterised by the service, not the renderer
    QSizeF preferredSize;      // DIP; Empty = measure from the artifact
    QString fallbackText;      // source shown when this fails or is pending
    QString error;             // Error: one line, for the block's tooltip
};

class CodeBlockProcessor {
    virtual QStringList languages() const = 0;   // aliases: {"mermaid"}
    virtual RenderedBlock render(const RenderRequest &) = 0;  // worker thread
};
```

Rules:

- **SVG-first.** A renderer returns SVG (or an `Image` if it is a raster engine);
  the *service* rasterises with `QSvgRenderer` at the current DPR and width. No
  renderer ever builds a `QPixmap`, so DPR, scaling and theme backgrounds are
  solved once.
- **`render()` may run off the GUI thread** and must be re-entrant; it receives
  no QObject or document pointer. That keeps subprocess and FFI adapters safe
  and makes cancellation possible between calls.
- **Fallback is the source text**, matching today's mermaid/math behaviour. An
  `Error` result keeps painting source and carries a one-line reason for a
  tooltip.

### 2.2 One service, in a new optional leaf

`libs/markoff-embed` (new leaf, depends on core + Qt6::Svg + Qt6::Concurrent):

- `EmbeddedRenderService : QObject` — owns a bounded worker pool, submits
  renders, enforces per-render timeouts, cancels on source/theme change,
  de-duplicates identical requests, and emits
  `renderReady(language, sourceHash, RenderedBlock)` /
  `renderFailed(...)` on the GUI thread (queued connection).
- **Cache:** LRU keyed by `(language, hash(source), themeKey, width bucket,
  dpr bucket)`. Entries survive caret moves; invalidation is explicit.
- **Subprocess runner:** `QProcess` with a per-engine sandbox policy (no
  network by default, bounded temp dir, timeout + kill, output size cap). One
  implementation shared by PlantUML/D2/Typst/Gnuplot.
- **SVG safety:** parse with `QSvgRenderer` only after stripping `<script>`,
  `<foreignObject>` and external `href`/`xlink:href`; reject documents over a
  size cap. (QSvgRenderer does not execute script, but it must not be able to
  fetch anything.)
- **No engine is implemented in this leaf initially.** It is the host; engines
  live in `libs/markoff-embed/engines/` as small adapters (or in the host, for
  proprietary/optional ones).

Canvas depends only on the *core interface* (`CodeBlockProcessorRegistry` +
`EmbeddedRenderService` as an abstract base declared in core), never on the new
leaf, so the leaf graph stays acyclic and a host can provide its own service.

### 2.3 Canvas integration

Replace the three ad-hoc seams with one path:

- `View::setEmbeddedRendererRegistry(CodeBlockProcessorRegistry*)` and
  `setEmbeddedRenderService(EmbeddedRenderService*)` (reference, not owner, the
  existing convention). `MermaidRenderer` and `ImageResourceLookup` retire;
  image blocks keep their own lookup (an image is not a rendered block).
- `BlockStyle` gains one flag (`isEmbeddedBlock`) and the fence language; the
  zero-or-more per-kind special cases collapse into it. Mermaid becomes
  "info string = mermaid" again, not a field.
- `Entry` gains `RenderedBlock artifact` + `renderKey` (source hash, theme,
  width, dpr) and a `PENDING` state. Cache lookup is synchronous and cheap; a
  miss paints source (or the previous artifact) and *asks* the service. That
  request is the only asynchronous step in the chain. **Constitution ruling
  needed:** C2 forbids view-side deferrals to escape ordering problems; a
  service-completion signal is a model-side event, not view deferral. The
  proposal is to record that distinction in the spec (§3) before coding, and to
  keep `renderReady` delivery identical in shape to `d2DocumentChanged`.
- Height: `preferredSize` (DIP) when known, else the pixmap height / DPR, else
  the source-text estimate. Width is the realized content width, passed in the
  request; a renderer may return a wider artifact, which the block clamps and
  paints centred (no per-block horizontal scroll, matching today).
- Hit-test: rendered blocks keep mapping clicks to *source offsets* (so caret
  placement and source reveal still work). A future `Scene` kind may add its own
  hit-test, but not in this plan.
- Theme/DPR changes: `themeKey`/dpr in the render key, so only affected entries
  re-render; drop the whole-cache clear for embeds.

### 2.4 Styled and headless

- `Styled::DocumentRenderer` (used by Corbomite for cards and hover previews)
  gains the same registry: it renders a `QTextDocument`, so embeds become
  `QTextImageFormat` images in a dedicated paragraph, sized from
  `preferredSize`. This is how a canvas card gets its diagram.
- `Styled::Editor` (interactive Reading) is frozen bug-fix-only; embeds there are
  read-only frames via the existing `OpaqueBlockRenderer` path **after** the
  service settles. It is a later phase and needs the owner's exception.
- Source stays text-only by design.

## 3. Phase 0 — prerequisites (do first, no engines)

1. **Restyle on attribute changes, then make the key general.**
   - Minimum, now: canvas subscribes to `blocksChanged` and invalidates the
     named entries (plus a per-block presentation sequence, since attributes are
     not in `blockEditSequence`). Fixes the live checkbox/display-mode staleness
     with a small diff.
   - General: one monotonic document content version bumped by every mutating
     path (text, structural, attribute, remote acceptance) and exposed, used in
     the cache key and available to the render cache. This is the prerequisite
     for M13/M18 and for keying embeds on more than the buffer sequence.
   - Acceptance: a test flips `DisplayMode` and `Checked` and asserts the entry
     restyles with no structural edit; a second asserts no restyle on an
     unrelated block.
2. **Restore the canvas rendering test harness.** Recreate the offscreen tests
   that A13 deleted, at least: math pixmap active/inactive, mermaid fallback,
   image placeholder, table geometry/paint, DPR scaling, theme switch. Use the
   existing inspection predicates. Acceptance: `scripts/run-tests.sh -R canvas`
   covers rendering again.
3. **Record the async ruling** in the canvas spec §3 (service signal vs C2) and
   extend `check-constitution.sh` only if a new forbidden pattern is needed.

## 4. Phase 1 — tables, the first customer

Recommendation: **tables are the framework's acceptance case, not its first
image.** Keep the editable grid (it is the only interactive embedded content in
the family); lift the implicit table contract into the framework's shape so the
next engine cannot invent a sixth special case.

What that means concretely:

1. Introduce the generic block-content contract in canvas (`measure(style,width)`
   → `size`; `paint(painter,rect,state)`; `hitTest(rect,pos)` → block byte
   offset) and make tables the first implementation of it. No behaviour change
   in the first commit; parity is the acceptance.
2. Fix the table gaps the recon found, each with a test: bold-header
   measurement, `alignment` actually applied, find/occurrence highlights drawn,
   empty-cell and escaped-pipe (`\|`) handling, a width policy for wide tables
   (clamp + centred, documented).
3. Accessibility: give tables the cell-level story they already have geometry
   for (`tableCellRect`, `caretTableCell` exist) or explicitly defer it with a
   logged decision.
4. Styled parity: keep `QTextTable` frames; add the theme values (borders,
   padding, header) that `TableFrame.cpp` currently hardcodes.

**Acceptance:** the existing table behaviour is unchanged in output, the new
tests pass, and a table inside a card preview still round-trips its pipe source
byte-for-byte.

## 5. Phase 2 — math (the first real engine)

- Display math moves from the direct `jkqtmathtext` call in
  `BlockLayoutCache::rebuildInline` to a `math` processor in the service:
  request carries font size (from `Theme` monospace slot × `fontScale`), fg/bg,
  DPR and width; the engine renders at DPR (fixing the HiDPI blur) and returns
  an `Image` or SVG.
- Inline math stays styled text until the Qt 6.12 gate opens; the spec §4.5
  mechanism (U+FFFC + `QTextImageFormat` in the single `setFormats()` call,
  painted at the reserved box) is already written down and is the implementation
  when the floor allows it.
- Acceptance: display math renders crisply at 1x and 2x, in both themes, is
  cached across caret moves, and falls back to source on a parse failure.

## 6. Phase 3 — Mermaid, formalized

- Engine: `mermaid-rs-renderer` (MIT; already proven through Corbomite's mmdr),
  behind a tiny C ABI: `render_utf8(src) -> svg`, `free`.
- **Reproducibility is a requirement:** build from a pinned commit with a
  committed `Cargo.lock`, record toolchain + checksum, and either vendor the
  crate or build it in CI. Do not accept a 34.8 MB blob without provenance —
  Corbomite's own README documents why that is a trap.
- Keep Merman (MIT/Apache, 37 diagram types) as a second backend behind the same
  adapter, selectable by a document-level setting; the survey's fidelity warning
  (browser text metrics differ) applies, so both go in the corpus.
- The old `Canvas::MermaidRenderer` seam and Corbomite's
  `CanvasMermaidAdapter` retire; the host registers the engine instead of
  writing a rasteriser.

## 7. Phase 4 onward — engine order

| # | Family | Engine | Method | Licence note |
|---|---|---|---|---|
| 4 | Graphviz DOT | Graphviz (`libgvc`) | **subprocess** (`dot`), later a linked engine only if the licence question is answered | EPL-2.0 is not GPL-compatible; a GPL-3.0-or-later library must not link it. Process = separate work, safe. |
| 5 | Pikchr | Pikchr | **direct link** (single C89 file, SVG out) | 0BSD — safest in-tree engine after math |
| 6 | WaveDrom | wavedrom-rs | Rust FFI, same shape as Mermaid | MIT |
| 7 | Svgbob | svgbob | Rust FFI | Apache-2.0 |
| 8 | D2 | D2 | subprocess | MPL-2.0 — process boundary keeps it out of the family binary |
| 9 | PlantUML | PlantUML | subprocess (JVM) | GPL/LGPL/others; process only |
| 10 | Typst | Typst | subprocess first; FFI later if valuable | Apache-2.0 |
| 11 | Gnuplot | gnuplot | subprocess | free licence; process only |
| 12 | Excalidraw | new native Qt viewer | in-tree, later | JSON scene → Qt painting; a separate project-sized arc |
| 13 | Vega-Lite | new native subset | in-tree, last | no native engine exists; explicit subset + declared "unsupported" list |

Every row lands with: an adapter, a corpus fixture, a golden SVG (tolerance
compared), a fallback test, and a row in `docs/embedding-support.md` (the
maintained capability matrix). Rows declared unsupported are listed there too —
"we want everything" means *every family has a decision*, not that every
renderer is native.

## 8. Fidelity, testing and CI

- **Corpus:** `tests/embed-corpus/<language>/<case>.md` + expected artifact.
  Start with the existing docs' own diagrams and one hostile input per engine
  (cycles, empty, 10k nodes, invalid syntax, RTL/CJK text, emoji labels).
- **Offscreen golden tests:** render via the service at 1x and 2x, compare SVG
  normalised (strip ids/order-independent attributes) or PNG with a perceptual
  threshold. A changing golden is a review event, never a silent pass.
- **Theme matrix:** light/dark for every engine; a diagram with hardcoded
  colours is allowed to be theme-independent, but its background must match the
  editor.
- **Failure matrix:** missing engine binary, non-zero exit, timeout, oversized
  output, invalid SVG, panic across FFI (must be caught: no unwinding across the
  C boundary) — each must fall back to source and never take the editor down.
- **Performance budget:** a render may not run on the paint path; first paint
  after a miss shows source; a 100-block document with 20 diagrams must scroll
  without a frame longer than the existing budget.

## 9. Security and robustness

- No network access from any engine (subprocess environment stripped, no proxy
  vars); embedded external references (`href`) are stripped from SVG.
- Subprocess: absolute engine path from config, no shell, fixed argument vector,
  bounded temp dir, wall-clock timeout, output cap, kill-on-cancel.
- FFI: catch panics at the boundary; validate UTF-8 in and out; free functions
  paired in the adapter.
- Cache keys include the engine id + version, so a changed engine invalidates
  old artifacts.
- Hostile-input fuzzing for the in-tree parsers (Pikchr C code especially) is a
  gate before Pikchr ships.

## 10. Packaging and licensing

- Family licence is GPL-3.0-or-later. Linkable: MIT, Apache-2.0 (with notice),
  BSD, 0BSD, LGPL (jkqtmathtext already). Not linkable: EPL-2.0, MPL-2.0,
  GPL-with-incompatible-terms — those engines go behind the process boundary.
- `LICENSES/` gains one entry per engine + the exact upstream revision; the
  `mmdr` lesson is now a rule: **no prebuilt binary without a recorded commit,
  toolchain, lockfile and checksum.**
- New leaf `libs/markoff-embed`: `MARKOFF_EMBED_BUILD_TESTS` seeded from
  `MARKOFF_BUILD_TESTS` (the canonical gate), optional dependency of canvas — a
  host that does not link it gets source-text fallback, never a broken build.
- Qt6::Svg becomes an optional (REQUIRED when the leaf is built) dependency;
  floor stays Qt 6.8.

## 11. Work breakdown

| Phase | Deliverable | Effort | Acceptance |
|---|---|---|---|
| 0 | content version + canvas cache key | S–M | attribute-only edit restyles; test |
| 0 | restored canvas render test harness | M | `-R canvas` covers render/math/table/image |
| 0 | constitution ruling recorded | S | spec §3 updated |
| 1 | generic block-content contract; tables migrated | M | table output parity + gap tests |
| 1 | table gaps (bold header, alignment, find highlights, escapes, width) | M | one test per gap |
| 2 | `EmbeddedRenderService` + registry in core + SVG rasteriser | M–L | fake engine round-trip, async, cancel, cache |
| 2 | canvas wired to the service; mermaid seam retired | M | mermaid via registry, no paint-path render |
| 2 | math engine (DPR/theme/cache) | S–M | 1x/2x, both themes, fallback |
| 3 | mermaid FFI, reproducible build, corpus | M | provenance recorded; golden tests |
| 4 | Graphviz subprocess + sandbox | S–M | timeout/failure/fallback tests |
| 5 | Pikchr linked engine | M | fuzz gate + golden tests |
| 6–11 | WaveDrom, Svgbob, D2, PlantUML, Typst, Gnuplot | M each | per-engine corpus + matrix row |
| 12–13 | Excalidraw, Vega-Lite | L each | separate arcs |

Relative sizes only; each row is a slice with its own commit and tests.

## 12. Decisions needed (recommendations in bold)

1. **Tables: keep the editable grid** (recommended) or render tables through an
   engine (loses cell editing/hit-test). If an engine-rendered table is wanted
   as a *fallback* for unparseable tables, say so and it becomes a renderer.
2. **Graphviz/PlantUML/D2/Typst:** subprocess adapters (recommended, licence-safe)
   or relax the linking rule. Linking EPL-2.0 Graphviz into a GPL-3.0 library is
   the one option I would not take.
3. **Mermaid:** adopt `mermaid-rs-renderer` as the family engine with a
   reproducible build (recommended), keep Corbomite's blob, or use Merman first.
4. **Scope of leaves:** canvas + headless renderer first; styled interactive
   embeds later behind a bug-fix exception (recommended). 
5. **Async ruling:** accept the service-signal model as not-a-C2-deferral
   (recommended; recorded in the spec before coding).
6. **New leaf vs core:** `libs/markoff-embed` optional leaf (recommended) vs
   putting the service inside canvas (rejected: thread + process code in the
   leaf, breaks its constitution's spirit and its testability).

## 13. Risks

- **Scope.** The survey lists ~20 families; this plan deliberately makes each a
  slice with a decision, not a commitment to native implementations of
  Excalidraw/Vega-Lite.
- **Fidelity expectations.** Native Mermaid will differ from browser Mermaid
  (text metrics, HTML labels). The corpus + an explicit "known differences"
  section per engine is the mitigation.
- **Async in a synchronous leaf.** The constitution question is real; do Phase 0
  first and do not smuggle `singleShot` into canvas.
- **Third-party blobs.** Enforced provenance; a missing commit is a blocker.
- **Cache staleness.** The content-version prerequisite is not optional; every
  engine that lands before it inherits the attribute-staleness bug.

## 14. Immediate next action

Phase 0, task 1: the document-wide content version, because it fixes a live
stale-rendering bug (checkbox/display-mode), unblocks the cache work on the asks
board, and is a prerequisite for every renderer.
