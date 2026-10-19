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

Known table gaps (one recon claim corrected during implementation: the header
**was** already measured with the bold font in both passes; paint's extra
`setFont` is harmless). Real gaps: `alignment` was parsed but never applied at
paint; column width was capped at 240 DIP independent of the editor, so a wide
table was clipped with no horizontal scroll; find/occurrence highlights were
not drawn in tables; escaped pipes (`\|`) split cells.

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

## 2. Architecture — contract in core, runtime and backends as optional modules

The first draft made this look like a choice between "put the renderers in core" and
"put them in each leaf". It is neither. Split it into four layers, each with one
job and a small dependency surface:

| Layer | What lives here | Depends on | Optional? |
|---|---|---|---|
| **Contract** (`markoff-core`) | `RenderRequest`, `RenderedBlock`, the renderer interface, the registry, a contract version | QtCore/QtGui only. No threads, no `QProcess`, no `Qt6::Svg` | No — every leaf and backend must agree on it |
| **Front-end** (each leaf) | "Is this block an embed? Ask for its artifact; paint the artifact, the source, or a pending state." | The contract only | No — but it is ~one helper per leaf, not engine code |
| **Runtime** (`markoff-embed`) | Registry implementation, worker pool, cache, SVG rasteriser, subprocess sandbox, backend registration | Contract, Qt6::Svg, Qt6::Concurrent | **Yes**, and replaceable by the host |
| **Backends** (`markoff-embed-<engine>`) | One adapter per engine: source → SVG/`RenderedBlock` | Runtime contract + that engine's own deps | **Yes, each independently** |
| **Host** | Chooses runtime and backends; may inject its own | Anything | — |

That is the plugin shape: a leaf has a generic front-end that knows only the
contract; backends are linked in or left out per target; the runtime (and with it
QtSvg, threads, process management) is not forced on anyone.

### 2.1 Why this is better than the earlier two-way choice

- **Core stays thin.** It gains value types and an interface, not an engine.
  A host that never renders a diagram pays nothing (no QtSvg, no thread pool,
  no process code).
- **Leaves stay clean.** Canvas's constitution forbids deferral and process code
  in the leaf; the runtime lives outside it, and the leaf only *consumes* a
  completion signal the same way it consumes `d2DocumentChanged`.
- **Backends are leaves in dependency terms.** `markoff-embed-graphviz` depends
  on the runtime contract; neither core nor canvas depends on it. A backend can
  be added, dropped, or licence-split (process vs FFI) without touching a leaf.
- **The document engine is untouched.** Backends are pure `source → artifact`.
  No new `BlockKind`, no CRDT change, no round-trip impact: a fenced block is
  still a `CodeBlock` with an info string, and math is still `BlockKind::Math`.
  The plugin layer never reads or writes the document.

### 2.2 Contract (core, header + tiny implementation) — **landed 2026-10-19**

Keep the existing names and headers — `CodeBlockProcessor`,
`CodeBlockProcessorRegistry`, `MarkoffServices` — because Corbomite already
builds a plugin registrar on them (`Corbomite/core/proxies/CodeBlockRegistrar`,
keyed by language tag, first-wins). Evolve them in place; do not add a parallel
API. The one breaking change is the render signature (below); the registrar only
forwards `shared_ptr`s and compiles unchanged.

```cpp
struct RenderRequest {
    QString language;          // fenced info string, or a synth key ("math")
    QString source;            // block content, fences/delimiters excluded
    QSizeF  availableWidth;    // column width in DIP, or -1 for unbounded
    qreal   devicePixelRatio;
    qreal   fontScale;
    QColor  foreground, background;
    QString themeKey;          // stable hash of the above colours
};

struct RenderedBlock {
    enum class Kind { Image, Svg, Scene, Highlighted, Empty, Error };
    Kind kind = Kind::Empty;
    QImage image;              // ready to paint (raster engines)
    QByteArray svg;            // rasterised by the runtime, never by the backend
    QSizeF preferredSize;      // DIP
    QString fallbackText;
    QString error;
};

class CodeBlockProcessor {
public:
    virtual ~CodeBlockProcessor() = default;
    virtual QStringList languages() const = 0;                 // aliases
    virtual int contractVersion() const { return kEmbedContractVersion; }
    virtual RenderedBlock render(const RenderRequest &) = 0;   // worker-thread safe
};
```

