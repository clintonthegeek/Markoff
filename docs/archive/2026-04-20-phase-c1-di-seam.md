# Phase C1 — DI Seam: Execution Plan

> **For a fresh agent:** this plan is self-contained. You can execute it without reading any conversation history. Read §0 (Orientation) first, then §1–§4 in order.
>
> Working repos are `/home/clinton/dev/Markoff/` (Markoff master branch — you commit here directly) and `/home/clinton/dev/Corbomite/` (Corbomite master branch — you commit here too). Both repos allow direct master commits per local convention; don't create feature branches.

**Companion spec:** [`docs/specs/2026-04-20-phase-c1-di-seam.md`](../specs/2026-04-20-phase-c1-di-seam.md). Read at least §1–§5 before starting.

**Status board:** [`docs/phase-c-status.md`](../phase-c-status.md). Update in-place per step.

**Handoff context (if unfamiliar):** [`docs/handoff/2026-04-20-phase-c-ownership-handoff.md`](../handoff/2026-04-20-phase-c-ownership-handoff.md) + Corbomite's `docs/CONTRIBUTING-OPS.md` Ritual 5.

---

## 0. Orientation

### What C1 does

Retires the Phase B bridge (`MARKOFF_READING_USE_REAL_COREDEPS` CMake option + `libs/markoff-reading/stubs/corbomite/` shim tree). Replaces Corbomite-named stub types (`Corbomite::Core::EmbedRegistry`, `Corbomite::MetadataCache`, etc. — ten of them) with Markoff-owned interfaces injected at runtime. Corbomite writes adapter subclasses so its existing concrete types implement the new Markoff interfaces.

### Why two Markoff tags (`v0.3.0-alpha.1` → `v0.3.0`)

Single-tag landing is not viable: the Markoff-side changes alone break CorbomiteApp, and removing the stubs before Corbomite has adapters breaks Markoff-reading's build. The plan is:

1. **Sub-phase C1a** — Markoff introduces the new types *alongside* the Phase B option (dual-mode), tag `v0.3.0-alpha.1`.
2. **Corbomite-side adapter commit** — bumps the pin to `v0.3.0-alpha.1`, writes adapters, switches CorbomiteApp onto the new Markoff types.
3. **Sub-phase C1b** — Markoff retires the Phase B option + stubs (now unused), tag `v0.3.0`.
4. **Corbomite-side cleanup commit** — bumps pin to `v0.3.0`, drops the top-level `set(MARKOFF_READING_USE_REAL_COREDEPS ON ...)` line.

### Invariants (every Markoff commit must preserve)

1. Standalone Markoff build + `ctest` green: `cd /home/clinton/dev/Markoff && rm -rf build-dev && cmake -S . -B build-dev && cmake --build build-dev -j 10 && cd build-dev && ctest --output-on-failure`.
2. No `Corbomite`-named types in Markoff public interfaces. Phase B stubs under `libs/markoff-reading/stubs/corbomite/` are the one exception — retired in C1b.
3. Tests needing Corbomite concretes gate appropriately.
4. Every Markoff tag is append-only. Never force-move.
5. Markoff `master` append-only. No force-push.
6. Unified commit identity; `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>` trailer.

### Build commands (cheat sheet)

**Markoff standalone:**
```bash
cd /home/clinton/dev/Markoff
rm -rf build-dev     # first time only; thereafter incremental
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 10
cd build-dev && ctest --output-on-failure -j 10
```

**Corbomite (always pass `-DCORBOMITE_DEV_BUILD=ON` — memory rule):**
```bash
cd /home/clinton/dev/Corbomite
cmake -B build -DCORBOMITE_DEV_BUILD=ON
cmake --build build -j 10
cd build && ctest --output-on-failure -j 10
./Corbomite   # smoke
```

### Submodule pin-bump protocol (Corbomite side)

The submodule is at `/home/clinton/dev/Corbomite/libs/markoff-family` and its `origin` remote points at `/home/clinton/dev/Markoff/`. To advance the pin:

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git fetch --tags        # pulls new Markoff tags
git checkout v0.3.0-alpha.1   # or whatever target tag
cd /home/clinton/dev/Corbomite
git add libs/markoff-family
```

**Pre-flight audit rule** (per `feedback_submodule_pin_audit` memory): before committing a pin bump, always diff both directions:

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git rev-list <current-sha>..<new-sha>   # new commits we'll gain
git rev-list <new-sha>..<current-sha>   # local commits we'll LOSE
```

If the second list is non-empty, stop — those are commits stranded on the submodule's local `master`. Address before bumping. (This caught three stranded editor commits during Phase B; don't repeat the mistake.)

### Header template (markoff-core convention)

All new Markoff-core headers use this template. Copy when creating each file.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

// <includes>

namespace Markoff {                  // or Markoff::Vault
// ... declarations ...
}  // namespace Markoff
```

`#pragma once` is used throughout `markoff-core/include/markoff/` — match existing style rather than `#ifndef` guards.

### Commit message format

