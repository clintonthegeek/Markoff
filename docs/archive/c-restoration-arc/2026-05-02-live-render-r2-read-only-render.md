# R2 — Read-Only Render Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement L0 (Coordinates), L1 (read-only QML render), and L2 (diff-driven model) of the live-render restoration, producing a test app that loads any Markdown file and displays it as a scrollable list of read-only block delegates.

**Architecture:** `Coordinates` provides allocation-free UTF-8↔UTF-16 conversion. `LiveBlockModel` (a `QAbstractListModel`) is kept up-to-date by `LiveListModelBinding` which subscribes to `MarkoffDocument::parseUpdated`, runs `BlockWalker` to snapshot the parsed tree, runs `AstBlockDiff` to produce a minimal edit script, and calls `model.applyOps`. Five read-only QML delegates (paragraph, heading, code-block, hr, image) are dispatched by a `DelegateChooser`. All types are in namespace `Markoff::LiveRender`, QML module `org.markoff.live.render 1.0`.

**Tech stack:** C++20, Qt 6.8 (Core, Gui, Widgets, Quick, QuickControls2, QmlModels, Test), KF6::SyntaxHighlighting (runtime QML import only — no C++ link), CMake 3.19+. `QTest` for unit tests. Foundation: `markoff_core` + `markoff-parser` (transitively available).

**Reference spec:** `docs/specs/2026-05-02-live-render-restoration-design.md` §6 (components), §7 (data flow), §11 R2.
**Prerequisite:** R1A (`parseUpdated` 4-arg signal) and R1B (`TopLevelBlock::inlineSpans`) are landed. R1C scaffold exists at `libs/markoff-live/`.

---

## File map

**New — public headers** (`libs/markoff-live/include/markoff/live-render/`):
- `BlockRecord.h` — `BlockRecord` (value type per block; holds kind, text, headingLevel, codeLanguage, blockAnchor, inlineSpans) + `BlockKey` (diff identity key)
- `BlockKind.h` — string constants `BlockKind::Paragraph`, `::Heading`, etc.
- `BlockKindDescriptor.h` — `BlockKindDescriptor` struct (full shape from spec §5.1; most fields default-empty in R2)
- `BlockKindRegistry.h` — `BlockKindRegistry` class; built-ins registered in ctor
- `Coordinates.h` — `Markoff::LiveRender::Coordinates::byteToQtPos`, `qtPosToByte`
- `LiveBlockModel.h` — `QAbstractListModel` over `BlockRecord` list; per-row edit sequence tracking
- `LiveListModelBinding.h` — `QML_ELEMENT`; subscribes to `MarkoffDocument::parseUpdated`; owns the model

**New — private sources** (`libs/markoff-live/src/`):
- `BlockKind.cpp` — constant definitions
- `BlockKindRegistry.cpp` — built-in registration logic
- `Coordinates.cpp` — `byteToQtPos` + `qtPosToByte` implementations
- `AstBlockDiff.h` / `AstBlockDiff.cpp` — internal; Myers/LCS diff over `BlockKey`
- `BlockWalker.h` / `BlockWalker.cpp` — internal; `Markoff::Document` → `QList<BlockRecord>`
- `LiveBlockModel.cpp` — model implementation
- `LiveListModelBinding.cpp` — binding implementation

**New — QML** (`libs/markoff-live/qml/`):
- `LiveView.qml` — replaces `Placeholder.qml`; `ListView` + `DelegateChooser` for 5 kinds
- `delegates/ParagraphDelegate.qml`
- `delegates/HeadingDelegate.qml`
- `delegates/CodeBlockDelegate.qml`
- `delegates/HorizontalRuleDelegate.qml`
- `delegates/ImageDelegate.qml`

**Modified:**
- `libs/markoff-live/CMakeLists.txt` — add new SOURCES + QML_FILES + KF6 (runtime); remove Placeholder.qml
- `libs/markoff-live/tests/CMakeLists.txt` — add `tst_live_render_coords`, `tst_live_render_block_model`
- `libs/markoff-live/app/main.cpp` — take file argument, load content
- `libs/markoff-live/app/Main.qml` — wire `LiveListModelBinding` + `LiveView`

---

## Task 1: Read context

- [ ] **Step 1: Read the existing ports we're basing this on**

```
libs/markoff-view-qml/src/AstBlockDiff.h        (port target — namespace changes only)
libs/markoff-view-qml/src/AstBlockDiff.cpp      (port target)
libs/markoff-view-qml/src/BlockWalker.h         (port target — updated for new BlockRecord + inlineSpans)
libs/markoff-view-qml/src/BlockWalker.cpp       (port target)
libs/markoff-view-qml/include/markoff/view/qml/BlockRecord.h   (model for our BlockRecord)
libs/markoff-view-qml/include/markoff/view/qml/BlockKind.h
libs/markoff-view-qml/include/markoff/view/qml/LiveBlockModel.h
libs/markoff-view-qml/src/LiveBlockModel.cpp    (model for our simplified version)
libs/markoff-core/include/markoff-foundation/MarkoffDocument.h  (parseUpdated signal)
libs/markoff-parser/include/markoff-parser/Document.h   (topLevelBlocks, markdownContent)
libs/markoff-parser/include/markoff-parser/SourceSpan.h (inlineSpans element type)
libs/markoff-live/CMakeLists.txt         (what we'll modify)
```

No code changes in this task.

---

## Task 2: Block value types

Create the public value-type headers that everything else builds on.

**Files:**
- Create: `libs/markoff-live/include/markoff/live-render/BlockRecord.h`
- Create: `libs/markoff-live/include/markoff/live-render/BlockKind.h`
- Create: `libs/markoff-live/src/BlockKind.cpp`
- Create: `libs/markoff-live/include/markoff/live-render/BlockKindDescriptor.h`

- [ ] **Step 1: Write `BlockRecord.h`**

Create `libs/markoff-live/include/markoff/live-render/BlockRecord.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/core/BlockAnchor.h>
#include <markoff-parser/SourceSpan.h>

#include <QString>
#include <QList>

namespace Markoff::LiveRender {

/// A snapshot of one top-level block from the parsed document.
/// Source-faithful: `text` holds the raw markdown bytes for the block's
/// [byteStart, byteEnd) range as they appear in the body buffer.
/// Kind-specific extras (headingLevel, codeLanguage) are populated where
/// the parser surfaces them.
///
/// `inlineSpans` is the pre-baked inline formatting data from R1B's
/// TopLevelBlock::inlineSpans. Populated by BlockWalker. Read-only in R2;
/// consumed by InlineFormatHighlighter in R6.
///
/// `lastEditEditSequence` is view-layer staleness tracking per spec §4.2.
/// Initialized to 0 by BlockWalker. Set by LiveEditBinding (R4) on each
/// local edit to this block's row.
struct MARKOFF_LIVE_RENDER_EXPORT BlockRecord {
    QString              kind;
    QString              text;          ///< Source-faithful markdown for this block.
    int                  headingLevel = 0;   ///< 1–6 if kind=="heading"; else 0.
    QString              codeLanguage;       ///< Fence info-string if kind=="code-block".
    Markoff::BlockAnchor blockAnchor;        ///< CRDT-stable identity (block's first byte).
    QList<Markoff::SourceSpan> inlineSpans; ///< Pre-baked inline spans (R1B). Read in R6.

    bool operator==(const BlockRecord &o) const noexcept {
        // inlineSpans intentionally excluded: diff identity is (kind, anchor).
        // Text changes update the Equal row via dataChanged without re-keying.
        return kind == o.kind && text == o.text
            && headingLevel == o.headingLevel
            && codeLanguage == o.codeLanguage
            && blockAnchor == o.blockAnchor;
    }
    bool operator!=(const BlockRecord &o) const noexcept { return !(*this == o); }
};

/// Diff identity key. Two blocks with the same kind+anchor are "the same
/// block" across parses: content edits keep delegates alive; kind-changes
/// and structural edits (splits/merges) produce Delete+Insert pairs.
struct MARKOFF_LIVE_RENDER_EXPORT BlockKey {
    QString              kind;
    Markoff::BlockAnchor anchor;
    bool operator==(const BlockKey &o) const noexcept {
        return kind == o.kind && anchor == o.anchor;
    }
    bool operator!=(const BlockKey &o) const noexcept { return !(*this == o); }
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 2: Write `BlockKind.h`**

Create `libs/markoff-live/include/markoff/live-render/BlockKind.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <QString>