`CodeBlockProcessorRegistry` stays as it is (language → shared_ptr, with the
registered/unregistered signals) — the front-end and the host already have the
shape they need.

### 2.3 Runtime (`markoff-embed`, optional)

`EmbedRenderService : QObject`, owned by the host, not by a leaf:

- synchronous cache lookup keyed by `(language, hash(source), themeKey, width,
  dpr)`, returning `RenderedBlock` or `Pending`;
- `request(RenderRequest)` on a miss → bounded worker pool; cancellation when
  the key changes; deduplication of identical requests;
- `artifactReady(language, sourceHash)` signal (GUI thread) — the leaves
  invalidate that block and repaint;
- the only place `Qt6::Svg` is used: SVG → `QImage`/`QPixmap` at the requested
  DPR and width;
- subprocess sandbox (timeout, bounded temp dir, no network, output cap) for
  engines that need it;
- capability/status query (`engineFor(language)`, last error) so a host can
  explain a missing diagram.

A host may replace the runtime entirely (for example with Corbomite's existing
mmdr renderer): it only has to implement the same small interface. A host that
links no runtime gets today's behaviour: source text.

### 2.4 Backends (each optional)

- One target per engine: `markoff-embed-pikchr`, `markoff-embed-mermaid`,
  `markoff-embed-graphviz` (process adapter), `markoff-embed-typst`, …
- Each has its own CMake option, its own licence gate, and its own corpus. A
  backend that vendors a toolchain (Rust engines) is where that dependency is
  paid, not in the runtime or a leaf.
- **Registration defaults to static**: a generated
  `registerBuiltinBackends(EmbedRenderService&)` TU compiled from the enabled
  options. That already gives "linked in or left out" with no ABI tax.
- **Dynamic loading is optional and later**: `QPluginLoader` with an IID and
  `contractVersion()`, for distro packages or third-party engines. It is not
  required for modularity, and it buys an ABI/versioning burden that a
  static registry does not have.
- Process-backed engines are backends too (a `QProcess` adapter inside the
  runtime is just one implementation of "render this source"); leaves see no
  difference.

### 2.5 Front-end in each leaf — **canvas done 2026-10-19**

A small consumer, not an engine host:

- canvas: `BlockStyle` gains `isEmbeddedBlock` + the language; `Entry` gains a
  cached `RenderedBlock`/`Pending` state. Paint order is artifact → source →
  pending (source until the artifact arrives). The existing
  `MermaidRenderer`/`ImageResourceLookup` seams retire; images keep their own
  lookup because an image is not a rendered block.
- styled/headless: `DocumentRenderer` uses the same registry/service to place
  an image in a `QTextDocument`; `Styled::Editor` (frozen) stays text-only until
  an exception is granted.
- source: unchanged by design.

### 2.6 What this costs

- One more interface to keep stable (`contractVersion`, additive changes only
  within a major version).
- A CMake option and a test gate per backend; the test matrix is
  "runtime + all enabled backends" rather than one monolith.
- Backends must never leak their engine types into the contract (only bytes and
  sizes), or the modularity is lost.
- The runtime becomes a shared dependency; it must not grow document or view
  knowledge. Code review rule: runtime knows requests and artifacts, nothing
  about blocks, carets, or sessions.

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

## 4. Phase 1 — tables, the first customer — **started 2026-10-19**

