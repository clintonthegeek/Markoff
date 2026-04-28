# Markoff family codebase audit — 2026-04-28

**Author:** Audit agent, requested by repo owner.
**Scope:** Full audit of the five-library Markoff stack (`markoff-core`, `markoff-live`, `markoff-source`, `markoff-reading`, `markoff-parser`) at `master = a4e6033`.
**Frame:** "Ideally a nice stack, unified at the bottom, expressed in three ways at the top." This audit measures the gap between that ideal and the current shape, prioritizing structural risk over cosmetic findings.

---

## 0. TL;DR

The shape of the family is roughly right — three leaves (`Editor`, `SourceEditor`, `ReadingView`) on a `MarkdownView` base with a shared `MarkoffDocument`. Two of the three leaves (Source, Reading) are **clean and faithful** to the contract. The third — **`markoff-live` is in trouble** and is what's making the user nervous.

The four most serious findings:

1. **`markoff-live`'s `Editor` has six known bandages stacked on top of each other**, two architectural flaws still unfixed (bare-key recursion, cursor-drift), shipped instrumentation logging on hot paths (every keystroke + every `setDocument`), and a "scene-out-of-sync" check that compares serialized markdown on every parse. Highlighting can be silently stale. This is the right widget to be worried about.
2. **The `MarkdownView` base contract leaks types out of the abstraction** — `Theme`, `ResourceProvider`, and `LinkResolver` are forward-declared in `markoff-core` but defined in **`markoff-live`**. Every consumer that wants to use the polymorphic base ends up depending on a leaf library to compile.
3. **Three Theme types** (`Markoff::Theme` in live, `Qutepart::Theme` in source, ad-hoc `Dark/Light` enum in reading), **two `LinkRenderer` classes** with the same name in different namespaces and divergent shapes (one orphaned), and **two-and-a-half code-block highlighting paths** are the symptoms of consolidation that was deferred to phases C2/C4 and hasn't started.
4. **Repository hygiene is rotting** — a fully stale `libs/markoff/` directory from before a rename, three loose review markdowns at repo root, an inactive `.worktrees/tri-view-phase-a/`, and a "still named internally" comment in the root CLAUDE.md describing a rename that already happened.

