# E1 — Inline-format highlighter (substantive design)

**Date:** 2026-05-08
**Branch:** `exploration/new-foundation`
**Status:** spec-approved (user pre-approved 2026-05-08 in brainstorm).
**Phase:** E-arc / E1 (active phase per `docs/e-arc/e-arc-status.md`).

**Predecessors / inputs:**

- `docs/specs/2026-05-08-e-arc-framing.md` — constitutional framing; §0.1, §2.E1, §5.1 (subtractability worked example), §5.2, §5.3.
- `docs/e-arc/2026-05-08-e-arc-roadmap.md` — orientation; phase ordering.
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` — pipeline-feature provenance; §A.7 erratum confirms `inlineSpansFor` is load-bearing E1 infrastructure.
- `docs/handoff/2026-05-08-defer-46-to-e-arc.md` — decision record for E-arc activation.
- Codebase discovery (this spec, see §0.2 amendment) — `Markoff::SourceSpan` is flag-based; `Markoff::Theme` already has the 8 inline `Slot`s; `BlockRecord::inlineSpans` already populated by the existing pipeline.
- Deleted `libs/markoff-view-qml/src/InlineFormatHighlighter.{h,cpp}` (commit `f646c90` parent) — historical reference. Used as inspiration; not ported verbatim due to D2/D3/D4 data-shape changes.

---

## 0. TL;DR

E1 makes Markoff Live render inline markdown formatting. Each text-bearing QML delegate constructs a per-delegate `Markoff::Live::InlineHighlighter` (a `QSyntaxHighlighter` subclass) bound to its `TextEdit.textDocument`. The highlighter consumes `BlockRecord::inlineSpans` (already populated; `QList<Markoff::SourceSpan>`) via `LiveBlockModel::spansAtRow(row)` and paints `QTextCharFormat` ranges driven by the existing `Markoff::Theme::Slot` palette. Markers stay visible; auto-hide is E2's work. Navigation/click contracts for link/wikilink/tag are deferred to E3.

8 inline kinds in scope: bold, italic, strikethrough, inline-code, highlight, link, wikilink, tag. No extension API in E1 — the flag-to-slot mapping is hardcoded.

Acceptance gate: 8 per-flag behavioral tests in paragraph delegate, ~6 cross-delegate sanity tests, edge-case tests for nesting + boundaries + flag-combinations, one perf benchmark `tst_live_render_inline_typing_perf` gated at <33ms p99 (60 FPS dev-hardware aspiration), dogfood pass. Tag on completion: `v0.7.0-e1`.

---

## 0.2 Amendment (2026-05-08): codebase reality vs. initial spec

The initial spec draft assumed `MarkoffParser::SourceSpan` with a `SpanKind` enum, new `Markoff::Theme::inlineBold/...` accessors, and a fresh `tests/e-arc/` test directory. Codebase discovery found:

- **`Markoff::SourceSpan` is flag-based.** Fields are bools: `bold`, `italic`, `strikethrough`, `code`, `math`, `highlight`, `isTag`, `isLink`, `isWikilink`, `isImage`, `isFootnoteRef`, plus `isDelimiter` (E2 carrier) and `parentCharStart/End` (E2 cursor-in-parent test). Multiple flags can be set on one span (e.g., `***bold-italic***` produces a span with both `bold` and `italic` true). The highlighter combines applicable flags into one `QTextCharFormat`.
- **`Markoff::Theme` already has 8 inline `Slot`s:** `BoldEmphasis`, `ItalicEmphasis`, `StrikeEmphasis`, `InlineCode`, `Highlight`, `Link`, `WikiLink`, `Tag` (defined in `libs/markoff-core/include/markoff/core/Theme.h` lines 19–41). The Theme exposes them via `color(Slot)`, `isBold(Slot)`, `isItalic(Slot)`, `font(FontRole)`. **E1 adds no new Theme slots**; it consumes the existing ones.
- **`BlockRecord::inlineSpans` already exists** (`libs/markoff-live/include/markoff/live/BlockRecord.h` line 33) and is already populated by `LiveListModelBinding::onD2Changed` (line 241 of the .cpp) calling `MarkoffDocument::inlineSpansFor(blockId)`.
- **`LiveBlockModel::spansAtRow(int)` already exists** (`libs/markoff-live/include/markoff/live/LiveBlockModel.h` line 81). It is currently *not* `Q_INVOKABLE` and the model has *no* `InlineSpansRole`. Both are added in E1.
- **Equality short-circuit limitation.** `BlockRecord::operator==` excludes `inlineSpans` (line 38 of BlockRecord.h, with explanatory comment). The pipeline currently never emits `dataChanged` when *only* spans change. Under D2's typing flow this is a non-issue (text changes whenever spans change), but future paths (parser re-runs without text edits) would silently drop span updates. E1 adds an explicit spans-comparison in `LiveBlockModel::applyOps` that emits `dataChanged(row, row, [InlineSpansRole])` when spans differ regardless of operator== outcome.
- **Tests live in `libs/markoff-live/tests/`** with naming `tst_live_render_*`. The framing-doc Q1 (cumulative pack vs per-phase) resolves to **cumulative-within-`libs/markoff-live/tests/`** — E-phase tests use the prefix `tst_live_render_inline_*`, `tst_live_render_e2_*`, etc., living alongside the existing `tst_live_render_*` family.
- **Public-header path is `include/markoff/live/`** despite `libs/markoff-live/CLAUDE.md` saying `markoff/live-render/`. The CLAUDE.md text is out-of-date with the code; new headers follow the actual code convention (`#include <markoff/live/InlineHighlighter.h>`).

