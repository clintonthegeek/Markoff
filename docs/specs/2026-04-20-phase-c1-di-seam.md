# Phase C1 — Dependency Injection Seam

**Status:** Draft
**Depends on:** Phase A (`2026-04-20-tri-view-unified-api-design.md`), Phase B (`2026-04-20-phase-b-corbomite-migration.md`). Assumes the Markoff submodule in Corbomite is pinned at `v0.2.9` or later and `MARKOFF_READING_USE_REAL_COREDEPS=ON` is the current bridging mechanism.
**Audience:** The Corbomite agent executing C1 against both `/home/clinton/dev/Markoff/` and `/home/clinton/dev/Corbomite/`. Produces a Markoff `v0.3.0-alpha.1` intermediate tag and a Markoff `v0.3.0` final tag; Corbomite submodule gets bumped twice.
**Absorbs Corbomite-side:** the `MARKOFF_READING_USE_REAL_COREDEPS` CMake option, the `libs/markoff-reading/stubs/corbomite/` shim tree, and the Corbomite-side top-level `set(MARKOFF_READING_USE_REAL_COREDEPS ON CACHE BOOL "" FORCE)`.

---

## 1. Goal

Retire the Phase B CMake-option bridge. Replace stub-shim-of-Corbomite-types with **Markoff-owned interfaces injected at runtime**. Consuming projects (CorbomiteApp or any future third-party) provide concrete implementations; Markoff ships default no-op implementations so the standalone build keeps working with zero external deps.

Scope is deliberately narrow: **swap the types**, preserve existing ownership shapes and call sites. Larger reshuffles (shared `MarkoffDocument` text routing, `Theme`/`ResourceProvider`/`LinkResolver` consolidation, renderer unification) are **not** in this phase — they're C3, C2, C4 respectively.

## 2. Non-goals

- Redesigning how `ReadingView` owns `CodeBlockProcessorRegistry` (stays internally-owned).
- Redesigning how host applications wire `EmbedRenderer` into ReadingView's pipeline (stays externally-constructed).
- Consolidating the three leaves' `Theme`/`ResourceProvider`/`LinkResolver` types — that's C2. Note that C1 DOES introduce new *vault-level* types (`Markoff::Vault::ResourceProvider`, `Markoff::Vault::LinkResolver`) which are a different concept from the forward-declared leaf-display `Markoff::ResourceProvider`/`Markoff::LinkResolver` in `MarkdownView.h`. The semantic split is explicit in §4.
- Absorbing mmdr or any Rust code into Markoff. `mmdr_ffi.h` stays in Corbomite; Markoff gets an `IMermaidRenderer` interface, Corbomite implements it using the Rust crate. (Per Phase B decision #2.)
- Moving embeds registration (`Corbomite::ReadingView::registerBuiltinEmbedFactories`) into Markoff. Built-in factories live in Corbomite and register against the Markoff-owned `EmbedRegistry`.
- Plugin-facing ABI. None of the new Markoff interfaces are exposed through Corbomite's plugin system. Plugins continue to see the Corbomite-side facade; whether/how that facade gets retyped against the new Markoff interfaces is deferred (plugin API stability is load-bearing — the plugin-ready surfaces pattern from Cluster N applies: "Don't expose a library's internals via plugin API until the library is stable standalone", and Markoff public API isn't stable until Phase C closes).

## 3. Current state (what C1 dismantles)

Per the Phase B migration spec, markoff-reading currently references these eight types from the `corbomite/` shim tree:

| Stub header                           | Type(s)                                                                                            | Real home (host) |
|---------------------------------------|----------------------------------------------------------------------------------------------------|------------------|
| `corbomite/core/EmbedRegistry.h`      | `Corbomite::Core::EmbedRegistry`, `EmbedRequest`, `EmbedFactory`                                    | `libs/core/`     |
| `corbomite/core/CodeBlockProcessorRegistry.h` | `Corbomite::Core::CodeBlockProcessorRegistry`, `CodeBlockContext`, `CodeBlockProcessor`    | `libs/core/`     |
| `corbomite/core/MarkdownRenderChild.h`| `Corbomite::Core::MarkdownRenderChild`                                                              | `libs/core/`     |
| `corbomite/core/EmbedDepthGuard.h`    | `Corbomite::Core::EmbedDepthGuard`                                                                  | `libs/core/`     |
| `corbomite/core/VaultResourceProvider.h` | `Corbomite::Core::VaultResourceProvider`                                                         | `libs/core/`     |
| `corbomite/storage/MetadataCache.h`   | `Corbomite::MetadataCache`                                                                          | `libs/storage/`  |
| `corbomite/storage/MetadataParser.h`  | `Corbomite::MetadataParser`, `MetadataParseResult`                                                  | `libs/storage/`  |
| `corbomite/storage/LinkResolver.h`    | `Corbomite::LinkResolver`                                                                           | `libs/storage/`  |
| `corbomite/storage/CachedMetadata.h`  | `Corbomite::CachedMetadata`, `SourcePosition`, `SourceRange`, `HeadingCache`, `BlockCache`, `SectionCache` | `libs/storage/`  |
| `mmdr_ffi.h`                          | `mmdr_render_svg`, `mmdr_free` — C FFI                                                              | `libs/mmdr/`     |

All ten stubs get retired by C1.

The CMake option that gates real-vs-stub resolution (`MARKOFF_READING_USE_REAL_COREDEPS`) also retires, as does the Corbomite-side top-level `set(... ON ...)` line.

## 4. Target architecture

### 4.1 New Markoff-owned types

All interfaces ship from `markoff-core` (the primitives library every leaf links). Header prefix: `<markoff/...>` or `<markoff/vault/...>`.

| Markoff type                              | Replaces stub for                                | Abstract or concrete? |
|-------------------------------------------|--------------------------------------------------|-----------------------|
| `Markoff::EmbedRegistry` (concrete)       | `Corbomite::Core::EmbedRegistry`                 | concrete              |
| `Markoff::EmbedRequest` (struct)          | `Corbomite::Core::EmbedRequest`                  | value                 |
| `Markoff::EmbedFactory` (using)           | `Corbomite::Core::EmbedFactory`                  | alias                 |
| `Markoff::CodeBlockProcessorRegistry` (concrete) | `Corbomite::Core::CodeBlockProcessorRegistry` | concrete           |
| `Markoff::CodeBlockContext` (struct)      | `Corbomite::Core::CodeBlockContext`              | value                 |
| `Markoff::CodeBlockProcessor` (using)     | `Corbomite::Core::CodeBlockProcessor`            | alias                 |
| `Markoff::MarkdownRenderChild` (base)     | `Corbomite::Core::MarkdownRenderChild`           | concrete base         |
| `Markoff::EmbedDepthGuard` (concrete)     | `Corbomite::Core::EmbedDepthGuard`               | concrete              |
| `Markoff::Vault::ResourceProvider` (abstract) | `Corbomite::Core::VaultResourceProvider`     | **abstract**          |
| `Markoff::Vault::LinkResolver` (abstract) | `Corbomite::LinkResolver`                        | **abstract**          |
| `Markoff::Vault::MetadataCache` (abstract)| `Corbomite::MetadataCache`                       | **abstract**          |
| `Markoff::Vault::CachedMetadata` (struct, + nested `HeadingCache`/`BlockCache`/`SectionCache`/`SourcePosition`/`SourceRange`) | `Corbomite::CachedMetadata` et al. | value |
| `Markoff::Vault::MetadataParser` (abstract) | `Corbomite::MetadataParser`                    | **abstract**          |
| `Markoff::Vault::MetadataParseResult` (struct) | `Corbomite::MetadataParseResult`             | value                 |
| `Markoff::MermaidRenderer` (abstract)     | `mmdr_render_svg`/`mmdr_free` C FFI              | **abstract**          |

**Why `Markoff::Vault::` sub-namespace** for `ResourceProvider`, `LinkResolver`, `MetadataCache`, `MetadataParser`, `CachedMetadata`:

- These describe *vault-level semantics* — resolving wiki-links against a note tree, caching per-file metadata, parsing frontmatter. They're not intrinsic to how a Markoff view renders.
- The names `ResourceProvider` and `LinkResolver` are already **forward-declared** in `markoff-core/include/markoff/MarkdownView.h` for the **leaf-display** concept (where fonts come from, how a displayed link turns into a `QUrl`). Those are C2's turf. Putting the vault-level concepts under `Markoff::Vault::` prevents collision and keeps the two concerns clearly separable.
- C2, when it consolidates the leaf-display triad, does not touch `Markoff::Vault::*`.

**Why concrete for the registries + guard + render-child:** these are pure in-process mechanisms with no host-environment dependence. They were already concrete in Corbomite; C1 doesn't add virtuality where there was none. Abstract types are limited to the cases where host vs. standalone behavior materially differs (vault lookups, mermaid rendering, metadata cache-hits).

**Why abstract for the four Vault types + MermaidRenderer:** each of these has a default no-op behavior (empty vault, empty cache, no mermaid) that Markoff standalone can ship, and a real behavior the host plugs in. The abstract interface is the seam.

### 4.2 Default implementations (standalone mode)

`markoff-core` also ships `Markoff::Default*` concrete classes with the exact same no-op semantics as today's stubs. These exist so the Markoff-only build (no host) is functionally equivalent to today's `MARKOFF_READING_USE_REAL_COREDEPS=OFF` build.

| Default class                                    | Behavior                                                                                 |
|--------------------------------------------------|------------------------------------------------------------------------------------------|
| `Markoff::Vault::DefaultResourceProvider`        | Returns empty `QUrl`/`std::optional<QString>`/`QByteArray` for all resolvers; `wikiLinkExists→false`. |
| `Markoff::Vault::DefaultLinkResolver`            | `resolve()` returns empty `QString`.                                                      |
| `Markoff::Vault::DefaultMetadataCache`           | `getFileCache()` returns `nullptr`. Markoff-reading falls back to synchronous parse.     |
| `Markoff::Vault::DefaultMetadataParser`          | `parse()` returns empty `CachedMetadata`. Callers treat as "no headings / no blocks".    |
| `Markoff::DefaultMermaidRenderer`                | `renderSvg()` returns empty `QByteArray`. Reading view shows the mermaid fence as code.  |

These are cheap to construct and have no state. Tests use them directly. The standalone test app (`markoff-testapp`) instantiates them inside `main()` and passes them to `ReadingView`.

### 4.3 Injection mechanism

Strictly a **setter-style seam on `ReadingView`**, matching today's `setVaultResourceProvider()` pattern. No new Context object; no ctor-injection redesign. Goal is minimum surface change.

Added to `Markoff::Reading::ReadingView`'s public API:

```cpp
void setEmbedRegistry(Markoff::EmbedRegistry *registry);
void setVaultResourceProvider(Markoff::Vault::ResourceProvider *provider);  // replaces existing signature
void setVaultLinkResolver(Markoff::Vault::LinkResolver *resolver);
void setVaultMetadataCache(Markoff::Vault::MetadataCache *cache);
void setVaultMetadataParser(Markoff::Vault::MetadataParser *parser);        // NEW — parser was previously a static function
void setMermaidRenderer(Markoff::MermaidRenderer *renderer);

Markoff::CodeBlockProcessorRegistry *codeBlockProcessorRegistry();  // shape unchanged; type renamed
Markoff::EmbedDepthGuard *embedDepthGuard();                        // exposed so host can tune (was internal field)
```

All setters take raw `T *` (non-owning). The host guarantees lifetime exceeds the `ReadingView`. If a setter is never called, ReadingView falls back to a lazily-constructed `Default*` (see §4.4 below). Lazy construction keeps `sizeof(ReadingView)` stable and avoids paying for defaults when the host replaces them immediately.

**Threading model:** all setter calls must happen on the GUI thread before rendering begins (or during rendering pauses — `ReadingView` reads the pointers on the GUI thread only). Setters that change mid-render invalidate the current section pipeline (follow the same pattern as `setTheme`).

