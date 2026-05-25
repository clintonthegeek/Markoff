# E3a — Wikilinks + navigation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire Ctrl/Cmd+click activation for `[[wikilinks]]` and `[text](url)` links through the existing `Markoff::LinkService` seam, with all Obsidian-syntax decomposition (page/section/blockRef/alias) baked at parse time on `SourceSpan::linkTarget`.

**Architecture:** Parser decomposes wikilink inner text into a `LinkTarget` struct attached to each link span. Live's `LiveListModelBinding` hit-tests click positions against per-block spans, builds a `LinkActivation`, and hands it to a swappable `LinkService`. QML delegates emit Ctrl-modifier click/hover through new Q_INVOKABLEs. Test app installs a demo `LinkService` proving the policy seam.

**Tech Stack:** Qt6.8+ (Widgets, QML, Test), C++20, CMake 3.19+, tree-sitter (parser already integrated), CollabText CRDT (untouched).

**Spec:** `docs/specs/2026-05-18-e3a-wikilinks-navigation-design.md`. Read it before starting.

**Conventions:**
- Build: `cmake --build build-dev --target <target> -j 8` (never bare `-j`).
- Tests: `scripts/run-tests.sh --bin <test_binary>` (defaults to `QT_QPA_PLATFORM=offscreen`; never run windowed tests against the user's desktop).
- SPDX header `// SPDX-License-Identifier: GPL-3.0-or-later` on every new source file.
- `tr()` for user-visible strings.
- TDD: failing test → implementation → green → commit. Frequent commits.
- Plan-time resolutions: see "Plan-time resolutions" below.

**Plan-time resolutions** (defer-from-spec decisions baked here):
- `tst_document_links` doesn't exist; the `LinkInfo` enrichment is verified in `tst_document_queries.cpp` (existing) per Task A8.
- `RecordingLinkService` lives in `libs/markoff-live/tests/` directly (no `support/` subdir — matches `QmlIntegrationFixture` convention).
- Test app for the demo is `libs/markoff-live/app/` (target `markoff-live-app`), not a separate `apps/markoff-live-app/`.
- `Qt.point(...)` use in QML for the globalPos argument; mapToGlobal returns a point.
- Where `Cmd::changeKind`-style commands or document mutation are touched, they are not — E3a is read-only on the document.

---

## Phase A — Parser-side foundation

### Task A1: Create `LinkTarget` struct

**Files:**
- Create: `libs/markoff-parser/include/markoff/parser/LinkTarget.h`
- Test: `libs/markoff-parser/tests/tst_link_target_decomposition.cpp` (created in Task A2)

- [ ] **Step 1: Create the header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_PARSER_LINK_TARGET_H
#define MARKOFF_PARSER_LINK_TARGET_H

#include <QMetaType>
#include <QString>

namespace Markoff {

/// Structured link target. Populated by the parser at parse time for
/// link/wikilink SourceSpans and LinkInfos. Consumers read the
/// pre-decomposed fields directly; no string-parsing in view layers.
///
/// For [text](url):     url is set; all other fields empty.
/// For [[Page]]:        page is set.
/// For [[Page|Alias]]:  page + alias.
/// For [[Page#Section]]: page + section.
/// For [[Page#^id]]:    page + blockRef (no leading '^').
/// For [[#Section]]:    section only (same-document anchor).
/// For [[#^id]]:        blockRef only (same-document block).
struct LinkTarget {
    QString url;
    QString page;
    QString section;
    QString blockRef;
    QString alias;

    bool isEmpty() const noexcept {
        return url.isEmpty() && page.isEmpty() && section.isEmpty()
            && blockRef.isEmpty() && alias.isEmpty();
    }

    bool operator==(const LinkTarget &) const = default;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::LinkTarget)

#endif  // MARKOFF_PARSER_LINK_TARGET_H
```

- [ ] **Step 2: Build to verify it compiles**

```bash
cmake --build build-dev --target markoff-parser -j 8
```

Expected: success (no consumers yet).

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-parser/include/markoff/parser/LinkTarget.h
git commit -m "feat(parser): LinkTarget struct for structured link payloads"
```

---

### Task A2: `decomposeWikilinkInner` helper + table-driven tests

**Files:**
- Create: `libs/markoff-parser/src/WikilinkDecomposition.h`
- Create: `libs/markoff-parser/src/WikilinkDecomposition.cpp`
- Create: `libs/markoff-parser/tests/tst_link_target_decomposition.cpp`
- Modify: `libs/markoff-parser/CMakeLists.txt` (add WikilinkDecomposition.cpp to sources)
- Modify: `libs/markoff-parser/tests/CMakeLists.txt` (add new test binary)

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "WikilinkDecomposition.h"

#include <QTest>

using Markoff::LinkTarget;
using Markoff::Detail::decomposeWikilinkInner;

class TestLinkTargetDecomposition : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void plain_page() {
        const LinkTarget t = decomposeWikilinkInner(u"Page");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QVERIFY(t.alias.isEmpty());
        QVERIFY(t.section.isEmpty());
        QVERIFY(t.blockRef.isEmpty());
    }

    void page_with_alias() {
        const LinkTarget t = decomposeWikilinkInner(u"Page|Alias text");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QCOMPARE(t.alias, QStringLiteral("Alias text"));
    }

    void page_with_section() {
        const LinkTarget t = decomposeWikilinkInner(u"Page#Section");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QCOMPARE(t.section, QStringLiteral("Section"));
        QVERIFY(t.blockRef.isEmpty());
    }

    void page_with_section_and_alias() {
        const LinkTarget t = decomposeWikilinkInner(u"Page#Section|Alias");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QCOMPARE(t.section, QStringLiteral("Section"));
        QCOMPARE(t.alias, QStringLiteral("Alias"));
    }

    void page_with_block_ref() {
        const LinkTarget t = decomposeWikilinkInner(u"Page#^abc123");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QCOMPARE(t.blockRef, QStringLiteral("abc123"));
        QVERIFY(t.section.isEmpty());
    }

    void page_with_block_ref_and_alias() {
        const LinkTarget t = decomposeWikilinkInner(u"Page#^abc123|Alias");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QCOMPARE(t.blockRef, QStringLiteral("abc123"));
        QCOMPARE(t.alias, QStringLiteral("Alias"));
    }

    void same_doc_section() {
        const LinkTarget t = decomposeWikilinkInner(u"#Section");
        QVERIFY(t.page.isEmpty());
        QCOMPARE(t.section, QStringLiteral("Section"));
    }

    void same_doc_block_ref() {
        const LinkTarget t = decomposeWikilinkInner(u"#^abc123");
        QVERIFY(t.page.isEmpty());
        QCOMPARE(t.blockRef, QStringLiteral("abc123"));
    }

    void empty_inner_is_empty_target() {
        const LinkTarget t = decomposeWikilinkInner(u"");
        QVERIFY(t.isEmpty());
    }

    void embed_image_filename() {
        const LinkTarget t = decomposeWikilinkInner(u"image.png");
        QCOMPARE(t.page, QStringLiteral("image.png"));
    }
};

QTEST_APPLESS_MAIN(TestLinkTargetDecomposition)
#include "tst_link_target_decomposition.moc"
```

- [ ] **Step 2: Add the new test target to CMake**

In `libs/markoff-parser/tests/CMakeLists.txt`, append (mirror the existing `tst_document_queries` block; check the file for the convention):

```cmake
qt_add_executable(tst_link_target_decomposition
    tst_link_target_decomposition.cpp
)
target_link_libraries(tst_link_target_decomposition PRIVATE
    Qt6::Test
    MarkoffParser::MarkoffParser
)
target_include_directories(tst_link_target_decomposition PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME tst_link_target_decomposition
         COMMAND tst_link_target_decomposition)
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cmake --build build-dev --target tst_link_target_decomposition -j 8
```

Expected: compile error (`WikilinkDecomposition.h` missing).

- [ ] **Step 4: Create the header**

`libs/markoff-parser/src/WikilinkDecomposition.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_PARSER_WIKILINK_DECOMPOSITION_H
#define MARKOFF_PARSER_WIKILINK_DECOMPOSITION_H

#include <QStringView>

#include <markoff/parser/LinkTarget.h>

namespace Markoff::Detail {

/// Decompose the inner text of a wikilink ([[...]]) into structured
/// page/section/blockRef/alias fields per the spec table. Pure
/// function; no allocation beyond the QStrings in the returned struct.
Markoff::LinkTarget decomposeWikilinkInner(QStringView inner);

}  // namespace Markoff::Detail

#endif  // MARKOFF_PARSER_WIKILINK_DECOMPOSITION_H
```

- [ ] **Step 5: Implement the helper**

`libs/markoff-parser/src/WikilinkDecomposition.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "WikilinkDecomposition.h"

namespace Markoff::Detail {

Markoff::LinkTarget decomposeWikilinkInner(QStringView inner)
{
    Markoff::LinkTarget t;
    if (inner.isEmpty()) return t;

    // Split on first '|' → ref + alias.
    QStringView ref = inner;
    const auto pipeIdx = inner.indexOf(QLatin1Char('|'));
    if (pipeIdx >= 0) {
        ref = inner.left(pipeIdx);
        t.alias = inner.mid(pipeIdx + 1).toString();
    }

    // Split ref on first '#' → page + anchor.
    QStringView page = ref;
    QStringView anchor;
    const auto hashIdx = ref.indexOf(QLatin1Char('#'));
    if (hashIdx >= 0) {
        page = ref.left(hashIdx);
        anchor = ref.mid(hashIdx + 1);
    }
    t.page = page.toString();

    // Anchor: '^prefix' → blockRef; otherwise → section.
    if (!anchor.isEmpty()) {
        if (anchor.startsWith(QLatin1Char('^')))
            t.blockRef = anchor.mid(1).toString();
        else
            t.section = anchor.toString();
    }
    return t;
}

}  // namespace Markoff::Detail
```

- [ ] **Step 6: Add the .cpp to the library sources**

In `libs/markoff-parser/CMakeLists.txt`, find the `target_sources(MarkoffParser ...)` block (or equivalent — locate the existing source list) and add `src/WikilinkDecomposition.cpp`.

- [ ] **Step 7: Run the test to verify it passes**

```bash
cmake --build build-dev --target tst_link_target_decomposition -j 8
scripts/run-tests.sh --bin tst_link_target_decomposition
```

Expected: all 10 slots PASS.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-parser/src/WikilinkDecomposition.h \
        libs/markoff-parser/src/WikilinkDecomposition.cpp \
        libs/markoff-parser/tests/tst_link_target_decomposition.cpp \
        libs/markoff-parser/CMakeLists.txt \
        libs/markoff-parser/tests/CMakeLists.txt
git commit -m "feat(parser): decomposeWikilinkInner + table-driven tests"
```

---

### Task A3: Add `linkTarget` field to `SourceSpan`

**Files:**
- Modify: `libs/markoff-parser/include/markoff/parser/SourceSpan.h`

- [ ] **Step 1: Add the field**

Add `#include <markoff/parser/LinkTarget.h>` near the existing includes. Add the field after `parentCharEnd`:

```cpp
    int parentCharStart = -1;
    int parentCharEnd = -1;

    // Structured link payload — populated when isLink or isWikilink.
    // Empty (default-constructed) for all other spans. See LinkTarget.h.
    LinkTarget linkTarget;
```

Extend `operator==` by appending `&& linkTarget == o.linkTarget` to the existing comparison.

- [ ] **Step 2: Build to verify compilation**

```bash
cmake --build build-dev --target markoff-parser -j 8
```

Expected: success.

- [ ] **Step 3: Run the full parser test suite to confirm no regressions**

```bash
scripts/run-tests.sh -R '^tst_(link_target|inline_spans|document|tree_sitter)'
```

Expected: all pass (existing tests don't read `linkTarget`; default-empty LinkTarget compares equal to default-empty LinkTarget).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-parser/include/markoff/parser/SourceSpan.h
git commit -m "feat(parser): SourceSpan carries LinkTarget (default-empty)"
```

---

### Task A4: Populate `linkTarget` on wikilink spans

**Files:**
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp` (wikilink span emission, near :282 and the link-emission paths around :650–:780)
- Create: `libs/markoff-parser/tests/tst_inline_spans_link_target.cpp`
- Modify: `libs/markoff-parser/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`libs/markoff-parser/tests/tst_inline_spans_link_target.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/parser/Document.h>
#include <markoff/parser/SourceSpan.h>

using Markoff::Document;
using Markoff::SourceSpan;

namespace {

QList<SourceSpan> spansForFirstBlock(Document &doc) {
    const auto blocks = doc.topLevelBlocks();
    if (blocks.isEmpty()) return {};
    return doc.inlineSpansForBlockAt(blocks.first().byteOffset);
    // NOTE: if Document doesn't expose a per-block span accessor, use the
    // closest API surface — see Document.h. Adjust at execution time.
}

bool hasWikilinkSpan(const QList<SourceSpan> &spans, const Markoff::LinkTarget &want) {
    for (const auto &s : spans)
        if (s.isWikilink && s.linkTarget == want) return true;
    return false;
}

}  // namespace

class TestInlineSpansLinkTarget : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void wikilink_plain_page_target() {
        Document d = Document::fromMarkdown(QStringLiteral("See [[Page]]."));
        Markoff::LinkTarget want;
        want.page = QStringLiteral("Page");
        QVERIFY(hasWikilinkSpan(spansForFirstBlock(d), want));
    }

    void wikilink_alias_target() {
        Document d = Document::fromMarkdown(QStringLiteral("See [[Page|Alias]]."));
        Markoff::LinkTarget want;
        want.page = QStringLiteral("Page");
        want.alias = QStringLiteral("Alias");
        QVERIFY(hasWikilinkSpan(spansForFirstBlock(d), want));
    }

    void wikilink_section_target() {
        Document d = Document::fromMarkdown(QStringLiteral("See [[Page#Sec]]."));
        Markoff::LinkTarget want;
        want.page = QStringLiteral("Page");
        want.section = QStringLiteral("Sec");
        QVERIFY(hasWikilinkSpan(spansForFirstBlock(d), want));
    }

    void wikilink_block_ref_target() {
        Document d = Document::fromMarkdown(QStringLiteral("See [[Page#^abc]]."));
        Markoff::LinkTarget want;
        want.page = QStringLiteral("Page");
        want.blockRef = QStringLiteral("abc");
        QVERIFY(hasWikilinkSpan(spansForFirstBlock(d), want));
    }

    void standard_link_url() {
        Document d = Document::fromMarkdown(QStringLiteral("See [text](https://x.y)."));
        Markoff::LinkTarget want;
        want.url = QStringLiteral("https://x.y");
        const auto spans = spansForFirstBlock(d);
        bool found = false;
        for (const auto &s : spans)
            if (s.isLink && !s.isWikilink && s.linkTarget == want) { found = true; break; }
        QVERIFY(found);
    }

    void non_link_span_has_empty_target() {
        Document d = Document::fromMarkdown(QStringLiteral("Plain text."));
        for (const auto &s : spansForFirstBlock(d))
            QVERIFY(s.linkTarget.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestInlineSpansLinkTarget)
#include "tst_inline_spans_link_target.moc"
```

Add the new test binary to `libs/markoff-parser/tests/CMakeLists.txt` (mirror Task A2's stanza, with `tst_inline_spans_link_target` as the name).

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build-dev --target tst_inline_spans_link_target -j 8
scripts/run-tests.sh --bin tst_inline_spans_link_target
```

Expected: FAIL — `linkTarget` not populated; current tree-sitter parser emits wikilink spans with default-empty `linkTarget`. If `inlineSpansForBlockAt` doesn't exist, adjust to use the actual per-block accessor on `Document` (likely `inlineSpans(blockIndex)` or similar — confirm at execution time by reading `Document.h`).

- [ ] **Step 3: Wire decomposition into wikilink span emission**

In `libs/markoff-parser/src/TreeSitterParser.cpp`, add `#include "WikilinkDecomposition.h"`.

Locate the wikilink span emission site (currently around `:282` where `span.isWikilink = true;` is set, AND the wikilink-spans-from-parent-format path near `:465`, AND the `LinkInfo` builder around `:782` where `inner = isEmbed ? raw.mid(3, raw.size() - 5) : raw.mid(2, raw.size() - 4)`).

For each span emission where `isWikilink` becomes true, also set:

```cpp
span.linkTarget = Markoff::Detail::decomposeWikilinkInner(/* inner text view */);
```

The "inner text" is the wikilink content without the `[[` `]]` delimiters. Extract it from the same source range already used to determine `isWikilink`. If the inner is not directly available at every emission site, hoist a helper at the top of the wikilink-handling block that computes it once from the link node and shares to all sub-span emissions.

- [ ] **Step 4: Wire URL into standard-link span emission**

For `[text](url)` link spans (parent format with `isLink && !isWikilink`), set:

```cpp
span.linkTarget.url = /* link_destination node text */;
```

The link destination is already accessible during link parsing (used elsewhere to detect images, etc.). Confirm callsite by searching for the existing `link_destination` handling in the file.

- [ ] **Step 5: Run the test to verify it passes**

```bash
cmake --build build-dev --target tst_inline_spans_link_target -j 8
scripts/run-tests.sh --bin tst_inline_spans_link_target
```

Expected: all 6 slots PASS.

- [ ] **Step 6: Run the wider parser suite for regressions**

```bash
scripts/run-tests.sh -R '^tst_(link_target|inline_spans|document|tree_sitter|wikilink)'
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-parser/src/TreeSitterParser.cpp \
        libs/markoff-parser/tests/tst_inline_spans_link_target.cpp \
        libs/markoff-parser/tests/CMakeLists.txt
git commit -m "feat(parser): populate SourceSpan::linkTarget for wiki + standard links"
```

---

### Task A5: Enrich `LinkInfo` with structured target

**Files:**
- Modify: `libs/markoff-parser/include/markoff/parser/Document.h`
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp` (LinkInfo build path near :782)
- Modify: `libs/markoff-parser/tests/tst_document_queries.cpp`

- [ ] **Step 1: Add the field to `LinkInfo`**

In `Document.h`, extend `LinkInfo`:

```cpp
#include <markoff/parser/LinkTarget.h>

// ... existing struct ...
struct LinkInfo {
    enum Type { Standard, Wiki, Image, Embed };
    Type    type;
    QString target;
    QString displayText;
    LinkTarget structured;  // NEW — same decomposition as SourceSpan::linkTarget
    int sourceOffset;
    int sourceLength = 0;
};
```

- [ ] **Step 2: Write the failing test slot**

Add to `libs/markoff-parser/tests/tst_document_queries.cpp` (or its closest equivalent — locate the test that covers `Document::wikiLinks()` / `Document::links()`):

```cpp
void wiki_links_carry_structured_target() {
    Document d = Document::fromMarkdown(QStringLiteral("[[Page|Alias]] and [[Other#Sec]]"));
    const auto wls = d.wikiLinks();
    QCOMPARE(wls.size(), 2);

    // Order is source-order; first is [[Page|Alias]].
    QCOMPARE(wls[0].structured.page,  QStringLiteral("Page"));
    QCOMPARE(wls[0].structured.alias, QStringLiteral("Alias"));
    QCOMPARE(wls[1].structured.page,    QStringLiteral("Other"));
    QCOMPARE(wls[1].structured.section, QStringLiteral("Sec"));
}

void standard_links_carry_url_in_structured() {
    Document d = Document::fromMarkdown(QStringLiteral("[t](https://x.y)"));
    const auto links = d.links();
    bool found = false;
    for (const auto &l : links) {
        if (l.type == LinkInfo::Standard && l.structured.url == QStringLiteral("https://x.y")) {
            found = true; break;
        }
    }
    QVERIFY(found);
}
```

- [ ] **Step 3: Run to verify failure**

```bash
cmake --build build-dev --target tst_document_queries -j 8
scripts/run-tests.sh --bin tst_document_queries
```

Expected: FAIL — `structured` is default-empty.

- [ ] **Step 4: Populate `structured` in the LinkInfo build path**

In `TreeSitterParser.cpp`, locate the `LinkInfo` construction site (around `:782` per the spec — `out.type = isEmbed ? ...`). Set `out.structured = Markoff::Detail::decomposeWikilinkInner(inner)` for wikilink and embed kinds. For standard links, set `out.structured.url = /* destination */`. For image links, set `out.structured.url = /* destination */` as well (the inline-span path may already handle image differently; mirror it).

- [ ] **Step 5: Run to verify pass**

```bash
cmake --build build-dev --target tst_document_queries -j 8
scripts/run-tests.sh --bin tst_document_queries
```

Expected: PASS.

- [ ] **Step 6: Run the parser suite for regressions**

```bash
scripts/run-tests.sh -R '^tst_(link_target|inline_spans|document)'
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-parser/include/markoff/parser/Document.h \
        libs/markoff-parser/src/TreeSitterParser.cpp \
        libs/markoff-parser/tests/tst_document_queries.cpp
git commit -m "feat(parser): LinkInfo::structured carries decomposed target"
```

---

## Phase B — Core-layer wiring

### Task B1: Extend `LinkActivation`

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/LinkActivation.h`
- Modify: `libs/markoff-core/tests/tst_foundation_link_service.cpp`

- [ ] **Step 1: Write the failing test slot**

Add to `tst_foundation_link_service.cpp`:

```cpp
void activation_carries_structured_fields() {
    Markoff::LinkActivation a;
    a.rawText = QStringLiteral("[[Page|Alias]]");
    a.kind = Markoff::LinkKind::WikiLink;
    a.page = QStringLiteral("Page");
    a.alias = QStringLiteral("Alias");
    a.modifiers = Qt::ControlModifier;

    QVariant v = QVariant::fromValue(a);
    auto round = v.value<Markoff::LinkActivation>();
    QCOMPARE(round.page, QStringLiteral("Page"));
    QCOMPARE(round.alias, QStringLiteral("Alias"));
    QCOMPARE(int(round.modifiers), int(Qt::ControlModifier));
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build-dev --target tst_foundation_link_service -j 8
scripts/run-tests.sh --bin tst_foundation_link_service
```

Expected: compile error — `page` / `alias` / `modifiers` not members.

- [ ] **Step 3: Extend the struct**

In `LinkActivation.h`, add:

```cpp
#include <QtCore/qnamespace.h>  // Qt::KeyboardModifiers

struct MARKOFF_CORE_EXPORT LinkActivation {
    QString  rawText;
    QUrl     resolvedTarget;
    LinkKind kind = LinkKind::Unknown;
    QString  anchorHint;
    QString  fromContext;

    // E3a additions: structured wikilink fields + click modifiers.
    QString  page;
    QString  section;
    QString  blockRef;
    QString  alias;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
};
```

- [ ] **Step 4: Run to verify pass**

```bash
scripts/run-tests.sh --bin tst_foundation_link_service
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/include/markoff/core/LinkActivation.h \
        libs/markoff-core/tests/tst_foundation_link_service.cpp
git commit -m "feat(core): LinkActivation gets page/section/blockRef/alias/modifiers"
```

---

### Task B2: Teach `DefaultLinkService` the `[[...]]` and `#` shapes

**Files:**
- Modify: `libs/markoff-core/src/DefaultLinkService.cpp`
- Modify: `libs/markoff-core/tests/tst_foundation_link_service.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `tst_foundation_link_service.cpp`:

```cpp
void classify_recognises_wikilink_shape() {
    Markoff::DefaultLinkService svc;
    QCOMPARE(svc.classify(QStringLiteral("[[Page]]")), Markoff::LinkKind::WikiLink);
    QCOMPARE(svc.classify(QStringLiteral("[[Page|Alias]]")), Markoff::LinkKind::WikiLink);
    QCOMPARE(svc.classify(QStringLiteral("[[Page#Section]]")), Markoff::LinkKind::WikiLink);
}

void classify_recognises_tag_shape_dormant() {
    Markoff::DefaultLinkService svc;
    // Dormant for E3a — E3b will wire the click path.
    QCOMPARE(svc.classify(QStringLiteral("#tag")), Markoff::LinkKind::Tag);
}

void classify_existing_external_kinds_unchanged() {
    Markoff::DefaultLinkService svc;
    QCOMPARE(svc.classify(QStringLiteral("https://x.y")), Markoff::LinkKind::External);
    QCOMPARE(svc.classify(QStringLiteral("mailto:x@y")), Markoff::LinkKind::External);
    QCOMPARE(svc.classify(QStringLiteral("nope")), Markoff::LinkKind::Unknown);
}
```

- [ ] **Step 2: Run to verify failure**

```bash
scripts/run-tests.sh --bin tst_foundation_link_service
```

Expected: classify("[[Page]]") returns Unknown.

- [ ] **Step 3: Extend classify**

In `DefaultLinkService.cpp`:

```cpp
Markoff::LinkKind Markoff::DefaultLinkService::classify(const QString &t) const
{
    if (t.startsWith(QStringLiteral("http://"),  Qt::CaseInsensitive))  return LinkKind::External;
    if (t.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))  return LinkKind::External;
    if (t.startsWith(QStringLiteral("mailto:"),  Qt::CaseInsensitive))  return LinkKind::External;
    if (t.startsWith(QStringLiteral("[[")) && t.endsWith(QStringLiteral("]]")))
        return LinkKind::WikiLink;
    if (t.startsWith(QLatin1Char('#')))
        return LinkKind::Tag;
    return LinkKind::Unknown;
}
```

- [ ] **Step 4: Run to verify pass**

```bash
scripts/run-tests.sh --bin tst_foundation_link_service
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/DefaultLinkService.cpp \
        libs/markoff-core/tests/tst_foundation_link_service.cpp
git commit -m "feat(core): DefaultLinkService classifies [[...]] and #tag"
```

---

## Phase C — Live hit-test infrastructure

### Task C1: `RecordingLinkService` test fixture

**Files:**
- Create: `libs/markoff-live/tests/RecordingLinkService.h`
- Create: `libs/markoff-live/tests/RecordingLinkService.cpp`

- [ ] **Step 1: Write the header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_LIVE_TESTS_RECORDING_LINK_SERVICE_H
#define MARKOFF_LIVE_TESTS_RECORDING_LINK_SERVICE_H

#include <QList>
#include <QString>

#include <markoff/core/LinkActivation.h>
#include <markoff/core/LinkService.h>

namespace Markoff::LiveTest {

/// LinkService that captures every activate/notifyHover/notifyHoverLeft
/// call for assertion in tests. Classify mirrors DefaultLinkService.
class RecordingLinkService : public Markoff::LinkService {
    Q_OBJECT
public:
    QList<Markoff::LinkActivation> activations;
    QList<Markoff::LinkActivation> hovers;
    QList<QString>                 hoverLefts;

    Markoff::LinkKind classify(const QString &t) const override;
    QUrl resolve(const QString &, const QString &) const override { return {}; }
    void activate(const Markoff::LinkActivation &a) override;
    void notifyHover(const Markoff::LinkActivation &a, const QPoint &p) override;
    void notifyHoverLeft(const QString &t) override;
};

}  // namespace Markoff::LiveTest

#endif  // MARKOFF_LIVE_TESTS_RECORDING_LINK_SERVICE_H
```

- [ ] **Step 2: Write the implementation**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecordingLinkService.h"

namespace Markoff::LiveTest {

Markoff::LinkKind RecordingLinkService::classify(const QString &t) const {
    if (t.startsWith(QStringLiteral("http://"),  Qt::CaseInsensitive))  return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))  return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("mailto:"),  Qt::CaseInsensitive))  return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("[[")) && t.endsWith(QStringLiteral("]]")))
        return Markoff::LinkKind::WikiLink;
    if (t.startsWith(QLatin1Char('#')))                                 return Markoff::LinkKind::Tag;
    return Markoff::LinkKind::Unknown;
}

