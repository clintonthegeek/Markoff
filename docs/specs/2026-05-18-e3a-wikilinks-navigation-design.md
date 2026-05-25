# E3a — Wikilinks + navigation (design)

**Status:** spec-approved (2026-05-18) — design walkthrough complete, user signed off in full.
**Parent phase:** E3 (Obsidian affordances) — see `docs/specs/2026-05-08-e-arc-framing.md` §E3.
**Phase decomposition:** E3 is being split into focused sub-specs. E3a is the first; E3b (tags + navigation), E3c (embeds), E3d (callouts) follow in their own brainstorm/spec/plan cycles.

---

## 0. TL;DR

Wire Ctrl/Cmd+click activation for wikilinks (`[[Page]]`, `[[Page|alias]]`, `[[Page#Section]]`, `[[Page#^block-id]]`) and standard Markdown links (`[text](url)`) through the existing-but-unused `Markoff::LinkService` seam.

All Obsidian-syntax decomposition (page / section / blockRef / alias) happens **at parse time inside `markoff-parser`**. The view layer is pure dispatch: hit-test the click position against the per-block span list, read the span's structured target, build a `LinkActivation`, hand it to the `LinkService`. Consumer-policy decides what to do with the activation.

Hover under Ctrl flips the cursor to `PointingHandCursor` and emits `linkHovered` for consumer-rendered tooltips/previews. Plain click is unchanged (caret placement, autohide-reveal). Image spans (`![...]`, `![[...]]`) are explicitly out of scope — E3c picks them up.

---

## 1. Frame

### 1.1 Why now

E1 shipped wikilink/link visual styling. E2 shipped cursor-aware autohide for their delimiters. Both treat link spans as "rendered text" with no interactivity beyond cursor placement. The next minimum-viable Obsidian affordance is **navigation** — making a wikilink do something when the user clicks it.

`Markoff::LinkService` + `LinkActivation` + `LinkKind::{WikiLink,Tag,Anchor}` already exist in `markoff-core` (introduced pre-D-arc, currently unused). E3a fills in:

- the parser-side structured-target plumbing that `LinkActivation` expects,
- the Live-side hit-test that maps a click to an activation,
- the QML wiring (TapHandler/HoverHandler) that catches the click and hover,
- the `DefaultLinkService` classifier for `[[...]]`,
- a test-app demo `LinkService` proving the policy seam works end-to-end.

### 1.2 What this is not

- **Not a wikilink resolver.** Markoff does not decide what `[[Page]]` means. It hands the activation to the consumer; the consumer's policy resolves the target (notes vault lookup, file open, anchor scroll, etc.).
- **Not tag navigation.** Clicking `#tag` is E3b. `DefaultLinkService::classify` learns the `[[...]]` shape in E3a; the `#tag` shape is added by E3b alongside the tag-click hit-test.
- **Not embeds.** `![[image.png]]`, `![[Page#Section]]` (content embeds), and `![alt](url)` (image links) are E3c. E3a treats `isImage` spans as no-op for activation.
- **Not the wikilink/regular-link distinction in visuals.** E1 already paints wikilinks via `Markoff::Theme::Slot::WikiLink`. No visual change here.
- **Not a hover preview UI.** Markoff emits `linkHovered(LinkActivation, globalPos)`. The consumer renders any popover. The test-app demo just sets a status-bar message.

### 1.3 What this enables

After E3a:

- `[[Page]]` and `[text](url)` are interactive in Live: Ctrl/Cmd+click → consumer is notified with a structured `LinkActivation`.
- Consumers can resolve wikilinks however they want (file-system lookup, in-doc anchors, external services).
- E3b/E3c/E3d inherit a tested QML + C++ seam — adding tag-clicks (E3b) is "extend `classify`, add a tag-specific hit-test branch."
- The `LinkService` abstraction has its first non-test consumer; the policy seam is proven in dogfood before being relied on by tags/embeds.

---

## 2. Architecture & data flow

