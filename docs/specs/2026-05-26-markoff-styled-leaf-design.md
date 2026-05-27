# `markoff-styled` — a third view leaf, design

**Date:** 2026-05-26
**Status:** Draft, awaiting plan
**Branch:** `master`

## 0. Context

Markoff today has two view leaves:

- **`markoff-source`** — `QPlainTextEdit` subclass with KF6 SyntaxHighlighting. Raw monospace markdown with syntax-coloured tokens.
- **`markoff-live`** — QML `ListView` of per-block delegates. Full live-render via the L0–L8 stack; the active maximalist view leaf (see `docs/specs/2026-05-08-e-arc-framing.md`).

This spec proposes a third leaf, **`markoff-styled`**: a "plain-jane" QWidget Markoff editor that styles the rendered markdown via proportional fonts and per-block formatting, **without** QML and **without** WYSIWYG mapping. Markdown source bytes stay 1:1 with what the user sees on screen (delimiters remain visible); styling is overlaid via `QTextBlockFormat` (block-level) and `QTextCharFormat` (inline-level).

Likely future: this leaf becomes the basis for a QWidget live-render view if the project decides QtQuick deps are no longer desirable. Out of scope here.

## 1. Goals and non-goals

**Goals:**

- Plain `QTextEdit` + `QTextDocument` view of a `MarkoffDocument`, with proportional/styled rendering.
- Same parser-driven source of truth as `markoff-live` (`MarkoffDocument::inlineSpansFor`, `iterateBlocks`, `blockKind`, etc.).
- Block-level styling: H1–H6 (sized), CodeBlock (monospace + tint), Blockquote (indented), ListItem (indented per depth), HorizontalRule.
- Inline char styling: bold, italic, strikethrough, code, highlight, tag, footnote-ref, link, wikilink.
- Delimiter visibility model: delimiters render styled but visible (matches `markoff-live` E1). Cursor-aware hide-when-leaves-parent-range arrives in v0.1 (low risk; small follow-up).
- Link interaction: click activate, hover, hover-leave — routed to the existing `Markoff::LinkService` abstraction so Corbomite's existing service implementation works unchanged.
- Theme switching (`Markoff::Theme`) and font scaling (`Ctrl+/−`).
- Find-bar highlights (v0.1), driven by the same `Markoff::FindController` Corbomite already uses.

**Non-goals (v0):**

- Math rendering (block or inline). Source is shown styled-but-textual.
- Image rendering (inline or block). Source is shown styled-but-textual.
- Table delegate rendering (whole markdown table shown as styled text).
- Callout boxes, embed blocks.
- WYSIWYG editing (delimiter-hiding follows the cursor in v0.1, never permanent).
- Replacing `markoff-source` or `markoff-live`.

**Non-goals (forever):**

- No KF6 SyntaxHighlighting in this leaf. Single source of truth = parser spans.
- No QML / QtQuick dependency, direct or transitive.

## 2. Layout

New leaf library at `libs/markoff-styled/`, parallel to `libs/markoff-source/`:

```
libs/markoff-styled/
├── CLAUDE.md
├── CMakeLists.txt
├── include/markoff/styled/
│   ├── Editor.h                # public widget
│   ├── FindBar.h               # public widget (v0.1)
│   └── MarkoffStyledExport.h
├── src/
│   ├── Editor.cpp
│   ├── StyleApplier.h/.cpp     # internal — parser-driven styling layer
│   ├── DocHighlighter.h/.cpp   # internal — whole-doc QSyntaxHighlighter for inline
│   └── LinkInteraction.h/.cpp  # internal — mouse → LinkActivation → LinkService
├── app/                        # markoff-styled-app demo (mirrors markoff-source/app)
└── tests/
    ├── tst_styled_block_formats.cpp
    ├── tst_styled_inline_formats.cpp
    ├── tst_styled_delimiter_visibility.cpp
    ├── tst_styled_link_interaction.cpp
    └── tst_styled_d2_integration.cpp
```

**C++ namespace:** `Markoff::Styled`.
**Public include root:** `<markoff/styled/...>`.
**QML module URI:** *none* (this leaf is QML-free).

## 3. Dependencies

- **Qt6 6.8+** Core, Gui, Widgets, Test.
- **`markoff-core`** — `MarkoffDocument`, `Session`, `SourceTextDocumentBinding`, `Theme`, `LinkService`/`DefaultLinkService`, `LinkActivation`, `LinkKind`, `SourceSpan` (transitive via `markoff-parser`), `BlockKindNames`, `InlineParseCache`.
- **`markoff-parser`** — transitive, for `SourceSpan` and `LinkTarget` value types.