void RecordingLinkService::activate(const Markoff::LinkActivation &a) {
    activations.append(a);
    Markoff::LinkService::activate(a);
}

void RecordingLinkService::notifyHover(const Markoff::LinkActivation &a, const QPoint &p) {
    hovers.append(a);
    Markoff::LinkService::notifyHover(a, p);
}

void RecordingLinkService::notifyHoverLeft(const QString &t) {
    hoverLefts.append(t);
    Markoff::LinkService::notifyHoverLeft(t);
}

}  // namespace Markoff::LiveTest
```

- [ ] **Step 3: No build step here — the file is used by the next task's tests. Commit.**

```bash
git add libs/markoff-live/tests/RecordingLinkService.h \
        libs/markoff-live/tests/RecordingLinkService.cpp
git commit -m "test(live): RecordingLinkService fixture for activation/hover assertions"
```

---

### Task C2: `LiveListModelBinding` link-service ownership + accessors

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveListModelBinding.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`

- [ ] **Step 1: Add the accessors to the header**

In `LiveListModelBinding.h`, add includes:

```cpp
#include <markoff/core/LinkService.h>
#include <markoff/core/DefaultLinkService.h>
#include <memory>
```

Inside the public section:

```cpp
    Markoff::LinkService *linkService() const;
    void setLinkService(Markoff::LinkService *service);

    QString fromContext() const;
    void setFromContext(const QString &);

Q_SIGNALS:
    void linkServiceChanged();
    void fromContextChanged();
```