These are simplifications: E1 ships less new infrastructure than the initial draft assumed. The design decisions (all 8 kinds, render-only, no extension API, per-delegate `QSyntaxHighlighter`, Obsidian-conventions defaults) are unchanged.

§§2–8 below reflect the corrected reality.

---

## 1. Frame

### 1.1 Why E1 (one paragraph)

Without E1, Markoff Live renders `**bold**` as the literal string `**bold**` — no inline formatting visible. E1 adds the inline-format painting layer that turns block-broken plaintext into actually-formatted live preview. E1 is the carrier on which E2 (cursor-aware delimiter visibility) rides; the framing-doc §1 establishes E1 as the foundational E-arc phase.

### 1.2 Subtractability anchor

Per framing-doc §5.1, E1's subtractability note IS the worked example in the framing doc. §9 below restates it for the spec reader; the implementation must exit acceptance with that subtractability holding (omit / invert / replace paths real, no wired-in capture).

### 1.3 What's not in this spec

Per framing-doc §1.2 + §2.E1, and per the 2026-05-08 brainstorm decisions:

- E2 auto-hide of markers (next phase). E1 leaves `SourceSpan::isDelimiter` ignored.
- E3 navigation contracts for link/wikilink/tag (linkActivated signal shape, modifier handling, hover policy).
- E4 frontmatter / table / footnote rendering. The user observed 2026-05-08 that "footnote rendering in some naive sense already works"; the parser does emit `SourceSpan::isFootnoteRef`, so some baseline rendering may already happen. E4's spec investigates and either confirms or supplements. **E1 does NOT paint footnote refs** — `isFootnoteRef` is ignored by `formatFor` for now.
- E5 math + Mermaid. `SourceSpan::math` and `mathDisplay` are ignored by E1's `formatFor`.
- Plugin-extensible inline-kind registration (punted to E6 promotion pass per brainstorm decision).
- Mobile, accessibility-beyond-Qt-defaults, i18n-beyond-`tr()` (framing-doc §1.2 out-of-scope list).

---

## 2. Architecture

### 2.1 The class

`Markoff::Live::InlineHighlighter` lives at:

- Header: `libs/markoff-live/include/markoff/live/InlineHighlighter.h`
- Impl: `libs/markoff-live/src/InlineHighlighter.cpp`

Public surface:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/parser/SourceSpan.h>

#include <QList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace Markoff { class Theme; }

namespace Markoff::Live {

class MARKOFF_LIVE_EXPORT InlineHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit InlineHighlighter(QTextDocument *parent);
    ~InlineHighlighter() override;

    /// Spans for the bound row. Setting triggers rehighlight().
    void setInlineSpans(const QList<Markoff::SourceSpan> &spans);
    const QList<Markoff::SourceSpan> &inlineSpans() const noexcept { return m_spans; }

    /// Theme to read tokens from. Setting triggers rehighlight().
    void setTheme(const Markoff::Theme *theme);
    const Markoff::Theme *theme() const noexcept { return m_theme; }

protected:
    void highlightBlock(const QString &text) override;

private:
    QTextCharFormat formatFor(const Markoff::SourceSpan &span) const;