Status: attribute-change restyling (Phase 0 task 1) and the table basics landed
(`tst_canvas_render`, 7 render/interaction cases; suite 96/96): escaped pipes,
delimiter-row alignment at paint, width budget from the editor's column, find and
occurrence highlights inside cells, click-to-caret and typing through a cell.
Still open in this phase: the generic block-content contract extraction, the
a11y table interface/decision, and styled table theming.

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
- New runtime leaf `libs/markoff-embed` plus one target per backend
  (`markoff-embed-<engine>`), each with its own CMake option and test gate seeded
  from `MARKOFF_BUILD_TESTS` (the canonical gate). A host that links neither gets
  source-text fallback, never a broken build.
- Qt6::Svg becomes an optional (REQUIRED when the leaf is built) dependency;
  floor stays Qt 6.8.

## 11. Work breakdown

| Phase | Deliverable | Effort | Acceptance |
|---|---|---|---|
| 0 | attribute restyle + canvas cache key | S–M | **done** (`61100e19`) |
| 0 | restored canvas render test harness | M | **done** (`tst_canvas_render`) |
| 0 | constitution ruling recorded | S | spec §3 amended |
| 1 | generic block-content contract; tables migrated | M | table output parity + gap tests |
| 1 | table a11y decision; styled table theming | S–M | decision logged / theme test |
| 2 | contract in core (request/result/version); registry evolved in place | M | **done** (`tst_embed_runtime`) |
| 2 | `markoff-embed` runtime (cache, pool, SVG, process sandbox) | M–L | **cache/pool/SVG done**; sandbox open |
| 2 | canvas front-end: one embedded path; mermaid seam retired | M | **generic path done** (service lookup → source fallback → rasterised paint); legacy `MermaidRenderer` seam still present until Corbomite's adapter moves |
| 2 | math backend (DPR/theme/cache) | S–M | **done** (`markoff_embed_math`, `tst_embed_math`; canvas display math goes through it) |
| 3 | mermaid backend, reproducible build, corpus | M | next: adapter around a pinned build |
| 4 | Graphviz subprocess backend + sandbox | S–M | timeout/failure/fallback tests |
| 5 | Pikchr backend (linked) | M | fuzz gate + golden tests |
| 6–11 | WaveDrom, Svgbob, D2, PlantUML, Typst, Gnuplot | M each | per-engine corpus + matrix row |
| 12–13 | Excalidraw, Vega-Lite | L each | separate arcs |

Relative sizes only; each row is a slice with its own commit and tests.

## 12. Decisions needed (recommendations in bold)

The plugin split makes most of the old decisions local to one module. What is
actually left to call:

1. **Plugin delivery: static optional modules (recommended)** — one CMake option
   per backend and a generated registration TU — vs adding `QPluginLoader`
   dynamic loading now. Dynamic buys distro/third-party packaging at the cost of
   ABI/version discipline; it can be added later without changing the contract.
2. **Runtime ownership: family runtime (recommended)** vs host-provided only.
   The family runtime is the convenience; the interface stays small enough that
   a host (Corbomite with mmdr) can replace it.
3. **Async ruling:** accept the runtime-completion signal as not-a-C2-deferral
   (recommended; recorded in the spec before coding), with the rule that a leaf
   never schedules work itself.
4. **Contract naming:** keep `CodeBlockProcessor`/`CodeBlockProcessorRegistry`
   and evolve them in place (recommended — Corbomite's registrar already builds
   on them) vs rename to `Markoff::Embed::*` and update that facade.

Engine choices are no longer architecture decisions — they are one backend each,
with their own licence and build gate:

- **Tables:** keep the editable grid (recommended); an engine-rendered table is
  at most a read-only/preview fallback.
- **Copyleft engines (Graphviz EPL-2.0, D2 MPL-2.0, PlantUML GPL):** process
  backends (recommended; a linked backend is a licence defect, not a trade-off).
- **Mermaid:** `mermaid-rs-renderer` with a reproducible build (recommended).

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