Markoff-side, no cluster footer. One-line subject, body explaining scope + why. Trailer:
```
Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

Corbomite-side, use `feat(markoff): …` subject for Phase C adaptation commits.

---

## 1. Sub-phase C1a — Markoff introduces new types (dual-mode landing)

Goal: land new `Markoff::*` and `Markoff::Vault::*` types in markoff-core, update markoff-reading to use them, preserve Phase B option as a no-op-for-link-shape so standalone build stays green. End-state: Markoff `v0.3.0-alpha.1` tag; nothing deleted yet.

### Task 1 — Add `markoff-core` primitive headers + value types

**Files to create** in `/home/clinton/dev/Markoff/libs/markoff-core/include/markoff/`:

- `EmbedRegistry.h` — ports the **stub shape** from `libs/markoff-reading/stubs/corbomite/core/EmbedRegistry.h`. Rename namespace `Corbomite::Core` → `Markoff`. Forward-declare `Markoff::MarkdownRenderChild` and `Markoff::Vault::ResourceProvider` (the `EmbedRequest` struct has a `VaultResourceProvider *resources` field → rename to `Markoff::Vault::ResourceProvider *resources`).
- `CodeBlockProcessorRegistry.h` — port stub shape, rename namespace.
- `MarkdownRenderChild.h` — port stub shape, rename namespace. Do NOT inherit `Corbomite::Component` (this is the adapter's job on the Corbomite side). Stay a simple concrete base class.
- `EmbedDepthGuard.h` — port stub shape, rename namespace.
- `MermaidRenderer.h` — **new** abstract interface, not in the stub tree. Signature:
  ```cpp
  namespace Markoff {
  class MermaidRenderer {
  public:
      virtual ~MermaidRenderer() = default;
      /// Render a Mermaid source string to SVG bytes. Returns empty on failure.
      virtual QByteArray renderSvg(const QString &source) const = 0;
  };
  }
  ```

**Files to create** in `/home/clinton/dev/Markoff/libs/markoff-core/src/`:

- `MarkdownRenderChild.cpp` — ctor/dtor defaulted, `setRenderedText` + `renderedText` trivial, `mountInto(QWidget *)` default no-op (real behavior comes from subclass on host side).

**CMakeLists edit:** `libs/markoff-core/CMakeLists.txt` — add the new header filenames to the `add_library(markoff_core ...)` source list. (The existing SearchController/SearchBar/MarkdownView files are the template.)

**Verify:**
```bash
cd /home/clinton/dev/Markoff
cmake --build build-dev -j 10 --target markoff_core
```
Expect clean compile; the new types are unreferenced by markoff-reading yet, so nothing else has to compile.

**Commit:**
```
markoff-core: port EmbedRegistry / CodeBlockProcessorRegistry / MarkdownRenderChild / EmbedDepthGuard + add MermaidRenderer

Phase C1a — introduce Markoff-owned rendering primitives that will
replace the Corbomite-named stubs in libs/markoff-reading/stubs/corbomite/core/.
Signatures match the existing stubs exactly (the stubs already expose
the minimum surface markoff-reading needs); only namespace changed.
MarkdownRenderChild is a simple concrete base; Corbomite's adapter
subclass will re-add the Component lifecycle.

New: Markoff::MermaidRenderer abstract. Previously the mmdr C FFI was
called directly from MermaidRenderer.cpp; the abstract interface moves
that decision to the host. Rust crate stays in Corbomite per Phase B
decision #2.

Per docs/specs/2026-04-20-phase-c1-di-seam.md §4.1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

### Task 2 — Add `markoff-core/include/markoff/vault/` abstracts + value types

**Files to create** in `/home/clinton/dev/Markoff/libs/markoff-core/include/markoff/vault/`:

- `CachedMetadata.h` — ports value-type definitions from `libs/markoff-reading/stubs/corbomite/storage/CachedMetadata.h`. Types: `SourcePosition`, `SourceRange`, `HeadingCache`, `BlockCache`, `SectionCache`, `CachedMetadata`. Put all in `namespace Markoff::Vault`.
- `ResourceProvider.h` — ports `VaultResourceProvider` abstract shape from the stub at `libs/markoff-reading/stubs/corbomite/core/VaultResourceProvider.h`. Class name: `Markoff::Vault::ResourceProvider`. All methods pure-virtual (`= 0`), matching the real Corbomite version's shape (the stub has default-empty impls — use pure-virtual instead; hosts are expected to provide, and `DefaultResourceProvider` in Task 4 supplies empty-return defaults for standalone).
- `LinkResolver.h` — ports `LinkResolver` abstract from `libs/markoff-reading/stubs/corbomite/storage/LinkResolver.h`. Class name: `Markoff::Vault::LinkResolver`. One method: `virtual QString resolve(const QString &linkText, const QString &fromPath) const = 0;`.
- `MetadataCache.h` — ports `MetadataCache` abstract from `libs/markoff-reading/stubs/corbomite/storage/MetadataCache.h`. Class name: `Markoff::Vault::MetadataCache`. One method: `virtual const CachedMetadata *getFileCache(const QString &path) const = 0;`.
- `MetadataParser.h` — port `MetadataParseResult` struct and **change the `MetadataParser` static-parse function into an abstract class** (this is a shape change from the stub):
  ```cpp
  namespace Markoff::Vault {
  struct MetadataParseResult { CachedMetadata cache; };
  class MetadataParser {
  public:
      virtual ~MetadataParser() = default;
      virtual MetadataParseResult parse(const QByteArray &content,
                                        const QString &path,
                                        const LinkResolver &resolver) const = 0;
  };
  }
  ```

**CMakeLists edit:** add the five new headers to `markoff_core`'s source list.

**Verify:** `cmake --build build-dev -j 10 --target markoff_core` — clean compile.

**Commit:**
```
markoff-core: add Markoff::Vault:: abstracts (ResourceProvider / LinkResolver / MetadataCache / MetadataParser) + CachedMetadata value types

Phase C1a — vault-level abstractions under Markoff::Vault:: namespace.
Replaces the Corbomite-named stubs at libs/markoff-reading/stubs/corbomite/{core/VaultResourceProvider.h,storage/*.h}.

Sub-namespace prevents collision with leaf-display Markoff::ResourceProvider /
LinkResolver (forward-declared in MarkdownView.h, consolidated in C2).

MetadataParser becomes abstract (was a static free function in the stub) so
standalone + host can both supply sensible behavior via DefaultMetadataParser
(Task 4) or a Corbomite adapter.

Per docs/specs/2026-04-20-phase-c1-di-seam.md §4.1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

### Task 3 — Add default (no-op) implementations

**Files to create** in `/home/clinton/dev/Markoff/libs/markoff-core/include/markoff/` (top-level, not vault/):

- `DefaultMermaidRenderer.h` — `class DefaultMermaidRenderer : public MermaidRenderer { QByteArray renderSvg(const QString &) const override { return {}; } };`. Header-only; no .cpp needed.

**Files to create** in `/home/clinton/dev/Markoff/libs/markoff-core/include/markoff/vault/`:

- `DefaultResourceProvider.h` — `class DefaultResourceProvider : public ResourceProvider { … }`. Every method returns the empty value (`QUrl{}`, `QByteArray{}`, `std::nullopt`, `false`). Header-only.
- `DefaultLinkResolver.h` — `resolve() → QString{}`.
- `DefaultMetadataCache.h` — `getFileCache() → nullptr`.
- `DefaultMetadataParser.h` — `parse() → MetadataParseResult{}` (empty CachedMetadata).

**CMakeLists:** add the five new default-impl headers to `markoff_core`'s source list.

**Verify:** `cmake --build build-dev -j 10 --target markoff_core`.

**Commit:**
```
markoff-core: add Default* no-op implementations for standalone builds