```
TreeSitterParser  ──► SourceSpan { …, linkTarget: LinkTarget }
                  ──► LinkInfo  { …, structured: LinkTarget }
                          │
                          ▼
              MarkoffDocument::inlineSpansFor(blockId)
                          │
                          ▼
        ┌────────────── QML delegate (UnifiedInlineTextDelegate) ──────────────┐
        │  TapHandler   { acceptedModifiers: Ctrl } ──┐                          │
        │  HoverHandler { ctrl-aware }              ──┤                          │
        └────────────────────────────────────────────┼──────────────────────────┘
                                                     ▼
                              LiveListModelBinding (hit-test)
                                  ├─ activateLinkAt(blockId, qtPos, mods)
                                  ├─ hoverLinkAt(blockId, qtPos, mods, globalPos)
                                  └─ clearLinkHover()
                                              │
                              builds LinkActivation { rawText, kind,
                                  page, section, blockRef, alias,
                                  resolvedTarget, modifiers, fromContext }
                                              │
                                              ▼
                                  LinkService::activate(LinkActivation)
                                              │
                                  emits linkActivated(LinkActivation)
                                              │
                                              ▼
                                  Consumer policy (test app, host)
```

**Two seams.**

- **Parser → span.** Structured target baked at parse time. View never re-parses Obsidian syntax.
- **Live → consumer.** `LinkService` signal. Already exists; E3a fills in callers.

**Edit-time invariant.** Click hit-test runs against `inlineSpansFor(blockId)` which is cached per `(BlockId, blockEditSequence)`. After a typing keystroke, the cache invalidates → next click sees fresh targets. No new caching layer.

**Cursor placement stays plain-click's job.** Ctrl-modifier on `TapHandler` means `TextEdit`'s plain-click caret placement is unaffected. No re-entrance with E2 autohide-reveal.

---

## 3. Parser-side design (`libs/markoff-parser`)

### 3.1 `LinkTarget` struct

New header `include/markoff/parser/LinkTarget.h`:

```cpp
namespace Markoff {
struct LinkTarget {
    QString url;       // [text](url) — full URL; empty for wikilinks
    QString page;      // [[Page]] / ![[Page]] — page name
    QString section;   // [[Page#Section]] — heading anchor (no '#')
    QString blockRef;  // [[Page#^id]] — block-id ref (no '^')
    QString alias;     // [[Page|alias]] — display alias

    bool isEmpty() const noexcept { return url.isEmpty() && page.isEmpty(); }
    bool operator==(const LinkTarget &) const = default;
};
}
Q_DECLARE_METATYPE(Markoff::LinkTarget)
```

### 3.2 `SourceSpan` extension

`SourceSpan::linkTarget` (new field, default-constructed). Populated by `TreeSitterParser` for spans where `isLink || isWikilink`. `operator==` extended to include `linkTarget`.

Memory cost: ~5 default QStrings per span (~120 bytes). Acceptable.

### 3.3 `LinkInfo` enrichment

`LinkInfo::structured` (new `LinkTarget` field). Existing `target` and `displayText` fields preserved verbatim — no consumer breakage.

### 3.4 Decomposition site

One free function, `Markoff::Detail::decomposeWikilinkInner(QStringView inner) → LinkTarget`, in a parser-internal header (`src/WikilinkDecomposition.h`).

| Input form                  | Result                                       |
|-----------------------------|----------------------------------------------|
| `Page`                      | `page="Page"`                                |
| `Page\|Alias text`          | `page="Page"`, `alias="Alias text"`          |
| `Page#Section`              | `page="Page"`, `section="Section"`           |
| `Page#Section\|Alias`       | `page="Page"`, `section="Section"`, `alias="Alias"` |
| `Page#^abc123`              | `page="Page"`, `blockRef="abc123"`           |
| `Page#^abc123\|Alias`       | `page="Page"`, `blockRef="abc123"`, `alias="Alias"` |
| `#Section`                  | `section="Section"` (same-document anchor)   |
| `#^abc123`                  | `blockRef="abc123"` (same-document block)    |
| `image.png` (embed inner)   | `page="image.png"` (embed-vs-link distinction stays on `LinkInfo::type`) |

Algorithm:

1. Split on `|` (first occurrence) → left = ref, right = alias.
2. Split ref on `#` (first occurrence) → left = page, right = anchor.
3. If anchor starts with `^` → blockRef = anchor.mid(1). Else → section = anchor.
4. Empty page side allowed.

### 3.5 `TreeSitterParser` integration

Three populate sites:

1. **Wikilink span emission** (already gated on `isWikilink`, near `TreeSitterParser.cpp:782`) — call `decomposeWikilinkInner` on the inner text the parser already extracts.
2. **Standard `[text](url)` link span emission** — set `linkTarget.url` directly from the `link_destination` node text.
3. **`LinkInfo` build path** (near `Document.cpp:228` / parser link query) — same helper called once per link.

### 3.6 No grammar work

Tree-sitter already produces `wiki_link` nodes (`TreeSitterParser.cpp:282`). Decomposition is post-AST in C++. No `tree-sitter-markdown` extension needed for E3a.

---

## 4. Core-layer design (`libs/markoff-core`)

### 4.1 `LinkActivation` extension

```cpp
struct MARKOFF_CORE_EXPORT LinkActivation {
    // Existing fields (preserved):
    QString  rawText;
    QUrl     resolvedTarget;
    LinkKind kind = LinkKind::Unknown;
    QString  anchorHint;
    QString  fromContext;

    // NEW — populated for WikiLink + Standard link kinds:
    QString  page;
    QString  section;
    QString  blockRef;
    QString  alias;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
};
```

`anchorHint` is kept as a duplicate of `section` for back-compat with the one existing test (`tst_foundation_link_service`). New code prefers the structured fields. A future cleanup can deprecate `anchorHint` once no consumer reads it.

### 4.2 `DefaultLinkService::classify` extension

```cpp
LinkKind DefaultLinkService::classify(const QString &t) const {
    if (t.startsWith(u"http://",  Qt::CaseInsensitive)) return LinkKind::External;
    if (t.startsWith(u"https://", Qt::CaseInsensitive)) return LinkKind::External;
    if (t.startsWith(u"mailto:",  Qt::CaseInsensitive)) return LinkKind::External;
    if (t.startsWith(u"[[") && t.endsWith(u"]]"))        return LinkKind::WikiLink;
    if (t.startsWith(u"#"))                              return LinkKind::Tag;  // dormant until E3b
    return LinkKind::Unknown;
}
```

`resolve` unchanged. Wikilink resolution stays consumer-policy (returns empty `QUrl{}` for WikiLink and Tag).

### 4.3 No new types in `markoff-core`

`LinkTarget` is parser-side; `LinkActivation` is core-side. The helper that builds a `LinkActivation` from a `LinkTarget` lives in `markoff-live` (where the hit-test runs); no need to drag spans into core.

---

## 5. Live-layer design (`libs/markoff-live`)

### 5.1 `LiveListModelBinding` extensions

```cpp
class MARKOFF_LIVE_EXPORT LiveListModelBinding : public QObject {
    Q_OBJECT
    // ... existing ...
public:
    Markoff::LinkService *linkService() const;        // never null
    void setLinkService(Markoff::LinkService *);      // non-owning; nullptr resets to default
    void setFromContext(const QString &);             // host passes current document path

    Q_INVOKABLE void activateLinkAt(const QString &blockId, int qtPos, int modifiers);
    Q_INVOKABLE bool hoverLinkAt(const QString &blockId, int qtPos, int modifiers,
                                 const QPoint &globalPos);  // returns true if hit
    Q_INVOKABLE void clearLinkHover();

Q_SIGNALS:
    void linkServiceChanged();

private:
    std::unique_ptr<Markoff::DefaultLinkService> m_defaultLinkService;
    Markoff::LinkService *m_linkService = nullptr;  // points at default or host-injected
    QString m_fromContext;
    QString m_currentHoveredRawText;  // tracks hover transitions for linkHoverLeft emission
};
```

Default-constructed binding owns a `DefaultLinkService`; `m_linkService` points at it. `setLinkService(host)` swaps the pointer without taking ownership. `setLinkService(nullptr)` resets to default.

### 5.2 Hit-test + activation builder

Private helpers in `src/LiveListModelBinding_links.cpp` (new file — keeps the already-large `LiveListModelBinding.cpp` lean):

```cpp
struct LinkHit { bool found = false; Markoff::SourceSpan span; };

LinkHit findLinkSpanAt(MarkoffDocument *doc, BlockId blockId, int qtPos) {
    if (!doc) return {};
    const QList<SourceSpan> spans = doc->inlineSpansFor(blockId);
    for (const auto &s : spans) {
        if (!(s.isLink || s.isWikilink)) continue;
        if (qtPos >= s.charOffset && qtPos < s.charOffset + s.charLength)
            return { true, s };
    }
    return {};
}
```