### 4.4 Default-fallback pattern inside `ReadingView`

For each injectable, `ReadingView` holds a raw pointer + a `std::unique_ptr<DefaultX>` fallback. Accessor inside the class:

```cpp
Markoff::Vault::ResourceProvider *resources() {
    if (m_vaultProvider) return m_vaultProvider;
    if (!m_defaultProvider) {
        m_defaultProvider = std::make_unique<Markoff::Vault::DefaultResourceProvider>();
    }
    return m_defaultProvider.get();
}
```

One fallback per abstract interface. The lazy-init means default implementations are only created when actually used, so the common case (host-provided deps) pays zero overhead.

`MermaidRenderer` uses the same pattern. `MetadataCache`/`MetadataParser`/`LinkResolver` same.

The concrete registries (`EmbedRegistry`, `CodeBlockProcessorRegistry`, `EmbedDepthGuard`) are owned by `ReadingView` when not externally set — same as today.

### 4.5 Corbomite-side adapter layer

Corbomite's `libs/core/` currently owns concrete `Corbomite::Core::EmbedRegistry`, `CodeBlockProcessorRegistry`, etc. Those types **stay** — they're part of Corbomite's own public surface (plugin-facing via `EmbedRegistrar`, `CodeBlockProcessorRegistrar`, …). What changes is that they **become adapters** that wrap a `Markoff::*` instance:

- `Corbomite::Core::EmbedRegistry` wraps a `Markoff::EmbedRegistry` — owns it by `std::unique_ptr`, delegates `registerExtension`/`dispatch`.
- `Corbomite::Core::CodeBlockProcessorRegistry` same — wraps `Markoff::CodeBlockProcessorRegistry`.
- `Corbomite::Core::MarkdownRenderChild` inherits `Markoff::MarkdownRenderChild` (and continues to inherit `Corbomite::Component` for host-side lifecycle — multiple inheritance is fine; both bases are lightweight).
- `Corbomite::Core::EmbedDepthGuard` becomes a thin wrapper or is retired entirely in favor of `Markoff::EmbedDepthGuard` (TBD during implementation; the decision is whether any plugin ABI references it).
- `Corbomite::Core::VaultResourceProvider` subclasses `Markoff::Vault::ResourceProvider`; the existing virtual methods match 1:1. Real implementation (Cluster J's `Corbomite::VaultResourceProviderImpl`) already returns sensible values; no behavior change.
- `Corbomite::MetadataCache`, `Corbomite::LinkResolver`, `Corbomite::MetadataParser`: subclass the `Markoff::Vault::*` abstracts. Delegate to existing implementations.
- `Corbomite::MermaidRenderer` (**new**): implements `Markoff::MermaidRenderer` by calling `mmdr_render_svg` / `mmdr_free`. Lives in `libs/mmdr/` or `libs/core/` — implementation choice during C1.

The adapter layer is thin enough that its diff is measured in `sizeof` changes and extra `override` keywords; no logic moves.

`MainWindow` gains a few lines to construct each Markoff-side interface-holder once at vault-open time and hand the pointers to `ReadingView` (and `EmbedRenderer`, `HoverPopover`).

### 4.6 CMake surface retirement

C1 end-state: `markoff-reading/CMakeLists.txt` has no conditional on `MARKOFF_READING_USE_REAL_COREDEPS`. The `target_link_libraries(markoff_reading ...)` always resolves because `markoff_reading` transitively links `markoff_core` for the new `Markoff::*` types; host-provided implementations are never referenced from markoff-reading's sources.

On the Corbomite side: the top-level `set(MARKOFF_READING_USE_REAL_COREDEPS ON CACHE BOOL "" FORCE)` is removed. The `Corbomite::Core` and `Corbomite::Storage` links that markoff-reading had in real-deps mode disappear — markoff-reading no longer links host libraries at all.

## 5. Breaking-change manifest (what CorbomiteApp sees after the C1 pin bump)

### 5.1 Public types on `markoff-reading` change

`ReadingView`'s public API surfaces the new Markoff types:

| Old (v0.2.x)                                              | New (v0.3.0)                                          |
|-----------------------------------------------------------|-------------------------------------------------------|
| `ReadingView::setVaultResourceProvider(Corbomite::Core::VaultResourceProvider *)` | `ReadingView::setVaultResourceProvider(Markoff::Vault::ResourceProvider *)` |
| `ReadingView::codeBlockProcessorRegistry()` returns `Corbomite::Core::CodeBlockProcessorRegistry *` | returns `Markoff::CodeBlockProcessorRegistry *`       |
| `EmbedRenderer(Corbomite::Core::EmbedRegistry *, Corbomite::MetadataCache *, Corbomite::Core::VaultResourceProvider *)` | `EmbedRenderer(Markoff::EmbedRegistry *, Markoff::Vault::MetadataCache *, Markoff::Vault::ResourceProvider *)` |
| `EmbedRenderer::setMetadataCache(Corbomite::MetadataCache *)` | `setMetadataCache(Markoff::Vault::MetadataCache *)` |
| `EmbedRenderer::setResources(Corbomite::Core::VaultResourceProvider *)` | `setResources(Markoff::Vault::ResourceProvider *)` |
| `Markoff::Reading::VaultResourceProvider` typedef         | unchanged (still aliases the vault-level type — now `Markoff::Vault::ResourceProvider`) |

Header-path renames:

| Old (stub)                                    | New                                         |
|-----------------------------------------------|---------------------------------------------|
| `<corbomite/core/EmbedRegistry.h>`            | `<markoff/EmbedRegistry.h>`                 |
| `<corbomite/core/CodeBlockProcessorRegistry.h>` | `<markoff/CodeBlockProcessorRegistry.h>`  |
| `<corbomite/core/MarkdownRenderChild.h>`      | `<markoff/MarkdownRenderChild.h>`           |
| `<corbomite/core/EmbedDepthGuard.h>`          | `<markoff/EmbedDepthGuard.h>`               |
| `<corbomite/core/VaultResourceProvider.h>`    | `<markoff/vault/ResourceProvider.h>`        |
| `<corbomite/storage/MetadataCache.h>`         | `<markoff/vault/MetadataCache.h>`           |
| `<corbomite/storage/MetadataParser.h>`        | `<markoff/vault/MetadataParser.h>`          |
| `<corbomite/storage/LinkResolver.h>`          | `<markoff/vault/LinkResolver.h>`            |
| `<corbomite/storage/CachedMetadata.h>`        | `<markoff/vault/CachedMetadata.h>`          |

CorbomiteApp's includes of the `corbomite/` headers **continue to work** — those real headers in `libs/core/` and `libs/storage/` haven't moved. What changes is that markoff-reading's sources now include `<markoff/...>` headers instead of `<corbomite/...>`, and the types in `ReadingView`'s API surface switched.

### 5.2 Corbomite adapter code required

The only structural change Corbomite has to make, beyond renames at call sites:

- `Corbomite::Core::EmbedRegistry`, `CodeBlockProcessorRegistry`, `MarkdownRenderChild`, `EmbedDepthGuard` — each retype to inherit/compose the corresponding `Markoff::*` type (see §4.5).
- `Corbomite::Core::VaultResourceProvider` — subclass `Markoff::Vault::ResourceProvider`. The virtual signatures are identical.
- `Corbomite::MetadataCache`, `LinkResolver`, `MetadataParser` — subclass corresponding `Markoff::Vault::*`.
- `Corbomite::MermaidRenderer` (new, thin, ~20 LOC) — wraps `mmdr_render_svg` in the `Markoff::MermaidRenderer` interface.
- `MainWindow::openVault` / constructor — instantiate the adapter instances, pass pointers to `ReadingView::setVault*` setters and `EmbedRenderer`'s ctor.

Call sites of `ReadingView::codeBlockProcessorRegistry()` (in `MainWindow::registerBuiltinCodeBlockProcessors` and Cluster V Phase 4 TBD) type-rename only: `Corbomite::Core::CodeBlockProcessorRegistry *` → `Markoff::CodeBlockProcessorRegistry *`. No other changes.

### 5.3 Plugin ABI impact

None. Plugins see `Corbomite::Core::*` facades; the facade signatures don't change. Internally those facades now delegate to Markoff, but that's implementation-detail. Confirmed on exhaustive grep 2026-04-20:

```
$ git grep -l '^#include <corbomite/core/EmbedRegistry' src/plugins/
(no matches)
```

Same for the other four types.

## 6. Migration strategy — two-sub-phase landing

Single-commit landing is **not** viable because Markoff standalone can't build against `Corbomite::Core::*` (they don't exist in Markoff), but Corbomite can't build against `Markoff::*` until the adapter layer is in place. Solution: two Markoff tags with Corbomite work in between.

