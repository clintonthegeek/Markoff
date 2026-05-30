# Design — `Markoff::Styled::DocumentRenderer` (headless read-only renderer)

**Date:** 2026-05-29
**Status:** Approved (brainstorm), pending plan + implementation
**Branch:** `feature/styled-document-renderer`
**Driver:** Corbomite port pressure —
[`Corbomite:docs/handoff/2026-05-29-to-markoff-styled-document-renderer.md`](../../../Corbomite/docs/handoff/2026-05-29-to-markoff-styled-document-renderer.md)
(refreshes the broader 2026-05-28 steer).

---

## 0. Summary

Factor a **headless, read-only `Markoff::Styled::DocumentRenderer`** out of the
existing `StyleApplier` so downstream consumers (Corbomite canvas cards, hover
popovers, a reading-mode leaf) can render Markoff content into a
caller-owned `QTextDocument`, or measure/paint it for a `QGraphicsItem`,
**without hosting a `QTextEdit`**.

Two tiers, both in this spec:

- **T1** — `renderInto(QTextDocument*, const MarkoffDocument*)` plus a
  raw-bytes convenience overload. Populates a caller-owned document.
- **T2** — `idealHeight(source, width)` + `paint(painter, rect, source)`.
  Convenience one-shots built over T1.

This is a **rendering-only** ask. No cursor, no selection, no editing, no
find, no delimiter-hide, no new block-kind coverage, no QML. It realizes the
"replace the live highlighter with a one-pass static render" path our own
E-arc framing already names
([`docs/specs/2026-05-08-e-arc-framing.md`](2026-05-08-e-arc-framing.md)),
for a real consumer.

It also (a) **retires** Corbomite's prior `Capabilities::Editable=false`
read-only-Live ask (reading mode now rides read-only styled), and
(b) unblocks the submodule re-pin Corbomite flagged (they are 79 commits
behind and will advance the pin to a master commit containing this renderer).

---

## 1. Why this is mostly repackaging

`StyleApplier` is already ~98% headless against a `QTextDocument`:

- Its constructor takes only a `QObject* parent`. Its working set is
  `QTextDocument*` + `MarkoffDocument*` + `const Theme*` + `fontScale`
  (`StyleApplier.h:80-90`).
- The **only** widget reach in the whole format pass is optional scrollbar
  capture/restore via `m_textEdit`, documented as optional and no-op when
  null (`StyleApplier.cpp:447-450, 692-697`).
- The per-kind format helpers (`applyHeading`, `applyParagraph`,
  `applyCodeBlock`, `applyBlockquote`, `applyListItem`, `applyHorizontalRule`,
  `charFormatForSpan`, `manageListMembership`, `computeBlockHash`,
  `inferKindFromPrefix`) already live as **free functions** in the anonymous
  namespace of `StyleApplier.cpp` — they relocate wholesale.

The "welding to `QTextEdit`" the handoff describes is really at the **Editor**
layer: `Editor` owns the `QTextEdit`, seeds the `QTextDocument`'s *text* (via
`SourceTextDocumentBinding`'s reverse path), and hands `textEdit->document()`
to `StyleApplier`, which overlays only *formats*.

---

## 2. Architecture — three pieces in `libs/markoff-styled`

### 2.1 `FormatPass` — new internal helper (NOT exported)

`src/FormatPass.{h,cpp}`. The block-walk + format-application logic lifted out
of `StyleApplier::applyFormats`, plus the anonymous-namespace per-kind helpers.
Single source of truth for "what formats does a Markoff block produce."

```cpp
namespace Markoff::Styled::FormatPass {

struct Options {
    qreal                 fontScale = 1.0;
    const Markoff::Theme *theme     = nullptr;
    bool                  inferKind = false;  // false ⇒ no kind suggestions emitted
};

struct KindSuggestion { Markoff::BlockId id; Markoff::BlockKind newKind; };

struct Result {
    quint64                      hashSkips = 0;
    bool                         structural = false;  // block set changed vs gate
    std::vector<KindSuggestion>  kindSuggestions;     // empty unless inferKind
};

using BlockHashMap = QHash<Markoff::BlockId, quint64>;

// Applies block + inline formats to `target` from `source`. Assumes `target`
// already holds widgetFlatView() text in the single-"\n" coordinate space.
// PURE w.r.t. the model: never issues Cmd::*, never mutates `source`.
// If `gate` is non-null, per-block hash gating is applied and the map updated
// (and stale entries pruned). If null, every block is (re)formatted.
Result apply(QTextDocument *target,
             const Markoff::MarkoffDocument *source,
             const Options &opts,
             BlockHashMap *gate);

} // namespace Markoff::Styled::FormatPass
```

**Authority note (INVARIANTS §2/§3):** `FormatPass` introduces **no** authority
and **retires** none. The block-content / kind-change authority for the styled
leaf remains exactly where it is: `StyleApplier` is still the sole actor that
issues `Cmd::changeKind`. The change is purely mechanical — the kind-inference
*decision* (`inferKindFromPrefix`) moves into `FormatPass` as a **returned
suggestion**, and `StyleApplier` remains the only thing that *acts* on it. A
read-only renderer passes `inferKind=false`, so no suggestion is ever produced
and the model is provably untouched (reinforced by `const MarkoffDocument*`).