namespace Markoff::LiveRender {

/// String constants for the five built-in block kinds. String-keyed
/// (not a closed enum) so plugin-registered kinds don't require
/// recompiling this library — they just call BlockKindRegistry::register_.
namespace BlockKind {
    MARKOFF_LIVE_RENDER_EXPORT extern const QString Paragraph;    // "paragraph"
    MARKOFF_LIVE_RENDER_EXPORT extern const QString Heading;      // "heading"
    MARKOFF_LIVE_RENDER_EXPORT extern const QString CodeBlock;    // "code-block"
    MARKOFF_LIVE_RENDER_EXPORT extern const QString HorizontalRule; // "hr"
    MARKOFF_LIVE_RENDER_EXPORT extern const QString Image;        // "image"
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 3: Write `BlockKind.cpp`**

Create `libs/markoff-live/src/BlockKind.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/BlockKind.h>

namespace Markoff::LiveRender::BlockKind {

const QString Paragraph     = QStringLiteral("paragraph");
const QString Heading       = QStringLiteral("heading");
const QString CodeBlock     = QStringLiteral("code-block");
const QString HorizontalRule = QStringLiteral("hr");
const QString Image         = QStringLiteral("image");

}  // namespace Markoff::LiveRender::BlockKind
```

- [ ] **Step 4: Write `BlockKindDescriptor.h`**

Create `libs/markoff-live/include/markoff/live-render/BlockKindDescriptor.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>

#include <QSet>
#include <QString>
#include <QStringList>

namespace Markoff::LiveRender {

/// Static metadata for a block kind. Registered in BlockKindRegistry.
/// All fields default to empty/false in R2 unless noted — they are filled
/// as subsequent phases (R3 cursor, R5 structural keys, R9 context menu)
/// add the relevant machinery.
///
/// `delegateUrl` is the QRC URL of the QML delegate file. Populated for
/// built-in kinds but not consulted by LiveView.qml in R2 (DelegateChooser
/// hardcodes the five built-in choices). Plugin-registered kinds use this
/// URL for dynamic Loader dispatch in R3+.
///
/// Spec §5.1.
struct MARKOFF_LIVE_RENDER_EXPORT BlockKindDescriptor {
    QString     id;              ///< "paragraph", "heading", etc.
    QString     delegateUrl;     ///< qrc: URL of the QML delegate (R3+ dispatch).

    /// Which Cursor variants this kind admits: "TextCaret", "BlockSelected",
    /// "BlockInternalEdit". Populated in R3 when LiveCursorState validates.
    QSet<QString> supportedCursorVariants;

    /// Internal-edit mode tokens (e.g. {"editing-latex"} for math). R8.
    QStringList internalEditModes;

    /// True for text-bearing kinds (paragraph, heading, code-block) — the
    /// view can call applyTextUpdate on their delegates. False for hr, image.
    bool acceptsTextRoleUpdates = false;

    /// Context-menu actions registered for this kind. R9.
    QStringList contextMenuActions;

    /// Structural keys this kind consumes. R5 dispatch.
    QSet<int>   consumedStructuralKeys;
};

}  // namespace Markoff::LiveRender
```

No source file for `BlockKindDescriptor` — it is a pure header value type.

---

## Task 3: BlockKindRegistry (test-first)

**Files:**
- Create: `libs/markoff-live/include/markoff/live-render/BlockKindRegistry.h`
- Create: `libs/markoff-live/src/BlockKindRegistry.cpp`
- Create: `libs/markoff-live/tests/tst_live_render_registry.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`
- Modify: `libs/markoff-live/CMakeLists.txt` (add new SOURCES)

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-live/tests/tst_live_render_registry.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/BlockKindDescriptor.h>
#include <markoff/live-render/BlockKindRegistry.h>

using namespace Markoff::LiveRender;

class TstLiveRenderRegistry : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void builtins_registered() {
        BlockKindRegistry reg;
        QVERIFY(reg.find(BlockKind::Paragraph) != nullptr);
        QVERIFY(reg.find(BlockKind::Heading) != nullptr);
        QVERIFY(reg.find(BlockKind::CodeBlock) != nullptr);
        QVERIFY(reg.find(BlockKind::HorizontalRule) != nullptr);
        QVERIFY(reg.find(BlockKind::Image) != nullptr);
    }

    void unknown_kind_returns_nullptr() {
        BlockKindRegistry reg;
        QVERIFY(reg.find("nonexistent-kind") == nullptr);
    }

    void paragraph_accepts_text_updates() {
        BlockKindRegistry reg;
        const auto *d = reg.find(BlockKind::Paragraph);
        QVERIFY(d != nullptr);
        QVERIFY(d->acceptsTextRoleUpdates);
    }

    void hr_does_not_accept_text_updates() {
        BlockKindRegistry reg;
        const auto *d = reg.find(BlockKind::HorizontalRule);
        QVERIFY(d != nullptr);
        QVERIFY(!d->acceptsTextRoleUpdates);
    }

    void image_does_not_accept_text_updates() {
        BlockKindRegistry reg;
        const auto *d = reg.find(BlockKind::Image);
        QVERIFY(d != nullptr);
        QVERIFY(!d->acceptsTextRoleUpdates);
    }

    void plugin_kind_registration() {
        BlockKindRegistry reg;
        BlockKindDescriptor custom;
        custom.id = QStringLiteral("my-custom-block");
        custom.acceptsTextRoleUpdates = false;
        reg.register_(custom);
        const auto *d = reg.find("my-custom-block");
        QVERIFY(d != nullptr);
        QCOMPARE(d->id, QStringLiteral("my-custom-block"));
    }

    void kinds_list_contains_all_builtins() {
        BlockKindRegistry reg;
        const auto kinds = reg.kinds();
        QVERIFY(kinds.contains(BlockKind::Paragraph));
        QVERIFY(kinds.contains(BlockKind::Heading));
        QVERIFY(kinds.contains(BlockKind::CodeBlock));
        QVERIFY(kinds.contains(BlockKind::HorizontalRule));
        QVERIFY(kinds.contains(BlockKind::Image));
    }
};

QTEST_APPLESS_MAIN(TstLiveRenderRegistry)
#include "tst_live_render_registry.moc"
```

- [ ] **Step 2: Write `BlockKindRegistry.h`**

Create `libs/markoff-live/include/markoff/live-render/BlockKindRegistry.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/BlockKindDescriptor.h>

#include <QHash>
#include <QStringList>

namespace Markoff::LiveRender {

/// Registry of block kind descriptors. Built-in kinds are registered in
/// the constructor. Plugin authors register custom kinds via register_().
/// LiveListModelBinding owns one instance; passes a pointer to downstream
/// components (LiveCursorState, LiveStructuralKeyHandler) in R3+.
class MARKOFF_LIVE_RENDER_EXPORT BlockKindRegistry {
public:
    BlockKindRegistry();   ///< Registers all five built-in kinds.

    void register_(BlockKindDescriptor descriptor);

    const BlockKindDescriptor *find(const QString &id) const;
    QStringList kinds() const;

private:
    void registerBuiltins();
    QHash<QString, BlockKindDescriptor> m_descriptors;
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 3: Write `BlockKindRegistry.cpp`**

Create `libs/markoff-live/src/BlockKindRegistry.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/BlockKind.h>

namespace Markoff::LiveRender {

BlockKindRegistry::BlockKindRegistry()
{
    registerBuiltins();
}

void BlockKindRegistry::registerBuiltins()
{
    // Paragraph: text-bearing, TextCaret (R3), Enter/Backspace/Delete (R5).
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Paragraph;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.consumedStructuralKeys = {};   // populated in R5
        d.delegateUrl = QStringLiteral("qrc:/qt/qml/org/markoff/live/render/delegates/ParagraphDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // Heading: text-bearing, TextCaret.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Heading;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.delegateUrl = QStringLiteral("qrc:/qt/qml/org/markoff/live/render/delegates/HeadingDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // CodeBlock: text-bearing, TextCaret. Tab inserts literal tab (R5).
    {
        BlockKindDescriptor d;
        d.id = BlockKind::CodeBlock;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.delegateUrl = QStringLiteral("qrc:/qt/qml/org/markoff/live/render/delegates/CodeBlockDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // HorizontalRule: non-text, BlockSelected only.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::HorizontalRule;
        d.acceptsTextRoleUpdates = false;
        d.supportedCursorVariants = { QStringLiteral("BlockSelected") };
        d.delegateUrl = QStringLiteral("qrc:/qt/qml/org/markoff/live/render/delegates/HorizontalRuleDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // Image: non-text, BlockSelected (default) + optional alt-edit (post-R6).
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Image;
        d.acceptsTextRoleUpdates = false;
        d.supportedCursorVariants = { QStringLiteral("BlockSelected") };
        d.delegateUrl = QStringLiteral("qrc:/qt/qml/org/markoff/live/render/delegates/ImageDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
}

void BlockKindRegistry::register_(BlockKindDescriptor descriptor)
{
    m_descriptors.insert(descriptor.id, std::move(descriptor));
}

const BlockKindDescriptor *BlockKindRegistry::find(const QString &id) const
{
    auto it = m_descriptors.find(id);
    return (it != m_descriptors.end()) ? &it.value() : nullptr;
}

QStringList BlockKindRegistry::kinds() const
{
    return QStringList(m_descriptors.keys());
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 4: Add `BlockKind.cpp`, `BlockKindRegistry.cpp` to library CMakeLists**

Edit `libs/markoff-live/CMakeLists.txt`. Change the `qt_add_qml_module` SOURCES block:

```cmake
qt_add_qml_module(markoff_live_render
    URI org.markoff.live.render
    VERSION 1.0
    STATIC
    SOURCES
        src/Version.cpp
        include/markoff/live-render/MarkoffLiveRenderExport.h
        include/markoff/live-render/Version.h
        include/markoff/live-render/BlockRecord.h
        include/markoff/live-render/BlockKind.h
        include/markoff/live-render/BlockKindDescriptor.h
        include/markoff/live-render/BlockKindRegistry.h
        src/BlockKind.cpp
        src/BlockKindRegistry.cpp
    QML_FILES
        qml/Placeholder.qml
)
```

Also add `markoff-parser` to `find_package` (it's transitive via markoff_core but explicit is safer):

```cmake
find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Widgets Quick QuickControls2 Qml Test)
```

(Leave as-is; markoff-parser headers are available transitively.)

- [ ] **Step 5: Add test to `tests/CMakeLists.txt`**

Edit `libs/markoff-live/tests/CMakeLists.txt`:

```cmake
# R1C provides one trivial test that proves the test infrastructure is
# alive. R2 onwards replaces this with the per-layer test executables
# named in spec §10.2 (tst_live_render_coords, tst_live_render_block_model,
# tst_live_render_cursor, …).

qt_add_executable(tst_live_render_skeleton
    tst_live_render_skeleton.cpp
)
target_link_libraries(tst_live_render_skeleton PRIVATE
    Qt6::Core Qt6::Test markoff_live_render)
add_test(NAME tst_live_render_skeleton COMMAND tst_live_render_skeleton)

qt_add_executable(tst_live_render_registry
    tst_live_render_registry.cpp
)
target_link_libraries(tst_live_render_registry PRIVATE
    Qt6::Core Qt6::Test markoff_live_render)
add_test(NAME tst_live_render_registry COMMAND tst_live_render_registry)
```

- [ ] **Step 6: Reconfigure and build to check the test compiles and fails**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target tst_live_render_registry -j 8 2>&1 | tail -20
```

Expected: build fails with "BlockKindRegistry: No such file or directory" until Step 2 creates it, or links with unresolved symbols until Step 3. If building after all steps above: build succeeds.

- [ ] **Step 7: Run the test**

```bash
ctest --test-dir build-dev -R '^tst_live_render_registry$' --output-on-failure
```

Expected: `6/6 tests passed`.

---

## Task 4: Coordinates (test-first)

**Files:**
- Create: `libs/markoff-live/include/markoff/live-render/Coordinates.h`
- Create: `libs/markoff-live/src/Coordinates.cpp`
- Create: `libs/markoff-live/tests/tst_live_render_coords.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt` (add Coordinates.cpp to SOURCES)
- Modify: `libs/markoff-live/tests/CMakeLists.txt` (add tst_live_render_coords)

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-live/tests/tst_live_render_coords.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QByteArray>

#include <markoff/live-render/Coordinates.h>

using namespace Markoff::LiveRender::Coordinates;

class TstLiveRenderCoords : public QObject {
    Q_OBJECT
private Q_SLOTS:

    // ---- byteToQtPos ----

    void byteToQtPos_empty() {
        QCOMPARE(byteToQtPos(QByteArray(), 0), qsizetype(0));
    }

    void byteToQtPos_ascii_identity() {
        // ASCII: each byte = 1 QChar
        const QByteArray buf = QByteArray("hello");
        QCOMPARE(byteToQtPos(buf, 0), qsizetype(0));
        QCOMPARE(byteToQtPos(buf, 3), qsizetype(3));
        QCOMPARE(byteToQtPos(buf, 5), qsizetype(5));
    }

    void byteToQtPos_two_byte_sequence() {
        // U+00E9 LATIN SMALL LETTER E WITH ACUTE: UTF-8 = 0xC3 0xA9 (2 bytes),
        // UTF-16 = 1 QChar.
        const QByteArray buf = QByteArray("\xC3\xA9");   // é
        QCOMPARE(byteToQtPos(buf, 0), qsizetype(0));
        QCOMPARE(byteToQtPos(buf, 2), qsizetype(1));     // after é = 1 QChar
    }

    void byteToQtPos_three_byte_sequence() {
        // U+20AC EURO SIGN: UTF-8 = 0xE2 0x82 0xAC (3 bytes), UTF-16 = 1 QChar.
        const QByteArray buf = QByteArray("\xE2\x82\xAC");   // €
        QCOMPARE(byteToQtPos(buf, 0), qsizetype(0));
        QCOMPARE(byteToQtPos(buf, 3), qsizetype(1));     // after € = 1 QChar
    }

    void byteToQtPos_four_byte_sequence() {
        // U+1F600 GRINNING FACE: UTF-8 = 0xF0 0x9F 0x98 0x80 (4 bytes),
        // UTF-16 = 2 QChars (surrogate pair).
        const QByteArray buf = QByteArray("\xF0\x9F\x98\x80");  // 😀
        QCOMPARE(byteToQtPos(buf, 0), qsizetype(0));
        QCOMPARE(byteToQtPos(buf, 4), qsizetype(2));     // surrogate pair = 2 QChars
    }

    void byteToQtPos_mixed() {
        // "a€b": a(1)+€(3)+b(1) = 5 bytes; a(1)+€(1)+b(1) = 3 QChars
        const QByteArray buf = QByteArray("a\xE2\x82\xACb");
        QCOMPARE(byteToQtPos(buf, 0), qsizetype(0));
        QCOMPARE(byteToQtPos(buf, 1), qsizetype(1));   // after 'a'
        QCOMPARE(byteToQtPos(buf, 4), qsizetype(2));   // after '€'
        QCOMPARE(byteToQtPos(buf, 5), qsizetype(3));   // after 'b'
    }

    void byteToQtPos_clamp_past_end() {
        // Offset past end → return full QChar length.
        const QByteArray buf = QByteArray("hi");
        QCOMPARE(byteToQtPos(buf, 100), qsizetype(2));
    }

    // ---- qtPosToByte ----

    void qtPosToByte_empty() {
        QCOMPARE(qtPosToByte(QByteArray(), 0), qsizetype(0));
    }

    void qtPosToByte_ascii_identity() {
        const QByteArray buf = QByteArray("hello");
        QCOMPARE(qtPosToByte(buf, 0), qsizetype(0));
        QCOMPARE(qtPosToByte(buf, 3), qsizetype(3));
        QCOMPARE(qtPosToByte(buf, 5), qsizetype(5));
    }

    void qtPosToByte_two_byte_sequence() {
        const QByteArray buf = QByteArray("\xC3\xA9");   // é
        QCOMPARE(qtPosToByte(buf, 0), qsizetype(0));
        QCOMPARE(qtPosToByte(buf, 1), qsizetype(2));     // 1 QChar → 2 bytes
    }

    void qtPosToByte_four_byte_sequence() {
        const QByteArray buf = QByteArray("\xF0\x9F\x98\x80");  // 😀
        QCOMPARE(qtPosToByte(buf, 0), qsizetype(0));
        QCOMPARE(qtPosToByte(buf, 2), qsizetype(4));     // 2 QChars → 4 bytes
    }

    // ---- round-trip ----