Inside the private section (or pimpl members — match the existing pattern; the class likely uses pimpl, so add fields to the d-pointer struct):

```cpp
    std::unique_ptr<Markoff::DefaultLinkService> m_defaultLinkService;
    Markoff::LinkService *m_linkService = nullptr;
    QString m_fromContext;
```

- [ ] **Step 2: Initialise in the constructor**

In `LiveListModelBinding.cpp`, in the constructor body (or pimpl ctor, wherever members are initialised):

```cpp
m_defaultLinkService = std::make_unique<Markoff::DefaultLinkService>();
m_linkService = m_defaultLinkService.get();
```

- [ ] **Step 3: Implement the accessors**

```cpp
Markoff::LinkService *LiveListModelBinding::linkService() const { return m_linkService; }

void LiveListModelBinding::setLinkService(Markoff::LinkService *s)
{
    Markoff::LinkService *target = s ? s : m_defaultLinkService.get();
    if (target == m_linkService) return;
    m_linkService = target;
    Q_EMIT linkServiceChanged();
}

QString LiveListModelBinding::fromContext() const { return m_fromContext; }

void LiveListModelBinding::setFromContext(const QString &v)
{
    if (m_fromContext == v) return;
    m_fromContext = v;
    Q_EMIT fromContextChanged();
}
```