    QList<Markoff::SourceSpan> m_spans;
    const Markoff::Theme      *m_theme = nullptr;
};

}  // namespace Markoff::Live
```

`InlineHighlighter` is widget-internal (markoff-live, not markoff-core). E6's promotion pass decides if the pattern moves to core.

### 2.2 Construction site

Each text-bearing QML delegate that owns a `TextEdit` constructs an `InlineHighlighter` after the `TextEdit` is loaded. To make this QML-idiomatic, an `InlineHighlighterAttached` shim wraps the C++ class with QML-property semantics:

```cpp
// libs/markoff-live/include/markoff/live/InlineHighlighterAttached.h
class MARKOFF_LIVE_EXPORT InlineHighlighterAttached : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(QVariantList spans          READ spans  WRITE setSpans  NOTIFY spansChanged)
    Q_PROPERTY(const Markoff::Theme *theme READ theme  WRITE setTheme  NOTIFY themeChanged)
public:
    // ...
};
```

The QML wrapper holds an owned `InlineHighlighter` constructed against `target->textDocument()` once `target` is set. `spans` is a `QVariantList` for QML; setter converts to `QList<Markoff::SourceSpan>` and forwards to the highlighter.

QML usage in delegates:

```qml
import org.markoff.live 1.0

TextEdit {
    id: edit
    text: model.text

    InlineHighlighterAttached {
        target: edit
        spans: model.inlineSpans            // new InlineSpansRole binding
        theme: ListView.view.binding.theme  // ambient theme
    }
}
```

The same construction lands in: ParagraphDelegate, HeadingDelegate (parameterised by level for heading-1..6 — single .qml file), BlockquoteDelegate, ListItemDelegate. Future delegates added in E3/E4 follow the same pattern.

### 2.3 What text the highlighter sees

The `TextEdit.textDocument` contains the *source-faithful* block text — D2 made `model.text` source-faithful. So `**bold**` in source appears as `**bold**` in the QTextDocument's text, and `inlineSpans` carries `Markoff::SourceSpan` entries with `bold = true`, `charOffset` and `charLength` covering the entire span (markers + content).

E1's painting policy: paint the *entire span* (markers + content) with the kind's format. Bold text + the surrounding `**` both render in bold weight. This is intentional — Obsidian does the same — and is a precondition for E2's auto-hide (markers fade against a backdrop already styled).

### 2.4 highlightBlock — flag-combining painter

```cpp
void InlineHighlighter::highlightBlock(const QString &text) {
    Q_UNUSED(text);  // text is whatever the TextEdit's document holds
    if (!m_theme) return;
    for (const Markoff::SourceSpan &span : std::as_const(m_spans)) {
        const QTextCharFormat fmt = formatFor(span);
        if (fmt == QTextCharFormat()) continue;  // no flags applied
        setFormat(span.charOffset, span.charLength, fmt);
    }
}
```

`formatFor(span)` walks the span's flags and OR-combines applicable Theme slots into one format (full mapping in §4.4).

`SourceSpan::isDelimiter` is ignored in E1 (E2 will use it). Block-level flags on `SourceSpan` (`isHeading`, `isBlockquoteMarker`, etc.) are also ignored — those describe block kind, not inline rendering.

`SourceSpan::math`, `mathDisplay`, `isFootnoteRef`, `isImage`, `isCalloutMarker`, `isCodeBlockFence`, `isCodeBlockContent`, `isFrontmatter`, `isHorizontalRule`, `isBlockquote`, `isTaskMarker`, `isListMarker`, `comment` are all out-of-scope for E1 — `formatFor` returns `QTextCharFormat()` (no painting) when only those flags are set.

---

## 3. Data flow and refresh triggers

### 3.1 Read path (already wired)

`MarkoffDocument::inlineSpansFor(BlockId)` returns `QList<Markoff::SourceSpan>`, cached per `(BlockId, blockEditSequence)` by `InlineParseCache`. `LiveListModelBinding::onD2Changed` already calls this per-block and stores the result in `BlockRecord::inlineSpans` (see `libs/markoff-live/src/LiveListModelBinding.cpp:241`). This is the load-bearing E-arc infrastructure named in the §A.7 erratum on the live-binding developmental history doc.

### 3.2 Model surface additions

`LiveBlockModel` (`libs/markoff-live/include/markoff/live/LiveBlockModel.h`) gains:

1. **New role** in the `Role` enum:
   ```cpp
   InlineSpansRole = Qt::UserRole + 12,  // next available
   ```
2. **`Q_INVOKABLE` annotation** on existing `spansAtRow`:
   ```cpp
   Q_INVOKABLE QList<Markoff::SourceSpan> spansAtRow(int row) const;
   // (Returning by value rather than const-ref to make the invocation
   // QML-meta-callable; existing const-ref callers stay source-compat
   // because QList has cheap copy-on-write.)
   ```
   *Existing consumers* of the const-ref form must be checked; if any rely on lifetime-of-reference, adapt them to value-return.
3. **`data(index, role)`** handles `InlineSpansRole`:
   ```cpp
   case InlineSpansRole:
       return QVariant::fromValue(m_rows[row].inlineSpans);
   ```
4. **`roleNames()`** maps `InlineSpansRole → "inlineSpans"`.
5. **`Q_DECLARE_METATYPE(QList<Markoff::SourceSpan>)`** at the top of `SourceSpan.h` if not already present, plus `qRegisterMetaType<QList<Markoff::SourceSpan>>()` in `LiveBlockModel`'s constructor (or `MarkoffDocument`'s alongside the existing `BlockAnchor` registration), to support the `QVariant::fromValue` round-trip and queued connections.

### 3.3 Refresh trigger

The data path `text → spans → dataChanged → highlighter rehighlight` requires that the model fire `dataChanged` with `InlineSpansRole` when spans change. The current `LiveBlockModel::applyOps` Equal-op handler short-circuits on `BlockRecord::operator==` which excludes `inlineSpans`. **E1 adds an explicit spans-comparison** to `applyOps` so that span-only changes still emit:

```cpp
// In LiveBlockModel::applyOps, Equal-op branch:
const BlockRecord &cur = m_rows[row];
const BlockRecord &nxt = nextRecords[op.nextIndex];
const bool spansDiffer = (cur.inlineSpans != nxt.inlineSpans);
const bool nonSpansDiffer = (cur != nxt);  // excludes inlineSpans