    void roundtrip_byte_to_qtpos_and_back() {
        // "café 😀 bar"
        const QString str = QStringLiteral("café \U0001F600 bar");
        const QByteArray utf8 = str.toUtf8();
        for (qsizetype byteOff = 0; byteOff <= utf8.size(); ++byteOff) {
            const qsizetype qtPos = byteToQtPos(utf8, byteOff);
            const qsizetype back  = qtPosToByte(utf8, qtPos);
            // Round-trip must return the start of the same code point.
            // byteOff might point into a multi-byte sequence; back points
            // to the start of that sequence. Verify back <= byteOff and that
            // the next code point starts at byteOff or later.
            QVERIFY2(back <= byteOff,
                     qPrintable(QString("byteOff=%1 qtPos=%2 back=%3")
                                    .arg(byteOff).arg(qtPos).arg(back)));
        }
    }
};

QTEST_APPLESS_MAIN(TstLiveRenderCoords)
#include "tst_live_render_coords.moc"
```

- [ ] **Step 2: Write `Coordinates.h`**

Create `libs/markoff-live/include/markoff/live-render/Coordinates.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <QByteArray>
#include <QtGlobal>

namespace Markoff::LiveRender::Coordinates {

/// Number of Qt UTF-16 code units (QChars) spanned by the first
/// `byteOffset` UTF-8 bytes in `utf8`. Returns the total QChar count
/// if `byteOffset >= utf8.size()`. No allocation; O(byteOffset) scan.
MARKOFF_LIVE_RENDER_EXPORT
qsizetype byteToQtPos(const QByteArray &utf8, qsizetype byteOffset);

/// Byte offset in `utf8` at which the `qtPos`-th UTF-16 code unit begins.
/// Returns `utf8.size()` if `qtPos` is past the end. No allocation;
/// O(qtPos) scan. For a surrogate pair (4-byte UTF-8 codepoint), qtPos N
/// and N+1 both map to the same 4-byte sequence's start byte.
MARKOFF_LIVE_RENDER_EXPORT
qsizetype qtPosToByte(const QByteArray &utf8, qsizetype qtPos);

}  // namespace Markoff::LiveRender::Coordinates
```

- [ ] **Step 3: Write `Coordinates.cpp`**

Create `libs/markoff-live/src/Coordinates.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/Coordinates.h>

namespace Markoff::LiveRender::Coordinates {

qsizetype byteToQtPos(const QByteArray &utf8, qsizetype byteOffset)
{
    const qsizetype size = utf8.size();
    const qsizetype limit = qMin(byteOffset, size);
    qsizetype byteCursor = 0;
    qsizetype qtCursor   = 0;
    while (byteCursor < limit) {
        const unsigned char c = static_cast<unsigned char>(utf8[byteCursor]);
        int seqLen;
        if      ((c & 0x80) == 0x00) seqLen = 1;
        else if ((c & 0xE0) == 0xC0) seqLen = 2;
        else if ((c & 0xF0) == 0xE0) seqLen = 3;
        else if ((c & 0xF8) == 0xF0) seqLen = 4;
        else                          seqLen = 1;  // malformed: skip
        // 4-byte UTF-8 → supplementary code point → surrogate pair = 2 QChars.
        qtCursor   += (seqLen == 4) ? 2 : 1;
        byteCursor += seqLen;
    }
    return qtCursor;
}

qsizetype qtPosToByte(const QByteArray &utf8, qsizetype qtPos)
{
    const qsizetype size = utf8.size();
    qsizetype byteCursor = 0;
    qsizetype qtCursor   = 0;
    while (byteCursor < size && qtCursor < qtPos) {
        const unsigned char c = static_cast<unsigned char>(utf8[byteCursor]);
        int seqLen;
        if      ((c & 0x80) == 0x00) seqLen = 1;
        else if ((c & 0xE0) == 0xC0) seqLen = 2;
        else if ((c & 0xF0) == 0xE0) seqLen = 3;
        else if ((c & 0xF8) == 0xF0) seqLen = 4;
        else                          seqLen = 1;
        qtCursor   += (seqLen == 4) ? 2 : 1;
        byteCursor += seqLen;
        // If a surrogate pair was consumed (seqLen==4, qtCursor advanced by 2),
        // and qtPos pointed at the trailing surrogate (qtPos == qtCursor - 1),
        // we overshot by 1 QChar but byteCursor is correct (both surrogates
        // map to the same byte sequence). The caller receives the start of that
        // 4-byte sequence, which is the right thing to do.
    }
    return byteCursor;
}

}  // namespace Markoff::LiveRender::Coordinates
```

- [ ] **Step 4: Add Coordinates.cpp to library CMakeLists and add test**

Edit `libs/markoff-live/CMakeLists.txt` — append to SOURCES:
```cmake
        src/Coordinates.cpp
        include/markoff/live-render/Coordinates.h
```

Edit `libs/markoff-live/tests/CMakeLists.txt` — append:
```cmake
qt_add_executable(tst_live_render_coords
    tst_live_render_coords.cpp
)
target_link_libraries(tst_live_render_coords PRIVATE
    Qt6::Core Qt6::Test markoff_live_render)
add_test(NAME tst_live_render_coords COMMAND tst_live_render_coords)
```

- [ ] **Step 5: Build and run**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target tst_live_render_coords -j 8 2>&1 | tail -5
ctest --test-dir build-dev -R '^tst_live_render_coords$' --output-on-failure
```

Expected: `11/11 tests passed`.

---

## Task 5: AstBlockDiff and BlockWalker (ports)

These are internal (private headers in `src/`). Port from `markoff-view-qml` with namespace change. No new tests — their behavior is covered by `tst_live_render_block_model` in Task 7.

**Files:**
- Create: `libs/markoff-live/src/AstBlockDiff.h`
- Create: `libs/markoff-live/src/AstBlockDiff.cpp`
- Create: `libs/markoff-live/src/BlockWalker.h`
- Create: `libs/markoff-live/src/BlockWalker.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Write `AstBlockDiff.h`**

Create `libs/markoff-live/src/AstBlockDiff.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <markoff/live-render/BlockRecord.h>

namespace Markoff::LiveRender {

/// Pure C++ Myers/LCS diff over BlockKey sequences. Output is a list of
/// edit operations referencing indices in `prev` and `next`. Used by
/// LiveListModelBinding to emit the minimal Qt model signal sequence so
/// ListView preserves delegates whose AST block still exists.
class AstBlockDiff {
public:
    enum class OpKind {
        Equal,    ///< prev[prevIndex] == next[nextIndex]; delegate persists
        Insert,   ///< next[nextIndex] is new (no prev counterpart)
        Delete    ///< prev[prevIndex] is gone (no next counterpart)
    };

    struct Op {
        OpKind kind;
        int    prevIndex = -1;   ///< -1 for Insert
        int    nextIndex = -1;   ///< -1 for Delete
    };

    static QList<Op> diff(const QList<BlockKey> &prev,
                          const QList<BlockKey> &next);
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 2: Write `AstBlockDiff.cpp`**

Create `libs/markoff-live/src/AstBlockDiff.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AstBlockDiff.h"

#include <vector>

namespace Markoff::LiveRender {

QList<AstBlockDiff::Op> AstBlockDiff::diff(const QList<BlockKey> &prev,
                                            const QList<BlockKey> &next)
{
    const int m = prev.size();
    const int n = next.size();

    // Fast path: identity (avoids O(m*n) on hot path of identical reparses).
    if (m == n) {
        bool same = true;
        for (int i = 0; i < m; ++i) {
            if (prev[i] != next[i]) { same = false; break; }
        }
        if (same) {
            QList<Op> ops;
            ops.reserve(m);
            for (int i = 0; i < m; ++i)
                ops.append(Op{ OpKind::Equal, i, i });
            return ops;
        }
    }

    // LCS table.
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (int i = 1; i <= m; ++i)
        for (int j = 1; j <= n; ++j)
            dp[i][j] = (prev[i-1] == next[j-1])
                       ? dp[i-1][j-1] + 1
                       : std::max(dp[i-1][j], dp[i][j-1]);

    // Backtrack (result prepended → forward order).
    QList<Op> ops;
    int i = m, j = n;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && prev[i-1] == next[j-1]) {
            ops.prepend(Op{ OpKind::Equal, i-1, j-1 });
            --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j-1] >= dp[i-1][j])) {
            ops.prepend(Op{ OpKind::Insert, -1, j-1 });
            --j;
        } else {
            ops.prepend(Op{ OpKind::Delete, i-1, -1 });
            --i;
        }
    }
    return ops;
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 3: Write `BlockWalker.h`**

Create `libs/markoff-live/src/BlockWalker.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <markoff/live-render/BlockRecord.h>

namespace Markoff { class Document; }

namespace Markoff::LiveRender {

/// Convert a parsed `Markoff::Document` into a flat list of `BlockRecord`s
/// in document order. Reads `Document::topLevelBlocks()` (no re-parsing)
/// and `Document::markdownContent()` (body text). Populates
/// `BlockRecord::inlineSpans` from `TopLevelBlock::inlineSpans` (pre-baked
/// in R1B). The `blockAnchor` field is left default-constructed here;
/// LiveListModelBinding fills it in from the `blockAnchors` list it
/// receives via `parseUpdated`.
class BlockWalker {
public:
    static QList<BlockRecord> walk(const Markoff::Document *parsed);
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 4: Write `BlockWalker.cpp`**

Create `libs/markoff-live/src/BlockWalker.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockWalker.h"

#include <markoff/live-render/BlockKind.h>
#include <markoff-parser/Document.h>

#include <QByteArray>
#include <QString>

namespace Markoff::LiveRender {

namespace {

QString mapKind(Markoff::TopLevelBlock::Kind k)
{
    using K = Markoff::TopLevelBlock::Kind;
    switch (k) {
        case K::AtxHeading:
        case K::SetextHeading:      return BlockKind::Heading;
        case K::FencedCodeBlock:
        case K::IndentedCodeBlock:  return BlockKind::CodeBlock;
        case K::ThematicBreak:      return BlockKind::HorizontalRule;
        // Everything else collapses to Paragraph for R2. Lists, blockquotes,
        // tables, and HTML blocks gain dedicated kinds in R7.
        case K::Paragraph:
        case K::BlockQuote:
        case K::ListTight:
        case K::ListLoose:
        case K::HtmlBlock:
        case K::LinkReferenceDefinition:
        case K::Table:
        case K::Other:
        default:                    return BlockKind::Paragraph;
    }
}

}  // namespace

QList<BlockRecord> BlockWalker::walk(const Markoff::Document *parsed)
{
    QList<BlockRecord> out;
    if (!parsed) return out;

    const QList<Markoff::TopLevelBlock> blocks = parsed->topLevelBlocks();
    if (blocks.isEmpty()) return out;

    const QString   body     = parsed->markdownContent();
    const QByteArray bodyUtf8 = body.toUtf8();

    out.reserve(blocks.size());

    // Single-pass UTF-8 byte → UTF-16 char-offset translation.
    // Maintains a running cursor (byteCursor, charCursor) advancing forward
    // through the body — no allocation needed (just integer arithmetic).
    int byteCursor = 0;
    int charCursor = 0;

    auto advanceTo = [&](int targetByte) {
        if (targetByte < byteCursor) {
            // Blocks are returned in document order; this should not happen.
            byteCursor = 0;
            charCursor = 0;
        }
        const int end = qMin(targetByte, static_cast<int>(bodyUtf8.size()));
        while (byteCursor < end) {
            const unsigned char c = static_cast<unsigned char>(bodyUtf8[byteCursor]);
            int seqLen;
            if      ((c & 0x80) == 0x00) seqLen = 1;
            else if ((c & 0xE0) == 0xC0) seqLen = 2;
            else if ((c & 0xF0) == 0xE0) seqLen = 3;
            else if ((c & 0xF8) == 0xF0) seqLen = 4;
            else                          seqLen = 1;
            charCursor += (seqLen == 4) ? 2 : 1;
            byteCursor += seqLen;
        }
    };

    for (const auto &tlb : blocks) {
        BlockRecord rec;
        rec.kind         = mapKind(tlb.kind);
        rec.headingLevel = tlb.headingLevel;
        rec.codeLanguage = tlb.codeLanguage;
        rec.inlineSpans  = tlb.inlineSpans;  // pre-baked by parser (R1B)
        // blockAnchor: filled in by LiveListModelBinding from parseUpdated's
        // blockAnchors list (aligned 1:1 with topLevelBlocks()).

        advanceTo(tlb.byteStart);
        const int charStart = charCursor;
        advanceTo(tlb.byteEnd);
        const int charEnd = charCursor;

        rec.text = body.mid(charStart, charEnd - charStart);
        out.append(rec);
    }

    return out;
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 5: Add private sources to library CMakeLists**

Edit `libs/markoff-live/CMakeLists.txt` — append to SOURCES in `qt_add_qml_module`:

```cmake
        src/AstBlockDiff.h
        src/AstBlockDiff.cpp
        src/BlockWalker.h
        src/BlockWalker.cpp
```

---

## Task 6: LiveBlockModel (test-first)

**Files:**
- Create: `libs/markoff-live/include/markoff/live-render/LiveBlockModel.h`
- Create: `libs/markoff-live/src/LiveBlockModel.cpp`
- Create: `libs/markoff-live/tests/tst_live_render_block_model.cpp`
- Modify: CMakeLists (both files)

- [ ] **Step 1: Write the failing tests**

Create `libs/markoff-live/tests/tst_live_render_block_model.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QAbstractItemModelTester>
#include <QSignalSpy>

#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/BlockKind.h>
#include "helpers.h"   // makeRecord() helper defined below in this file

// Helper to build a minimal BlockRecord for testing.
static Markoff::LiveRender::BlockRecord makeRecord(
    const QString &kind, const QString &text,
    int headingLevel = 0, const QString &codeLang = {})
{
    Markoff::LiveRender::BlockRecord r;
    r.kind         = kind;
    r.text         = text;
    r.headingLevel = headingLevel;
    r.codeLanguage = codeLang;
    // blockAnchor and inlineSpans left default-constructed.
    return r;
}

// Helper to build a BlockKey from a record (uses kind + default anchor).
static Markoff::LiveRender::BlockKey keyOf(const Markoff::LiveRender::BlockRecord &r)
{
    Markoff::LiveRender::BlockKey k;
    k.kind   = r.kind;
    k.anchor = r.blockAnchor;
    return k;
}

using namespace Markoff::LiveRender;

class TstLiveRenderBlockModel : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void initially_empty() {
        LiveBlockModel m;
        QCOMPARE(m.rowCount(), 0);
    }