- [ ] **Step 4: Build to verify**

```bash
cmake --build build-dev --target markoff-live -j 8
```

Expected: success.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveListModelBinding.h \
        libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "feat(live): LiveListModelBinding owns default LinkService + fromContext"
```

---

### Task C3: Hit-test + activation-builder helpers in new TU

**Files:**
- Create: `libs/markoff-live/src/LiveListModelBinding_links.h` (internal)
- Create: `libs/markoff-live/src/LiveListModelBinding_links.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Write the internal header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_LIVE_INTERNAL_LINK_HITTEST_H
#define MARKOFF_LIVE_INTERNAL_LINK_HITTEST_H

#include <QString>

#include <markoff/core/LinkActivation.h>
#include <markoff/parser/SourceSpan.h>

namespace Markoff {
class MarkoffDocument;
class LinkService;
}

namespace Markoff::LiveInternal {

struct LinkHit {
    bool found = false;
    Markoff::SourceSpan span;
};

/// Find the link-or-wikilink span containing qtPos in the block's spans.
/// Returns {false, {}} if no such span. blockIdStr is parsed by caller.
LinkHit findLinkSpanAt(Markoff::MarkoffDocument *doc,
                       const QString &blockIdStr, int qtPos);

/// Build a LinkActivation from a hit span, asking the service to resolve
/// non-wikilink targets.
Markoff::LinkActivation buildActivation(const Markoff::SourceSpan &span,
                                        Qt::KeyboardModifiers mods,
                                        const QString &fromContext,
                                        Markoff::LinkService *service);

}  // namespace Markoff::LiveInternal

#endif  // MARKOFF_LIVE_INTERNAL_LINK_HITTEST_H
```

- [ ] **Step 2: Write the implementation**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveListModelBinding_links.h"

#include <markoff/core/LinkKind.h>
#include <markoff/core/LinkService.h>
#include <markoff/core/MarkoffDocument.h>

namespace Markoff::LiveInternal {

LinkHit findLinkSpanAt(Markoff::MarkoffDocument *doc,
                       const QString &blockIdStr, int qtPos)
{
    if (!doc) return {};
    const Markoff::BlockId blockId{blockIdStr};
    const QList<Markoff::SourceSpan> spans = doc->inlineSpansFor(blockId);
    for (const auto &s : spans) {
        if (!(s.isLink || s.isWikilink)) continue;
        if (qtPos >= s.charOffset && qtPos < s.charOffset + s.charLength)
            return { true, s };
    }
    return {};
}

Markoff::LinkActivation buildActivation(const Markoff::SourceSpan &span,
                                        Qt::KeyboardModifiers mods,
                                        const QString &fromContext,
                                        Markoff::LinkService *service)
{
    Markoff::LinkActivation a;
    a.kind        = span.isWikilink ? Markoff::LinkKind::WikiLink
                                    : (service ? service->classify(span.linkTarget.url)
                                               : Markoff::LinkKind::Unknown);
    a.page        = span.linkTarget.page;
    a.section     = span.linkTarget.section;
    a.blockRef    = span.linkTarget.blockRef;
    a.alias       = span.linkTarget.alias;
    a.anchorHint  = a.section;
    a.modifiers   = mods;
    a.fromContext = fromContext;

    if (span.isWikilink) {
        // Reconstruct rawText from the structured fields so consumers see
        // the canonical form. (Avoids stashing the source range on the
        // span just for this.)
        QString inner = a.page;
        if (!a.section.isEmpty())  inner += QLatin1Char('#') + a.section;
        if (!a.blockRef.isEmpty()) inner += QStringLiteral("#^") + a.blockRef;
        if (!a.alias.isEmpty())    inner += QLatin1Char('|') + a.alias;
        a.rawText = QStringLiteral("[[%1]]").arg(inner);
        a.resolvedTarget = service ? service->resolve(a.rawText, fromContext) : QUrl{};
    } else {
        a.rawText        = span.linkTarget.url;
        a.resolvedTarget = service ? service->resolve(a.rawText, fromContext)
                                   : QUrl(a.rawText);
    }
    return a;
}

}  // namespace Markoff::LiveInternal
```

NOTE: confirm `MarkoffDocument::inlineSpansFor(BlockId)` signature and `BlockId{QString}` ctor at execution time by reading the header. Adjust if API differs (e.g. if BlockId is constructed via a free helper).

- [ ] **Step 3: Add to CMake**

In `libs/markoff-live/CMakeLists.txt`, append `src/LiveListModelBinding_links.cpp` to the library sources list. Verify by building:

```bash
cmake --build build-dev --target markoff-live -j 8
```

Expected: success.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/src/LiveListModelBinding_links.h \
        libs/markoff-live/src/LiveListModelBinding_links.cpp \
        libs/markoff-live/CMakeLists.txt
git commit -m "feat(live): link hit-test + activation-builder helpers (no Q_INVOKABLE yet)"
```

---

### Task C4: `Q_INVOKABLE activateLinkAt`

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveListModelBinding.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`
- Create: `libs/markoff-live/tests/tst_live_link_activation.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>

#include "RecordingLinkService.h"

using namespace Markoff;

class TestLiveLinkActivation : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void wikilink_click_dispatches_activation() {
        auto doc = std::make_unique<MarkoffDocument>();
        doc->loadFromMarkdown(QStringLiteral("See [[Page|Alias]] now."));

        LiveListModelBinding binding;
        binding.setDocument(doc.get());  // confirm setter name in header

        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        // Find the wikilink span to compute a qtPos inside it.
        const auto blocks = doc->topLevelBlockIds();  // confirm at execution time
        QVERIFY(!blocks.isEmpty());
        const auto spans = doc->inlineSpansFor(blocks.first());
        int hitPos = -1;
        for (const auto &s : spans) {
            if (s.isWikilink) { hitPos = s.charOffset + s.charLength / 2; break; }
        }
        QVERIFY(hitPos >= 0);

        binding.activateLinkAt(blocks.first().toString(), hitPos, int(Qt::ControlModifier));

        QCOMPARE(svc.activations.size(), 1);
        const auto &a = svc.activations.first();
        QCOMPARE(a.kind, LinkKind::WikiLink);
        QCOMPARE(a.page, QStringLiteral("Page"));
        QCOMPARE(a.alias, QStringLiteral("Alias"));
        QCOMPARE(int(a.modifiers), int(Qt::ControlModifier));
    }

    void click_outside_link_is_noop() {
        auto doc = std::make_unique<MarkoffDocument>();
        doc->loadFromMarkdown(QStringLiteral("plain text"));
        LiveListModelBinding binding;
        binding.setDocument(doc.get());
        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blocks = doc->topLevelBlockIds();
        binding.activateLinkAt(blocks.first().toString(), 2, int(Qt::ControlModifier));
        QCOMPARE(svc.activations.size(), 0);
    }

    void standard_link_click_dispatches_external() {
        auto doc = std::make_unique<MarkoffDocument>();
        doc->loadFromMarkdown(QStringLiteral("[text](https://x.y)"));
        LiveListModelBinding binding;
        binding.setDocument(doc.get());
        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blocks = doc->topLevelBlockIds();
        const auto spans = doc->inlineSpansFor(blocks.first());
        int hitPos = -1;
        for (const auto &s : spans) {
            if (s.isLink && !s.isWikilink) { hitPos = s.charOffset + s.charLength / 2; break; }
        }
        QVERIFY(hitPos >= 0);

        binding.activateLinkAt(blocks.first().toString(), hitPos, 0);
        QCOMPARE(svc.activations.size(), 1);
        QCOMPARE(svc.activations.first().kind, LinkKind::External);
        QCOMPARE(svc.activations.first().rawText, QStringLiteral("https://x.y"));
    }

    void image_span_is_skipped_in_e3a() {
        auto doc = std::make_unique<MarkoffDocument>();
        doc->loadFromMarkdown(QStringLiteral("![alt](img.png)"));
        LiveListModelBinding binding;
        binding.setDocument(doc.get());
        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blocks = doc->topLevelBlockIds();
        const auto spans = doc->inlineSpansFor(blocks.first());
        int hitPos = -1;
        for (const auto &s : spans) {
            if (s.isImage) { hitPos = s.charOffset + s.charLength / 2; break; }
        }
        if (hitPos < 0) QSKIP("No image span in fixture; parser shape changed.");

        binding.activateLinkAt(blocks.first().toString(), hitPos, int(Qt::ControlModifier));
        QCOMPARE(svc.activations.size(), 0);
    }
};

QTEST_MAIN(TestLiveLinkActivation)
#include "tst_live_link_activation.moc"
```