The activation builder reads `span.linkTarget` directly — no string parsing in Live.

### 5.3 Q_INVOKABLE implementations

`activateLinkAt`: find span → build activation → `linkService->activate(...)`.

`hoverLinkAt`: find span → build activation → if `rawText` changed from previous hover, emit `linkHoverLeft` for old + `notifyHover` for new. Return bool so QML can flip the cursor.

`clearLinkHover`: emit `linkHoverLeft` for the current rawText (if any), clear state.

### 5.4 QML wiring (`UnifiedInlineTextDelegate.qml`)

Post-tier-3, `UnifiedInlineTextDelegate` is the single delegate that hosts inline-formatted text (paragraphs / headings / blockquotes / list-items all route through it). One TapHandler + one HoverHandler covers them all.

```qml
TextEdit {
    id: edit
    // ... existing properties ...

    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: Qt.ControlModifier
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: (eventPoint) => {
            const qtPos = edit.positionAt(eventPoint.position.x, eventPoint.position.y);
            modelBinding.activateLinkAt(record.blockId, qtPos, eventPoint.modifiers);
        }
    }

    HoverHandler {
        id: linkHover
        cursorShape: ctrlActive && currentHit ? Qt.PointingHandCursor : Qt.IBeamCursor
        property bool ctrlActive: false
        property bool currentHit: false

        onPointChanged: {
            if (!hovered) { currentHit = false; modelBinding.clearLinkHover(); return; }
            ctrlActive = (point.modifiers & Qt.ControlModifier) !== 0;
            if (!ctrlActive) { currentHit = false; modelBinding.clearLinkHover(); return; }
            const qtPos = edit.positionAt(point.position.x, point.position.y);
            const globalPt = edit.mapToGlobal(point.position.x, point.position.y);
            currentHit = modelBinding.hoverLinkAt(record.blockId, qtPos, point.modifiers,
                                                  Qt.point(globalPt.x, globalPt.y));
        }
        onHoveredChanged: { if (!hovered) modelBinding.clearLinkHover(); }
    }
}
```

Plain click goes to `TextEdit`'s built-in caret placement (untouched). Ctrl+click is captured by `TapHandler` (`gesturePolicy: ReleaseWithinBounds` consumes the event) and does *not* move the caret. E2 autohide-reveal is driven by caret position, so Ctrl+click leaves the autohide state alone.

### 5.5 Files touched

- `libs/markoff-live/include/markoff/live/LiveListModelBinding.h` — new accessors + member.
- `libs/markoff-live/src/LiveListModelBinding.cpp` — initialise default service; route activate/hover.
- `libs/markoff-live/src/LiveListModelBinding_links.cpp` *(new)* — hit-test + builder helpers.
- `libs/markoff-live/qml/UnifiedInlineTextDelegate.qml` — TapHandler + HoverHandler.
- `libs/markoff-live/CMakeLists.txt` — add the new `.cpp`.

---

## 6. Testing strategy

### 6.1 Parser layer

| Test binary | Slots | Purpose |
|---|---|---|
| `tst_link_target_decomposition` *(new)* | ~10 | Pure `decomposeWikilinkInner` — table-driven, no Qt window. One slot per row of §3.4. |
| `tst_inline_spans_link_target` *(new)* | ~6 | Parse fixtures with each wikilink form + `[text](url)`; assert spans carry the expected `linkTarget`. |
| Existing `tst_document_links` (extend) | +3 | `Document::wikiLinks()` returns enriched `LinkInfo`s. Span equality respects `linkTarget`. |

### 6.2 Core layer

| Test binary | Slots | Purpose |
|---|---|---|
| `tst_foundation_link_service` (extend) | +3 | `DefaultLinkService::classify("[[X]]") == WikiLink`. `LinkActivation` round-trips via `QVariant`. Hover/activate signal payloads preserve structured fields. |

### 6.3 Live layer