**Not** depended upon:

- `markoff-live` — explicitly excluded. It's a `qt_add_qml_module` target linking `Quick`/`QuickControls2`/`QuickWidgets`/`Qml`; depending on it would drag QtQuick into a widget-only library. Patterns from `markoff-live::InlineHighlighter` are mirrored, not imported.
- `KF6SyntaxHighlighting` — explicitly excluded. Single source of truth is the parser, not regex.

## 4. Public API surface

### `Markoff::Styled::Editor`

`QTextEdit` subclass. Mirrors `Markoff::Source::Editor`'s setter shape where overlap makes sense:

```cpp
namespace Markoff::Styled {

class MARKOFF_STYLED_EXPORT Editor : public QTextEdit {
    Q_OBJECT
    Q_PROPERTY(Markoff::MarkoffDocument *markoffDocument
               READ markoffDocument WRITE setMarkoffDocument
               NOTIFY markoffDocumentChanged)
    Q_PROPERTY(Markoff::Session *session
               READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(const Markoff::Theme *theme
               READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(Markoff::LinkService *linkService
               READ linkService WRITE setLinkService NOTIFY linkServiceChanged)
    Q_PROPERTY(QString fromContext
               READ fromContext WRITE setFromContext NOTIFY fromContextChanged)
    Q_PROPERTY(qreal fontScale
               READ fontScale WRITE setFontScale NOTIFY fontScaleChanged)
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    Markoff::MarkoffDocument *markoffDocument() const;
    void                      setMarkoffDocument(Markoff::MarkoffDocument *);

    Markoff::Session *session() const;
    void              setSession(Markoff::Session *);

    const Markoff::Theme *theme() const;
    void                  setTheme(const Markoff::Theme *);

    Markoff::LinkService *linkService() const;
    void                  setLinkService(Markoff::LinkService *);

    QString fromContext() const;
    void    setFromContext(const QString &);

    qreal fontScale() const;
    void  setFontScale(qreal);

Q_SIGNALS:
    void markoffDocumentChanged();
    void sessionChanged();
    void themeChanged();
    void linkServiceChanged();
    void fromContextChanged();
    void fontScaleChanged();
};

}  // namespace Markoff::Styled
```