The good news: the bottom of the stack (`markoff-core`'s `MarkoffDocument` + `CanonicalBuffer` + `MarkdownDelta` + `ParsePool`) is **well designed**, and `markoff-parser` is **clean**. The instability is concentrated, not pervasive.

The bad news: the most-load-bearing leaf is also the worst-aged one, and it's the one users see first.

---

## 1. Architectural integrity — the unified bottom

**Stack as designed (per the spec):**

```
                 host (CorbomiteApp / testapp)
                            |
                  MarkdownView * (polymorphic)
              /             |             \
         Editor       SourceEditor      ReadingView
       (live, scene)  (Qutepart)        (read-only)
              \             |             /
            shared MarkoffDocument (canonical bytes)
                            |
                    markoff-parser (AST)
```

### 1.1 Findings on the contract — `MarkdownView`

`libs/markoff-core/include/markoff/MarkdownView.h`

- ✅ All three leaves correctly inherit `MarkdownView` and override the abstract methods (`setDocument`, `searchAdapter`, scroll, zoom, ephemeral state, etc.). Capability probes (`hasCursor/hasEditing/hasFold`) are honored.
- ❌ **The base API leaks leaf types.** `setViewTheme(const Theme &)`, `setViewResourceProvider(ResourceProvider *)`, `setViewLinkResolver(LinkResolver *)` take types that are forward-declared in `MarkdownView.h` but **defined in `libs/markoff-live/include/markoff/Theme.h`** etc. Any consumer that calls these on a `MarkdownView *` must include a leaf header — which is the opposite of what polymorphic dispatch is for. The `markoff-core/CLAUDE.md` admits this is "future work in Phase C." It is C2's responsibility but C2 is not yet started, and it's the single biggest layering violation in the family.
- ❌ **Per-leaf API surfaces are wildly asymmetric.** `Markoff::Editor` (`libs/markoff-live/include/markoff/Editor.h`) exposes ~150 public methods including `setPlainText`, `toggleBold`, `insertTable`, `tableInsertRowAbove`, `setHeadingLevel`, `foldAll`, `setCurrentNotePath`, `testActivateLink`, `coordinatorForTesting`, etc. `SourceEditor` is ~80 lines. `ReadingView` is in the middle. Hosts that want richer functionality on a Live editor have to `static_cast<Editor *>(view)` or test for capabilities by RTTI — defeating the polymorphic-base promise. Some of this asymmetry is justified (Live does more), but the Editor public surface mixes (a) polymorphic overrides, (b) host-driven editing actions, (c) test-only accessors, and (d) Cluster-specific glue (`setCurrentNotePath`) without organization.

### 1.2 Findings on the bottom — `MarkoffDocument` and Phase C3 primitives

`libs/markoff-core/`

- ✅ The C3 design — `CanonicalBuffer` interface, anchor handles with `CursorBias`, `MarkdownDelta` with `mergeWith` coalescing, `ParsePool` debounce, `Origin` enum on `resetContent` — is **sound**. It's the strongest piece of the codebase and the pieces have unit tests.
- ⚠️ **The C3 landing review** (in repo-root `2026-04-21-c3-landing-review.md`) flagged that the bridge between this clean bottom and the live leaf is the weakest link: `m_sceneNeedsFullRebuildOnNextParse` in `SceneCoordinator` is "an escape hatch at the critical seam" that fires on every multi-paragraph paste, multi-line delete, image-block edit. Each fall-through costs focus loss + scroll jump + ephemeral-state reset. The escape hatch is reliable; it just hurts.
- ⚠️ **`MarkoffDocument` API surface still has `applyCanonicalDelta` / `canonicalSubstring` / `releaseAnchorHandle` exposed publicly** (`MarkoffDocument.h:59-63`) with a comment "Kept public for simplicity; a later pass can tighten via friend decls." Make sure that pass actually happens — these are `package-private` in intent, internal-API in shape, and currently public.

### 1.3 Findings on the parser

`libs/markoff-parser/`

- ✅ Clean abstraction. Tree-sitter is fully hidden; `Document::fromMarkdown()` returns a `unique_ptr<Document>` with a stable query API. All three leaves use it consistently.
- ⚠️ Minor: `extractSubpath()` in `Document.cpp:274-374` walks the source lines with regex rather than using `buildDocumentQueries()`'s already-extracted headings. Works, but duplicates heading detection.

---

## 2. `markoff-live` deep dive — the janky one

**File sizes (the four big ones, ~38% of `markoff-live` source):**
- `Editor.cpp`: 2,910 lines
- `TextControl.cpp`: 2,596 lines (Qt private-class fork)
- `SceneCoordinator.cpp`: 1,383 lines
- `MarkdownTextItem.cpp`: 1,370 lines

This is where to focus engineering attention. Listed by structural severity.

### 2.1 Key dispatch architecture (already known, but worth re-stating in this report's terms)

`libs/markoff-live/src/Editor.cpp:803-887`

The flaw is well documented in `docs/specs/2026-04-21-editor-key-dispatch-architecture.md`. To summarize for this audit's purposes:

- `Editor` sets `setFocusProxy(m_view)` AND `Editor::keyPressEvent` calls `QApplication::sendEvent(m_view, e)`. These two contradict each other; bare-modifier and unbound-shortcut keys recurse infinitely until `m_inKeyPressEvent` (the `v0.6.0-alpha.8` bandage) catches them.
- The recommended fix (Option A in the spec) is small (~20 lines), but the spec is correct that it touches every key-driven feature so it deserves a real pass.
- **What this audit adds:** the bandage doesn't just "mask" the flaw — it **silently swallows valid keystrokes** when the scene loses its focus item (D1 in the spec). The instrumentation in `Editor.cpp:858-867` was put there to *observe* that during dogfood. **It is still in the code, on the hot path, at `qCWarning` level (default-on)**. Every keystroke logs a sprintf. Every. Keystroke. See §2.4.

C7 (find/replace) is correctly blocked on this. Don't ship more keys until it's fixed.

### 2.2 Scene rebuild state machine

`libs/markoff-live/src/Editor.cpp:2701-2780` (`onCanonicalParseUpdated`) and `libs/markoff-live/src/SceneCoordinator.h:154-161` (`consumeRebuildFlag`) and the multi-item full-rebuild fallback.

- The path is: per-item `QTextDocument::contentsChange` → `onLocalItemContentsChange` → `MarkdownDelta` → `MarkoffDocument::applyCanonicalDelta` → emits `contentsChanged` → `Editor::onCanonicalDocumentReloaded` or coordinator's `applyCanonicalDelta` (inbound) → debounced `parseUpdated` → `onCanonicalParseUpdated` → rebuild-or-not decision.
- The decision in `onCanonicalParseUpdated` is `if (!rebuildFlag && !sceneOutOfSync) return;` — and **`sceneOutOfSync = m_coordinator->toMarkdown() != m_sourceText`**. That's a full re-serialize-and-string-compare on every parse. Comment admits it's "more robust than checking items.isEmpty() alone." That's a polite way to say "we couldn't keep accurate `dirty` state, so we reverify via reverse-projection." This is a smell — the truth is in two places (canonical + per-item docs), and the only way to reconcile is to materialize one and string-equal it.
- **Worse**: the comment at `Editor.cpp:2720-2723` reads "*Consequence: syntax highlighting / AST-dependent rendering may be slightly stale between edits and the next forced rebuild. That's a known soak-week limitation.*" There is a known correctness gap between displayed formatting and document state. Users can see markdown not become formatted until something — anything — flips the rebuild flag. This is not okay long-term, even if it's tolerable short-term.
- **The `m_sceneNeedsFullRebuildOnNextParse` escape hatch** triggers on multi-item span deltas, non-text items, and out-of-range deltas — precisely the cases users hit (paste, multi-line delete, table edits). Each one tears down + rebuilds the scene = focus lost, scroll position lost. C3 landing review correctly flagged this as the hardest engineering problem in C3 and the actual solution (extending single-item splice to multi-item, or adopting full-rebuild and optimizing it) was deferred.

**Recommendation:** before C7, decide on the splice strategy. Either:
- (a) extend the splice path to multi-item / non-text / out-of-range deltas and drop the escape hatch,
- (b) accept full-rebuild on those cases but preserve focus + scroll + selection across the rebuild via opaque ephemeral-state save/restore,
- (c) replace `sceneOutOfSync` with explicit dirty-bit tracking, accept stale highlighting only for the duration of one parse-debounce window, and have parseUpdated unconditionally refresh formatting (not items).

### 2.3 Focus management is implicit and load-bearing

The "first text item gets focus" appears at least three times in `Editor.cpp` (lines 2666-2673 in `setDocument`, 2752-2756 in `onCanonicalParseUpdated`, plus an `onCanonicalParseUpdated` "focus-loop" block from `47cd4fa`). Each is a defensive measure to ensure the scene has a focused item *before any key arrives*, because if it doesn't, keys bubble back to Editor → recursion → SEGV (pre-bandage) or silently-eaten-keys (post-bandage).

This is fragile. Focus invariants are stated as code patterns, not enforced as a single chokepoint. Any future code path that mutates scene items without re-focusing risks reintroducing the dropped-keystroke bug. If Option A in §2.1 lands, this whole class of defensive code can simplify — but until then, every scene-mutation path needs a "did we re-focus?" review.

### 2.4 Shipped instrumentation on hot paths

- `Editor.cpp:812, 858, 2591, 2703` — four `static QLoggingCategory lcDog("markoff.live.dogfood")` declarations inside function bodies, each followed by `qCWarning(lcDog, ...)` with sprintf-style format strings.
- `qCWarning` in Qt is **enabled by default** unless the category is explicitly suppressed via `QT_LOGGING_RULES`. Most users will see all of this output on stderr.
- `Editor.cpp:858-867` fires on every keystroke, formatting six pointer/integer values into a warning. `Editor.cpp:2648-2651` and `2674-2678` fire on every `setDocument` (= every note open). `Editor.cpp:2703-2705, 2730-2734` fire on every `parseUpdated`.
- `TextControl.cpp:1699` adds another `lcCursorDrift` category around the alpha.6 defensive clamp.
- These were intentional dogfood instrumentation. **None are #ifdef'd, gated, or downgraded to `qCDebug`.** They will stay loud in production builds until someone explicitly removes them. Recommend either: gate behind a build flag, downgrade to `qCDebug` (off-by-default), or delete now that the soak data has been collected.

### 2.5 `TextControl` — the Qt-private fork

`libs/markoff-live/src/TextControl.{h,cpp,_p.h}`

`TextControl` is a fork of Qt's internal `QWidgetTextControl`. It's ~2,900 lines forked, used by `MarkdownTextItem` for cursor + IME + clipboard + extra-selections plumbing because `QGraphicsTextItem` doesn't expose what's needed. Some thoughts:

- ✅ The fork is honestly attributed (header at `TextControl.h:1-3` declares the GPL-2/3 origin from Qt) and has clear scope.
- ⚠️ It's 2,900 lines of essentially-cloned Qt code that has no upstream. It is the single largest source of "we don't fully understand what's happening" risk in the codebase. The cursor-drift bug (`alpha.6`'s defensive clamp at `TextControl.cpp` `rectForPosition`) and the CJK-autocorrect divergence (`MarkdownTextItem::applyCjkBracketAutocorrect` using a *local* `QTextCursor` while `TextControlPrivate::cursor` tracks elsewhere) both live in this fork.
- ⚠️ The cursor-drift "QTextCursor::setPosition: Position 'N' out of range" warnings noted in the architecture spec's D2 are still happening — defensively clamped, not root-caused. The architecture spec says "Prime suspect: `MarkdownTextItem::applyCjkBracketAutocorrect`" but no fix.
- **Question for the user:** is `TextControl` long-term tenable, or is the strategy to eventually replace `QGraphicsTextItem`-per-block with a single `QPlainTextEdit`-style approach? The current architecture pays a large fork-maintenance bill forever.