| Test binary | Slots | Purpose |
|---|---|---|
| `tst_live_link_activation` *(new, C++)* | ~5 | Drive `LiveListModelBinding::activateLinkAt` directly with synthetic blocks; assert the right `LinkActivation` reaches a test `LinkService` (signal spy). Covers all four wikilink sub-forms; standard link; click outside link → no-op; click on `isImage` span → no-op (E3c turns this on); modifier bits round-trip. |
| `tst_live_link_hover` *(new, C++)* | ~4 | Hover transitions: enter → `linkHovered`; move within same span → no second `linkHovered`; move to different link → `linkHoverLeft` then `linkHovered`; leave entirely → `linkHoverLeft`. |
| `tst_live_link_qml_integration` *(new, QML fixture)* | ~3 | **Production-callsite test** per invariant #5. Uses `QmlIntegrationFixture` to mount `UnifiedInlineTextDelegate`; sends `QTest::mouseClick` with `Qt::ControlModifier` over a wikilink span; asserts the registered test `LinkService` got the activation. Also: plain click on link does **not** activate, only places caret. |

### 6.4 Reusable `RecordingLinkService`

Lives under `libs/markoff-live/tests/support/RecordingLinkService.{h,cpp}`:

```cpp
class RecordingLinkService : public Markoff::LinkService {
    Q_OBJECT
public:
    QList<Markoff::LinkActivation> activations;
    QList<Markoff::LinkActivation> hovers;
    QList<QString>                 hoverLefts;
    Markoff::LinkKind classify(const QString &) const override;  // mirrors Default
    QUrl resolve(const QString &, const QString &) const override { return {}; }
    void activate(const Markoff::LinkActivation &a) override
        { activations.append(a); Markoff::LinkService::activate(a); }
    void notifyHover(const Markoff::LinkActivation &a, const QPoint &p) override
        { hovers.append(a); Markoff::LinkService::notifyHover(a, p); }
    void notifyHoverLeft(const QString &t) override
        { hoverLefts.append(t); Markoff::LinkService::notifyHoverLeft(t); }
};
```

### 6.5 Falsifiability proof

For `tst_live_link_qml_integration`: stub `activateLinkAt` to do nothing; assert all three slots fail. Commit/revert pair recorded in history per invariant #4.

### 6.6 Regression baseline

Current 208/211 fast tests pass; the three pre-existing failures (setext S1, structured-paste SIGABRT, source-editor cursor round-trip) are unrelated to link plumbing. E3a must leave them at three.

---

## 7. Test-app demo (`apps/markoff-live-app`)

New class `MarkdownLinkService` (~80 LoC) installed at startup:

```cpp
class MarkdownLinkService : public Markoff::LinkService {
    Q_OBJECT
public:
    Markoff::LinkKind classify(const QString &t) const override;
    QUrl resolve(const QString &t, const QString &fromCtx) const override;
    void activate(const Markoff::LinkActivation &a) override {
        qInfo() << "Link activated:" << a.rawText << "kind=" << int(a.kind)
                << "page=" << a.page << "section=" << a.section
                << "blockRef=" << a.blockRef << "alias=" << a.alias
                << "modifiers=" << a.modifiers;
        if (a.kind == Markoff::LinkKind::WikiLink) {
            QFileInfo here(a.fromContext);
            QFileInfo target(here.dir(), a.page + QStringLiteral(".md"));
            if (target.exists())
                Q_EMIT openRequested(target.absoluteFilePath(), a.section, a.blockRef);
            else
                qInfo() << "  (would open" << target.absoluteFilePath() << "but not found)";
        } else {
            QDesktopServices::openUrl(a.resolvedTarget.isEmpty()
                ? QUrl(a.rawText) : a.resolvedTarget);
        }
    }
    void notifyHover(const Markoff::LinkActivation &a, const QPoint &) override {
        Q_EMIT statusMessage(tr("Ctrl+click to open: %1").arg(
            a.page.isEmpty() ? a.rawText : a.page));
    }
    void notifyHoverLeft(const QString &) override { Q_EMIT statusMessage({}); }
Q_SIGNALS:
    void openRequested(const QString &path, const QString &section, const QString &blockRef);
    void statusMessage(const QString &);
};
```

Main window wires `openRequested` to load the new file in-place, calls `binding.setFromContext(currentFilePath())` whenever a doc loads, and shows `statusMessage` in a `QStatusBar`. Validates the policy seam end-to-end without dragging notes-vault concepts into Markoff.