**Behaviour on null service.** If `setLinkService(nullptr)` is the state, `Editor` lazily instantiates a `Markoff::DefaultLinkService` (same lifecycle as `LiveListModelBinding`'s default service). Editor remains functional standalone.

**Behaviour on null document.** `Editor` displays empty; setting a non-null `MarkoffDocument` wires the binding and triggers initial restyle.

### `Markoff::Styled::FindBar`

v0.1 only. Mirrors `Markoff::Source::FindBar`. Drives a `Markoff::FindController` (already used by Corbomite). Out of v0; mentioned for layout reservation.

## 5. Internal architecture

Three layers inside the widget:

1. **`Markoff::Styled::Editor`** — `QTextEdit` subclass. Owns its `QTextDocument`. Public surface.
2. **`Markoff::SourceTextDocumentBinding`** (reused from `markoff-core`) — handles text sync: forward (`QTextDocument::contentsChange` → `MarkoffDocument::applyFlatEdit`), reverse (`d2DocumentChanged` → full-replace `setPlainText`), and cursor/selection anchoring via `Session::primarySelection()`.
3. **`Markoff::Styled::StyleApplier`** (new, internal) — subscribes to `MarkoffDocument::d2DocumentChanged` *independently* of the binding. After the binding's reverse-path `setPlainText` (or no-op on local edits), walks blocks and applies block + inline formats via `QTextCursor`.

Connection ordering: `StyleApplier` is connected **after** the binding in `Editor::setMarkoffDocument`, so the binding's text changes complete before the styler paints over them.

### `StyleApplier` responsibilities

- Block iteration: `MarkoffDocument::iterateBlocks()`.
- Block-byte-range → Qt-position-range conversion via `SourceTextDocumentBinding::byteOffsetToQtPos` (already present in `markoff-core`).
- Per-block kind → `QTextBlockFormat` mapping:
  - `Heading` (level 1–6) → font-point-size step, top/bottom margins.
  - `CodeBlock` → monospace family override, background tint via `Theme::CodeBlockBackground`, left margin.
  - `Blockquote` (depth → left margin), grey foreground via `Theme::Quote` (per-depth via repeated indent for v0; visual bar punted).
  - `ListItem` → left indent computed from MarkoffDocument's list metadata (depth × indent unit).
  - `HorizontalRule` → grey monospace; content is raw `---`/`***`.
  - `Paragraph` (default) → no overrides beyond Theme defaults.
- A Markoff block may span multiple `QTextBlock`s (notably fenced code). `StyleApplier` walks the range with `QTextCursor::movePosition(NextBlock)` and applies the block format to each.
- Per-block inline span loop: `inlineSpansFor(blockId)` returns `QList<SourceSpan>`. For each span:
  - `bold`/`italic`/`strikethrough`/`code`/`highlight`/`isTag`/`isFootnoteRef` → `QTextCharFormat` configured from `Theme` slots, applied via `QTextCursor::mergeCharFormat`.
  - `isLink`/`isWikilink` → link colour + underline + `setAnchor(true)` + `setAnchorHref(span.linkTarget.rawText)`. Href is set for accessibility tools and possible future web-export but is *not* the resolution path; mouse handling goes through `inlineSpansFor` (see §6).
  - `isDelimiter` → grey foreground (visible). Cursor-aware hide is v0.1.

### `DocHighlighter`

Whole-document `QSyntaxHighlighter` subclass. Its job in v0 is **only** delimiter visibility transitions and find-span overlays (when wired in v0.1). Block + inline formats are applied by `StyleApplier` via `QTextCursor`; `DocHighlighter` is the right place for content that depends on **cursor position** (delimiter hide-on-leave) because Qt re-runs `highlightBlock` on each affected block when caret crosses block boundaries.

For v0, `DocHighlighter` is structurally present but effectively a stub — all formatting goes through `StyleApplier`. v0.1 promotes delimiter visibility into it.

**Rationale for the split.** Combining cursor-aware delimiter visibility with block-format application produces a hybrid system that fights both Qt's idea of `QSyntaxHighlighter` (which only paints `QTextCharFormat`) and our preference for one explicit `StyleApplier` pass. Keeping them separate has clear ownership: `StyleApplier` owns content-derived formats; `DocHighlighter` owns cursor-derived formats.

## 6. Link interaction

API symmetric with `markoff-live`'s `LiveListModelBinding::setLinkService` / `setFromContext`. Same `LinkService` interface, same `LinkActivation` payload — Corbomite's `LinkService` implementation works unchanged.

### Click path

1. `Editor::mousePressEvent` override.
2. `cursorForPosition(event->pos())` → `QTextCursor` → Qt char position.
3. Resolve `(BlockId, qtPos-within-block)` via `MarkoffDocument::blockAt(textAnchor)` + `offsetInBlock`.
4. Walk `inlineSpansFor(blockId)` for a span where `isLink || isWikilink` covers `qtPos`.
5. Hit → build `LinkActivation { kind, rawText, modifiers, globalPos }`, call `m_linkService->activate(...)`, consume event.
6. Miss → forward to base class (normal cursor placement).

### Hover path

`viewport()->setMouseTracking(true)` enabled in constructor. `mouseMoveEvent` override:

1. Same `(BlockId, qtPos)` resolution as click.
2. Find covering link span. Span's raw text is the identity key (`m_currentHoveredRawText`, matching `LiveListModelBinding`'s field).
3. If covering raw text differs from cached:
   - If cached non-empty → `m_linkService->notifyHoverLeft(cached)`.
   - If newly hovering link → `m_linkService->notifyHover(activation, event->globalPosition().toPoint())`.
   - Update cached.
4. `viewport()->setCursor(Qt::PointingHandCursor)` on link, `Qt::IBeamCursor` off link.
5. Idempotent within the same raw text (no re-emit).

### Mouse-leave path

`leaveEvent` override:
- If `m_currentHoveredRawText` non-empty → `notifyHoverLeft(cached)`, clear cache.
- Restore cursor.

### Why no `QTextCharFormat::setAnchorHref` round-trip

Considered: serialise `LinkTarget` into the anchor href, recover via `QTextEdit::anchorAt(QPoint)`. Rejected because:
- `LinkTarget` is a structured payload; round-tripping through a URL string is lossy and brittle.
- `inlineSpansFor` already caches per-block; resolving via the cache means the same source of truth used by the styler.
- `QTextEdit::anchorAt` requires `setOpenLinks(false)` and a separate signal wiring (`anchorClicked` is `QTextBrowser`-only).

Anchor href is set to the span's raw text (for accessibility introspection / future web export), but mouse handling does not consume it — resolution flows through `inlineSpansFor` and `LinkService`. `Editor` calls `setOpenLinks(false)` in its constructor so Qt does not attempt to open anchors as URLs.

### Popup composition