if (nonSpansDiffer) {
    m_rows[row] = nxt;  // existing path
    emit dataChanged(index(row), index(row));  // existing all-roles emit
} else if (spansDiffer) {
    m_rows[row].inlineSpans = nxt.inlineSpans;
    emit dataChanged(index(row), index(row), {InlineSpansRole});
}
// (If neither differ, no emit — current behaviour.)
```

The `SourceSpan::operator==` member-by-member already exists implicitly via the struct's POD-ish shape, but if the inline pipeline finds it doesn't compile (e.g., needs explicit `==`), add the operator to `SourceSpan.h` as part of E1.

Path:

1. User types in a delegate's TextEdit.
2. `LiveEditBinding::onContentsChange` → `MarkoffDocument::d2ApplyBufferEdit`.
3. `MarkoffDocument::scheduleD2Changed` fires `d2DocumentChanged`.
4. `LiveListModelBinding::onD2Changed` rebuilds `BlockRecord`s (existing); applyOps fires `dataChanged` per §3.3 logic.
5. Delegate's QML binding `model.inlineSpans` re-evaluates, calls `InlineHighlighterAttached::setSpans`.
6. `setSpans` → `InlineHighlighter::setInlineSpans` → `QSyntaxHighlighter::rehighlight()`.
7. `highlightBlock` walks spans, paints via `setFormat`.

Rehighlight-on-spans-change is the *single* trigger; the highlighter does NOT directly listen to QTextDocument's `contentsChange` (which would double-fire under D2's model-driven flow).

### 3.4 Empty / no-spans case

If `spans` is empty (block has no inline kinds), `highlightBlock` is a no-op. The TextEdit renders the document's default formatting, correct for plain-text paragraphs.

---

## 4. Theme integration

### 4.1 Existing slots used (no new ones)

`Markoff::Theme::Slot` already includes the 8 slots E1 needs:

| Inline flag (`SourceSpan::*`) | Theme `Slot` |
|---|---|
| `bold` | `BoldEmphasis` |
| `italic` | `ItalicEmphasis` |
| `strikethrough` | `StrikeEmphasis` |
| `code` | `InlineCode` |
| `highlight` | `Highlight` |
| `isLink` | `Link` |
| `isWikilink` | `WikiLink` |
| `isTag` | `Tag` |

Theme accessors in use: `color(Slot)`, `isBold(Slot)`, `isItalic(Slot)`, `font(FontRole)` (for monospace fallback on `InlineCode`).

### 4.2 Default values

Defaults are provided by `Theme::defaultLight()` and `Theme::defaultDark()`. The E1 implementer **verifies** that these defaults produce visually-distinct rendering for each of the 8 slots; if any slot's default is missing or visually indistinct (e.g., link color same as plain text), the implementer updates the default in the same E1 commit series. Target convention is Obsidian-equivalent: bold = weight-bold, italic = italic, strike = strikethrough, inline-code = monospace + light-bg, highlight = yellow-bg, link = blue + underline, wikilink = purple + underline, tag = green text.

This verification + (if needed) default-update task lands as a focused commit in the plan.

### 4.3 Override path

E1 reuses the existing Theme override mechanism (the `setColor`/`setBold`/`setItalic` setters on `Theme`). No new theme machinery; defaults are tweakable via the existing path.

### 4.4 `formatFor(span)` mapping — flag-combining

```cpp
QTextCharFormat InlineHighlighter::formatFor(const Markoff::SourceSpan &span) const {
    if (!m_theme) return QTextCharFormat();
    QTextCharFormat fmt;
    bool any = false;

    auto applyEmphasis = [&](Markoff::Theme::Slot slot) {
        const QColor c = m_theme->color(slot);
        if (c.isValid()) fmt.setForeground(c);
        if (m_theme->isBold(slot))   fmt.setFontWeight(QFont::Bold);
        if (m_theme->isItalic(slot)) fmt.setFontItalic(true);
        any = true;
    };

    if (span.bold)          applyEmphasis(Markoff::Theme::Slot::BoldEmphasis);
    if (span.italic)        applyEmphasis(Markoff::Theme::Slot::ItalicEmphasis);
    if (span.strikethrough) {
        fmt.setFontStrikeOut(true);
        const QColor c = m_theme->color(Markoff::Theme::Slot::StrikeEmphasis);
        if (c.isValid()) fmt.setForeground(c);
        any = true;
    }
    if (span.code) {
        const QColor fg = m_theme->color(Markoff::Theme::Slot::InlineCode);
        const QColor bg = m_theme->color(Markoff::Theme::Slot::CodeBlockBackground);
        if (fg.isValid()) fmt.setForeground(fg);
        if (bg.isValid()) fmt.setBackground(bg);
        fmt.setFont(m_theme->font(Markoff::Theme::FontRole::Monospace));
        any = true;
    }
    if (span.highlight) {
        // Highlight uses background color from the slot's color() —
        // backgrounds are stored under the Slot via setColor (existing
        // convention for bg-coloured slots).
        const QColor c = m_theme->color(Markoff::Theme::Slot::Highlight);
        if (c.isValid()) fmt.setBackground(c);
        any = true;
    }
    if (span.isLink)     applyEmphasis(Markoff::Theme::Slot::Link),     fmt.setFontUnderline(true);
    if (span.isWikilink) applyEmphasis(Markoff::Theme::Slot::WikiLink), fmt.setFontUnderline(true);
    if (span.isTag)      applyEmphasis(Markoff::Theme::Slot::Tag);

    return any ? fmt : QTextCharFormat();
}
```

**Combination rules.**

- Multiple flags on one span layer: `***bold-italic***` → both `applyEmphasis(BoldEmphasis)` and `applyEmphasis(ItalicEmphasis)` run; the second's color may overwrite the first's, which is acceptable since the parser typically emits one inner span and one outer span (so colors don't fight in practice).
- `[**bold link**](url)` may parse as a link span containing a bold span. If the parser emits two separate `SourceSpan` records (one for link, one for bold-inner), each `setFormat` call paints over its range and Qt's `QTextCharFormat::merge` semantics combine them naturally.
- Highlight + link + bold all combine via separate `setFormat` calls layered by `QSyntaxHighlighter`'s default behaviour.

If `any` is false (only out-of-scope flags set: math, isFootnoteRef, isImage, isDelimiter, etc.), return default `QTextCharFormat()` so `highlightBlock` skips painting.

---

## 5. Test surface

### 5.1 Layout

Tests live in `libs/markoff-live/tests/` (existing convention). E1 introduces:

- `tests/tst_live_render_inline_per_kind.cpp` — 8 per-flag tests in paragraph harness.
- `tests/tst_live_render_inline_combined.cpp` — flag-combination tests (bold+italic, link+bold, etc.).
- `tests/tst_live_render_inline_cross_delegate.cpp` — ~6 cross-delegate sanity tests.
- `tests/tst_live_render_inline_edge_cases.cpp` — boundary, empty, malformed, marker-spanning.
- `tests/tst_live_render_inline_typing_perf.cpp` — perf benchmark.
- `tests/CMakeLists.txt` — register the 5 new test executables (mirror the existing `qt_add_executable` + `add_test` pattern).
- `tests/fixtures/inline-formats/` — markdown corpus (per-flag .md + nested + edge-case + 1000-line typing corpus).

### 5.2 Per-flag tests (8)

Each test:

1. Constructs a `Markoff::Theme` (defaultLight is fine).
2. Instantiates an `InlineHighlighter` against a `QTextDocument` populated with the test string (e.g., `"plain **bold** plain"`).
3. Constructs a `Markoff::SourceSpan` with the relevant flag set + correct `charOffset`/`charLength` covering `**bold**`.
4. Calls `setInlineSpans({span})`.
5. Iterates the `QTextDocument`'s formats and asserts the expected property at the expected `(start, length)` range. E.g. for Bold: assert `format.fontWeight() == QFont::Bold` at `[6, 8)`.

8 tests, one per flag (bold, italic, strikethrough, code, highlight, isLink, isWikilink, isTag).

### 5.3 Combined-flag tests

- Bold + italic (`***foo***` parser emits one span with both flags).
- Link containing bold (`[**foo**](url)` — multiple spans; both formats apply on the inner range).
- Strikethrough + code (`~~`code`~~`).
- ~3 more representative combinations.

### 5.4 Cross-delegate sanity tests (~6)

- bold-in-heading-2
- italic-in-blockquote
- inline-code-in-list-item
- link-in-heading-3
- tag-in-blockquote
- highlight-in-list-item

These instantiate the actual delegate harnesses (or a minimal QML scaffold) and verify the `InlineHighlighterAttached` shim renders correctly across delegate kinds.

### 5.5 Edge-case tests

- **Boundary**: span at start of block, span at end, span covering whole block.
- **Empty span**: `charLength = 0` — no-op, no crash.
- **Span longer than visible row**: visual-layout overflow doesn't break highlighting.
- **Malformed**: `**unclosed` — parser falls back; highlighter receives whatever spans the parser emits (likely none); test asserts absence of crash.
- **Marker-spanning**: span inside a list item — ListItem delegate's marker rendering must not double-paint markers.
- **No-Theme-set**: `setTheme(nullptr)` — `highlightBlock` returns without painting.
- **Out-of-scope flag-only**: span with only `isFootnoteRef = true` — no painting (E4's job).

### 5.6 Performance benchmark

`tst_live_render_inline_typing_perf`:

1. Load `tests/fixtures/inline-formats/typing-corpus-1k.md` (1000-line markdown with mixed inline kinds, ~50% blocks containing at least one span).
2. Bind a paragraph delegate harness's `InlineHighlighter` to the first paragraph.
3. Loop 100 times: simulate one-character insertion via `MarkoffDocument::d2ApplyBufferEdit`, await the `dataChanged(InlineSpansRole)` signal, capture time-to-rehighlight-complete.
4. Assert per-iteration p99 < **33ms** (CI gate; 30 FPS budget).
5. Log p50, p99 to console; aspirational target is p99 < 16ms (60 FPS dev-hardware).

If CI variance pushes p99 >33ms in noisy environments, the implementer follows the Markoff convention for benchmark-test tolerance (check existing benchmark tests in the repo if any; if none, the simple <33ms assertion is the gate).

### 5.7 Test cadence

Test files land per implementation step (see plan); all run on every ctest invocation under `libs/markoff-live/tests/`. The CMakeLists.txt entry registers each.

---

## 6. Edge cases and notes

### 6.1 ListItem delegate's marker text

ListItem delegates render the list marker (`-`, `1.`, `* `) as part of the block text. If a span starts before the marker boundary, the highlighter paints across the marker — expected and harmless (the marker is part of the source-faithful text). The ListItem delegate's marker-rendering customisation happens in the delegate, not in the highlighter.

### 6.2 Code-block content delegate

The code-block content delegate (`CodeBlockDelegate.qml`) is **not** in E1's text-bearing set. Code-block content has its own syntax-highlighting concern (governed by a CodeBlockProcessor / Kf6SyntaxHighlightService) — separate from inline-format highlighting. E1's 4 delegates: ParagraphDelegate, HeadingDelegate, BlockquoteDelegate, ListItemDelegate.

### 6.3 Math `$x^2$`, footnote `[^foo]`, image `![](url)`

`SourceSpan::math`, `mathDisplay`, `isFootnoteRef`, `isImage` are ignored by E1's `formatFor`. E5 handles math, E4 handles footnotes (note: user observed naive footnote rendering may already exist — investigate at E4 spec time), E3 may handle images alongside embeds.

### 6.4 Theme reload

If the theme is swapped at runtime (e.g., dark/light toggle), `setTheme(theme)` triggers `rehighlight()`, repainting visible delegates. Cost is bounded by the visible-delegate count.

### 6.5 Collab cursor disambiguation

E1 does not handle local-vs-peer cursor distinction — there's no cursor-aware behaviour in E1 (that's E2). The §5 framing-doc invariant "collab correctness preserved" applies trivially: E1 doesn't touch cursor state.

### 6.6 No double-paint

`QSyntaxHighlighter::rehighlight()` resets formats before each call; subsequent `setFormat` calls overlay on the cleared baseline. No accumulation across calls.

### 6.7 SourceSpan flags out of scope for E1

For documentation: `isDelimiter` (E2 carrier), `parentCharStart`/`parentCharEnd` (E2 cursor-in-parent test), `comment` (no current consumer), `isHeading`/`isBlockquote`/`isCodeBlockFence`/`isCodeBlockContent`/`isFrontmatter`/`isHorizontalRule`/`isCalloutMarker`/`isTaskMarker`/`isListMarker` (block-level metadata, not inline rendering), `isImage`/`isFootnoteRef` (E3/E4), `math`/`mathDisplay` (E5).

---

## 7. Out of scope for E1

Restated for clarity (also in framing-doc §1.2):

- E2 auto-hide of markers (E1 ignores `isDelimiter`).
- E3 link/wikilink/tag navigation contracts.
- E4 frontmatter / table / footnote rendering.
- E5 math (inline + block) and Mermaid.
- Plugin-extensible inline-kind registration (E6 promotion pass).
- Visual-regression / screenshot-diff harness.
- Mobile, deeper a11y, deeper i18n.

---

## 8. Files touched

### 8.1 New files

- `libs/markoff-live/include/markoff/live/InlineHighlighter.h`
- `libs/markoff-live/src/InlineHighlighter.cpp`
- `libs/markoff-live/include/markoff/live/InlineHighlighterAttached.h`
- `libs/markoff-live/src/InlineHighlighterAttached.cpp`
- `libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp`
- `libs/markoff-live/tests/tst_live_render_inline_combined.cpp`
- `libs/markoff-live/tests/tst_live_render_inline_cross_delegate.cpp`
- `libs/markoff-live/tests/tst_live_render_inline_edge_cases.cpp`
- `libs/markoff-live/tests/tst_live_render_inline_typing_perf.cpp`
- `libs/markoff-live/tests/fixtures/inline-formats/<flag>.md` × 8 + nested + boundary + empty + malformed + marker-spanning + typing-corpus-1k

### 8.2 Modified files

- `libs/markoff-parser/include/markoff/parser/SourceSpan.h` — add `operator==` if missing; add `Q_DECLARE_METATYPE(QList<Markoff::SourceSpan>)` (after the namespace).
- `libs/markoff-live/include/markoff/live/LiveBlockModel.h` — add `InlineSpansRole`; add `Q_INVOKABLE` to `spansAtRow`; consider value-return signature change.
- `libs/markoff-live/src/LiveBlockModel.cpp` — handle `InlineSpansRole` in `data()`; add to `roleNames()`; update `applyOps` Equal-op branch with explicit spans-comparison + `dataChanged(InlineSpansRole)` emit; `qRegisterMetaType<QList<Markoff::SourceSpan>>()` in constructor.
- `libs/markoff-core/include/markoff/core/Theme.h` — only if `Theme::defaultLight/defaultDark` defaults need updating per §4.2 verification.
- `libs/markoff-core/src/Theme.cpp` — same.
- `libs/markoff-live/CMakeLists.txt` — add `InlineHighlighter.{h,cpp}` + `InlineHighlighterAttached.{h,cpp}` to SOURCES list.
- `libs/markoff-live/tests/CMakeLists.txt` — register 5 new test executables (mirror existing `qt_add_executable` + `target_link_libraries` + `add_test` pattern).
- `libs/markoff-live/qml/delegates/ParagraphDelegate.qml` — add `InlineHighlighterAttached` block.
- `libs/markoff-live/qml/delegates/HeadingDelegate.qml` — same.
- `libs/markoff-live/qml/delegates/BlockquoteDelegate.qml` — same.
- `libs/markoff-live/qml/delegates/ListItemDelegate.qml` — same.
- `libs/markoff-live/CLAUDE.md` — paragraph note on `InlineHighlighter` + which Theme slots it consumes; also fix the out-of-date "include/markoff/live-render/" reference.
- `docs/e-arc/e-arc-status.md` — phase-board updates per cadence + recent-changes-log entries.
- `docs/e-arc/2026-05-08-e-arc-roadmap.md` — phase-summary update on completion.

### 8.3 Spec / plan files

- `docs/specs/2026-05-08-e1-inline-highlighter-design.md` (this doc)
- `docs/plans/2026-05-08-e1-inline-highlighter.md` (companion plan)

---

## 9. Subtractability note

(Per framing-doc §5.1 invariant — restated here.)

**What.** Per-delegate `QSyntaxHighlighter` painting `QTextCharFormat` ranges from `BlockRecord::inlineSpans` over the rendered text in each text-bearing delegate.

**A view that needs it links** the inline-span data path (`MarkoffDocument::inlineSpansFor` + `InlineParseCache`), the `Markoff::Live::InlineHighlighter` class, the `Markoff::Theme` slot palette for inline-kind styling, and (for E2) any per-delegate cursor watcher driving reveal/hide.

**A view that doesn't need it can:**

- **Omit entirely** for a plain-text-style view that renders raw markdown source unstyled — `InlineHighlighter` is constructed delegate-side; a delegate that doesn't construct one pays nothing.
- **Invert** for source mode — the highlighter pattern is reused but bound to monospace source rendering, with all delimiter spans always-visible instead of auto-hidden. Inline-kind styling tokens differ but the data path is shared.
- **Replace** for a Reading-style preview pane — the highlighter is replaced by a one-pass render that bakes spans into static formatting at parse time. Trade-off: no incremental update and no cursor-aware reveal.

**Recipe-violation flag.** None expected. `InlineHighlighter` is widget-internal and constructed per-delegate; consumers that don't construct it pay nothing. If implementation discovers a wired-in capture, the §5.1 cadence applies: pause at green-tree, reshape, then proceed.

---

## 10. Acceptance criteria

E1 is complete when:

1. `Markoff::Live::InlineHighlighter` exists per §2.1; `InlineHighlighterAttached` QML shim exists per §2.2.
2. 4 text-bearing delegates (paragraph, heading, blockquote, list-item) construct + bind the highlighter per §2.2.
3. `LiveBlockModel::InlineSpansRole`, `Q_INVOKABLE spansAtRow`, value-return signature, `data()`/`roleNames()` updates, and `applyOps` spans-comparison emit per §3.2 + §3.3.
4. `SourceSpan::operator==` exists (added if not already); `Q_DECLARE_METATYPE(QList<Markoff::SourceSpan>)` registered.
5. `Theme::defaultLight/defaultDark` produce visually-distinct rendering for the 8 inline slots per §4.2 verification.
6. **Per-flag tests** (§5.2) — 8 tests pass.
7. **Combined-flag tests** (§5.3) — pass.
8. **Cross-delegate sanity tests** (§5.4) — ~6 tests pass.
9. **Edge-case tests** (§5.5) — pass.
10. **Performance benchmark** (§5.6) — `tst_live_render_inline_typing_perf` runs without failing CI gate (<33ms p99).
11. **Dogfood pass** — user opens a realistic-corpus markdown via `markoff-live-app`, types in varied positions, confirms inline rendering correctness + perceived smoothness.
12. **Markoff version tag** `v0.7.0-e1` lands on the final commit (resolves audit B1 in favour of `v0.7.0-eN` per phase, with `v1.0.0-pre.1` reserved for post-E6).
13. CLAUDE.md note on `InlineHighlighter` + Theme slots + the `markoff/live/` include-path correction lands in `libs/markoff-live/CLAUDE.md`.
14. Phase-board update: e-arc-status `E1 → complete`; recent-changes log entry; roadmap §2 `E1 → complete`.

---

## 11. See also

- `docs/specs/2026-05-08-e-arc-framing.md` — constitutional framing.
- `docs/e-arc/2026-05-08-e-arc-roadmap.md` — orientation.
- `docs/e-arc/e-arc-status.md` — live status board.
- `docs/handoff/2026-05-08-defer-46-to-e-arc.md` — E-arc activation decision record.
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` — pipeline-feature provenance (esp. §A.7 erratum).
- `docs/plans/2026-05-08-e1-inline-highlighter.md` — implementation plan (companion).