    void model_passes_tester() {
        LiveBlockModel m;
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::Fatal);
        // Add some rows and verify the model stays consistent.
        QList<BlockRecord> recs = {
            makeRecord(BlockKind::Paragraph, "hello"),
            makeRecord(BlockKind::Heading,   "# World", 1),
        };
        QList<BlockKey> prevKeys;
        QList<BlockKey> nextKeys = { keyOf(recs[0]), keyOf(recs[1]) };
        const auto ops = AstBlockDiff::diff(prevKeys, nextKeys);
        m.applyOps(ops, recs);
        QCOMPARE(m.rowCount(), 2);
    }

    void apply_ops_insert_two_rows() {
        LiveBlockModel m;
        QList<BlockRecord> recs = {
            makeRecord(BlockKind::Paragraph, "para 1"),
            makeRecord(BlockKind::Heading,   "# h1", 1),
        };
        QList<BlockKey> prev;
        QList<BlockKey> next = { keyOf(recs[0]), keyOf(recs[1]) };
        QSignalSpy spy(&m, &QAbstractListModel::rowsInserted);

        m.applyOps(AstBlockDiff::diff(prev, next), recs);

        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(spy.count(), 2);   // one insertRows signal per Insert op
        QCOMPARE(m.data(m.index(0), LiveBlockModel::KindRole).toString(),
                 BlockKind::Paragraph);
        QCOMPARE(m.data(m.index(1), LiveBlockModel::KindRole).toString(),
                 BlockKind::Heading);
        QCOMPARE(m.data(m.index(1), LiveBlockModel::HeadingLevelRole).toInt(), 1);
    }

    void apply_ops_delete_first_row() {
        LiveBlockModel m;
        QList<BlockRecord> initial = {
            makeRecord(BlockKind::Paragraph, "para 1"),
            makeRecord(BlockKind::Paragraph, "para 2"),
        };
        QList<BlockKey> prevKeys;
        QList<BlockKey> initKeys = { keyOf(initial[0]), keyOf(initial[1]) };
        m.applyOps(AstBlockDiff::diff(prevKeys, initKeys), initial);
        QCOMPARE(m.rowCount(), 2);

        // Now delete the first row.
        QList<BlockRecord> next = { initial[1] };
        QList<BlockKey> nextKeys = { keyOf(initial[1]) };
        QSignalSpy spy(&m, &QAbstractListModel::rowsRemoved);

        m.applyOps(AstBlockDiff::diff(initKeys, nextKeys), next);

        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m.data(m.index(0), LiveBlockModel::TextRole).toString(),
                 QStringLiteral("para 2"));
    }

    void apply_ops_equal_with_text_change_emits_data_changed() {
        LiveBlockModel m;
        BlockRecord rec = makeRecord(BlockKind::Paragraph, "original");
        QList<BlockKey> keys = { keyOf(rec) };
        m.applyOps(AstBlockDiff::diff({}, keys), { rec });

        rec.text = "updated";
        QSignalSpy spy(&m, &QAbstractListModel::dataChanged);
        m.applyOps(AstBlockDiff::diff(keys, { keyOf(rec) }), { rec });

        QCOMPARE(spy.count(), 1);
        QCOMPARE(m.data(m.index(0), LiveBlockModel::TextRole).toString(),
                 QStringLiteral("updated"));
    }

    void apply_ops_identity_no_signals() {
        LiveBlockModel m;
        BlockRecord rec = makeRecord(BlockKind::Paragraph, "text");
        QList<BlockKey> keys = { keyOf(rec) };
        m.applyOps(AstBlockDiff::diff({}, keys), { rec });

        QSignalSpy insertSpy(&m, &QAbstractListModel::rowsInserted);
        QSignalSpy removeSpy(&m, &QAbstractListModel::rowsRemoved);
        QSignalSpy changeSpy(&m, &QAbstractListModel::dataChanged);

        // Re-apply exactly the same — identity diff.
        m.applyOps(AstBlockDiff::diff(keys, keys), { rec });

        QCOMPARE(insertSpy.count(), 0);
        QCOMPARE(removeSpy.count(), 0);
        QCOMPARE(changeSpy.count(), 0);
    }

    void role_names_exposed() {
        LiveBlockModel m;
        const auto names = m.roleNames();
        QVERIFY(names.values().contains("kind"));
        QVERIFY(names.values().contains("text"));
        QVERIFY(names.values().contains("headingLevel"));
        QVERIFY(names.values().contains("codeLanguage"));
    }

    void row_edit_sequence_defaults_to_zero() {
        LiveBlockModel m;
        BlockRecord rec = makeRecord(BlockKind::Paragraph, "hi");
        QList<BlockKey> keys = { keyOf(rec) };
        m.applyOps(AstBlockDiff::diff({}, keys), { rec });
        QCOMPARE(m.rowEditSequence(0), quint64(0));
    }

    void set_row_edit_sequence() {
        LiveBlockModel m;
        BlockRecord rec = makeRecord(BlockKind::Paragraph, "hi");
        QList<BlockKey> keys = { keyOf(rec) };
        m.applyOps(AstBlockDiff::diff({}, keys), { rec });
        m.setRowEditSequence(0, quint64(42));
        QCOMPARE(m.rowEditSequence(0), quint64(42));
    }
};

QTEST_APPLESS_MAIN(TstLiveRenderBlockModel)
#include "tst_live_render_block_model.moc"
```

Note: this test includes `"helpers.h"` which doesn't exist — the test is self-contained (all helpers are defined inline in the test file above). Remove the `#include "helpers.h"` line before compiling — it was left in accidentally. (Or, if you prefer, write an empty `helpers.h` in the tests directory.) The `makeRecord` and `keyOf` functions are defined directly in the test file.

Actually: Remove the `#include "helpers.h"` line from the test file — it is not needed, all helpers are local. The corrected file omits that line:

```cpp
// (remove the #include "helpers.h" line; everything else stays)
```

- [ ] **Step 2: Write `LiveBlockModel.h`**

Create `libs/markoff-live/include/markoff/live-render/LiveBlockModel.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/BlockRecord.h>
#include "../../../../src/AstBlockDiff.h"  // private; included only from src/

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <qqmlintegration.h>

namespace Markoff::LiveRender {

/// `QAbstractListModel` over a list of `BlockRecord`s. Driven by
/// `LiveListModelBinding::applyOps`. Roles: kind, text, headingLevel,
/// codeLanguage, blockAnchor. Per-row edit-sequence tracking for the R4
/// freshness rule (§4.3).
///
/// Simpler than `markoff-view-qml`'s LiveBlockModel: no holes, no
/// composing-row deferral, no speculative-kind registry. Those are
/// retired in the C-architecture (holes by premise 6; composing by the
/// three-guard protocol in §4.5; speculative kinds by sequence-tagging).
class MARKOFF_LIVE_RENDER_EXPORT LiveBlockModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveBlockModel is provided by LiveListModelBinding")

public:
    enum Role {
        KindRole         = Qt::UserRole + 1,
        TextRole,
        HeadingLevelRole,
        CodeLanguageRole,
        BlockAnchorRole,
    };

    explicit LiveBlockModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Apply a diff op sequence relative to `nextRecords`. Operations:
    ///   Equal  → update text/metadata if changed, emit dataChanged.
    ///   Insert → beginInsertRows, insert, endInsertRows.
    ///   Delete → beginRemoveRows, remove, endRemoveRows.
    void applyOps(const QList<AstBlockDiff::Op> &ops,
                  const QList<BlockRecord> &nextRecords);

    const BlockRecord &recordAt(int row) const { return m_rows.at(row); }

    // ---- R4 freshness tracking (§4.2/§4.3). ----
    // Set by LiveEditBinding on each local keystroke. Read by
    // LiveListModelBinding::onParseUpdated to decide whether to apply the
    // parse output to a row (parseFreshForRow rule). In R2 these are always
    // 0 (no editing yet); the machinery is in place.

    quint64 rowEditSequence(int row) const;
    void setRowEditSequence(int row, quint64 editSeq);

    // ---- R6: C++ accessor for inline spans. ----
    // InlineFormatHighlighter reads spans from the model to avoid re-parsing.
    const QList<Markoff::SourceSpan> &spansAtRow(int row) const;

private:
    QList<BlockRecord> m_rows;
    QList<quint64>     m_rowEditSequences;  // parallel to m_rows; see §4.2
};

}  // namespace Markoff::LiveRender
```

Wait — `LiveBlockModel.h` includes `AstBlockDiff.h` via a relative path into `src/`. That is a public header including a private header, which is fragile. Fix: move `AstBlockDiff::Op` type into the public surface, or forward-declare the type in `LiveBlockModel.h`. The cleanest approach for R2 is to declare `applyOps` using a forward declaration and include `AstBlockDiff.h` only from `LiveBlockModel.cpp`.

Revise `LiveBlockModel.h` to forward-declare:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/BlockRecord.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <qqmlintegration.h>

namespace Markoff::LiveRender {

// Forward-declare AstBlockDiff::Op without including the private header.
// LiveBlockModel.cpp includes AstBlockDiff.h directly.
struct AstBlockDiffOp {
    int opKind   = 0;  // 0=Equal, 1=Insert, 2=Delete
    int prevIndex = -1;
    int nextIndex = -1;
};

class MARKOFF_LIVE_RENDER_EXPORT LiveBlockModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveBlockModel is provided by LiveListModelBinding")

public:
    enum Role {
        KindRole         = Qt::UserRole + 1,
        TextRole,
        HeadingLevelRole,
        CodeLanguageRole,
        BlockAnchorRole,
    };