Out of scope for this leaf. `Editor` emits via `LinkService::notifyHover`; the consumer (Corbomite, etc.) owns the popup widget and its placement.

## 7. Data flow + cycle correctness

Same conventions Markoff already follows.

**Authoritative state:**

| Concern | Owner |
|---|---|
| Document text bytes | `MarkoffDocument` (D2 per-block buffers) |
| Cursor + selection (anchors) | `Session::primarySelection()` |
| Block kinds + attrs | `MarkoffDocument` causal-LWW maps |
| Inline span data | `MarkoffDocument::inlineSpansFor` (parser cache) |
| `QTextDocument` text | Mirror of MarkoffDocument, maintained by `SourceTextDocumentBinding` |
| `QTextDocument` formats | `StyleApplier`-owned, recomputed from spans |
| Link service activations | External `LinkService` |

No L4 ambiguity: formats are pure derived state with no parallel store.

**Forward path (local typing):**
```
QTextEdit keystroke
  → QTextDocument::contentsChange(qtPos, removed, added)
  → SourceTextDocumentBinding::onQtContentsChange
      m_applyingLocalEdit = true
      → MarkoffDocument::applyFlatEdit(...)
          → debounced d2DocumentChanged scheduled
      m_applyingLocalEdit = false
  → d2DocumentChanged (later)
  → SourceTextDocumentBinding::onD2DocumentChanged
      sees m_applyingLocalEdit → skips setPlainText (correct)
  → StyleApplier::onD2Changed
      QSignalBlocker(textDocument)
      cursor.beginEditBlock()
      restyle all blocks
      cursor.endEditBlock()
```

**Reverse path (remote edit, undo, paste, reset):**
```
MarkoffDocument mutation (not from our QTextDocument)
  → d2DocumentChanged
  → SourceTextDocumentBinding::onD2DocumentChanged
      m_applyingRemoteEdit = true
      QTextDocument::setPlainText(serializeForSave())  ← wipes formats
      restore cursor from Session anchors
      m_applyingRemoteEdit = false
  → StyleApplier::onD2Changed
      restyle (re-applies formats)
```

**Five cycle guards** (three already exist):

1. `SourceTextDocumentBinding::m_applyingLocalEdit` — existing.
2. `SourceTextDocumentBinding::m_applyingRemoteEdit` — existing.
3. `SourceTextDocumentBinding::m_applyingBackendCursor` — existing.
4. `StyleApplier::m_applyingFormats` — **new.** Re-entry guard on `onD2Changed`. Defended by `QSignalBlocker` on the `QTextDocument` plus `beginEditBlock`/`endEditBlock`.
5. `Editor::m_applyingFontScale` — **new.** Prevents font-scale-triggered restyle from loop-back through edit signals.

**Invariant discipline:** `m_applyingFormats` and `m_applyingFontScale` are re-entry guards (invariant 7 smell). Justification: format application is genuinely re-entrant via Qt format-change signals; alternative is hoping no observer attaches to our QTextDocument, which is fragile. Logged in `docs/queue.md` Discipline Log per invariant 8 when the code lands.

**Performance budget:** Full restyle on every debounced `d2DocumentChanged`. Cost: `O(N + Σ spans_per_block)` with parser cache hits making per-block span lookup O(1) when block content unchanged. For 1000-block docs comfortably under 16 ms. Per-block content-hash skip is YAGNI until profiled.

## 8. Testing

Test prefix `tst_styled_*`, run via `scripts/run-tests.sh` (offscreen by default — never `--direct` without explicit user permission).

| Binary | Coverage |
|---|---|
| `tst_styled_block_formats` | Block-level styling per kind (H1–H6, CodeBlock, Blockquote, ListItem indent depth, HorizontalRule). Loads fixture document, asserts `QTextBlockFormat` properties on resulting `QTextBlock`s. |
| `tst_styled_inline_formats` | Inline char formats per `SourceSpan` flag (bold/italic/strike/code/highlight/tag/footnote-ref/link/wikilink). Mirrors `tst_live_render_inline_per_kind` / `_combined`. |
| `tst_styled_delimiter_visibility` | (v0 stub; v0.1 implementation.) Cursor-aware delimiter behaviour: parent-range visibility for `**bold**`, `*em*`, `` `code` ``, `[text](url)`, `[[wikilink]]`. v0 asserts delimiters render visible-but-styled; v0.1 promotes to cursor-aware hide. |
| `tst_styled_link_interaction` | Click + hover + leave: mock `LinkService`, simulate `QTest::mouseClick`/`mouseMove`/`QTest::mouseMove + leaveEvent` against `Editor::viewport()`. Asserts `activate`/`notifyHover`/`notifyHoverLeft` call sequence + payload + idempotency-within-span + cursor-shape change. |
| `tst_styled_d2_integration` | Forward + reverse path: type → formats applied; `applyFlatEdit` remote → formats survive after `setPlainText` round-trip; `undoD2` → formats restored; `resetContent` → no doubling/leak. |