### 2.6 `MarkdownTextItem` and `SceneCoordinator` coupling

The flow for outbound deltas is: per-item `QTextDocument::contentsChange` → `SceneCoordinator::onLocalItemContentsChange` → translate via `m_itemMap[idx].canonicalStart + localPos` → push `MarkdownDelta`. The coupling is:
- `SceneCoordinator` knows about per-item local offsets
- `MarkdownTextItem` knows about its position in the scene
- Both must agree on the offset map shape
- The map is rebuilt on every reparse

Reasonably well factored, but the offset map is a separate truth source from the items themselves; bugs around its staleness are the kind of thing that produces the alpha.3-alpha.5 series. The `alpha.5` fix (skip internal reparse when canonical-bound) is in `Editor.cpp:716` (`716a652`); the comment around line 2716 alludes to this. Worth a focused test pass that asserts: after every canonical mutation, every `(itemMap[i].canonicalStart, canonicalEnd)` agrees with the actual item content. This is a good place for a property-based test.

### 2.7 Editor public API is overgrown

`libs/markoff-live/include/markoff/Editor.h` exposes ~150 public methods. They divide into:

- **MarkdownView overrides** (~25 methods) — required.
- **Editor-specific actions** for a host: undo/redo/cut/copy/paste, formatting toggles, table operations, fold operations, search, font size — most of these are reasonable, but they overlap with `QAction *action(ActionId)` (`Editor.h:61`) which exposes the same operations via the action collection. Two ways to do the same thing.
- **QGraphicsView passthroughs** (`scene()`, `viewport()`, `verticalScrollBar()`, `mapToScene/FromScene` — 5 overloads): test/internal use per the comment at `Editor.h:74`. Should not be public.
- **Test-only methods** (`coordinatorForTesting`, `testActivateLink`, `testHoverLink`, `blockCount`): explicitly test-only per their comments but still public, "because the alternative `QT_TESTLIB_LIB` guard only fires when Qt6::Test is linked."
- **Phase-specific glue** (`setCurrentNotePath`, `currentNotePath`): meant for one specific consumer.