Add to `libs/markoff-live/tests/CMakeLists.txt` (mirror an existing C++ test stanza like `tst_live_render_actions_dispatch`; include `RecordingLinkService.cpp` in the target's sources):

```cmake
qt_add_executable(tst_live_link_activation
    tst_live_link_activation.cpp
    RecordingLinkService.cpp
)
target_link_libraries(tst_live_link_activation PRIVATE
    Qt6::Test
    Markoff::Live
    Markoff::Core
    MarkoffParser::MarkoffParser
)
add_test(NAME tst_live_link_activation
         COMMAND tst_live_link_activation)
```

- [ ] **Step 2: Declare `activateLinkAt` in the header**

In `LiveListModelBinding.h`, public section:

```cpp
    Q_INVOKABLE void activateLinkAt(const QString &blockId, int qtPos, int modifiers);
```

- [ ] **Step 3: Implement**

In `LiveListModelBinding.cpp`, add `#include "LiveListModelBinding_links.h"`:

```cpp
void LiveListModelBinding::activateLinkAt(const QString &blockId, int qtPos, int modifiers)
{
    const auto hit = Markoff::LiveInternal::findLinkSpanAt(document(), blockId, qtPos);
    if (!hit.found) return;
    auto a = Markoff::LiveInternal::buildActivation(
        hit.span, Qt::KeyboardModifiers(modifiers), m_fromContext, m_linkService);
    if (m_linkService) m_linkService->activate(a);
}
```

- [ ] **Step 4: Run the test to verify pass**

```bash
cmake --build build-dev --target tst_live_link_activation -j 8
scripts/run-tests.sh --bin tst_live_link_activation
```

Expected: all 4 slots PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveListModelBinding.h \
        libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/tests/tst_live_link_activation.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "feat(live): activateLinkAt Q_INVOKABLE + signal-spy tests"
```

---

### Task C5: `Q_INVOKABLE hoverLinkAt` + `clearLinkHover`

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveListModelBinding.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`
- Create: `libs/markoff-live/tests/tst_live_link_hover.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QPoint>
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>

#include "RecordingLinkService.h"

using namespace Markoff;

class TestLiveLinkHover : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void enter_link_emits_hover() {
        auto doc = std::make_unique<MarkoffDocument>();
        doc->loadFromMarkdown(QStringLiteral("See [[Page]] then [[Other]]."));
        LiveListModelBinding binding;
        binding.setDocument(doc.get());
        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blocks = doc->topLevelBlockIds();
        const auto spans = doc->inlineSpansFor(blocks.first());
        int posPage = -1, posOther = -1;
        for (const auto &s : spans) {
            if (s.isWikilink && s.linkTarget.page == QStringLiteral("Page"))
                posPage = s.charOffset + s.charLength / 2;
            if (s.isWikilink && s.linkTarget.page == QStringLiteral("Other"))
                posOther = s.charOffset + s.charLength / 2;
        }
        QVERIFY(posPage >= 0 && posOther >= 0);

        QVERIFY(binding.hoverLinkAt(blocks.first().toString(), posPage,
                                    int(Qt::ControlModifier), QPoint(0, 0)));
        QCOMPARE(svc.hovers.size(), 1);
        QCOMPARE(svc.hovers.first().page, QStringLiteral("Page"));
    }

    void same_span_repeated_hover_no_duplicate() {
        auto doc = std::make_unique<MarkoffDocument>();
        doc->loadFromMarkdown(QStringLiteral("[[Page]]"));
        LiveListModelBinding binding;
        binding.setDocument(doc.get());
        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blocks = doc->topLevelBlockIds();
        const auto spans = doc->inlineSpansFor(blocks.first());
        int pos = -1;
        for (const auto &s : spans)
            if (s.isWikilink) { pos = s.charOffset + s.charLength / 2; break; }
        QVERIFY(pos >= 0);

        binding.hoverLinkAt(blocks.first().toString(), pos,     int(Qt::ControlModifier), QPoint());
        binding.hoverLinkAt(blocks.first().toString(), pos + 1, int(Qt::ControlModifier), QPoint());
        QCOMPARE(svc.hovers.size(), 1);
        QCOMPARE(svc.hoverLefts.size(), 0);
    }

    void cross_link_hover_emits_left_then_hover() {
        auto doc = std::make_unique<MarkoffDocument>();
        doc->loadFromMarkdown(QStringLiteral("[[Page]] and [[Other]]"));
        LiveListModelBinding binding;
        binding.setDocument(doc.get());
        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blocks = doc->topLevelBlockIds();
        const auto spans = doc->inlineSpansFor(blocks.first());
        int posPage = -1, posOther = -1;
        for (const auto &s : spans) {
            if (s.isWikilink && s.linkTarget.page == QStringLiteral("Page"))
                posPage = s.charOffset + s.charLength / 2;
            if (s.isWikilink && s.linkTarget.page == QStringLiteral("Other"))
                posOther = s.charOffset + s.charLength / 2;
        }
        QVERIFY(posPage >= 0 && posOther >= 0);

        binding.hoverLinkAt(blocks.first().toString(), posPage,  int(Qt::ControlModifier), QPoint());
        binding.hoverLinkAt(blocks.first().toString(), posOther, int(Qt::ControlModifier), QPoint());

        QCOMPARE(svc.hovers.size(), 2);
        QCOMPARE(svc.hoverLefts.size(), 1);
    }

    void clear_emits_left() {
        auto doc = std::make_unique<MarkoffDocument>();
        doc->loadFromMarkdown(QStringLiteral("[[Page]]"));
        LiveListModelBinding binding;
        binding.setDocument(doc.get());
        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blocks = doc->topLevelBlockIds();
        const auto spans = doc->inlineSpansFor(blocks.first());
        int pos = -1;
        for (const auto &s : spans) if (s.isWikilink) { pos = s.charOffset + s.charLength / 2; break; }
        QVERIFY(pos >= 0);

        binding.hoverLinkAt(blocks.first().toString(), pos, int(Qt::ControlModifier), QPoint());
        binding.clearLinkHover();
        QCOMPARE(svc.hoverLefts.size(), 1);
        binding.clearLinkHover();  // idempotent
        QCOMPARE(svc.hoverLefts.size(), 1);
    }
};

QTEST_MAIN(TestLiveLinkHover)
#include "tst_live_link_hover.moc"
```

Add the binary to `libs/markoff-live/tests/CMakeLists.txt` (same pattern as Task C4; include `RecordingLinkService.cpp` in sources).

- [ ] **Step 2: Declare in the header**

```cpp
    Q_INVOKABLE bool hoverLinkAt(const QString &blockId, int qtPos, int modifiers,
                                 const QPoint &globalPos);
    Q_INVOKABLE void clearLinkHover();
```

Private member:

```cpp
    QString m_currentHoveredRawText;
```

- [ ] **Step 3: Implement**

```cpp
bool LiveListModelBinding::hoverLinkAt(const QString &blockId, int qtPos, int modifiers,
                                       const QPoint &globalPos)
{
    const auto hit = Markoff::LiveInternal::findLinkSpanAt(document(), blockId, qtPos);
    if (!hit.found) { clearLinkHover(); return false; }
    const auto a = Markoff::LiveInternal::buildActivation(
        hit.span, Qt::KeyboardModifiers(modifiers), m_fromContext, m_linkService);
    if (a.rawText != m_currentHoveredRawText) {
        if (!m_currentHoveredRawText.isEmpty() && m_linkService)
            m_linkService->notifyHoverLeft(m_currentHoveredRawText);
        if (m_linkService) m_linkService->notifyHover(a, globalPos);
        m_currentHoveredRawText = a.rawText;
    }
    return true;
}

void LiveListModelBinding::clearLinkHover()
{
    if (m_currentHoveredRawText.isEmpty()) return;
    if (m_linkService) m_linkService->notifyHoverLeft(m_currentHoveredRawText);
    m_currentHoveredRawText.clear();
}
```

- [ ] **Step 4: Run the test to verify pass**

```bash
cmake --build build-dev --target tst_live_link_hover -j 8
scripts/run-tests.sh --bin tst_live_link_hover
```

Expected: all 4 slots PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveListModelBinding.h \
        libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/tests/tst_live_link_hover.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "feat(live): hoverLinkAt / clearLinkHover with transition tracking"
```

---

## Phase D — QML integration

### Task D1: `TapHandler` for Ctrl+click activation

**Files:**
- Modify: `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml`
- Create: `libs/markoff-live/tests/tst_live_link_qml_integration.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing QML-integration test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "QmlIntegrationFixture.h"
#include "RecordingLinkService.h"

using namespace Markoff;

class TestLiveLinkQmlIntegration : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void ctrl_click_on_wikilink_dispatches_activation() {
        QmlIntegrationFixture fx;
        fx.loadMarkdown(QStringLiteral("See [[Page]] now."));

        LiveTest::RecordingLinkService svc;
        fx.binding()->setLinkService(&svc);

        // Locate the wikilink span and compute a point in window-local
        // coordinates to click. Helper details depend on fixture API.
        const QPoint clickPt = fx.scenePointAtFirstWikilink();
        QVERIFY(!clickPt.isNull());

        QTest::mouseClick(fx.window(), Qt::LeftButton, Qt::ControlModifier, clickPt);
        QTRY_COMPARE(svc.activations.size(), 1);
        QCOMPARE(svc.activations.first().page, QStringLiteral("Page"));
    }

    void plain_click_on_wikilink_does_not_activate() {
        QmlIntegrationFixture fx;
        fx.loadMarkdown(QStringLiteral("See [[Page]] now."));
        LiveTest::RecordingLinkService svc;
        fx.binding()->setLinkService(&svc);

        const QPoint clickPt = fx.scenePointAtFirstWikilink();
        QVERIFY(!clickPt.isNull());

        QTest::mouseClick(fx.window(), Qt::LeftButton, Qt::NoModifier, clickPt);
        // Allow event loop to process — should still be zero.
        QTest::qWait(50);
        QCOMPARE(svc.activations.size(), 0);
    }
};

QTEST_MAIN(TestLiveLinkQmlIntegration)
#include "tst_live_link_qml_integration.moc"
```

NOTE: `QmlIntegrationFixture::scenePointAtFirstWikilink()` doesn't exist yet. If the fixture has a position helper (e.g. `textEditAtRow(0)` returning a QObject), compute the click point in the test by walking spans → `TextEdit::positionToRectangle(qtPos)` → `mapToGlobal/mapFromScene`. Otherwise add a small helper to the fixture (1-method addition). Confirm fixture API at execution time.

Add the test binary to `libs/markoff-live/tests/CMakeLists.txt` (mirror an existing QML-integration test like `tst_live_render_cursor_qml`; include `QmlIntegrationFixture.cpp` and `RecordingLinkService.cpp` in sources).

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build-dev --target tst_live_link_qml_integration -j 8
scripts/run-tests.sh --bin tst_live_link_qml_integration
```

Expected: FAIL — TapHandler not wired.

- [ ] **Step 3: Add the TapHandler in QML**

In `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml`, inside the `TextEdit { id: edit; ... }` block (or whatever the inline-text editor element is named), add:

```qml
TapHandler {
    acceptedButtons: Qt.LeftButton
    acceptedModifiers: Qt.ControlModifier
    gesturePolicy: TapHandler.ReleaseWithinBounds
    onTapped: (eventPoint) => {
        const qtPos = edit.positionAt(eventPoint.position.x, eventPoint.position.y);
        modelBinding.activateLinkAt(record.blockId, qtPos, eventPoint.modifiers);
    }
}
```

`modelBinding` and `record` are the QML names already used elsewhere in the delegate; confirm by reading the existing file. If different, adjust.

- [ ] **Step 4: Run to verify pass**

```bash
cmake --build build-dev --target tst_live_link_qml_integration -j 8
scripts/run-tests.sh --bin tst_live_link_qml_integration
```

Expected: both slots PASS.