---

## 8. Open questions — resolved at design time

1. **Same-doc anchors (`[[#Section]]`, `[[#^id]]`).** Activation contract: `page` is empty; `section` or `blockRef` is set. Test app's demo policy: scroll to the matching heading (via `Document::headings()`) or to the row whose `BlockRecord` carries the matching block-id attr. Tests cover the activation contract; in-doc scroll is demo-only and lives in the test app, not in Live. (Block-id-attr plumbing through to the model may itself be a small follow-up if not already present — to be confirmed at plan time.)

2. **Image spans (`![alt](url)`, `![[image.png]]`).** Out of scope for E3a. `isImage` spans are no-op for `activateLinkAt`. E3c (embeds) takes them.

3. **`setLinkService(nullptr)` mid-hover.** `m_currentHoveredRawText` is cleared without a `linkHoverLeft` to the old service. Acceptable — the old service is being replaced. Documented in the header comment for `setLinkService`.

4. **Click on a wikilink span when the caret is inside it (autohide-reveal mode).** Plain click stays plain-click (caret placement). Ctrl+click activates. Autohide state untouched. Test slot in `tst_live_link_qml_integration` covers this.

5. **`Qt.ControlModifier` on macOS.** Maps to Cmd at the Qt event layer. No platform-specific code path needed — the literal `Qt.ControlModifier` does the right thing on all three desktop platforms.

---

## 9. Subtractability note (E-arc invariant 4.2)

A view that does not need link activation: does not call `setLinkService`, does not include the `TapHandler` / `HoverHandler` in its delegate, pays no runtime cost beyond `LiveListModelBinding`'s default-allocated `DefaultLinkService` (~tens of bytes).

If a view needs link activation but with a different gesture (e.g. plain click in a read-only context), it overrides the QML handler. The C++ Q_INVOKABLEs are gesture-agnostic.

If a view needs activation but not hover, it omits the `HoverHandler` — `clearLinkHover` is idempotent.

The one unconditional cost (`DefaultLinkService` allocation) is the recipe seam for E6 distillation: "default-service ownership at binding construction" goes on the recipe sheet as either "always-pay" or "opt-in via factory."

---

## 10. Discipline check (`docs/INVARIANTS.md`)

- **L4 (block-content authority).** Unchanged — clicks are read-only on the document.
- **New authority retires old.** N/A — no prior link-click code in Live to retire.
- **`Qt.callLater` / `QTimer::singleShot(0)` / re-entrance guards.** None introduced. `m_currentHoveredRawText` is a tracking variable for emit deduplication, not a re-entrance guard.
- **Production callsite tested, not synonym.** `tst_live_link_qml_integration` drives `TapHandler.onTapped` through `QmlIntegrationFixture` + `QTest::mouseClick`. Direct-call tests (`tst_live_link_activation`) cover the contract; the QML test covers the wiring per invariant #5.

---

## 11. Phase board updates

- `docs/e-arc/e-arc-status.md` Phase board: E3 row currently `pending`. After E3a lands, add E3a row `complete` and keep parent E3 row `pending` until all sub-phases ship.
- Recent-changes log entry on E3a tag.
- Tag: `v0.7.0-e3a` after dogfood signoff.

---

## 12. References

- Parent framing: `docs/specs/2026-05-08-e-arc-framing.md` §E3.
- Roadmap: `docs/e-arc/2026-05-08-e-arc-roadmap.md`.
- Status board: `docs/e-arc/e-arc-status.md`.
- Existing seam types: `libs/markoff-core/include/markoff/core/{LinkService,LinkActivation,LinkKind}.h`, `libs/markoff-core/src/{LinkService,DefaultLinkService}.cpp`.
- Existing visual treatment: E1 spec (`docs/specs/2026-05-08-e1-inline-highlighter-design.md`), E2 spec (`docs/specs/2026-05-08-e2-cursor-aware-view-design.md`).
- Parser link plumbing: `libs/markoff-parser/src/TreeSitterParser.cpp` (`isWikilink` emission near :282 and :782), `libs/markoff-parser/src/Document.cpp::wikiLinks()` (:228).
- Engineering discipline: `docs/INVARIANTS.md`.