### 2.2 `StyleApplier` — refactored, stays internal

Public method signatures and all stateful responsibilities are **unchanged**:
d2 subscription, hash map (`m_blockHashes`), scroll capture/restore via
`m_textEdit`, deferred `applyPendingKindChanges`, re-entrancy guard. Its
`applyFormats` body shrinks to:

1. snapshot scroll (existing logic),
2. `auto r = FormatPass::apply(m_textDocument, m_markoffDocument,
   {m_fontScale, m_theme, /*inferKind=*/true}, &m_blockHashes);`
3. translate `r.kindSuggestions` into `m_pendingKindChanges` and schedule
   `applyPendingKindChanges` (existing deferred `Cmd::changeKind` path),
4. `m_hashSkipsLastPass = r.hashSkips; ++m_restyleCount;`
5. deferred scroll restore gated on `r.structural` (existing logic).

`Editor` wiring is untouched. This refactor is the accepted regression-risk
surface; the existing `tst_styled_*` suite is the guard (see §4).

### 2.3 `DocumentRenderer` — new exported public class

`include/markoff/styled/DocumentRenderer.h` + `src/DocumentRenderer.cpp`.
Stateless-per-call, read-only, **no** `QObject`, **no** signals, **no**
`QTextEdit`/`QQuickWidget` anywhere in the translation unit.

```cpp
namespace Markoff::Styled {

class MARKOFF_STYLED_EXPORT DocumentRenderer {
public:
    DocumentRenderer();
    ~DocumentRenderer();

    void setTheme(const Markoff::Theme *theme);   // non-owning, may be null
    void setFontScale(qreal s);

    // T1 — populate a caller-owned document.
    void renderInto(QTextDocument *target,
                    const Markoff::MarkoffDocument *source) const;
    void renderInto(QTextDocument *target,
                    const QByteArray &markdownUtf8) const;

    // T2 — convenience one-shots (build a transient QTextDocument over T1).
    qreal idealHeight(const Markoff::MarkoffDocument *source,
                      qreal width) const;
    void  paint(QPainter *painter, const QRectF &rect,
                const Markoff::MarkoffDocument *source) const;

private:
    const Markoff::Theme *m_theme     = nullptr;
    qreal                 m_fontScale = 1.0;
};

} // namespace Markoff::Styled
```

---

## 3. Data flow & subtleties

### 3.1 `renderInto(target, source)`

1. **Base font.** Set a deterministic base font on `target` so headless
   metrics are stable and the golden compare is meaningful:
   `target->setDefaultFont(m_theme ? m_theme->font(Theme::FontRole::Body)
   : QFont());`. (Until Theme-font wiring lands — see §5 — `QFont()` is the
   system default, identical to what the widget path inherits, so golden
   parity holds.)
2. **Seed text.** `target->setPlainText(QString::fromUtf8(
   source->widgetFlatView()));` — establishes the single-`\n` separator
   coordinate space `FormatPass` expects. *Using `flatView()` (`\n\n`) here
   would reintroduce the per-boundary drift fixed in `6b5abcc`.*
3. **Format.** `FormatPass::apply(target, source,
   {m_fontScale, m_theme, /*inferKind=*/false}, /*gate=*/nullptr);`

### 3.2 `renderInto(target, bytes)`

Construct a throwaway `MarkoffDocument doc(1);` (single-replica), call
`doc.loadFromMarkdown(bytes)`, then delegate to `renderInto(target, &doc)`.
No `Session` is required — `iterateBlocks`/`blockText`/`blockKind`/
`inlineSpansFor`/`blockAttrs` all work on a plain loaded document, and
`inlineSpansFor` parses per-block on demand and caches.

### 3.3 T2 — `idealHeight` / `paint`

Both build a transient `QTextDocument`, `renderInto` it, then:

- `idealHeight`: `doc.setTextWidth(width); return doc.size().height();`
- `paint`: `doc.setTextWidth(rect.width());` then
  `painter->translate(rect.topLeft());` and
  `doc.documentLayout()->draw(painter, ctx);` with `ctx.clip =
  QRectF(0,0,rect.width(),rect.height())` (save/restore the painter).