**Fixtures:** Hand-built `MarkoffDocument`s seeded via `loadFromMarkdown` or `applyFlatEdit`. No real files, no network.

**`LinkService` mock:** Test-local `RecordingLinkService` capturing every `activate`/`notifyHover`/`notifyHoverLeft` call.

**Mouse simulation:** `QTest` events against `Editor::viewport()`, positions derived from `QTextCursor::cursorRect`.

**Performance test:** `tst_styled_typing_perf` exists as a future binary, not v0. Type-into-1000-blocks budget assertion; added when evidence justifies.

**Demo app:** `markoff-styled-app <markdown-file>`. Mirrors `markoff-source-app`. Used for dogfood verification; agents do not spawn windows without permission.

## 9. Build glue

Add to top-level `CMakeLists.txt`:
```cmake
add_subdirectory(libs/markoff-styled)
```
gated on the same `MARKOFF_BUILD_APPS` option `markoff-live` and `markoff-source` use, since submodule consumers may want the lib without the demo app.

Per-leaf CMake mirrors `markoff-source/CMakeLists.txt`: no QML module, plain `qt_add_library(markoff_styled STATIC ...)`.

Build conventions: `compile_commands.json` exported via top-level setting; `.clangd` already points to `build-dev`.

## 10. Phased delivery (v0 → v0.2)

**v0 (this spec):**

- Library skeleton + public `Editor` with all setters.
- `SourceTextDocumentBinding` wiring.
- `StyleApplier` block-level pass (all 7 block kinds in §1 Goals).
- `StyleApplier` inline char pass (9 inline `SourceSpan` flags).
- `LinkInteraction` mouse handling — click + hover + leave → `LinkService`.
- 5 test binaries.
- Demo app.

**v0.1:**

- `DocHighlighter` cursor-aware delimiter hide-on-leave-parent-range.
- `FindBar` + `FindController` integration.
- Font-scale persistence in `Session`.

**v0.2+ (future micro-specs, one each):**

- Math inline + display rendering (mirrors `markoff-live`'s `MathDelegate`).
- Image inline rendering.
- Table styled rendering (block, not editable in this leaf v0.2; editable later if pressure justifies).
- Callout box rendering.
- Promotion of `InlineHighlighter` to `markoff-core` if drift cost between styled + live exceeds the cost of generalising the per-block model.

## 11. Risks / open questions

- **Block-format → multi-`QTextBlock` application** for fenced code: needs care to not over-format the closing fence's trailing `QTextBlock`. Test coverage in `tst_styled_block_formats`.
- **`leaveEvent` on widget reparenting / focus loss** may not fire as expected on all platforms. Hover-leave fallback: also clear on `focusOutEvent`.
- **Performance on huge documents** (10k+ blocks). Restyle pass is O(N); not benchmarked. Mitigation deferred until evidence.
- **`LinkService` lifetime** when `setLinkService(nullptr)` is called after a non-null was set: must disconnect old, restore lazy `DefaultLinkService`. Same pattern `markoff-live` handles.
- **CodeBlock background paint width** stops at line content, not full editor width. Cosmetic limitation of `QTextBlockFormat::setBackground`. Acceptable for v0; full-width tinting needs a custom `paintEvent` later.

## 12. References

- Branch posture: `CLAUDE.md` (project root) — port-first phase, foundation rebuild complete.
- Sibling leaf: `libs/markoff-source/CLAUDE.md` + `docs/specs/2026-04-29-source-widget-design.md`.
- Sibling leaf: `libs/markoff-live/CLAUDE.md`.
- Foundation: `libs/markoff-core/CLAUDE.md`.
- Engineering invariants: `docs/INVARIANTS.md`.
- Reuse references:
  - `Markoff::Live::InlineHighlighter` — exemplar for inline span → `QTextCharFormat`.
  - `Markoff::Live::LiveListModelBinding::hoverLinkAt`/`activateLinkAt` — exemplar for LinkService wiring.
  - `Markoff::SourceTextDocumentBinding` — reused as-is for text sync.
  - `Markoff::LinkService` — reused as the link-interaction contract.
- Span structure: `libs/markoff-parser/include/markoff/parser/SourceSpan.h`.