    explicit LiveBlockModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void applyOps(const QList<AstBlockDiffOp> &ops,
                  const QList<BlockRecord> &nextRecords);

    const BlockRecord &recordAt(int row) const { return m_rows.at(row); }

    quint64 rowEditSequence(int row) const;
    void    setRowEditSequence(int row, quint64 editSeq);

    const QList<Markoff::SourceSpan> &spansAtRow(int row) const;

private:
    QList<BlockRecord> m_rows;
    QList<quint64>     m_rowEditSequences;
};

}  // namespace Markoff::LiveRender
```

Then `LiveListModelBinding` converts `AstBlockDiff::Op` to `AstBlockDiffOp` internally before calling `applyOps`. Actually that adds friction. Better solution: make `AstBlockDiff.h` a public header too (in `include/`), since `LiveBlockModel::applyOps` needs it in its signature.

**Revised approach:** promote `AstBlockDiff` to the public include path.

Move the file plan:
- `include/markoff/live-render/AstBlockDiff.h` (public)
- `src/AstBlockDiff.cpp` (private)

This is cleaner. Use this approach for Steps 1-4 in Task 5 as well (adjust path).

The final `LiveBlockModel.h` that uses the public `AstBlockDiff.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/BlockRecord.h>
#include <markoff/live-render/AstBlockDiff.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <qqmlintegration.h>

namespace Markoff::LiveRender {

class MARKOFF_LIVE_RENDER_EXPORT LiveBlockModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveBlockModel is provided by LiveListModelBinding")

public:
    enum Role {
        KindRole         = Qt::UserRole + 1,
        TextRole,
        HeadingLevelRole,
        CodeLanguageRole,
        BlockAnchorRole,
    };

    explicit LiveBlockModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void applyOps(const QList<AstBlockDiff::Op> &ops,
                  const QList<BlockRecord> &nextRecords);

    const BlockRecord &recordAt(int row) const { return m_rows.at(row); }

    quint64 rowEditSequence(int row) const;
    void    setRowEditSequence(int row, quint64 editSeq);

    const QList<Markoff::SourceSpan> &spansAtRow(int row) const;

private:
    QList<BlockRecord> m_rows;
    QList<quint64>     m_rowEditSequences;
};

}  // namespace Markoff::LiveRender
```

**Correction to Task 5:** Move `AstBlockDiff.h` to `include/markoff/live-render/AstBlockDiff.h` and keep `AstBlockDiff.cpp` in `src/`. Keep `BlockWalker.h` and `BlockWalker.cpp` in `src/` (private — only `LiveListModelBinding.cpp` includes it).

Apply this correction when executing Task 5 Step 1.

- [ ] **Step 3: Write `LiveBlockModel.cpp`**

Create `libs/markoff-live/src/LiveBlockModel.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveBlockModel.h>

#include <markoff/core/BlockAnchor.h>

namespace Markoff::LiveRender {

namespace {
const QList<Markoff::SourceSpan> kEmptySpans;
}

LiveBlockModel::LiveBlockModel(QObject *parent) : QAbstractListModel(parent) {}

int LiveBlockModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_rows.size();
}

QHash<int, QByteArray> LiveBlockModel::roleNames() const
{
    return {
        { KindRole,         "kind" },
        { TextRole,         "text" },
        { HeadingLevelRole, "headingLevel" },
        { CodeLanguageRole, "codeLanguage" },
        { BlockAnchorRole,  "blockAnchor" },
    };
}

QVariant LiveBlockModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const BlockRecord &r = m_rows[index.row()];
    switch (role) {
        case KindRole:          return r.kind;
        case TextRole:          return r.text;
        case HeadingLevelRole:  return r.headingLevel;
        case CodeLanguageRole:  return r.codeLanguage;
        case BlockAnchorRole:   return QVariant::fromValue(r.blockAnchor);
        default:                return {};
    }
}

void LiveBlockModel::applyOps(const QList<AstBlockDiff::Op> &ops,
                              const QList<BlockRecord> &nextRecords)
{
    int row = 0;
    for (const auto &op : ops) {
        switch (op.kind) {
            case AstBlockDiff::OpKind::Equal: {
                const BlockRecord &next = nextRecords[op.nextIndex];
                if (m_rows[row] != next) {
                    m_rows[row] = next;
                    Q_EMIT dataChanged(index(row), index(row));
                }
                ++row;
                break;
            }
            case AstBlockDiff::OpKind::Insert: {
                beginInsertRows(QModelIndex(), row, row);
                m_rows.insert(row, nextRecords[op.nextIndex]);
                m_rowEditSequences.insert(row, quint64(0));
                endInsertRows();
                ++row;
                break;
            }
            case AstBlockDiff::OpKind::Delete: {
                beginRemoveRows(QModelIndex(), row, row);
                m_rows.removeAt(row);
                m_rowEditSequences.removeAt(row);
                endRemoveRows();
                // Do NOT increment row — next op references the shifted index.
                break;
            }
        }
    }
}

quint64 LiveBlockModel::rowEditSequence(int row) const
{
    if (row < 0 || row >= m_rowEditSequences.size()) return 0;
    return m_rowEditSequences[row];
}

void LiveBlockModel::setRowEditSequence(int row, quint64 editSeq)
{
    if (row >= 0 && row < m_rowEditSequences.size())
        m_rowEditSequences[row] = editSeq;
}

const QList<Markoff::SourceSpan> &LiveBlockModel::spansAtRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) return kEmptySpans;
    return m_rows[row].inlineSpans;
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 4: Update the test to remove the stray include and use correct types**

Remove `#include "helpers.h"` from the test file. Also replace `AstBlockDiff::diff` calls with the public type. The test uses `AstBlockDiff::diff(prev, next)` where `prev` and `next` are `QList<BlockKey>` — this is correct since `AstBlockDiff` is now public. Verify the test as written compiles by checking all types are imported correctly.

The test includes:
```cpp
#include <markoff/live-render/LiveBlockModel.h>
// LiveBlockModel.h pulls in AstBlockDiff.h and BlockRecord.h transitively.
```

This is sufficient.

- [ ] **Step 5: Update CMakeLists files**

Edit `libs/markoff-live/CMakeLists.txt` — add to SOURCES:
```cmake
        include/markoff/live-render/AstBlockDiff.h
        src/AstBlockDiff.cpp
        src/BlockWalker.h
        src/BlockWalker.cpp
        include/markoff/live-render/LiveBlockModel.h
        src/LiveBlockModel.cpp
```

Edit `libs/markoff-live/tests/CMakeLists.txt` — append:
```cmake
qt_add_executable(tst_live_render_block_model
    tst_live_render_block_model.cpp
)
target_link_libraries(tst_live_render_block_model PRIVATE
    Qt6::Core Qt6::Test markoff_live_render)
add_test(NAME tst_live_render_block_model COMMAND tst_live_render_block_model)
```