**Perf framing (matches the 28th handoff's own-the-doc model).** These two
`MarkoffDocument`-taking methods rebuild a `QTextDocument` per call — correct
for hover-popover sizing or a single static render, **not** for per-frame
canvas repaint. The canvas hot path is: the card owns its own
`QTextDocument`, calls `renderInto` once on content change, then paints it
(`doc.documentLayout()->draw(...)`) and measures it (`doc.size()`) directly
each frame. Those are trivial Qt calls on the doc the consumer owns; we do
not wrap them. The spec deliberately keeps T2 thin and points cards at
own-the-doc + T1 for the hot loop.

### 3.4 `const`-correctness

`DocumentRenderer` honors `const MarkoffDocument*` end-to-end — this enforces
the read-only contract at the type level. All consumed accessors
(`iterateBlocks`, `blockKind`, `blockText`, `inlineSpansFor`, `blockAttrs`,
`widgetFlatView`) are `const`. **Implementation gate:** confirm `const`-ness at
the first TDD step; if `inlineSpansFor`'s on-demand cache is not behind a
`mutable` member (i.e. the method is non-const), resolve by making the cache
`mutable` in `markoff-core` rather than dropping `const` from the renderer
(consolidate at the core layer — consistent with the standing parser-logic
guidance).

---

## 4. Testing (falsifiable-first)

New binary `tests/tst_styled_document_renderer.cpp`:

1. **Correctness (oracle = explicit expectations).** Mirror
   `tst_styled_block_formats.cpp` against a *headless* `renderInto`'d
   document: H1..H6 descending sizes + bold, paragraph non-bold, code-block
   monospace + background, blockquote left-margin > 0, list-item marker/indent,
   HR monospace, plus inline spans (bold/italic/strike/code/link/wikilink/
   tag/footnote) from `tst_styled_inline_formats.cpp`. **Falsifiable:**
   breaking any per-kind helper in `FormatPass` fails these.
2. **Consistency (oracle = widget path).** `renderInto(headlessDoc, source)`
   vs `Editor(source).textEdit()->document()` — block + char formats equal
   block-by-block, after setting the *same* base font on both. **Falsifiable
   for the wrapper specifically:** if `renderInto` seeds `flatView()` instead
   of `widgetFlatView()`, or skips the base-font/text-seed, positions drift and
   formats mismatch. (This is the handoff's acceptance #1.)
3. **Bytes overload.** `renderInto(doc, "# H\n\nbody\n\n- a\n- b")` yields a
   non-empty document with the expected per-block kinds/formats.
4. **Graceful degradation (acceptance #3).** A table (or other unsupported
   kind) renders as source text / placeholder — non-empty, no crash.
5. **T2.** `idealHeight(source, w) > 0`; height is non-decreasing as width
   shrinks (more wrapping) for a multi-line paragraph; `paint` into a
   `QImage` of `idealHeight` height draws something (image not entirely the
   fill color) and does not crash.
6. **No-widget guarantee (acceptance #4).** `DocumentRenderer.cpp` constructs
   no `QTextEdit`/`QQuickWidget`/top-level widget — enforced by inspection
   and by the test itself instantiating only `DocumentRenderer` +
   `QTextDocument` (no widget).

**Regression guard.** The full existing `tst_styled_*` suite must stay green
after the `StyleApplier`→`FormatPass` refactor. Run via
`scripts/run-tests.sh -R styled` plus the fast inner loop
`scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`.

Per discipline rule #4, the correctness + consistency tests are written and
shown to fail against a stub `FormatPass`/`renderInto` **before** the real
implementation lands.

---

## 5. Scope boundaries (YAGNI)

**In scope:** `FormatPass` extraction, `StyleApplier` refactor onto it,
`DocumentRenderer` (T1 + T2), tests, CMake/exports, per-lib `CLAUDE.md`
update.

**Explicitly NOT in scope** (deferred; each its own future port-pressure
spec if/when Corbomite pulls on it):

- Embed / object-replacement-character hook (the 28th handoff's T1-3). The
  `FormatPass` boundary leaves clean room for it later, but no registration
  API is built now.
- New block-kind coverage: math, tables, images, callouts, mermaid, embeds —
  these graceful-degrade to source text exactly as styled does today.
- Lightweight/suspended mode, `snapshotPicture`, persistence key
  (`saveState`/`restoreState`), callout `QTextFrameFormat` treatment (28th
  handoff T2-5/6/7).
- `Capabilities::Editable` on Live — retired by this consumer; not revived
  here.

**Unchanged:** `StyleApplier`'s public API, `Editor`'s wiring and behavior,
Theme-color wiring (still hardcoded pending the separate Theme-wiring task —
`DocumentRenderer` benefits automatically once that lands, since it shares
`FormatPass`).

---

## 6. Risks

- **`StyleApplier` refactor regression** (accepted). Mitigation: behavior-
  preserving extraction; existing `tst_styled_*` suite as guard; the
  kind-suggestion plumbing keeps the deferred-`changeKind` semantics identical.
- **Coordinate-space drift** if `renderInto` seeds the wrong flat view.
  Mitigation: consistency test #2 fails loudly on drift.
- **Base-font mismatch** between headless and widget metrics. Mitigation:
  explicit `setDefaultFont`; golden test sets the same font on both sides.
- **`const` friction** at `inlineSpansFor`'s cache. Mitigation: fix at the
  core layer (`mutable` cache), not by weakening the renderer's contract.

---

## 7. Acceptance (from the handoff, restated)

1. `renderInto(doc, markoffDoc)` over all 7 supported block kinds yields a
   non-empty document whose block/char formats match the widget path. → test #2.
2. `idealHeight` monotonic-ish; `paint` into a rect of that height clips
   nothing for supported kinds. → test #5.
3. Unsupported block → source text / placeholder; never empty, never crash. →
   test #4.
4. No QML, no `QQuickWidget`, no top-level widget in the path. → test #6 +
   design (§2.3).