Phase C1a — ships no-op concretes for each abstract interface introduced
in Tasks 1-2: DefaultMermaidRenderer, DefaultResourceProvider,
DefaultLinkResolver, DefaultMetadataCache, DefaultMetadataParser.

Same behavior as the retiring stub types (return empty / nullptr /
std::nullopt / empty struct). Header-only. ReadingView uses these as
lazy-constructed fallbacks when the host has not injected a real
implementation.

Per docs/specs/2026-04-20-phase-c1-di-seam.md §4.2-4.4.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

### Task 4 — Add markoff-core tests

**Files to create** in `/home/clinton/dev/Markoff/libs/markoff-core/tests/`:

- `tst_markoff_embed_registry.cpp` — register a factory under "png"; dispatch a matching + non-matching EmbedRequest; assert factory called once; assert case-insensitivity.
- `tst_markoff_codeblock_registry.cpp` — `registerLanguage` + `dispatch` + `hasLanguage`, analogous.
- `tst_markoff_embed_depth_guard.cpp` — `allow(0..5)` results; `placeholder`/`placeholderTarget` round-trip.
- `tst_markoff_default_vault_provider.cpp` — instantiate each `Default*` class and verify the stub-equivalent behavior.

Link each against `Qt6::Test` + `markoff_core`. Add to `libs/markoff-core/tests/CMakeLists.txt` (look at existing tests like `tst_markoff_document.cpp` for the pattern).

**Verify:**
```bash
cd /home/clinton/dev/Markoff/build-dev && ctest -R 'tst_markoff_(embed|codeblock|default)' --output-on-failure
```
All four new tests pass.

**Commit:**
```
markoff-core: tests for new primitive + default-impl types

tst_markoff_embed_registry - registerExtension + dispatch
  + case-insensitivity.
tst_markoff_codeblock_registry - registerLanguage / dispatch /
  hasLanguage.
tst_markoff_embed_depth_guard - threshold + placeholder round-trip.
tst_markoff_default_vault_provider - each Default* returns the
  stub-equivalent empty value.

Per docs/specs/2026-04-20-phase-c1-di-seam.md §7.1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

### Task 5 — Flip markoff-reading to use the new Markoff types

**Files to edit** in `/home/clinton/dev/Markoff/libs/markoff-reading/`:

1. **`include/markoff/reading/VaultResourceProvider.h`** — the forwarding typedef. Change the include and the `using` target:
   ```cpp
   // was:  #include "corbomite/core/VaultResourceProvider.h"
   //       using VaultResourceProvider = Corbomite::Core::VaultResourceProvider;
   #include <markoff/vault/ResourceProvider.h>
   namespace Markoff::Reading {
   using VaultResourceProvider = Markoff::Vault::ResourceProvider;
   }
   ```

2. **`include/markoff/reading/ReadingView.h`:**
   - Replace `#include "corbomite/core/CodeBlockProcessorRegistry.h"` with `#include <markoff/CodeBlockProcessorRegistry.h>`.
   - Change `Corbomite::Core::CodeBlockProcessorRegistry` → `Markoff::CodeBlockProcessorRegistry` on lines 92-94, 186-187.
   - Add new setters (per spec §4.3):
     ```cpp
     void setEmbedRegistry(Markoff::EmbedRegistry *registry);
     void setVaultLinkResolver(Markoff::Vault::LinkResolver *resolver);
     void setVaultMetadataCache(Markoff::Vault::MetadataCache *cache);
     void setVaultMetadataParser(Markoff::Vault::MetadataParser *parser);
     void setMermaidRenderer(Markoff::MermaidRenderer *renderer);
     Markoff::EmbedDepthGuard *embedDepthGuard();
     ```
   - Keep existing `setVaultResourceProvider` / `vaultResourceProvider` / `codeBlockProcessorRegistry` with retyped parameters.
   - Add private members: `m_embedRegistry`, `m_vaultLinkResolver`, `m_vaultMetadataCache`, `m_vaultMetadataParser`, `m_mermaidRenderer`, plus `std::unique_ptr<Markoff::Vault::Default*>`s for each lazy default.

3. **`src/ReadingView.cpp`** — implement the new setters (trivial pointer stores). Implement the lazy-default accessor pattern for each abstract:
   ```cpp
   Markoff::Vault::ResourceProvider *ReadingView::resources() {
       if (m_vaultProvider) return m_vaultProvider;
       if (!m_defaultVaultProvider) {
           m_defaultVaultProvider = std::make_unique<Markoff::Vault::DefaultResourceProvider>();
       }
       return m_defaultVaultProvider.get();
   }
   ```
   Do this for all five abstracts (ResourceProvider, LinkResolver, MetadataCache, MetadataParser, MermaidRenderer). Route existing internal usages through these accessors rather than through the raw pointer fields.