- [ ] **Step 6: Build and run**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target tst_live_render_block_model -j 8 2>&1 | tail -10
ctest --test-dir build-dev -R '^tst_live_render_block_model$' --output-on-failure
```

Expected: `8/8 tests passed`.

---

## Task 7: LiveListModelBinding

The binding subscribes to `MarkoffDocument::parseUpdated` (4-arg), runs `BlockWalker`, computes the diff, and applies ops to the model.

**Files:**
- Create: `libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h`
- Create: `libs/markoff-live/src/LiveListModelBinding.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Write `LiveListModelBinding.h`**

Create `libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/BlockKindRegistry.h>

#include <QObject>
#include <memory>
#include <qqmlintegration.h>

namespace Markoff { class Document; class MarkoffDocument; }
namespace Markoff { class BlockAnchor; }

namespace Markoff::LiveRender {

/// Subscribes to `MarkoffDocument::parseUpdated`, runs `BlockWalker` to
/// snapshot the parsed tree, runs `AstBlockDiff` to produce a minimal edit
/// script, and calls `LiveBlockModel::applyOps`. Owns a `LiveBlockModel`
/// and a `BlockKindRegistry`.
///
/// R2 — read-only render. Cursor, selection, and freshness-rule application
/// are added in R3–R4.
class MARKOFF_LIVE_RENDER_EXPORT LiveListModelBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Markoff::MarkoffDocument *document
               READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Markoff::LiveRender::LiveBlockModel *model
               READ model CONSTANT)

public:
    explicit LiveListModelBinding(QObject *parent = nullptr);
    ~LiveListModelBinding() override;

    Markoff::MarkoffDocument *document() const;
    void setDocument(Markoff::MarkoffDocument *doc);

    LiveBlockModel *model() const;
    const BlockKindRegistry *registry() const;

Q_SIGNALS:
    void documentChanged();

private:
    void onParseUpdated(const Markoff::Document *parsed,
                        quint64 parseSequence,
                        const QList<Markoff::BlockAnchor> &blockAnchors,
                        quint64 parseInputEditSequence);

    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 2: Write `LiveListModelBinding.cpp`**

Create `libs/markoff-live/src/LiveListModelBinding.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/AstBlockDiff.h>
#include "BlockWalker.h"

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockAnchor.h>
#include <markoff-parser/Document.h>

#include <QList>

namespace Markoff::LiveRender {

struct LiveListModelBinding::Private {
    Markoff::MarkoffDocument *document = nullptr;
    LiveBlockModel            *model   = nullptr;
    BlockKindRegistry          registry;   // built-ins registered in ctor
    QList<BlockKey>            lastKeys;
    quint64                    lastParseInputEditSeq = 0; // for R4 freshness
};

LiveListModelBinding::LiveListModelBinding(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->model = new LiveBlockModel(this);
}

LiveListModelBinding::~LiveListModelBinding() = default;

Markoff::MarkoffDocument *LiveListModelBinding::document() const
{
    return d->document;
}

void LiveListModelBinding::setDocument(Markoff::MarkoffDocument *doc)
{
    if (d->document == doc) return;
    if (d->document)
        QObject::disconnect(d->document, nullptr, this, nullptr);
    d->document = doc;
    if (d->document) {
        QObject::connect(d->document, &Markoff::MarkoffDocument::parseUpdated,
                         this, &LiveListModelBinding::onParseUpdated);
    }
    Q_EMIT documentChanged();
}

LiveBlockModel *LiveListModelBinding::model() const
{
    return d->model;
}

const BlockKindRegistry *LiveListModelBinding::registry() const
{
    return &d->registry;
}

void LiveListModelBinding::onParseUpdated(const Markoff::Document *parsed,
                                          quint64 /*parseSequence*/,
                                          const QList<Markoff::BlockAnchor> &blockAnchors,
                                          quint64 parseInputEditSequence)
{
    if (!parsed) return;

    d->lastParseInputEditSeq = parseInputEditSequence;

    QList<BlockRecord> records = BlockWalker::walk(parsed);

    // Align BlockAnchors with records (1:1 with topLevelBlocks()).
    QList<BlockKey> nextKeys;
    nextKeys.reserve(records.size());
    for (qsizetype i = 0; i < records.size(); ++i) {
        const Markoff::BlockAnchor anchor =
            (i < blockAnchors.size()) ? blockAnchors[i] : Markoff::BlockAnchor{};
        records[i].blockAnchor = anchor;
        nextKeys.append(BlockKey{ records[i].kind, anchor });
    }

    const QList<AstBlockDiff::Op> ops =
        AstBlockDiff::diff(d->lastKeys, nextKeys);

    d->model->applyOps(ops, records);
    d->lastKeys = std::move(nextKeys);
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 3: Add to library CMakeLists**

Edit `libs/markoff-live/CMakeLists.txt` — add to SOURCES:

```cmake
        include/markoff/live-render/LiveListModelBinding.h
        src/LiveListModelBinding.cpp
```

- [ ] **Step 4: Build the library**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff_live_render -j 8 2>&1 | tail -10
```

Expected: library builds cleanly.

---

## Task 8: QML files

Create the minimal `LiveView.qml` and five read-only block delegates. Also create the `qml/delegates/` directory.

**Files:**
- Create: `libs/markoff-live/qml/LiveView.qml` (replaces Placeholder.qml)
- Create: `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`
- Create: `libs/markoff-live/qml/delegates/HeadingDelegate.qml`
- Create: `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml`
- Create: `libs/markoff-live/qml/delegates/HorizontalRuleDelegate.qml`
- Create: `libs/markoff-live/qml/delegates/ImageDelegate.qml`
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Create the `qml/delegates/` directory**

```bash
mkdir -p libs/markoff-live/qml/delegates
```

- [ ] **Step 2: Write `LiveView.qml`**

Create `libs/markoff-live/qml/LiveView.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Qt.labs.qmlmodels 1.0

import "delegates"

/// Read-only live render. Displays markdown as a scrollable list of block
/// delegates dispatched by kind. No cursor, selection, or key handling in
/// R2 — those land in R3–R5.
///
/// Usage:
///   LiveListModelBinding { id: binding; document: ctxDocument }
///   LiveView { anchors.fill: parent; binding: binding }
ListView {
    id: root

    required property var binding   // LiveListModelBinding *

    model: binding ? binding.model : null
    clip: true
    spacing: 2

    delegate: DelegateChooser {
        role: "kind"

        DelegateChoice {
            roleValue: "paragraph"
            delegate: ParagraphDelegate {}
        }
        DelegateChoice {
            roleValue: "heading"
            delegate: HeadingDelegate {}
        }
        DelegateChoice {
            roleValue: "code-block"
            delegate: CodeBlockDelegate {}
        }
        DelegateChoice {
            roleValue: "hr"
            delegate: HorizontalRuleDelegate {}
        }
        DelegateChoice {
            roleValue: "image"
            delegate: ImageDelegate {}
        }
    }
}
```

- [ ] **Step 3: Write `ParagraphDelegate.qml`**

Create `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

/// Read-only paragraph delegate. Uses PlainText mode so structural is
/// correct for R4 when LiveEditBinding + InlineFormatHighlighter land.
/// Inline formatting (bold/italic etc.) is not rendered in R2; that
/// requires InlineFormatHighlighter (R6).
TextEdit {
    width: ListView.view ? ListView.view.width : 600
    readOnly: true
    textFormat: TextEdit.PlainText
    text: model.text
    wrapMode: TextEdit.Wrap
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    font.pixelSize: 14
    color: palette.text
    selectionColor: palette.highlight
}
```

- [ ] **Step 4: Write `HeadingDelegate.qml`**

Create `libs/markoff-live/qml/delegates/HeadingDelegate.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

/// Read-only heading delegate. Font size is driven by headingLevel (1–6).
TextEdit {
    width: ListView.view ? ListView.view.width : 600
    readOnly: true
    textFormat: TextEdit.PlainText
    text: model.text
    wrapMode: TextEdit.Wrap
    leftPadding: 8
    rightPadding: 8
    topPadding: 6
    bottomPadding: 2
    font.pixelSize: {
        switch (model.headingLevel) {
            case 1: return 28
            case 2: return 24
            case 3: return 20
            case 4: return 18
            case 5: return 16
            default: return 14
        }
    }
    font.bold: model.headingLevel <= 3
    color: palette.text
}
```

- [ ] **Step 5: Write `CodeBlockDelegate.qml`**

Create `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.kde.syntaxhighlighting

/// Read-only code block delegate. KSyntaxHighlighting colors the content;
/// the language is driven by the fence info-string (`codeLanguage` role).
Rectangle {
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight + 16
    color: Qt.rgba(0, 0, 0, 0.05)
    radius: 4

    TextEdit {
        id: edit
        anchors {
            left: parent.left; right: parent.right
            top: parent.top; bottom: parent.bottom
            margins: 8
        }
        readOnly: true
        textFormat: TextEdit.PlainText
        text: model.text
        wrapMode: TextEdit.NoWrap
        font.family: "monospace"
        font.pixelSize: 13
        color: palette.text

        SyntaxHighlighter {
            textEdit: edit
            definition: model.codeLanguage.length > 0 ? model.codeLanguage : "None"
        }
    }
}
```

- [ ] **Step 6: Write `HorizontalRuleDelegate.qml`**

Create `libs/markoff-live/qml/delegates/HorizontalRuleDelegate.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

/// Read-only horizontal rule. A thin separator line.
Rectangle {
    width: ListView.view ? ListView.view.width : 600
    height: 1
    color: palette.mid
    topPadding: 8
    bottomPadding: 8
}
```

- [ ] **Step 7: Write `ImageDelegate.qml`**

Create `libs/markoff-live/qml/delegates/ImageDelegate.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

/// Read-only image delegate. The parser's topLevelBlocks() does not extract
/// imageSrc from the image syntax in R2; the source markdown text is shown
/// as a styled placeholder. Image URL extraction and live rendering land
/// in a later phase (R6+).
TextEdit {
    width: ListView.view ? ListView.view.width : 600
    readOnly: true
    textFormat: TextEdit.PlainText
    text: model.text
    wrapMode: TextEdit.Wrap
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    font.pixelSize: 13
    color: palette.placeholderText   // visually distinct from paragraph text
}
```

- [ ] **Step 8: Update CMakeLists.txt — replace Placeholder.qml, add all new QML files**

Edit `libs/markoff-live/CMakeLists.txt`. Replace the QML_FILES block:

```cmake
    QML_FILES
        qml/LiveView.qml
        qml/delegates/ParagraphDelegate.qml
        qml/delegates/HeadingDelegate.qml
        qml/delegates/CodeBlockDelegate.qml
        qml/delegates/HorizontalRuleDelegate.qml
        qml/delegates/ImageDelegate.qml
```

(Remove `qml/Placeholder.qml`.)

Also delete the placeholder file from disk:

```bash
rm libs/markoff-live/qml/Placeholder.qml
```

---

## Task 9: App update

Wire the test app to load a Markdown file and display it via `LiveListModelBinding` + `LiveView`.

**Files:**
- Modify: `libs/markoff-live/app/main.cpp`
- Modify: `libs/markoff-live/app/Main.qml`
- Modify: `libs/markoff-live/app/CMakeLists.txt` (add foundation headers)

- [ ] **Step 1: Rewrite `app/main.cpp`**

Replace the contents of `libs/markoff-live/app/main.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QQuickStyle>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

/// Test app for markoff-live-render R2. Loads a Markdown file and renders
/// it read-only via LiveListModelBinding + LiveView. No editing.
/// Usage: markoff-live-app <markdown-file>
int main(int argc, char *argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication app(argc, argv);

    if (argc < 2) {
        qWarning("Usage: %s <markdown-file>", argv[0]);
        return 1;
    }

    QFile file(argv[1]);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Cannot open '%s': %s", argv[1],
                 qUtf8Printable(file.errorString()));
        return 1;
    }
    const QByteArray content = file.readAll();