- [ ] **Step 5: Falsifiability proof** (per INVARIANTS.md #4)

Temporarily replace the `activateLinkAt` body in `LiveListModelBinding.cpp` with `return;` (no-op). Run the test:

```bash
scripts/run-tests.sh --bin tst_live_link_qml_integration
```

Expected: `ctrl_click_on_wikilink_dispatches_activation` FAILS. Revert the stub, re-run to confirm PASS. Commit nothing from the stub.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml \
        libs/markoff-live/tests/tst_live_link_qml_integration.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "feat(live): Ctrl+click TapHandler on UnifiedInlineTextDelegate"
```

---

### Task D2: `HoverHandler` for Ctrl-hover cursor + signal

**Files:**
- Modify: `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml`
- Modify: `libs/markoff-live/tests/tst_live_link_qml_integration.cpp` (add hover slot)

- [ ] **Step 1: Add the hover slot to the test**

Append to `tst_live_link_qml_integration.cpp`:

```cpp
    void ctrl_hover_emits_hover_and_flips_cursor() {
        QmlIntegrationFixture fx;
        fx.loadMarkdown(QStringLiteral("See [[Page]] now."));
        LiveTest::RecordingLinkService svc;
        fx.binding()->setLinkService(&svc);

        const QPoint pt = fx.scenePointAtFirstWikilink();
        QVERIFY(!pt.isNull());

        // Move with Ctrl held — QML HoverHandler reads point.modifiers
        // from the QHoverEvent; the underlying QTest::mouseMove honours
        // QGuiApplication::keyboardModifiers via a QInputDevice key state.
        // If direct keyboardModifiers manipulation isn't available,
        // use the fixture's "simulate Ctrl-hover" helper.
        fx.simulateCtrlHoverAt(pt);
        QTRY_COMPARE(svc.hovers.size(), 1);
        QCOMPARE(svc.hovers.first().page, QStringLiteral("Page"));
    }
```

NOTE: Modifier state during a synthetic hover is tricky in `QTest`; a small fixture helper (`simulateCtrlHoverAt(QPoint)`) that posts a `QHoverEvent` with `Qt::ControlModifier` directly to the appropriate item is the cleanest path. Add it to `QmlIntegrationFixture` if missing.

- [ ] **Step 2: Run to verify failure**

```bash
scripts/run-tests.sh --bin tst_live_link_qml_integration
```

Expected: the new slot FAILS.

- [ ] **Step 3: Add the HoverHandler in QML**

In the same `TextEdit` block as Task D1:

```qml
HoverHandler {
    id: linkHover
    cursorShape: ctrlActive && currentHit ? Qt.PointingHandCursor : Qt.IBeamCursor
    property bool ctrlActive: false
    property bool currentHit: false

    onPointChanged: {
        if (!hovered) {
            currentHit = false;
            modelBinding.clearLinkHover();
            return;
        }
        ctrlActive = (point.modifiers & Qt.ControlModifier) !== 0;
        if (!ctrlActive) {
            currentHit = false;
            modelBinding.clearLinkHover();
            return;
        }
        const qtPos = edit.positionAt(point.position.x, point.position.y);
        const globalPt = edit.mapToGlobal(point.position.x, point.position.y);
        currentHit = modelBinding.hoverLinkAt(
            record.blockId, qtPos, point.modifiers,
            Qt.point(globalPt.x, globalPt.y));
    }
    onHoveredChanged: { if (!hovered) modelBinding.clearLinkHover(); }
}
```

- [ ] **Step 4: Run to verify pass**

```bash
scripts/run-tests.sh --bin tst_live_link_qml_integration
```

Expected: all 3 slots PASS.

- [ ] **Step 5: Run the fast regression suite**

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: same baseline (208/211 + new tests; the 3 pre-existing failures unchanged).

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml \
        libs/markoff-live/tests/tst_live_link_qml_integration.cpp
git commit -m "feat(live): Ctrl-hover HoverHandler + cursor flip"
```

---

## Phase E — Test-app demo

### Task E1: `MarkdownLinkService` class

**Files:**
- Create: `libs/markoff-live/app/MarkdownLinkService.h`
- Create: `libs/markoff-live/app/MarkdownLinkService.cpp`
- Modify: `libs/markoff-live/app/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_LIVE_APP_MARKDOWN_LINK_SERVICE_H
#define MARKOFF_LIVE_APP_MARKDOWN_LINK_SERVICE_H

#include <QObject>
#include <QString>

#include <markoff/core/LinkService.h>

class MarkdownLinkService : public Markoff::LinkService {
    Q_OBJECT
public:
    explicit MarkdownLinkService(QObject *parent = nullptr);

    Markoff::LinkKind classify(const QString &t) const override;
    QUrl     resolve(const QString &t, const QString &fromCtx) const override;
    void     activate(const Markoff::LinkActivation &a) override;
    void     notifyHover(const Markoff::LinkActivation &a, const QPoint &p) override;
    void     notifyHoverLeft(const QString &t) override;

Q_SIGNALS:
    void openRequested(const QString &path, const QString &section, const QString &blockRef);
    void statusMessage(const QString &);
};

#endif  // MARKOFF_LIVE_APP_MARKDOWN_LINK_SERVICE_H
```

- [ ] **Step 2: Write the implementation**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MarkdownLinkService.h"

#include <QDebug>
#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

#include <markoff/core/DefaultLinkService.h>
#include <markoff/core/LinkActivation.h>
#include <markoff/core/LinkKind.h>

MarkdownLinkService::MarkdownLinkService(QObject *parent)
    : Markoff::LinkService(parent) {}

Markoff::LinkKind MarkdownLinkService::classify(const QString &t) const
{
    if (t.startsWith(QStringLiteral("http://"),  Qt::CaseInsensitive))  return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))  return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("mailto:"),  Qt::CaseInsensitive))  return Markoff::LinkKind::External;
    if (t.startsWith(QStringLiteral("[[")) && t.endsWith(QStringLiteral("]]")))
        return Markoff::LinkKind::WikiLink;
    if (t.startsWith(QLatin1Char('#')))                                 return Markoff::LinkKind::Tag;
    return Markoff::LinkKind::Unknown;
}

QUrl MarkdownLinkService::resolve(const QString &t, const QString &) const
{
    if (classify(t) == Markoff::LinkKind::External) return QUrl(t);
    return {};
}

void MarkdownLinkService::activate(const Markoff::LinkActivation &a)
{
    qInfo() << "[link] activated:" << a.rawText
            << "kind=" << int(a.kind)
            << "page=" << a.page
            << "section=" << a.section
            << "blockRef=" << a.blockRef
            << "alias=" << a.alias
            << "modifiers=" << int(a.modifiers);

    if (a.kind == Markoff::LinkKind::WikiLink) {
        QFileInfo here(a.fromContext);
        QFileInfo target(here.dir(), a.page + QStringLiteral(".md"));
        if (target.exists())
            Q_EMIT openRequested(target.absoluteFilePath(), a.section, a.blockRef);
        else
            qInfo() << "[link]   (would open" << target.absoluteFilePath() << "but not found)";
    } else {
        QDesktopServices::openUrl(a.resolvedTarget.isEmpty()
            ? QUrl(a.rawText) : a.resolvedTarget);
    }
    Markoff::LinkService::activate(a);
}

void MarkdownLinkService::notifyHover(const Markoff::LinkActivation &a, const QPoint &)
{
    Q_EMIT statusMessage(tr("Ctrl+click to open: %1").arg(
        a.page.isEmpty() ? a.rawText : a.page));
    Markoff::LinkService::notifyHover(a, {});
}

void MarkdownLinkService::notifyHoverLeft(const QString &t)
{
    Q_EMIT statusMessage({});
    Markoff::LinkService::notifyHoverLeft(t);
}
```

- [ ] **Step 3: Add to the app CMake**

In `libs/markoff-live/app/CMakeLists.txt`, find the `qt_add_executable(markoff-live-app ...)` block and add `MarkdownLinkService.cpp` to its sources.

- [ ] **Step 4: Build to verify**

```bash
cmake --build build-dev --target markoff-live-app -j 8
```

Expected: success.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/app/MarkdownLinkService.h \
        libs/markoff-live/app/MarkdownLinkService.cpp \
        libs/markoff-live/app/CMakeLists.txt
git commit -m "feat(live-app): MarkdownLinkService demo policy implementation"
```

---

### Task E2: Install the service + wire status bar + setFromContext

**Files:**
- Modify: `libs/markoff-live/app/MainController.cpp` (or `main.cpp` — wherever the QQmlApplicationEngine + binding live; confirm at execution time)
- Modify: `libs/markoff-live/app/MainController.h` if class members added

- [ ] **Step 1: Construct + install the service**

In `MainController.cpp` (or equivalent), add:

```cpp
#include "MarkdownLinkService.h"

// ... wherever the LiveListModelBinding is set up ...
auto linkSvc = new MarkdownLinkService(this);
modelBinding->setLinkService(linkSvc);
```

- [ ] **Step 2: Wire `setFromContext` on document load**

Find the existing "document loaded from path" path (search for `loadFromMarkdown` callsites in `app/`). After the load, call:

```cpp
modelBinding->setFromContext(currentFilePath);
```

`currentFilePath` is the absolute path of the just-loaded file (empty for "new" docs).

- [ ] **Step 3: Wire `statusMessage` to a `QStatusBar` (or the QML status string property)**

If the main window has a `QStatusBar`:

```cpp
connect(linkSvc, &MarkdownLinkService::statusMessage,
        statusBar(), [this](const QString &m) {
    if (m.isEmpty()) statusBar()->clearMessage();
    else             statusBar()->showMessage(m, 3000);
});
```

If the app is QML-only, add a `Q_PROPERTY(QString statusMessage ...)` on a controller and bind it in QML to a Text item. Match the existing app structure.

- [ ] **Step 4: Build + smoke-test**

```bash
cmake --build build-dev --target markoff-live-app -j 8
```

Expected: success.

Manual smoke (requires user permission — defer to dogfood task F1 if not in an interactive session): launch the app on a doc that contains `[[Some Page]]`, Ctrl-hover → status bar shows "Ctrl+click to open: Some Page"; Ctrl+click → stdout shows the activation. No need to run windowed in CI.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/app/MainController.cpp \
        libs/markoff-live/app/MainController.h