### 6.1 Sub-phase C1a (Markoff v0.3.0-alpha.1) — introduce new types alongside existing stubs

Markoff-side work, landing in order:

1. Add `libs/markoff-core/include/markoff/` headers for the four primitives (`EmbedRegistry`, `CodeBlockProcessorRegistry`, `MarkdownRenderChild`, `EmbedDepthGuard`) plus their value types.
2. Add `libs/markoff-core/include/markoff/vault/` headers for the four abstracts (`ResourceProvider`, `LinkResolver`, `MetadataCache`, `MetadataParser`) plus `CachedMetadata`.
3. Add `libs/markoff-core/include/markoff/MermaidRenderer.h` (abstract).
4. Add `libs/markoff-core/include/markoff/Default*.h` + `src/Default*.cpp` for the no-op defaults.
5. Update `markoff_reading` sources to use `Markoff::*` types everywhere (drop `Corbomite::*` includes). Introduce the new `ReadingView::setVault*` setters and the type-renamed `codeBlockProcessorRegistry()` return.
6. Keep the Phase B CMake option `MARKOFF_READING_USE_REAL_COREDEPS` working **for standalone builds only** — its presence no longer affects link shape (markoff-reading always links `markoff_core` for the new types). Standalone tests get the `Default*` impls; the option becomes a no-op and gets a deprecation notice in the CMakeLists comment.
7. Update `markoff-testapp` to instantiate defaults in `main()`.
8. Full `ctest` green for the standalone build.
9. Tag **`v0.3.0-alpha.1`**.

Corbomite-side work after bumping the submodule to `v0.3.0-alpha.1`:

10. Write the Corbomite-side adapters (`Corbomite::Core::EmbedRegistry` inheriting `Markoff::EmbedRegistry`, etc.). Most are one-liner `class X : public Y { };` with no method bodies since the signatures are identical.
11. Subclass the four `Markoff::Vault::*` abstracts with Corbomite's existing concrete types (`VaultResourceProviderImpl`, `MetadataCache`, `LinkResolver`, `MetadataParserImpl` if one exists; or wrap the static `MetadataParser::parse` in a concrete class).
12. Write the new `Corbomite::MermaidRenderer` concrete that calls `mmdr_render_svg`/`mmdr_free`.
13. In `MainWindow`: construct adapter instances, call the new `ReadingView::setVault*` setters.
14. Type-rename at call sites in `MainWindow`, `HoverPopover`, tests — `Corbomite::Core::CodeBlockProcessorRegistry` → `Markoff::CodeBlockProcessorRegistry`, etc.
15. Build + full ctest + CorbomiteApp smoke.
16. Commit: single Corbomite-side commit bumping the pin to `v0.3.0-alpha.1` and writing the adapters.

### 6.2 Sub-phase C1b (Markoff v0.3.0) — retire the bridge

Markoff-side work after Corbomite ships on C1a:

17. Delete `libs/markoff-reading/stubs/corbomite/` (entire tree).
18. Delete `libs/markoff-reading/stubs/mmdr_ffi.h`.
19. Remove the `MARKOFF_READING_USE_REAL_COREDEPS` option from `libs/markoff-reading/CMakeLists.txt`; remove its `if(... REAL_COREDEPS) ... else() ... endif()` block (both branches retire — the real-deps branch is now the *only* branch but its contents are already implicit: markoff-reading just links `markoff_core`, no host deps).
20. Delete the four `# TODO Phase B:` → `if(MARKOFF_READING_USE_REAL_COREDEPS)` test gates from `tests/CMakeLists.txt`; the gated tests now run unconditionally (they test against the `Markoff::*` types, which markoff-core provides without host help).
21. Full `ctest` green (standalone + from CorbomiteApp).
22. Tag **`v0.3.0`**.