Recommend: split `Editor.h` into a public host-facing header and an internal/test header. Move QGraphicsView passthroughs out (composed-widget shape doesn't need them externally). Pick one of {direct methods, ActionId+action()} for editing operations and stick with it.

---

## 3. Cross-cutting concerns

### 3.1 Theme handling — three types, no unified owner

| Library | Type | Definition | Used for |
|---|---|---|---|
| markoff-live | `Markoff::Theme` | `libs/markoff-live/include/markoff/Theme.h` | rich `QHash<Element, QTextCharFormat> formats` + `PaintColors` (checkbox glyphs, code block backdrop, search highlights) |
| markoff-source | `Qutepart::Theme` | `libs/markoff-source/include/qutepart/theme.h` | JSON-loaded Kate-XML theme for gutter colors |
| markoff-reading | ad-hoc enum | `libs/markoff-reading/src/CodeBlockHighlighter.cpp:25` | Dark/Light selector for `KSyntaxHighlighting::Repository` |
| markoff-core | forward-decl only | `MarkdownView.h:16` | base API references this name but no definition |

**`SourceEditor::setViewTheme(const Markoff::Theme &)` is an empty stub** (`libs/markoff-source/src/SourceEditor.cpp:137-139`). Same for `setViewResourceProvider`, `setViewLinkResolver`. Source mode currently can't respond to theme changes from the host.

C2 ("Theme/ResourceProvider/LinkResolver consolidation") is the planned fix and is the **next-most-important consolidation work-unit after the Editor key-dispatch fix**. Until it lands, every `setViewTheme` call site is partially broken.

### 3.2 LinkRenderer — same name, different shapes, one orphan

- `Markoff::LinkRenderer` in `markoff-live/src/LinkRenderer.cpp` (36 lines) — pure signal forwarder. Used in production by `Editor`.
- `Markoff::Reading::LinkRenderer` in `markoff-reading/src/LinkRenderer.cpp` (44 lines) — different shape. Owns a `Markoff::Vault::ResourceProvider *`, performs wiki-link resolution before emission. **Constructed nowhere in production** (only in `tst_readingview_linkrenderer.cpp`). The C5 spec already noted this as orphaned.
- `markoff-source` has none.

The two classes share a name (across namespace) but have **incompatible signal signatures** (`fileLinkActivated(linkText, sourceId, fromPath)` vs `linkClicked(target, sourceId)`). A consumer cannot connect to both via a uniform pattern.

**Recommend:** delete `Markoff::Reading::LinkRenderer` (orphan) or, if the intent was for Reading to grow link emission, promote a unified `Markoff::LinkRenderer` to `markoff-core` with a single signal contract.

### 3.3 Search adapters — appropriately thin

LiveSearchAdapter / SourceSearchAdapter / ReadingSearchAdapter are all thin shims over the leaf's selection/highlight machinery, delegating engine logic to `markoff-core`'s `SearchController`. **No consolidation needed here.** ReadingSearchAdapter is mostly stubs (read-only mode), which is correct.

### 3.4 Inline span detection — three different walkers

- Live's `MarkdownHighlighter` consumes pre-built `QList<SourceSpan>` from the parser, applies via `highlightBlock()`. Works on the formatted `QTextDocument`.
- Reading's `SpanRenderer` walks raw markdown text with regex, builds spans inline. **Does not use the AST.** Different pipeline phase from Live.
- Source uses Qutepart's `hl/` (Kate-XML, slated for KF6 replacement).

Three different inline-span representations across the family. The C3 design's `SourceSpan` (in `markoff-parser`) was meant to be the shared truth. Live uses it; Reading does not. C4 was meant to fix this and is not started.

**This is a bigger latent issue than it looks** — it means inline highlighting differs between modes by construction. A user editing in Live and switching to Reading sees the same markdown rendered through two unrelated code paths. Bugs in one won't reproduce in the other.

### 3.5 Code-block rendering — three paths, registry not consulted in two

`Markoff::CodeBlockProcessorRegistry` is in `markoff-core` (good). Live consults it. **Reading does not** (`CodeBlockHighlighter` always falls back to `KSyntaxHighlighting`). **Source does not** (uses Qutepart's `hl/`, slated for KF6 replacement). Same code block (e.g. ```mermaid```) renders differently in each mode.

C4 unification is the planned fix. Until it lands, code blocks are inconsistent across modes.

### 3.6 Fold representations — three independent

| Library | Internal repr | `MarkdownView::foldedHeadings()` mapping |
|---|---|---|
| Live | `QSet<QStringList>` (heading paths) | lossy — paths → nearest source line |
| Reading | `QVector<int>` (heading line indices) | direct |
| Source | none yet | returns `{}` (Phase A stub) |

`FoldSpec` carries `(line, level)` only — no heading-text-hash, so two parses can disagree about which heading line `7` refers to if intervening content shifted. C7 inherits this when Source implements folds.

### 3.7 Math rendering — two parallel jkqtmathtext integrations

- `MathRenderer` + `MathTextObject` in markoff-live (TypeId 1)
- `ReadingMathObject` in markoff-reading (TypeId 10)
- Same engine (jkqtmathtext), independent integrations. The peer-library constraint justifies the duplication today; C2/C4 should consolidate.

---

## 4. Dead code, leftovers, repository hygiene

| # | Item | Location | Recommendation |
|---|---|---|---|
| 1 | Empty stale library directory (rename leftover) | `libs/markoff/` | **Delete**. Pre-rename build dir + `.cache/` + `.claude/settings.local.json`. Untracked. |
| 2 | Stale CLAUDE.md note about the rename | `CLAUDE.md:76` ("still named `libs/markoff/CLAUDE.md` internally at v0.2.0") | **Update**: rename happened; leaf has its own CLAUDE.md correctly named. |
| 3 | Loose review notes at repo root | `2026-04-21-c3-api-shape-response.md`, `2026-04-21-c3-landing-review.md`, `2026-04-21-undo-strategy-response.md` | **Move** to `docs/handoff/` or `docs/specs/`. They're substantive, not throwaway. |
| 4 | Quarantined tests | `libs/markoff-reading/tests/CMakeLists.txt` (4 tests gated `if(FALSE AND MARKOFF_READING_USE_REAL_COREDEPS)`) | **Keep, documented**. Track as Phase C1 follow-up; user-visible since the `MARKOFF_READING_USE_REAL_COREDEPS` option was supposed to be retired. The `if(FALSE AND ...)` is a deliberate parking pattern. |
| 5 | Orphaned `Markoff::Reading::LinkRenderer` | `libs/markoff-reading/{include,src}/.../LinkRenderer.{h,cpp}` | **Delete or claim**. Production never instantiates. |
| 6 | Dead Qutepart features | `libs/markoff-source/src/{indent/, hl/, completer.cpp, bracket_highlighter.cpp, html_delegate.cpp, side_areas.cpp Minimap}` (~2.5K LOC of unused multi-language indenters + autocomplete + minimap) | **Defer to phase-aware cleanup** (C7 scope). Don't delete piecemeal — Source's plan calls for a phased trim. |
| 7 | `#if 0` block | `libs/markoff-source/src/html_delegate.cpp:18-60` ("`FIXME not used. Remove?`") | **Delete**. |
| 8 | Typo | `libs/markoff-source/src/side_areas.h:31` (`lastHoeveredLine`) | **Rename**. |
| 9 | Old worktree | `.worktrees/tri-view-phase-a/` (branch `feature/tri-view-phase-a` at `382e262`) | **Confirm with user**. Per handoff doc it's "preserved as reference"; if no longer needed, `git worktree remove`. |
| 10 | Shipped instrumentation logging | `Editor.cpp:812, 858, 2591, 2703`; `TextControl.cpp:1699`; `SceneCoordinator.cpp` (4 sites) | **Downgrade to `qCDebug` or delete**. `qCWarning` is on by default; this fires on every keystroke. |
| 11 | Public package-private API | `MarkoffDocument.h:59-63` (`applyCanonicalDelta`, `canonicalSubstring`, `releaseAnchorHandle`) | **Tighten via `friend`** as the comment promises. |
| 12 | `LinkRenderer.h` exposed in markoff-live public API | `libs/markoff-live/include/markoff/LinkRenderer.h` | **Reconsider**. Either it's leaf-public (then naming should signal Live-specific) or it's a candidate for promotion to markoff-core. |
| 13 | Untracked `libs/jkqtmathtext` | symlink to `/home/clinton/dev/Corbomite/libs/jkqtmathtext` | **Document**. Per top-level CLAUDE.md it's expected as sibling lib; the symlink works for this dev's setup but won't survive a fresh-clone standalone build. Either commit a real submodule or document the symlink expectation in a setup note. |

---

## 5. Per-leaf brief notes (summarizing parallel agent reports)

### 5.1 markoff-source — clean-but-incomplete

- ✅ MarkdownView contract faithfully implemented; SearchAdapter is a thin shim.
- ❌ **Theme stub** — `setViewTheme/Resource/LinkResolver` are empty bodies.
- ❌ **Fold-gutter UI is wired but `foldedHeadings()` returns `{}`** — Phase A stub, blocks C7's session-state persistence.
- ⚠️ Carries 2.5K LOC of unused Qutepart machinery (multi-language indenters, autocomplete, minimap, bracket highlighter, `html_delegate` with explicit "FIXME not used" `#if 0`). All deferred to C7 cleanup.
- ⚠️ `Qutepart::Theme` is JSON-loaded; no bridge to `Markoff::Theme`. This is the exact kind of gap C2 needs to close.
- 11 TODOs across the fork; all marked.

### 5.2 markoff-reading — clean

- ✅ Public API matches MarkdownView with cohesive Reading-specific extensions.
- ✅ `stubs/corbomite/` cleanup from C1b is complete; no `Corbomite::` references remain.
- ✅ DI seam (`setEmbedRegistry` etc.) is clean; lazy-default fallbacks don't thrash.
- ✅ Section layout, virtual scroll, recycle pool all cleanly separated.
- ⚠️ `LinkRenderer` is orphaned (already covered, §3.2).
- ⚠️ `setViewTheme` accepts `Markoff::Theme` (which doesn't define paragraph-styling colors usable by Reading's renderer); same C2 gap as Source.
- ❌ **CodeBlockProcessorRegistry not consulted** during section layout — special languages render differently in Reading vs Live.

### 5.3 markoff-parser — clean, no concerns of note

- ✅ Public API minimal and stable; tree-sitter fully hidden.
- ✅ rapidyaml integration clean; YAML 1.2 type resolution in-house and well-tested.
- ⚠️ `extractSubpath` walks source lines with regex rather than reusing the existing headings query — minor duplication within the library.

---

## 6. Recommended remediation order

This is the audit agent's view on what to do, **in order**, based on impact-per-effort and dependency graph. The user is the decision-maker on phasing.

### Phase R0 — Repo hygiene (1 hour)
1. Delete `libs/markoff/` (untracked).
2. Move three loose `2026-04-21-*.md` review notes into `docs/`.
3. Update `CLAUDE.md:76` (drop the "still named internally" comment).
4. Confirm with user whether `.worktrees/tri-view-phase-a/` is still wanted; remove if not.
5. Fix `lastHoeveredLine` typo.

### Phase R1 — Live mode triage (small, high-value)
1. Downgrade or delete dogfood `qCWarning` instrumentation in `Editor.cpp` and `TextControl.cpp`. Keep one trigger gated behind a `Q_LOGGING_CATEGORY` that is OFF by default.
2. Implement Option A from the key-dispatch architecture spec (already designed, ~20 lines + test pass). Remove the `m_inKeyPressEvent` bandage. Re-tag `v0.6.1`. **This unblocks C7.**
3. Address the cursor-drift root cause (architecture spec §D2, "Prime suspect: `MarkdownTextItem::applyCjkBracketAutocorrect`"). Remove the `rectForPosition` defensive clamp.

### Phase R2 — Live mode scene rebuild correctness
1. Make a decision on splice-vs-rebuild for multi-item / non-text / out-of-range deltas (§2.2 above). Document the choice.
2. Implement focus + scroll + selection preservation across rebuilds (so even when full-rebuild fires, the user doesn't lose state).
3. Replace `sceneOutOfSync = m_coordinator->toMarkdown() != m_sourceText` with explicit dirty tracking.
4. Property-based test for `(itemMap[i].canonicalStart, canonicalEnd)` consistency after every mutation type.
5. Address the "highlighting may be stale" comment — either ensure it isn't, or expose it as a documented invariant ("formatting may lag canonical by one parse-debounce window").

### Phase R3 — C2 (planned): Theme / ResourceProvider / LinkResolver consolidation
- Move `Markoff::Theme` from markoff-live to markoff-core. Extend its surface for Source's gutter colors and Reading's paragraph colors.
- `SourceEditor::setViewTheme` and `ReadingView::setViewTheme` actually do something.
- Remove the leaf-defined-but-base-referenced layering violation from `MarkdownView.h`.

### Phase R4 — C7 (planned): Source feature completion
- Find/replace API on Source.
- Fold-gutter coordinator across Source + Live. (Reading already has fold; Source's `foldedHeadings()` stop being a stub.)
- Trim the dead Qutepart machinery (indent/, hl/ multi-language, completer, bracket_highlighter, minimap) per a phased plan.
- Strengthen `FoldSpec` with heading-text-hash so it survives parse drift.

### Phase R5 — C4 (planned): Renderer unification
- Reading consults `CodeBlockProcessorRegistry`.
- Promote `SpanRenderer` (Reading's) to markoff-core with a unified `InlineSpan` shape; Live + Source delegate to it.
- Promote a shared math object interface to markoff-core; Live and Reading both implement it.
- Delete or unify the two `LinkRenderer` classes.

### Phase R6 — Editor public API cleanup
- Split `Editor.h` into host-facing and internal/test headers.
- Pick a single dispatch for editing operations: direct methods OR `ActionId`+`action()`. Drop the duplication.
- Hide QGraphicsView passthroughs.
- Tighten `MarkoffDocument`'s package-private API with `friend` decls per the existing comment.

### Phase R7 — TextControl strategy decision
- Decide whether `TextControl` (Qt private fork) is the long-term plan or whether to migrate `MarkdownTextItem` off `QGraphicsTextItem` toward a `QPlainTextEdit`-style architecture, which would let `TextControl` be retired. This is the single biggest "do we keep this debt forever?" question in the codebase.

---

## 7. Open questions for the user

These are decisions only the user can make.

1. **`TextControl` direction** (§2.5 + R7). Keep the 2,900-line Qt private fork forever, or plan a migration off it? This shapes the next year of Live development.

2. **Splice strategy** (§2.2 + R2). Are you comfortable with full-rebuild on multi-item edits if focus/scroll/selection are preserved? If so, the C3 escape-hatch can be accepted as the model. If not, the splice needs to extend, which is the actual hard engineering.

3. **Phase ordering**. The original plan is C7 → C2 → C4. R1+R2 (Live triage) is implicit; R0 is hygiene. **Recommend interleaving:** R0 + R1 first (cheap, high-impact), then C2 *before* C7 because Source's empty `setViewTheme` + the leaky base contract are both blocking — getting Theme into core unblocks Source's theme bridge, which is more honest engineering than leaving the stub in place while shipping new C7 features.

4. **`Markoff::Reading::LinkRenderer`** (§3.2). Delete it (orphan) or claim it (intentional API for future Reading link emission)? If claim, it should be aligned with the Live one.

5. **`libs/jkqtmathtext` symlink**. Is this dev-machine-specific, or do all Markoff devs have the same Corbomite-as-sibling layout? Standalone Markoff documentation should make this explicit.

6. **C5 deferred items** (per phase-c-status.md): regular-URL hover-popover in Reading is uncovered; section-pipeline URL hit-testing was punted to C3/C4. Confirm this is still acceptable.

7. **Dogfood instrumentation policy**. Is there a project convention for soak-week instrumentation that didn't get followed (so we can fix it for next time), or was each `qCWarning` an ad-hoc decision? Recommend establishing one: instrumentation goes behind a build flag or an OFF-by-default `Q_LOGGING_CATEGORY`, removed within N days of the soak that motivated it.

8. **`v0.6.1` tagging gate**. Per the spec, `v0.6.1` should not be cut while bandages are in place. Current state (`v0.6.0-alpha.8` with `m_inKeyPressEvent` guard + cursor-drift clamp + dogfood instrumentation) is bandage-saturated. Confirm: no `v0.6.1` until R1 (Live triage) lands.

---

## 8. Methodology notes

- Coverage: every public header in every leaf was read or summarized; the four largest source files in `markoff-live` (`Editor.cpp`, `TextControl.{h,cpp}`, `SceneCoordinator.{h,cpp}`, `MarkdownTextItem.cpp` headers) were sampled deeply on key sections (key dispatch, scene rebuild, setDocument). Source/Reading/Parser were investigated by parallel research agents whose reports are reflected in §3 and §5.
- This audit did not run the build or tests. Findings are static-analysis + read-through. Where a finding asserts behavior (e.g., "fires on every keystroke"), the assertion is grounded in the code path and Qt's default logging behavior, not in a runtime trace.
- Uncertainty markers: items I'm less sure about are noted with "Question for the user" or "Recommend confirming." Items asserted without hedging are well-grounded in the code or spec.
- Bias disclosure: the user asked for a critical audit and led with concern about Live mode. The audit is critical by design. The good parts (`markoff-core` Phase C3 primitives, `markoff-parser`, Reading's structure, the per-phase activity logs) are genuinely good and not under-credited here, but the body weight is on what to fix because that's what was asked for.