4. **`include/markoff/reading/EmbedRenderer.h`** and **`src/EmbedRenderer.cpp`:**
   - Replace all `Corbomite::Core::` and `Corbomite::` prefixes on EmbedRegistry, EmbedRequest, MarkdownRenderChild, EmbedDepthGuard, VaultResourceProvider, MetadataCache, LinkResolver, HeadingCache, BlockCache, SectionCache, CachedMetadata with `Markoff::` or `Markoff::Vault::`.
   - Replace `#include "corbomite/..."` lines with `#include <markoff/...>` and `#include <markoff/vault/...>` equivalents.
   - `registerBuiltinEmbedFactories` — type-rename parameters but leave the body alone.

5. **`src/MermaidRenderer.cpp`** (note: this file is a helper, not a class; it wraps `mmdr_render_svg`) — decide:
   - Keep `MermaidRenderer.cpp` as a thin internal class that *calls* the new `Markoff::MermaidRenderer` interface instead of calling mmdr directly. Swap `#include "mmdr_ffi.h"` for a `Markoff::MermaidRenderer *` pulled from the pipeline context.
   - Or: rename `MermaidRenderer` class → `MermaidRendererClient` (or similar) to avoid name collision with the new `Markoff::MermaidRenderer` interface, and have it take the interface pointer as a ctor arg.

   Pick whichever yields the smaller diff. Grep for all callers first.

6. **`src/SectionLayout.cpp`**, **`src/ReadingParseWorker.cpp`**, **`src/SectionRenderer.cpp`** (and any other .cpp in `libs/markoff-reading/src/`) — mechanical sed pass:
   ```bash
   cd /home/clinton/dev/Markoff/libs/markoff-reading/src
   sed -i 's|corbomite/core/|markoff/|g; s|corbomite/storage/|markoff/vault/|g' *.cpp *.h
   sed -i 's|Corbomite::Core::|Markoff::|g; s|Corbomite::|Markoff::Vault::|g' *.cpp *.h
   ```
   WARNING: the `Corbomite::` → `Markoff::Vault::` rule is over-broad for anything not in `core/`. Inspect the diff. The actual affected types are: `MetadataCache`, `LinkResolver`, `MetadataParser`, `CachedMetadata`, `HeadingCache`, `BlockCache`, `SectionCache`, `SourcePosition`, `SourceRange`, `MetadataParseResult`. `EmbedRegistry`/`EmbedRequest`/`EmbedFactory`/`MarkdownRenderChild`/`EmbedDepthGuard` go to `Markoff::` (no Vault sub-namespace).

   Safer approach: do the Core-prefixed rename first, then hand-edit storage-prefixed references one type at a time.

**Verify** (standalone build, stubs still present but unused by markoff-reading sources):
```bash
cd /home/clinton/dev/Markoff
cmake --build build-dev -j 10
cd build-dev && ctest --output-on-failure
```
All non-gated tests pass. The four `# TODO Phase B:` gated tests stay skipped in this state (they test the stub-path; Task 8 will update them).

**Commit:**
```
markoff-reading: replace Corbomite::*-stubbed types with Markoff::* / Markoff::Vault::*

Phase C1a - flip all markoff-reading sources + public headers to use
the Markoff-owned types introduced in Tasks 1-3. Drops every
<corbomite/...> include from src/ and include/; types rename to
Markoff:: (EmbedRegistry, CodeBlockProcessorRegistry,
MarkdownRenderChild, EmbedDepthGuard, MermaidRenderer) or
Markoff::Vault:: (ResourceProvider, LinkResolver, MetadataCache,
MetadataParser, CachedMetadata, HeadingCache, BlockCache, SectionCache).

ReadingView gains the new setters from spec §4.3 and the lazy-default
fallback pattern from spec §4.4 (each abstract has a std::unique_ptr<Default*>
backing store that allocates on first use when the host has not
injected a real implementation).

MermaidRenderer.cpp retargeted to call the new Markoff::MermaidRenderer
interface; mmdr_ffi.h is no longer included from markoff-reading.

Stubs directory stays in place this commit; Task 8 + Task 10 (C1b)
retire it. Phase B's MARKOFF_READING_USE_REAL_COREDEPS option is now
a no-op for link shape but kept so standalone + host can both build
during the transition.

Per docs/specs/2026-04-20-phase-c1-di-seam.md §5.1 and §6.1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

### Task 6 — Update `markoff-testapp`

**File to edit:** `/home/clinton/dev/Markoff/tests/markoff/main.cpp` (or wherever `markoff-testapp`'s main is; grep for `markoff-testapp` in the top-level CMakeLists if unclear).

If the testapp instantiates `ReadingView` and doesn't set injectables, no change needed — ReadingView's lazy defaults cover standalone. If it manually wires defaults (unlikely), swap to the new `Markoff::Vault::Default*` types.

**Verify:**
```bash
cd /home/clinton/dev/Markoff
cmake --build build-dev -j 10 --target markoff-testapp
QT_QPA_PLATFORM=offscreen ./build-dev/bin/markoff-testapp --help   # sanity check it starts
```

**Commit if changes needed:** `markoff-testapp: wire new defaults for standalone DI seam` (only if Task 6 produced a diff).

### Task 7 — Standalone green

```bash
cd /home/clinton/dev/Markoff
rm -rf build-dev
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 10
cd build-dev && ctest --output-on-failure -j 10
```

Expected: 100% pass (minus the four Phase-B-gated tests, which remain skipped — they still exist in the stubs-on path). No compile warnings about unused variables from the stub types if you did Task 5 correctly.

If any failure is non-obvious, triage. Don't proceed to Task 8 with red tests.

### Task 8 — Update Phase-B-gated tests to un-gate *part*-way (Markoff side)

The four tests currently gated on `MARKOFF_READING_USE_REAL_COREDEPS`:
- `tst_sectionlayout_mermaid`
- `tst_readingview_embedrenderer`
- `tst_readingview_mermaid_registered`
- `tst_readingview_embed_builtins`

In C1a, these should run *under the standalone build too* because the Markoff types and default impls now exist. But some of them may assert against real mmdr output or real Corbomite::Storage metadata — those assertions need updating to accept the defaults' empty-results behavior, OR the test gets split into a standalone variant + a host-side variant that only runs from CorbomiteApp.

**For each test, grep for explicit asserts that assume real impls:**
```bash
cd /home/clinton/dev/Markoff/libs/markoff-reading/tests
grep -l 'QVERIFY\|QCOMPARE' tst_sectionlayout_mermaid.cpp tst_readingview_embedrenderer.cpp tst_readingview_mermaid_registered.cpp tst_readingview_embed_builtins.cpp
```

For each, update to work with defaults (accept empty SVG, accept `nullptr` from cache), OR if the assertion is specifically about the host integration, leave the test gated and file a Corbomite-side test equivalent in Task 13.

**Edit `libs/markoff-reading/tests/CMakeLists.txt`** — change the C1b-style gate to a pragmatic mix: tests that pass with defaults move outside the `if()`; tests that require real mmdr/metadata stay inside.

**Open sub-question** (per PROJECT-STATE): `tst_sectionlayout_mermaid` specifically — does standalone-with-empty-SVG behavior match what the test was asserting? If yes, un-gate; if no, keep gated and rely on a Corbomite-side test to cover real mmdr. Decide based on the grep above.

**Verify:** standalone ctest green including the un-gated tests.

**Commit:**
```
markoff-reading/tests: un-gate tests that pass against Default* implementations