git commit -m "feat(live-app): install MarkdownLinkService + wire status bar"
```

---

### Task E3: Wire `openRequested` to in-place document load

**Files:**
- Modify: `libs/markoff-live/app/MainController.cpp`

- [ ] **Step 1: Connect the signal**

After installing the service in E2:

```cpp
connect(linkSvc, &MarkdownLinkService::openRequested,
        this, [this](const QString &path, const QString &section, const QString &blockRef) {
    loadDocumentFromPath(path);
    // Section/blockRef scroll is best-effort: if Document::headings()
    // exposes the matching heading's blockId, request a focus on that row.
    // Otherwise, log "navigate to section: X" for now — full anchor scroll
    // is a polish follow-up.
    if (!section.isEmpty())
        qInfo() << "[link] navigate to section:" << section;
    if (!blockRef.isEmpty())
        qInfo() << "[link] navigate to block:" << blockRef;
});
```

`loadDocumentFromPath` is the existing app helper for opening a file (confirm name at execution time by searching for the existing "Open File" / "Recent Files" path).

- [ ] **Step 2: Build + smoke-test**

```bash
cmake --build build-dev --target markoff-live-app -j 8
```

Expected: success.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/app/MainController.cpp
git commit -m "feat(live-app): wikilink openRequested loads target doc in-place"
```

---

## Phase F — Dogfood + tag

### Task F1: Update phase board + INVARIANTS check + dogfood request

**Files:**
- Modify: `docs/e-arc/e-arc-status.md`
- Create: `docs/handoff/2026-05-18-e3a-dogfood-request.md`

- [ ] **Step 1: Update the phase board**

In `docs/e-arc/e-arc-status.md`:

- Add a new row under the E3 entry (or modify the E3 row to break out sub-phases): `E3a` status `dogfood`, spec link, plan link, notes "Wikilinks + navigation. Wired Ctrl+click + Ctrl-hover through LinkService; structured target baked at parse time. Tag pending dogfood."
- Append a recent-changes entry: `2026-05-18 | (commit-sha) | **E3a wikilinks + navigation landed.** Parser-side LinkTarget on every link/wikilink span; LiveListModelBinding hits-tests + dispatches; UnifiedInlineTextDelegate TapHandler/HoverHandler under Ctrl modifier; markoff-live-app demo service. Tag v0.7.0-e3a held pending dogfood.`

- [ ] **Step 2: Write the dogfood-request handoff**

`docs/handoff/2026-05-18-e3a-dogfood-request.md`:

```markdown
# E3a — dogfood request (2026-05-18)

**Scope:** wikilink + standard-link navigation in Live.

## Checklist

Run `./build-dev/bin/markoff-live-app some-doc.md` on a document that contains
at least one `[[Wikilink]]`, one `[[Page|Alias]]`, one `[[Page#Section]]`, and
one `[text](https://example.com)`.

- [ ] Plain click on a wikilink places the caret (no navigation).
- [ ] Ctrl+click on `[[Page]]` logs activation to stdout (kind=WikiLink, page="Page").
- [ ] Ctrl+click on `[[Page|Alias]]` logs activation with alias populated.
- [ ] Ctrl+click on `[[Page#Section]]` logs activation with section populated.
- [ ] Ctrl+click on `[text](https://...)` opens the URL in the system browser.
- [ ] Ctrl-hover over a link flips the cursor to a pointing hand; status bar shows "Ctrl+click to open: ...".
- [ ] Releasing Ctrl (without moving) flips the cursor back to I-beam.
- [ ] Hover off the link clears the status bar message.
- [ ] If a sibling `<page>.md` exists, Ctrl+click on `[[<page>]]` opens it in-place.
- [ ] Caret-inside-link autohide-reveal: caret in the link span still shows the source delimiters; Ctrl+click still navigates; plain click moves caret normally.

## Out of scope for E3a (do not regress; do not test)

- Tags (`#tag`) navigation — E3b.
- Embeds (`![[image.png]]`, `![[Page]]`) — E3c.
- Callouts — E3d.

## On pass

Tag `v0.7.0-e3a` at the dogfood-confirmed commit.

## On fail

File specific bug under `docs/handoff/2026-05-18-e3a-dogfood-findings.md` with
repro steps. Hold the tag.
```

- [ ] **Step 3: Commit**

```bash
git add docs/e-arc/e-arc-status.md \
        docs/handoff/2026-05-18-e3a-dogfood-request.md
git commit -m "docs(e3a): phase-board entry + dogfood request"
```

---

### Task F2: Full-suite green check before dogfood

- [ ] **Step 1: Run the fast suite**

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: 208 + (new) tests pass; the three pre-existing failures (`setext_e2e::S1`, `tst_markoff_doc_apply_structured_paste`, `tst_v10_source_editor_view_contract::cursor_position_round_trips`) unchanged.

- [ ] **Step 2: Run the full suite (incl. realistic + benchmark) if time permits**

```bash
scripts/run-tests.sh
```

Expected: same as Step 1, plus `tst_realistic` (~70 s) and `tst_benchmark` (~340 s).

- [ ] **Step 3: If any regression appears, fix it before handing off to dogfood**

Do not handwave a regression as "pre-existing" without verifying against `git stash` of the E3a diff. Classify per `feedback_classify_before_fixing_test_failures` memory: drift / real-bug / redundant / in-flight.

- [ ] **Step 4: No commit; this is a verification gate.**

---

### Task F3: Tag after dogfood signoff

(Manual / user-driven — agent does not tag without user confirmation.)

- [ ] **Step 1: Wait for the user's dogfood-pass signal.**

- [ ] **Step 2: Tag at the dogfood-confirmed commit**

```bash
git tag -a v0.7.0-e3a -m "E3a — wikilinks + navigation (dogfood confirmed)"
```

(Do **not** push the tag unless user explicitly asks — per project convention.)

- [ ] **Step 3: Final phase-board update**

In `docs/e-arc/e-arc-status.md`:
- Set E3a row to `complete (YYYY-MM-DD, tag v0.7.0-e3a)`.
- Append a recent-changes entry citing the tag SHA.

```bash
git add docs/e-arc/e-arc-status.md
git commit -m "docs(e3a): tag v0.7.0-e3a confirmed at dogfood"
```

---

## Spec coverage map (self-review)

| Spec section | Task(s) |
|---|---|
| §3.1 `LinkTarget` struct | A1 |
| §3.2 `SourceSpan::linkTarget` | A3 |
| §3.3 `LinkInfo::structured` | A5 |
| §3.4 Decomposition rules | A2 |
| §3.5 Parser integration (3 sites) | A4 (wikilink + standard), A5 (LinkInfo path) |
| §3.6 No grammar work | — (verified by A4/A5 staying in TreeSitterParser.cpp) |
| §4.1 LinkActivation extension | B1 |
| §4.2 DefaultLinkService classify | B2 |
| §5.1 LiveListModelBinding accessors | C2 |
| §5.2 Hit-test / builder helpers | C3 |
| §5.3 Q_INVOKABLEs activate / hover / clear | C4, C5 |
| §5.4 QML TapHandler + HoverHandler | D1, D2 |
| §5.5 Files-touched roster | covered across A–D |
| §6 Testing strategy | A2, A4, A5, B1, B2, C4, C5, D1, D2, F2 |
| §6.4 RecordingLinkService | C1 |
| §6.5 Falsifiability proof | D1 step 5 |
| §6.6 Regression baseline | F2 |
| §7 Test-app demo | E1, E2, E3 |
| §8 Open questions | resolved at spec time; demo handles them in E1–E3 |
| §9 Subtractability note | — (design-time; not an implementation task) |
| §10 Discipline check | — (design-time; verified by absence of callLater / re-entrance guards in C3–D2) |
| §11 Phase board updates | F1, F3 |

---

## Notes for the executing agent

- **Don't add features not listed.** Tags, embeds, callouts, hover preview UI, anchor scroll polish — all out of scope.
- **Trust the spec on layering.** Decomposition is parser-side; Live is pure dispatch; consumer-policy decides target resolution. Resist the urge to "just put a Markdown wikilink resolver in core."
- **Check API surfaces at execution time.** Several tasks reference `MarkoffDocument::topLevelBlockIds()`, `BlockId::toString()`, `setDocument(...)`, `loadFromMarkdown(...)`, `QmlIntegrationFixture::scenePointAtFirstWikilink()` — these names are my best guess from the spec and project conventions. The actual API may differ slightly; adjust without changing semantics. Don't invent new public APIs to make a test compile.
- **Falsifiability is non-negotiable.** Every QML-integration test must be stubbable to fail (Task D1 Step 5 demonstrates the pattern). If the stub doesn't fail the test, the test is too lenient — fix the test before claiming the wiring works.
- **Discipline log.** If you spot a smell from `docs/INVARIANTS.md` while passing through code, append a one-liner to `docs/queue.md`'s Discipline Log. Don't fix it in-line; the point is visibility.