Corbomite-side work after bumping to `v0.3.0`:

23. Delete the top-level `set(MARKOFF_READING_USE_REAL_COREDEPS ON CACHE BOOL "" FORCE)` line.
24. Any other bridge code that survived — grep for `MARKOFF_READING_USE_REAL_COREDEPS` anywhere, delete.
25. Build + full ctest + smoke.
26. Commit: single Corbomite-side commit bumping the pin to `v0.3.0` and pruning the bridge.

## 7. Test strategy

### 7.1 Markoff-side tests added in C1a

- `tst_markoff_embed_registry` — concrete registry dispatch + extension-case-insensitivity, parity with the retiring stub's behavior.
- `tst_markoff_codeblock_registry` — same for `CodeBlockProcessorRegistry`.
- `tst_markoff_embed_depth_guard` — `allow(depth)` threshold + `placeholder`/`placeholderTarget` round-trip.
- `tst_markoff_default_vault_provider` — all `Default*` impls return the expected no-op values.
- `tst_markoff_default_mermaid_renderer` — `renderSvg` returns empty `QByteArray`.

### 7.2 Existing tests that un-gate in C1b

The four `# TODO Phase B:` tests already in `libs/markoff-reading/tests/CMakeLists.txt` drop their conditional gate:

- `tst_sectionlayout_mermaid` — will use the `DefaultMermaidRenderer` in standalone. (Note: the standalone case now *passes* with empty-SVG rendering; the test needs updating to accept empty SVG rather than assert real mermaid output, or the test moves to Corbomite-side where mmdr is injected. Decide during C1b implementation.)
- `tst_readingview_embedrenderer`
- `tst_readingview_mermaid_registered`
- `tst_readingview_embed_builtins`

### 7.3 Corbomite-side tests updated in C1a

Adapter correctness — all existing `tst_embedregistry`, `tst_vaultresourceprovider`, etc. still pass (the adapter-inheriting-Markoff's-interface shouldn't change observable behavior).

One new test: `tst_core_markoff_adapters` — round-trip registering a real Corbomite embed factory against `Corbomite::Core::EmbedRegistry`, dispatching through the `Markoff::EmbedRegistry *` base pointer, and getting the same `MarkdownRenderChild` back. Proves the adapter layer is live.

## 8. Acceptance criteria

**After C1a (v0.3.0-alpha.1 + Corbomite adapter commit):**

- Markoff standalone build: `cmake -B build-dev && cmake --build build-dev && cd build-dev && ctest` — green, zero external projects present.
- CorbomiteApp builds and runs: `cmake --build build -j 10 && ./build/Corbomite` — opens a vault, renders reading mode, resolves embeds, renders mermaid, honors code-block processor registration.
- `git grep 'corbomite/core\|corbomite/storage' libs/markoff-family/libs/markoff-reading/src/` — no matches. (Markoff-reading no longer includes `corbomite/` headers.)
- `git grep 'Corbomite::' libs/markoff-family/libs/markoff-reading/` — matches only in `stubs/corbomite/` (still present) and nowhere else inside markoff-reading.

**After C1b (v0.3.0 + Corbomite pruning commit):**

- Everything from C1a, plus:
- `libs/markoff-family/libs/markoff-reading/stubs/` directory does not exist.
- `git grep 'MARKOFF_READING_USE_REAL_COREDEPS'` in either repo — no matches.
- `git grep 'Corbomite::' libs/markoff-family/` — matches only `libs/markoff-family/docs/` (spec/plan/handoff prose); no code references remain.
- The four Phase-B-gated markoff-reading tests run unconditionally.

## 9. Decisions recorded