    const quint16 replicaId =
        static_cast<quint16>(QRandomGenerator::global()->generate() & 0xFFFF);
    auto doc = std::make_unique<Markoff::MarkoffDocument>(replicaId);
    doc->resetContent(content, Markoff::Origin::FirstOpen);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        QStringLiteral("ctxDocument"), doc.get());
    engine.rootContext()->setContextProperty(
        QStringLiteral("ctxTitle"),
        QFileInfo(argv[1]).fileName());

    engine.loadFromModule("org.markoff.live.render.app", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
```

- [ ] **Step 2: Rewrite `app/Main.qml`**

Replace the contents of `libs/markoff-live/app/Main.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

ApplicationWindow {
    id: window
    width: 900
    height: 700
    visible: true
    title: ctxTitle + " — markoff-live-render (R2)"

    LiveListModelBinding {
        id: modelBinding
        document: ctxDocument
    }

    LiveView {
        anchors.fill: parent
        binding: modelBinding
    }
}
```

- [ ] **Step 3: Update `app/CMakeLists.txt` to link foundation (for markoff-foundation headers)**

The app's C++ now includes `<markoff-foundation/MarkoffDocument.h>`. `markoff_core` is already reachable transitively via `markoff_live_render`, but explicit linking is cleaner:

Replace the contents of `libs/markoff-live/app/CMakeLists.txt`:

```cmake
qt_add_executable(markoff-live-app
    main.cpp
)

qt_add_qml_module(markoff-live-app
    URI org.markoff.live.render.app
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(markoff-live-app PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    markoff_live_render
    markoff_live_renderplugin
    markoff_core
)

qt_import_qml_plugins(markoff-live-app)
```

---

## Task 10: Full build, test run, and acceptance verification

- [ ] **Step 1: Reconfigure CMake**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -5
```

Expected: configure succeeds, no errors.

- [ ] **Step 2: Build all targets**

```bash
cmake --build build-dev --target markoff_live_render tst_live_render_coords tst_live_render_block_model tst_live_render_registry tst_live_render_skeleton markoff-live-app -j 8 2>&1 | tail -20
```

Expected: all six targets build without error.

- [ ] **Step 3: Run the new test suite**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 4
```

Expected: all four test executables pass (skeleton + registry + coords + block_model).

- [ ] **Step 4: Run the full fast-tier suite**

```bash
ctest --test-dir build-dev -E "tst_realistic|tst_benchmark|tst_view_qml_live_view_qml" --output-on-failure -j 8 2>&1 | tail -10
```

Expected: N+K tests pass (prior baseline +K new tests). No regressions.

- [ ] **Step 5: Manual acceptance check — launch the app**

```bash
./build-dev/bin/markoff-live-app docs/specs/2026-05-02-live-render-restoration-design.md
```

Expected:
- Window opens showing the spec document as a scrollable list of blocks.
- Headings are visually larger and bold (levels 1–3).
- Code blocks have syntax-highlighted text in a monospace font on a tinted background.
- Horizontal rules appear as thin separator lines.
- Document is scrollable; resizing the window reflows the text.
- No crash on open or scroll.

---

## Task 11: Status doc update and commit

- [ ] **Step 1: Update `docs/restoration-status.md`**

Update the R2 row in the Phase board:
```
| **R2** | [r2-read-only-render](plans/2026-05-02-live-render-r2-read-only-render.md) | `complete` | see commit | Read-only render: L0 Coords + L1 view + L2 diff model. |
```

Add an entry to the Recent-changes log:
```
| 2026-05-02 | see commit | feat(live-render): R2 complete — read-only render with diff model |
```

Update the TL;DR to point at R3:
```
> **R1 and R2 are complete.** Next session: R3 — cursor + selection (Shape 1; LiveCursorState; BlockHitTester). Write the R3 plan from spec §11 R3, then execute it.
```

- [ ] **Step 2: Review the diff before committing**

```bash
git status
git diff --stat
```

Verify staged files match what was planned: new library files, updated CMakeLists, updated app, updated QML, status doc.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt libs/markoff-live docs/restoration-status.md

git commit -m "$(cat <<'EOF'
feat(live-render): R2 — read-only render with diff-driven model

L0 (Coordinates: byteToQtPos/qtPosToByte, allocation-free UTF-8↔UTF-16),
L1 (LiveView.qml + 5 read-only delegates: paragraph, heading, code-block,
hr, image), and L2 (LiveBlockModel + AstBlockDiff port + BlockWalker port +
LiveListModelBinding subscribing to the 4-arg parseUpdated) are complete.

BlockKindRegistry ships with all five built-in descriptors. Per-row edit
sequence tracking is in place for the R4 freshness rule. inlineSpans are
baked into BlockRecord via BlockWalker for InlineFormatHighlighter (R6).

App now takes a file argument and renders any Markdown file read-only.
Acceptance: visual check passes; fast-tier suite unchanged.

Spec: docs/specs/2026-05-02-live-render-restoration-design.md §11 R2.
EOF
)"
```

- [ ] **Step 4: Fix commit SHA in restoration-status.md**

```bash
git log --oneline -1
```

Replace `see commit` with the actual SHA in both status doc locations, then amend:

```bash
# Edit docs/restoration-status.md to replace "see commit" with the actual SHA.
git add docs/restoration-status.md
git commit -m "docs(status): fix R2 commit SHA in restoration-status.md"
```

---

## Self-review

**Spec coverage check:**

| Spec §11 R2 requirement | Task |
|---|---|
| `Coordinates.{h,cpp}` with unit-test coverage | Task 4 |
| `LiveBlockModel` with per-row `lastEditEditSequence` | Task 6 |
| Diff via `AstBlockDiff` (port) | Task 5 |
| `LiveListModelBinding` subscribes to `parseUpdated` 4-arg | Task 7 |
| `LiveView.qml` + 5 read-only delegates | Task 8 |
| `BlockKindRegistry` with built-ins | Task 3 |
| Test app loads Markdown file, renders correctly | Task 9 |
| KSyntaxHighlighting on code blocks | Task 8 Step 5 |
| `tst_live_render_block_model` passes | Task 6 |

**Placeholder scan:** No TBDs, no incomplete steps. Every step has actual code.

**Type consistency:**
- `AstBlockDiff::OpKind::{Equal, Insert, Delete}` used in `LiveBlockModel::applyOps` — matches `AstBlockDiff.h` definition.
- `BlockKey {kind, anchor}` — matches `BlockRecord.h` definition and test helper `keyOf()`.
- `LiveListModelBinding::onParseUpdated` signature matches `MarkoffDocument::parseUpdated` 4-arg signal.
- `Markoff::LiveRender::Coordinates::byteToQtPos` / `qtPosToByte` — used consistently in tests and header.

**One architectural note:** `AstBlockDiff.h` is promoted to the public include path (Task 5 correction) so that `LiveBlockModel.h` can use `AstBlockDiff::Op` in its public API. `BlockWalker.h` stays private in `src/` — only `LiveListModelBinding.cpp` includes it.