Phase C1a - the four # TODO Phase B tests gated on
MARKOFF_READING_USE_REAL_COREDEPS can now run under the standalone
build because Markoff ships Default* no-op implementations. Tests
that asserted against real mmdr output or real Corbomite::Storage
metadata are adjusted to accept the empty defaults (or kept gated if
the real-integration case is better tested from the host side).

Per docs/specs/2026-04-20-phase-c1-di-seam.md §7.2.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

### Task 9 — Tag `v0.3.0-alpha.1`

```bash
cd /home/clinton/dev/Markoff
git log --oneline -10   # confirm the Task 1-8 commits are on master and in order
git tag -a v0.3.0-alpha.1 -m "Phase C1a: Markoff DI seam types introduced; Phase B option kept for transition"
git log --oneline --decorate -5
```

**Update** `docs/phase-c-status.md`:
- Work-unit table, C1 row: Status → `markoff ready` (alpha); Markoff PR/branch → `master`; Tag → `v0.3.0-alpha.1`.
- Activity log: append an entry for the C1a landing.

```bash
git add docs/phase-c-status.md
git commit -m "phase-c-status: C1a landed at v0.3.0-alpha.1"
```

No new tag for the status update — the Markoff convention is one tag per work-unit milestone, not per doc edit.

---

## 2. Corbomite-side adapter commit (between C1a and C1b)

Goal: bump submodule pin to `v0.3.0-alpha.1`, write adapters so Corbomite's existing `Corbomite::Core::*` / `Corbomite::*` types implement the new `Markoff::*` / `Markoff::Vault::*` interfaces. Single commit on Corbomite master.

### Task 10 — Pre-flight submodule audit + pin bump

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git fetch --tags
current=$(git rev-parse HEAD)
target=$(git rev-parse v0.3.0-alpha.1)
echo "Current: $current"
echo "Target:  $target"
echo "--- will gain ---"
git rev-list $current..$target
echo "--- will lose (must be empty) ---"
git rev-list $target..$current
```

If "will lose" is non-empty, stop and cherry-pick those onto Markoff master first. Otherwise:

```bash
git checkout v0.3.0-alpha.1
cd /home/clinton/dev/Corbomite
git add libs/markoff-family
# don't commit yet - the adapter changes go into the same commit
```

### Task 11 — Retype `Corbomite::Core::*` to inherit `Markoff::*`

**Files to edit** in `/home/clinton/dev/Corbomite/libs/core/include/corbomite/core/`:

- `EmbedRegistry.h` — change `class EmbedRegistry` → `class EmbedRegistry : public Markoff::EmbedRegistry`. Add `#include <markoff/EmbedRegistry.h>`. The `Handle` struct + `unregister` method stay (richer API than Markoff base; that's fine). `registerExtension(const QString &, EmbedFactory)` — Corbomite's version returns `Handle`; Markoff's base returns `void`. Use a different method name for the overriding method to avoid hiding: introduce a `void registerExtensionSimple(const QString &, EmbedFactory)` as the `override` and have Corbomite's existing `Handle registerExtension(...)` call it internally. Alternative: rename the base virtual to `registerExtensionSimple` in Markoff. Decide during implementation — whichever keeps existing Corbomite callers working without renames.
- `CodeBlockProcessorRegistry.h` — similar inheritance move.
- `MarkdownRenderChild.h` — change `class MarkdownRenderChild : public Corbomite::Component` → `class MarkdownRenderChild : public Markoff::MarkdownRenderChild, public Corbomite::Component`. Multiple inheritance; both bases are lightweight.
- `EmbedDepthGuard.h` — change to inherit `Markoff::EmbedDepthGuard`. The Corbomite type may not need any additions; if the inherited behavior is identical, the class body shrinks to `class EmbedDepthGuard : public Markoff::EmbedDepthGuard {};` (type-alias would also work but subclass lets Corbomite add methods later).
- `VaultResourceProvider.h` — change `class VaultResourceProvider` → `class VaultResourceProvider : public Markoff::Vault::ResourceProvider`. All existing methods become `override`.

**Files to edit** in `/home/clinton/dev/Corbomite/libs/storage/include/corbomite/storage/`:

- `MetadataCache.h` — change `class MetadataCache` → `class MetadataCache : public Markoff::Vault::MetadataCache`. The real Corbomite class has many methods beyond `getFileCache`; those stay, that single method becomes `override`.
- `LinkResolver.h` — change `class LinkResolver` → `class LinkResolver : public Markoff::Vault::LinkResolver`. `resolve` becomes `override`.
- `MetadataParser.h` — if Corbomite has a static `parse`, introduce a new `class MetadataParserImpl : public Markoff::Vault::MetadataParser` whose `parse()` override delegates to the static function. Keep the static function for existing Corbomite callers.
- `CachedMetadata.h` — decide: either (a) Corbomite's `CachedMetadata` etc. becomes a type alias `using CachedMetadata = Markoff::Vault::CachedMetadata;` (if shapes are identical) or (b) Corbomite keeps its own struct and provides a converter. Pick (a) if the field list matches; the spec's §4.1 table says it does.

**Libs/core CMakeLists:** add `target_link_libraries(corbomite-core PUBLIC $<BUILD_INTERFACE:markoff_core>)` if not already present (already is, because `<markoff-parser/Document.h>` is also from the submodule).

### Task 12 — Add `Corbomite::MermaidRenderer` concrete

**New files** in `/home/clinton/dev/Corbomite/libs/mmdr/` (or `libs/core/`, implementer's choice — mmdr has the Rust dep but core is where the rest of the adapters live):

- `MermaidRenderer.h` — `class MermaidRenderer : public Markoff::MermaidRenderer { QByteArray renderSvg(const QString &) const override; };`
- `MermaidRenderer.cpp` — implementation calls `mmdr_render_svg` + `mmdr_free`. ~20 LOC.

CMakeLists edit: add to the library target.

### Task 13 — Wire adapters in `MainWindow`

**File to edit:** `/home/clinton/dev/Corbomite/src/app/MainWindow.cpp`.

Find where ReadingView is constructed / obtained (grep for `ReadingView`, usually in `openVault` or the constructor). After construction:

```cpp
m_readingView->setEmbedRegistry(m_embedRegistry.get());  // adapter now IS-A Markoff::EmbedRegistry
m_readingView->setVaultResourceProvider(m_vaultResources.get());  // already in place
m_readingView->setVaultLinkResolver(m_linkResolver.get());
m_readingView->setVaultMetadataCache(m_metadataCache.get());
m_readingView->setVaultMetadataParser(m_metadataParser.get());  // may need to construct MetadataParserImpl
m_readingView->setMermaidRenderer(m_mermaidRenderer.get());  // new; construct in MainWindow ctor or openVault
```

If `MainWindow` already holds unique_ptrs to these, just change the member types to use `Corbomite::`-adapter types. If it holds raw handles from a registrar, adapt accordingly.

**File to edit:** `/home/clinton/dev/Corbomite/src/editor/HoverPopover.cpp` (and potentially other ReadingView users). Any direct `EmbedRenderer(registry, cache, resources)` call — verify the parameters are the adapter types (or `Markoff::*` base pointers). Type rename if needed.

### Task 14 — Type-rename at remaining call sites

```bash
cd /home/clinton/dev/Corbomite
grep -rn 'Corbomite::Core::CodeBlockProcessorRegistry' src/ tests/ 2>/dev/null
```

For each hit, leave it alone if the variable is typed as `Corbomite::Core::CodeBlockProcessorRegistry *` (adapter type). If the variable is typed to call through the Markoff base, retype to `Markoff::CodeBlockProcessorRegistry *`. Use this judgement per site — most should stay Corbomite-typed since plugins see the Corbomite facade.

The one guaranteed change: places where a `ReadingView` accessor is called that now returns a `Markoff::*` pointer:
```cpp
// was: Corbomite::Core::CodeBlockProcessorRegistry *reg = readingView->codeBlockProcessorRegistry();
// now:          Markoff::CodeBlockProcessorRegistry *reg = readingView->codeBlockProcessorRegistry();
```

Same for any other accessor whose return type changed per spec §5.1.

### Task 15 — Build + test + smoke

```bash
cd /home/clinton/dev/Corbomite
cmake --build build -j 10
```

Expected: clean compile. If you get errors about stub path resolution (`corbomite/core/...` not found), that shouldn't happen — the real headers are in `libs/core/include/corbomite/core/` and `libs/storage/include/corbomite/storage/`, not in the submodule's stubs. Inspect.

```bash
cd build && ctest --output-on-failure -j 10
cd .. && ./build/Corbomite   # smoke: open a vault, switch to Reading mode, verify an embed renders
```

Expected: 241/243 pre-existing flakes stay flakes; no new failures.

### Task 16 — Commit the Corbomite-side adapter work

```bash
cd /home/clinton/dev/Corbomite
git add libs/markoff-family libs/core libs/storage libs/mmdr src/ tests/
git commit -m "feat(markoff): Phase C1a adaptation - Corbomite types inherit Markoff interfaces

Bumps libs/markoff-family to Markoff v0.3.0-alpha.1 and retypes
Corbomite's Core/Storage primitives to inherit the new
Markoff::* / Markoff::Vault:: interfaces.

Adapter layer (thin; shapes are 1:1 with existing types):
- Corbomite::Core::EmbedRegistry : Markoff::EmbedRegistry
- Corbomite::Core::CodeBlockProcessorRegistry : Markoff::CodeBlockProcessorRegistry
- Corbomite::Core::MarkdownRenderChild : Markoff::MarkdownRenderChild, Corbomite::Component
- Corbomite::Core::EmbedDepthGuard : Markoff::EmbedDepthGuard
- Corbomite::Core::VaultResourceProvider : Markoff::Vault::ResourceProvider
- Corbomite::MetadataCache : Markoff::Vault::MetadataCache
- Corbomite::LinkResolver : Markoff::Vault::LinkResolver
- New Corbomite::MetadataParserImpl : Markoff::Vault::MetadataParser
  (wraps existing static Corbomite::MetadataParser::parse)
- New Corbomite::MermaidRenderer : Markoff::MermaidRenderer
  (wraps mmdr_render_svg / mmdr_free)

MainWindow wires each adapter into ReadingView via the new
setVault*/setEmbedRegistry/setMermaidRenderer setters; existing
plugin-facing facades (EmbedRegistrar, CodeBlockProcessorRegistrar)
unchanged.

Phase B's MARKOFF_READING_USE_REAL_COREDEPS=ON still set in top-level
CMakeLists; retires in C1b (next pin bump).

Per docs/specs/2026-04-20-phase-c1-di-seam.md §4.5 and §6.1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
"
```

**Update Corbomite PROJECT-STATE:**
- §Markoff Phase C table: C1 status → `corbomite adapting`.
- In-flight row: update last-completed-step.

```bash
git add docs/PROJECT-STATE.md
git commit -m "docs(project-state): Markoff C1a adapter shipped on Corbomite side"
```

---

## 3. Sub-phase C1b — Markoff retires the bridge

Goal: delete `stubs/corbomite/` and `MARKOFF_READING_USE_REAL_COREDEPS`; tag `v0.3.0`.

### Task 17 — Delete the stubs tree

```bash
cd /home/clinton/dev/Markoff
git rm -r libs/markoff-reading/stubs/corbomite
git rm libs/markoff-reading/stubs/mmdr_ffi.h
rmdir libs/markoff-reading/stubs   # if empty
```

### Task 18 — Retire `MARKOFF_READING_USE_REAL_COREDEPS`

**File to edit:** `/home/clinton/dev/Markoff/libs/markoff-reading/CMakeLists.txt`.

Delete the `option(MARKOFF_READING_USE_REAL_COREDEPS ...)` line and the `if(MARKOFF_READING_USE_REAL_COREDEPS)` / `else()` / `endif()` block. The real branch currently `target_link_libraries`-es Corbomite::Core + Corbomite::Storage + mmdr; none of those links are needed now (markoff-reading doesn't include corbomite/ headers anymore). The else branch adds the stubs dir to includes; the stubs dir just got deleted.

End state: a single unconditional block linking `markoff_core` (plus Qt + KF6 + MarkoffParser + jkqtmathtext as before).

### Task 19 — Un-gate any remaining Phase-B-gated tests (full pass)

Any `if(MARKOFF_READING_USE_REAL_COREDEPS)` in `libs/markoff-reading/tests/CMakeLists.txt` — drop the condition. The tests now always run; they pass against Default* impls or against CorbomiteApp-injected concretes depending on how they were written in Task 8.

### Task 20 — Verify standalone

```bash
cd /home/clinton/dev/Markoff
rm -rf build-dev
cmake -S . -B build-dev
cmake --build build-dev -j 10
cd build-dev && ctest --output-on-failure -j 10
```

Expected: 100% pass. No more mentions of `MARKOFF_READING_USE_REAL_COREDEPS` anywhere:
```bash
git grep 'MARKOFF_READING_USE_REAL_COREDEPS' -- . ':!docs'
```
Should return no results (except potentially historical doc mentions under `docs/` which are fine).

### Task 21 — Commit + tag

```bash
git add libs/markoff-reading
git commit -m "markoff-reading: retire Phase B bridge (stubs + MARKOFF_READING_USE_REAL_COREDEPS option)

Phase C1b - Corbomite now ships the adapter layer (v0.3.0-alpha.1
merged at Corbomite commit <SHA>), so markoff-reading no longer needs
the stub shims or the dual-mode CMake option.

- Deletes libs/markoff-reading/stubs/corbomite/ (8 header files).
- Deletes libs/markoff-reading/stubs/mmdr_ffi.h.
- Removes the MARKOFF_READING_USE_REAL_COREDEPS option + its
  conditional block in libs/markoff-reading/CMakeLists.txt.
- Un-gates any remaining Phase-B-gated tests.

Standalone Markoff builds against markoff_core + Markoff::Vault::Default*
with zero external deps. CorbomiteApp builds via its adapter subclasses
that inherit the Markoff interfaces.

Per docs/specs/2026-04-20-phase-c1-di-seam.md §6.2.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
"
git tag -a v0.3.0 -m "Phase C1 complete: DI seam live, Phase B bridge retired"
```

### Task 22 — Status board update

Edit `docs/phase-c-status.md`:
- Work-unit table, C1 row: Status → `markoff ready`; Tag → `v0.3.0`.
- Activity log: append an entry for C1b landing.

```bash
git add docs/phase-c-status.md
git commit -m "phase-c-status: C1 done at v0.3.0; bridge retired"
```

---

## 4. Corbomite-side cleanup

### Task 23 — Bump submodule to v0.3.0 + drop the bridge

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git fetch --tags
git rev-list $(git rev-parse HEAD)..v0.3.0
git rev-list v0.3.0..$(git rev-parse HEAD)   # should be empty
git checkout v0.3.0
cd /home/clinton/dev/Corbomite
git add libs/markoff-family
```

**File to edit:** `/home/clinton/dev/Corbomite/CMakeLists.txt` — delete:
```cmake
# Tell markoff-reading to link Corbomite::Core + mmdr instead of its
# standalone stub headers. Phase B bridge; replaced by a DI seam in Phase C.
set(MARKOFF_READING_USE_REAL_COREDEPS ON CACHE BOOL "" FORCE)
```

### Task 24 — Grep for bridge residue

```bash
cd /home/clinton/dev/Corbomite
grep -rn 'MARKOFF_READING_USE_REAL_COREDEPS' --include='*.cmake' --include='*.txt' --include='*.cpp' --include='*.h' . 2>/dev/null | grep -v docs | grep -v .worktrees
```
Any hits? Delete them.

### Task 25 — Build + test + smoke

```bash
cmake --build build -j 10
cd build && ctest --output-on-failure -j 10
cd .. && ./build/Corbomite
```

Expected: clean build; test-result parity with pre-C1; smoke passes (vault opens, Reading mode renders embeds + mermaid).

### Task 26 — Commit + close the work-unit

```bash
cd /home/clinton/dev/Corbomite
git add libs/markoff-family CMakeLists.txt
git commit -m "feat(markoff): Phase C1 cleanup - drop MARKOFF_READING_USE_REAL_COREDEPS

Bumps submodule to Markoff v0.3.0 (Phase C1 complete) and removes the
Phase B bridge flag from the top-level CMakeLists. markoff-reading
links markoff_core for its new Markoff::* / Markoff::Vault::* types
regardless of host; Corbomite's adapter subclasses (landed in the C1a
commit) implement those interfaces.

Phase C1 work-unit closed. Next: C5 spec (Reading-mode interaction
parity - absorbs Cluster V Phase 4).

Per docs/specs/2026-04-20-phase-c1-di-seam.md §6.2 acceptance criteria.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
"
```

**Update PROJECT-STATE:**
- §Markoff Phase C table: C1 status → `done`; remove the "— next up" hint.
- Current focus: "Markoff Phase C — C5 spec next."
- In-flight row: change last-completed-step to the C1 closeout; next-expected-step to "draft C5 spec".
- Recent-decisions: add one bullet summarising C1 closeout.

Optional memory: append a one-line `project_markoff_c1_done.md` entry if the DI seam's shape is load-bearing for future work (likely yes).

```bash
git add docs/PROJECT-STATE.md
git commit -m "docs(project-state): Markoff C1 closed; C5 next"
```

---

## Acceptance checklist (before declaring C1 done)

From spec §8:

- [ ] Markoff standalone: `cd /home/clinton/dev/Markoff && rm -rf build-dev && cmake -S . -B build-dev && cmake --build build-dev && cd build-dev && ctest` green. Zero host project present.
- [ ] CorbomiteApp: `cmake --build build -j 10 && ./build/Corbomite` — opens a vault, renders reading mode, resolves embeds, renders mermaid, honors code-block processor registration.
- [ ] `git grep 'corbomite/core\|corbomite/storage' libs/markoff-family/libs/markoff-reading/` — no matches (the forwarding `VaultResourceProvider.h` in markoff-reading is fine because it no longer includes `corbomite/`).
- [ ] `git grep 'Corbomite::' libs/markoff-family/` — matches only in `libs/markoff-family/docs/`; no code references.
- [ ] `git grep 'MARKOFF_READING_USE_REAL_COREDEPS'` in either repo — no matches outside docs.
- [ ] `libs/markoff-family/libs/markoff-reading/stubs/` does not exist.
- [ ] Phase-C-status work-unit table shows C1 as `done` with tag `v0.3.0`.
- [ ] Corbomite `PROJECT-STATE.md` reflects C1 closed and C5 as the next work-unit.

## Troubleshooting notes

- **Build fails with "markoff/EmbedRegistry.h: No such file or directory"** after Task 1 — the CMakeLists edit didn't pick up the new header. `libs/markoff-core/CMakeLists.txt` needs both the source-list entry and possibly `target_sources(... PUBLIC FILE_SET HEADERS ...)` if the project uses that pattern. Look at how `MarkdownView.h` is registered for the template.

- **"class redefinition" errors** in Task 5 because the stub and real headers both declare a same-named struct — the stubs are still in the include path via `target_include_directories`. Don't remove stubs yet (that's C1b); instead, `#undef` or `#include` order trickery is *not* the fix — the correct fix is that markoff-reading's .cpp files must no longer `#include <corbomite/...>` at all. Grep again for any straggler.

- **Cherry-picks needed in Task 10's pre-flight audit** — if the Corbomite-side submodule's local master is ahead of Markoff origin, cherry-pick onto Markoff master first, tag a new `v0.2.X`, then restart Task 10. Do NOT bump the pin past those commits; they'll strand.

- **Corbomite::Core::EmbedRegistry `registerExtension` name collision** (Task 11) — the Corbomite version returns `Handle`, Markoff's base returns `void`. Pick one resolution pre-commit:
  - (a) Rename Markoff base's method to `registerExtensionSimple(...) → void`; Corbomite's `Handle registerExtension(...)` is a new method, not an override.
  - (b) Markoff's base returns `Handle` too (promote the `Handle` struct into `Markoff::EmbedRegistry`); Corbomite's `registerExtension` becomes a true override.
  - (c) Accept the hiding; `using Markoff::EmbedRegistry::registerExtension;` in Corbomite's class body exposes both.
  
  Option (b) is cleanest long-term; (c) is smallest diff. Both preserve plugin-facing callers.

- **`MetadataParser::parse` static vs instance** (Task 11) — the spec's §4.1 lists `Markoff::Vault::MetadataParser` as abstract with an instance `parse()`. Corbomite's existing `Corbomite::MetadataParser::parse` is static. Resolve by introducing a new `Corbomite::MetadataParserImpl : public Markoff::Vault::MetadataParser` whose instance-method wraps the static, rather than retyping the static itself.

- **Mermaid test assertions** (Task 8's open sub-question) — if `tst_sectionlayout_mermaid` asserts specific SVG markup, the Default* impl returning empty bytes will fail. Either adjust the assertion to "empty SVG acceptable when no renderer injected", or keep the test gated (via `if(TARGET Corbomite::mmdr)` or similar host-detection guard, not the retired CMake option), or move to Corbomite side.

- **Adapter's multiple-inheritance `MarkdownRenderChild`** (Task 11) — `class Corbomite::Core::MarkdownRenderChild : public Markoff::MarkdownRenderChild, public Corbomite::Component`. If `Corbomite::Component` has signals and `Markoff::MarkdownRenderChild` is a QObject (it's not, per spec), there's no MOC conflict. Verify with the `Q_OBJECT` macro — if Markoff's base has one, the diamond gets awkward. The spec says it doesn't; double-check before committing.

## Timeline estimate

- C1a (Tasks 1–9): ~1 working day.
- Corbomite adapter (Tasks 10–16): ~1 working day.
- C1b (Tasks 17–22): ~2 hours (it's almost all deletion).
- Corbomite cleanup (Tasks 23–26): ~1 hour.

Total ~2.5 days across both repos.