1. **Injection mechanism:** setter-style on `ReadingView`, one setter per abstract interface. Rationale: preserves existing call-site shape (`setVaultResourceProvider` pattern is already there). Alternatives considered: (a) a single `Markoff::Context` struct-of-pointers passed to the ctor — rejected because it would force a larger call-site rewrite and close the door on tests that want to swap one dep at a time; (b) construction-only injection — rejected because it prevents the host from updating deps mid-lifetime (e.g., vault switch).

2. **Namespace split:** `Markoff::` for core primitives, `Markoff::Vault::` for vault-level abstractions. Rationale: prevents collision with the leaf-display `Markoff::ResourceProvider`/`LinkResolver` forward-declared in `MarkdownView.h` (C2 turf). C2 does not touch `Markoff::Vault::*`.

3. **Default implementations ship in `markoff-core`:** as concrete classes with `Default` prefix. Rationale: the standalone test app and Markoff-internal tests need them; shipping them from a single library avoids duplication across leaves.

4. **Lazy-init of defaults inside `ReadingView`:** pay zero overhead when host sets the pointer first. Rationale: avoid default instances allocating in the common case.

5. **Two-tag landing (v0.3.0-alpha.1 → v0.3.0):** mandatory because Corbomite adapter code must exist before the stubs can be removed. Rationale: a single-tag landing would either break the standalone Markoff build or break CorbomiteApp at the pin bump.

6. **`MermaidRenderer` is an abstract interface; mmdr stays in Corbomite.** Phase B decision #2 stands. Rationale: Rust toolchain in Markoff is a bigger lift than the interface pattern, and this way third-party consumers can supply any SVG-producing implementation.

7. **`CodeBlockProcessorRegistry` stays internally-owned by ReadingView.** Host uses `ReadingView::codeBlockProcessorRegistry()` accessor to register, same as today. Rationale: minimal surface change; alternative (expose the registry as a setter) has no clear benefit and would break existing Corbomite-side call sites.

8. **`MetadataParser` becomes abstract** (was a static free function `parse()`). Rationale: Markoff's standalone build needs to return something sensible, and a `DefaultMetadataParser` pattern matches the other abstracts. Corbomite wraps its existing `MetadataParser::parse` in a concrete subclass; no behavior change for the host.

9. **Adapter-layer inheritance is in one direction:** Corbomite types inherit Markoff types, not the other way. Rationale: Markoff must never see `Corbomite::` names (invariant #2). Host inheriting library type is the standard shape.

10. **Spec approval latency:** self-approve. Rationale: interfaces are a direct port of existing Corbomite types, with cosmetic renames (`Corbomite::Core::EmbedRegistry` → `Markoff::EmbedRegistry`) and a namespace split (`Corbomite::Core::VaultResourceProvider` → `Markoff::Vault::ResourceProvider`). Signatures of every method on every abstract are 1:1 with the existing implementations. No novel design calls.

## 10. Out-of-scope follow-ups (noted for future work-units)

- **Plugin-facing re-typing.** Corbomite's plugin ABI still surfaces `Corbomite::Core::*`. Whether to surface `Markoff::*` directly to plugins is a plugin-API-stability decision (per memory: "don't expose a library's internals via plugin API until the library is stable standalone"). Revisit after Markoff Phase C closes entirely.
- **`EmbedRenderer` absorption into `ReadingView`.** Currently external and managed by `MainWindow` — could move into `ReadingView` so the host's only job is registering factories against `ReadingView::embedRegistry()`. Good future simplification, out of scope for C1.
- **`MetadataParser` async worker.** C3's scope. `MetadataParser::parse()` is synchronous today; C3's shared-document work will refactor this.
- **Mermaid via registry.** Today mermaid is dispatched directly; C4 unifies it through the code-block processor registry.

## 11. Length / risk

~8–12 new files in `markoff-core` (interfaces + default impls + headers). ~200 lines of change in `markoff-reading`. ~150 lines of adapter code in `libs/core/`. Single-day effort for C1a + single-day for C1b. Low implementation risk — the types are already battle-tested in Corbomite; C1 is a namespace + inheritance restructuring, not new behavior.
