# markoff-embed — embedded-renderer plugin runtime

The "machinery" module of the plugin architecture. Owns the artifact cache,
the worker pool and SVG rasterisation; backends are `CodeBlockProcessor`
plugins registered into a `CodeBlockProcessorRegistry` this runtime reads.
Written 2026-10-19 (the leaf had no guide before).

## Contract lives in core, not here

- `markoff-core/include/markoff/core/CodeBlockProcessor.h` —
  `kEmbedContractVersion` (currently 2), `RenderRequest`,
  `CodeBlockProcessor` (languages/render/contractVersion).
- `markoff-core/include/markoff/core/EmbedRender.h` — `EmbedArtifact`
  (Missing/Pending/Ready/Failed), `EmbedRenderService` (artifact/request/
  supportedLanguages/engineDescription + artifactReady/artifactFailed),
  `embedArtifactKey`, `embedThemeKey`.
- `markoff-core/include/markoff/core/RenderedBlock.h` — the artifact
  (`Image`/`Svg` kinds; `Scene` is reserved and **not implemented**).

A leaf consumes the contract without depending on this runtime; a null
`EmbedRenderService` means "no embedded rendering" and blocks fall back to
source. A host may replace this runtime with its own service implementation.

## What this leaf is

- `EmbedRuntime` (`include/markoff/embed/EmbedRuntime.h`) — final
  `EmbedRenderService`: LRU cache (default 64 entries), `QThreadPool`
  (max 2), pending-set dedup, SVG rasteriser, contract-version refusal.
  Public API is UI-thread affine; `render()` runs on the pool and completion
  is delivered with a queued invocation.
- `backends/math` (`markoff_embed_math`) — the first real engine:
  jkqtmathtext, returns an `Image` at the request's DPR, strips math
  delimiters, applies theme colours. Opaque background (documented
  limitation); it bypasses the runtime's SVG rasteriser by returning `Image`.

## Consumers

- `markoff-canvas`: `BlockLayoutCache` looks the artifact up, paints
  `embedImage`, requests on `Missing`, falls through to source on
  `Pending`/`Failed`; `View::setEmbedRenderService` wires
  artifactReady/artifactFailed to `invalidateByEmbedKey` + viewport update.
- Test-only: `FakeSvgProcessor` in `tst_embed_runtime.cpp`.

## Tests

- `tst_embed_runtime` — cache hit/miss, pending dedup, completion delivery,
  contract-version refusal (`backendFromAnotherContractVersionIsRefused`).
- `tst_embed_math` — contract version + language registration, DPR handling.
- Canvas exercises the path end-to-end in `tst_canvas_render`
  (`embeddedFenceRendersThroughTheRuntime`, `displayMathRendersThroughTheRuntime`).

```bash
scripts/run-tests.sh -R embed    # offscreen
```

## Build

`MARKOFF_EMBED_BUILD_TESTS` follows the family `MARKOFF_BUILD_TESTS`
switch the root sets; a leaf configured alone defaults to ON. Export is
gated on `KDE_INSTALL_LIBDIR` exactly like the other leaves — a standalone
Markoff installs nothing.

## Known gaps (2026-10-19)

- Only two backends exist: the fake (test) and math. Mermaid still runs on
  canvas's legacy `MermaidRenderer` seam (`BlockLayoutCache.cpp:478-486`)
  until Corbomite's adapter moves; graphviz/pikchr/typst/… are unbuilt —
  the survey is `docs/research/mermade-and-others.md` and the phased plan
  is `docs/plans/2026-10-19-embedded-renderers-plan.md`.
- `engineDescription` currently returns `"<lang> (embed contract N)"`, not a
  human backend name; the contract asks for the latter ("mermaid-rs").
- No subprocess sandbox for process-backed engines yet.
