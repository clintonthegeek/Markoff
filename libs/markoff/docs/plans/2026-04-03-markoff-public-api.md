# Markoff Public API Implementation Plan

> **Status: IMPLEMENTED** — All seven public headers, Theme system,
> Document query API, and ResourceProvider interface shipped.

**Goal:** Implement the public API defined in `libs/markoff/docs/specs/2026-04-02-markoff-public-api-design.md` — seven public headers giving host applications full control over theming, editor behavior, resource resolution, and document querying.

**Architecture:** Composed API with shared configuration objects. Widgets (`Editor`, `ReadingView`) accept focused config structs (`Theme`, `EditorSettings`, `RenderSettings`) and an abstract `ResourceProvider`. The `Document` class gains rich query methods. Theming is harvested from QOwnNotes' scheme model at `~/src/QOwnNotes/src/configurations/schemes.conf`.

**Tech Stack:** C++20, Qt6 (Core/Gui/Widgets), KDE Frameworks 6 (SyntaxHighlighting), CMake

**Important context:**
- The spec is at `libs/markoff/docs/specs/2026-04-02-markoff-public-api-design.md` — read it first.
- Build with: `cmake -B build -DCORBOMITE_DEV_BUILD=ON && cmake --build build` (from repo root `/home/clinton/dev/Corbomite`)
- Run tests: `cd build && ctest -R tst_markoff --output-on-failure`
- Do NOT create feature branches — work directly on master.
- The markoff library lives at `libs/markoff/`. Its public headers are in `libs/markoff/include/markoff/`. Its internal sources are in `libs/markoff/src/`.
- Corbomite consumes markoff via `libs/core/src/MarkoffRenderEngine.cpp` and `libs/core/include/corbomite/core/MarkoffRenderEngine.h`.
- Tests define expected behavior. When a test fails, fix the code, not the test.

---

## File Map

### New files to create

| File | Purpose |
|------|---------|
| `libs/markoff/include/markoff/Theme.h` | Element enum, Theme struct, factory declarations |
| `libs/markoff/src/Theme.cpp` | Default themes, fromSchemeFile() |
| `libs/markoff/include/markoff/EditorSettings.h` | Behavioral editor config struct |
| `libs/markoff/include/markoff/ResourceProvider.h` | Abstract interface + FilesystemResourceProvider |
| `libs/markoff/src/ResourceProvider.cpp` | FilesystemResourceProvider implementation |
| `libs/markoff/tests/tst_theme.cpp` | Theme factory + scheme loading tests |
| `libs/markoff/tests/tst_document_queries.cpp` | Document query API tests |
| `libs/markoff/tests/tst_resourceprovider.cpp` | FilesystemResourceProvider tests |

### Existing files to modify

| File | Changes |
|------|---------|
| `libs/markoff/include/markoff/Document.h` | Add info structs + query method declarations |
| `libs/markoff/src/Document.cpp` | Implement query methods |
| `libs/markoff/include/markoff/RenderSettings.h` | Remove `baseFontSizePt` and `basePath` |
| `libs/markoff/include/markoff/Editor.h` | Full API expansion (config, signals, formatting, search) |
| `libs/markoff/src/Editor.cpp` | Implement new API methods |
| `libs/markoff/include/markoff/ReadingView.h` | Expand API (theme, resource provider, setMarkdown, naturalHeight) |
| `libs/markoff/src/ReadingView.cpp` | Implement expanded API |
| `libs/markoff/src/MarkdownHighlighter.h` | Accept Theme instead of hardcoded formats |
| `libs/markoff/src/MarkdownHighlighter.cpp` | Apply Theme formats |
| `libs/markoff/src/SceneCoordinator.h` | Accept Theme, ResourceProvider |
| `libs/markoff/src/SceneCoordinator.cpp` | Pass theme/provider to items |
| `libs/markoff/src/Renderer.cpp` | Use Theme for colors (replaces hardcoded values) |
| `libs/markoff/CMakeLists.txt` | Add new source files |
| `libs/markoff/tests/CMakeLists.txt` | Add new test targets |
| `libs/core/include/corbomite/core/MarkoffRenderEngine.h` | Adapt to RenderSettings changes |
| `libs/core/src/MarkoffRenderEngine.cpp` | Adapt to RenderSettings changes |

---

### Task 1: Create Theme.h and EditorSettings.h public headers

**Files:**
- Create: `libs/markoff/include/markoff/Theme.h`
- Create: `libs/markoff/include/markoff/EditorSettings.h`

These are pure header files with no implementation dependencies yet. They compile standalone (only need Qt headers).

- [ ] **Step 1: Create Theme.h**

```cpp
// libs/markoff/include/markoff/Theme.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_THEME_H
#define MARKOFF_THEME_H

#include <QFont>
#include <QHash>
#include <QString>
#include <QTextCharFormat>

namespace Markoff {

enum class Element {
    // Base
    Text,
    CurrentLineBackground,

    // Headings
    H1, H2, H3, H4, H5, H6,

    // Inline formatting
    Bold, Italic, Strikethrough, InlineCode,
    Highlight,          // ==text==
    Comment,            // %%text%%
    Tag,                // #tag

    // Links
    Link,               // [text](url)
    WikiLink,           // [[note]]
    BrokenLink,         // unresolvable link
    Image,              // ![alt](src)

    // Block-level
    CodeBlock,
    BlockQuote,
    HorizontalRule,
    ListMarker,
    Table,
    FrontmatterBlock,
    Callout,

    // Task checkboxes
    CheckboxUnchecked,
    CheckboxChecked,

    // Syntax highlighting within code blocks
    CodeKeyword, CodeString, CodeComment, CodeType,
    CodeNumLiteral, CodeBuiltIn, CodeOther,

    // Misc
    MaskedSyntax,       // hidden delimiters in live preview
    TrailingSpace,
};

struct Theme {
    QHash<Element, QTextCharFormat> formats;

    QFont textFont;     // base proportional font
    QFont codeFont;     // base monospace font

    static Theme defaultLight();
    static Theme defaultDark();
    static Theme fromSchemeFile(const QString &path);
};

} // namespace Markoff

#endif // MARKOFF_THEME_H
```

- [ ] **Step 2: Create EditorSettings.h**

```cpp
// libs/markoff/include/markoff/EditorSettings.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_EDITORSETTINGS_H
#define MARKOFF_EDITORSETTINGS_H

namespace Markoff {

struct EditorSettings {
    int tabSize = 4;
    bool lineNumbers = false;
    bool lineWrap = true;
    bool highlightCurrentLine = true;
    bool highlightingEnabled = true;
};

} // namespace Markoff

#endif // MARKOFF_EDITORSETTINGS_H
```

- [ ] **Step 3: Verify headers compile**

Add a minimal source file entry to CMakeLists.txt so the headers are included in the build (they'll be used by Theme.cpp in the next task, but for now just verify they parse):

Run:
```bash
cd /home/clinton/dev/Corbomite && cmake --build build --target markoff 2>&1 | tail -5
```

The build should succeed (the headers aren't included anywhere yet, but they should be syntactically valid for the next task).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff/include/markoff/Theme.h libs/markoff/include/markoff/EditorSettings.h
git commit -m "api: add Theme.h and EditorSettings.h public headers"
```

---

### Task 2: Implement Theme factory methods (defaultLight, defaultDark)

**Files:**
- Create: `libs/markoff/src/Theme.cpp`
- Create: `libs/markoff/tests/tst_theme.cpp`
- Modify: `libs/markoff/CMakeLists.txt`
- Modify: `libs/markoff/tests/CMakeLists.txt`

The default themes are harvested from QOwnNotes' Light and Dark schemes at `~/src/QOwnNotes/src/configurations/schemes.conf`. Study that file for the exact color values. The QOwnNotes scheme uses integer indices for elements — you'll need to map those to `Markoff::Element` values. The key mappings are:

| QOwnNotes index | Markoff::Element |
|-----------------|------------------|
| -1 | Text |
| 0 | Link |
| 4 | CodeBlock |
| 7 | Italic |
| 8 | Bold |
| 9 | ListMarker |
| 11 | Comment (HTML) |
| 12-17 | H1-H6 |
| 18 | BlockQuote |
| 21 | HorizontalRule |
| 22 | Table |
| 23 | InlineCode |
| 24 | MaskedSyntax |
| 25 | CurrentLineBackground |
| 26 | BrokenLink |
| 27 | FrontmatterBlock |
| 28 | TrailingSpace |
| 29 | CheckboxUnchecked |
| 30 | CheckboxChecked |
| 1000 | CodeKeyword |
| 1001 | CodeString |
| 1002 | CodeComment |
| 1003 | CodeType |
| 1004 | CodeOther |
| 1005 | CodeNumLiteral |
| 1006 | CodeBuiltIn |

QOwnNotes stores colors as `@Variant(...)` — Qt's QSettings serialization of QColor. Heading font sizes use `FontSizeAdaption_N` as a percentage (H1=200%, H2=160%, H3=130%, H4=100%, H5=90%, H6=90%).

Elements not in QOwnNotes that markoff adds (Highlight, Tag, WikiLink, Image, Callout, Strikethrough, CheckboxChecked/Unchecked) need sensible defaults defined by us.

- [ ] **Step 1: Write failing test**

```cpp
// libs/markoff/tests/tst_theme.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "markoff/Theme.h"

class TestTheme : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testDefaultLightHasTextFormat();
    void testDefaultLightHasAllHeadings();
    void testDefaultLightHeadingSizeAdaptation();
    void testDefaultLightCodeUsesMonospace();
    void testDefaultDarkHasTextFormat();
    void testDefaultDarkBackgroundIsDark();
    void testDefaultLightBoldIsBold();
};

void TestTheme::testDefaultLightHasTextFormat()
{
    auto theme = Markoff::Theme::defaultLight();
    QVERIFY(theme.formats.contains(Markoff::Element::Text));
    // Text foreground should be dark (near black)
    QColor fg = theme.formats[Markoff::Element::Text].foreground().color();
    QVERIFY(fg.isValid());
    QVERIFY(fg.lightness() < 100);
}

void TestTheme::testDefaultLightHasAllHeadings()
{
    auto theme = Markoff::Theme::defaultLight();
    QVERIFY(theme.formats.contains(Markoff::Element::H1));
    QVERIFY(theme.formats.contains(Markoff::Element::H2));
    QVERIFY(theme.formats.contains(Markoff::Element::H3));
    QVERIFY(theme.formats.contains(Markoff::Element::H4));
    QVERIFY(theme.formats.contains(Markoff::Element::H5));
    QVERIFY(theme.formats.contains(Markoff::Element::H6));
}

void TestTheme::testDefaultLightHeadingSizeAdaptation()
{
    auto theme = Markoff::Theme::defaultLight();
    int baseSize = theme.textFont.pointSize();
    QVERIFY(baseSize > 0);

    // H1 should be larger than H2, H2 larger than H3, etc.
    auto h1Font = theme.formats[Markoff::Element::H1].font();
    auto h2Font = theme.formats[Markoff::Element::H2].font();
    auto h3Font = theme.formats[Markoff::Element::H3].font();
    QVERIFY(h1Font.pointSize() > h2Font.pointSize());
    QVERIFY(h2Font.pointSize() > h3Font.pointSize());
}

void TestTheme::testDefaultLightCodeUsesMonospace()
{
    auto theme = Markoff::Theme::defaultLight();
    QVERIFY(theme.codeFont.fixedPitch() || theme.codeFont.family().contains(
        QStringLiteral("Mono"), Qt::CaseInsensitive) ||
        theme.codeFont.styleHint() == QFont::Monospace);
}

void TestTheme::testDefaultDarkHasTextFormat()
{
    auto theme = Markoff::Theme::defaultDark();
    QVERIFY(theme.formats.contains(Markoff::Element::Text));
    // Text foreground should be light (near white)
    QColor fg = theme.formats[Markoff::Element::Text].foreground().color();
    QVERIFY(fg.isValid());
    QVERIFY(fg.lightness() > 150);
}

void TestTheme::testDefaultDarkBackgroundIsDark()
{
    auto theme = Markoff::Theme::defaultDark();
    QColor bg = theme.formats[Markoff::Element::Text].background().color();
    QVERIFY(bg.isValid());
    QVERIFY(bg.lightness() < 80);
}

void TestTheme::testDefaultLightBoldIsBold()
{
    auto theme = Markoff::Theme::defaultLight();
    QVERIFY(theme.formats.contains(Markoff::Element::Bold));
    QVERIFY(theme.formats[Markoff::Element::Bold].fontWeight() >= QFont::Bold);
}

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
```

- [ ] **Step 2: Add test target to CMakeLists**

Add to `libs/markoff/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_theme tst_theme.cpp)
add_test(NAME tst_markoff_theme COMMAND tst_markoff_theme)
target_link_libraries(tst_markoff_theme PRIVATE Qt6::Test markoff)
set_tests_properties(tst_markoff_theme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Add Theme.cpp to library CMakeLists**

Add `src/Theme.cpp` to the `add_library(markoff STATIC ...)` list in `libs/markoff/CMakeLists.txt`.

- [ ] **Step 4: Run test to verify it fails**

```bash
cd /home/clinton/dev/Corbomite && cmake -B build -DCORBOMITE_DEV_BUILD=ON && cmake --build build --target tst_markoff_theme 2>&1 | tail -10
```

Expected: Fails to compile because `Theme.cpp` doesn't exist yet.

- [ ] **Step 5: Implement Theme.cpp**

Create `libs/markoff/src/Theme.cpp`. The implementation must:

1. Set `textFont` to the application's default font at the base size (14pt).
2. Set `codeFont` to `QFontDatabase::systemFont(QFontDatabase::FixedFont)`.
3. For `defaultLight()`: populate the `formats` hash with colors from QOwnNotes' Light scheme. Read `~/src/QOwnNotes/src/configurations/schemes.conf` to get exact colors for the Light scheme (`EditorColorSchema-6033d61b-cb96-46d5-a3a8-20d5172017eb`). The `@Variant(...)` values are Qt-serialized QColors — you need to decode the hex bytes to extract RGB values. Alternatively, construct the QColors directly from the visible color values. Key colors from the Light scheme:
   - Text: foreground `#000000`, background `#ffffff`
   - Link: foreground `#fc6d00` (orange)
   - Bold: bold weight
   - Italic: italic style
   - H1-H6: foreground `#004599` (dark blue), background `#f1f1f4`, bold, sizes 200%/160%/130%/100%/90%/90% of base
   - CodeBlock/InlineCode: foreground `#008000` (green), background `#edfced`
   - BlockQuote: foreground `#6f9f00` (olive green)
   - ListMarker: foreground (accent color)
   - HorizontalRule: foreground muted gray, background `#ebebeb`
   - Table: foreground with purple tint, background `#f7f6ff`
   - FrontmatterBlock: muted, background tint
   - CurrentLineBackground: background `#fffae2`
   - Highlight: yellow background `#fff9c4`
   - Comment: gray/muted foreground
   - Tag: foreground `#E65100` (deep orange)
   - WikiLink: foreground teal/blue (distinct from standard Link)
   - MaskedSyntax: light gray foreground
   - Callout: blue foreground `#448aff`
   - Strikethrough: strikethrough decoration
   - CheckboxUnchecked/Checked: accent colors
   - Code syntax elements (CodeKeyword, CodeString, etc.): use InlineCode background with varied foreground colors

4. For `defaultDark()`: invert the Light theme — light text on dark background, muted heading backgrounds, etc. Use QOwnNotes' Dark scheme (`EditorColorSchema-cdbf28fc-1ddc-4d13-bb21-6a4043316a2f`) as reference if available, otherwise create a reasonable dark palette.

5. For heading font sizes, compute from `textFont.pointSize()`:
   ```cpp
   int baseSize = textFont.pointSize(); // 14
   // H1: 200% → 28pt, H2: 160% → 22pt, H3: 130% → 18pt
   // H4: 100% → 14pt, H5: 90% → 13pt, H6: 90% → 13pt
   ```

6. `fromSchemeFile()` can be a stub that returns `defaultLight()` for now — Task 10 implements the full parser.

```cpp
// libs/markoff/src/Theme.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/Theme.h"
#include <QFontDatabase>

namespace Markoff {

// Helper to create a format with foreground color
static QTextCharFormat fgFormat(const QColor &color)
{
    QTextCharFormat fmt;
    fmt.setForeground(color);
    return fmt;
}

// Helper to create a format with foreground + background
static QTextCharFormat fgBgFormat(const QColor &fg, const QColor &bg)
{
    QTextCharFormat fmt;
    fmt.setForeground(fg);
    fmt.setBackground(bg);
    return fmt;
}

// Helper to create a heading format
static QTextCharFormat headingFormat(const QColor &fg, const QColor &bg,
                                      const QFont &baseFont, int sizePercent)
{
    QTextCharFormat fmt;
    fmt.setForeground(fg);
    fmt.setBackground(bg);
    fmt.setFontWeight(QFont::Bold);
    QFont font = baseFont;
    font.setPointSize(qRound(baseFont.pointSize() * sizePercent / 100.0));
    fmt.setFont(font, QTextCharFormat::FontPropertiesSpecifiedOnly);
    // Re-set weight after setFont since setFont may reset it
    fmt.setFontWeight(QFont::Bold);
    return fmt;
}

Theme Theme::defaultLight()
{
    Theme t;
    t.textFont = QFont();
    t.textFont.setPointSize(14);
    t.codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    t.codeFont.setPointSize(14);

    // --- Base ---
    t.formats[Element::Text] = fgBgFormat(QColor(0, 0, 0), QColor(255, 255, 255));

    QTextCharFormat currentLineFmt;
    currentLineFmt.setBackground(QColor(255, 250, 226)); // #fffae2
    t.formats[Element::CurrentLineBackground] = currentLineFmt;

    // --- Headings (dark blue on light gray, bold, scaled sizes) ---
    QColor headFg(0, 69, 153);      // #004599
    QColor headBg(241, 241, 244);   // #f1f1f4
    t.formats[Element::H1] = headingFormat(headFg, headBg, t.textFont, 200);
    t.formats[Element::H2] = headingFormat(headFg, headBg, t.textFont, 160);
    t.formats[Element::H3] = headingFormat(headFg, headBg, t.textFont, 130);
    t.formats[Element::H4] = headingFormat(headFg, headBg, t.textFont, 100);
    t.formats[Element::H5] = headingFormat(headFg, headBg, t.textFont, 90);
    t.formats[Element::H6] = headingFormat(headFg, headBg, t.textFont, 90);

    // --- Inline formatting ---
    QTextCharFormat boldFmt;
    boldFmt.setFontWeight(QFont::Bold);
    t.formats[Element::Bold] = boldFmt;

    QTextCharFormat italicFmt;
    italicFmt.setFontItalic(true);
    t.formats[Element::Italic] = italicFmt;

    QTextCharFormat strikeFmt;
    strikeFmt.setFontStrikeOut(true);
    strikeFmt.setForeground(QColor(150, 150, 150));
    t.formats[Element::Strikethrough] = strikeFmt;

    QTextCharFormat inlineCodeFmt;
    inlineCodeFmt.setForeground(QColor(0, 128, 0));     // #008000
    inlineCodeFmt.setBackground(QColor(237, 252, 237));  // #edfced
    inlineCodeFmt.setFont(t.codeFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
    t.formats[Element::InlineCode] = inlineCodeFmt;

    QTextCharFormat highlightFmt;
    highlightFmt.setBackground(QColor(255, 249, 196));   // #fff9c4
    t.formats[Element::Highlight] = highlightFmt;

    QTextCharFormat commentFmt;
    commentFmt.setForeground(QColor(150, 150, 150));
    t.formats[Element::Comment] = commentFmt;

    t.formats[Element::Tag] = fgFormat(QColor(230, 81, 0)); // #E65100

    // --- Links ---
    QTextCharFormat linkFmt;
    linkFmt.setForeground(QColor(252, 109, 0));   // #fc6d00
    linkFmt.setFontUnderline(true);
    t.formats[Element::Link] = linkFmt;

    QTextCharFormat wikiLinkFmt;
    wikiLinkFmt.setForeground(QColor(0, 137, 123));  // #00897b teal
    wikiLinkFmt.setFontUnderline(true);
    t.formats[Element::WikiLink] = wikiLinkFmt;

    QTextCharFormat brokenLinkFmt;
    brokenLinkFmt.setForeground(QColor(211, 47, 47));  // #d32f2f red
    brokenLinkFmt.setFontUnderline(true);
    t.formats[Element::BrokenLink] = brokenLinkFmt;

    t.formats[Element::Image] = fgFormat(QColor(0, 137, 123)); // same as wikilink

    // --- Block-level ---
    QTextCharFormat codeBlockFmt;
    codeBlockFmt.setForeground(QColor(0, 128, 0));
    codeBlockFmt.setBackground(QColor(237, 252, 237));
    codeBlockFmt.setFont(t.codeFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
    t.formats[Element::CodeBlock] = codeBlockFmt;

    t.formats[Element::BlockQuote] = fgFormat(QColor(111, 159, 0)); // #6f9f00
    t.formats[Element::HorizontalRule] = fgBgFormat(QColor(180, 180, 180), QColor(235, 235, 235));
    t.formats[Element::ListMarker] = fgFormat(QColor(163, 0, 123)); // #a3007b
    t.formats[Element::Table] = fgBgFormat(QColor(99, 109, 239), QColor(247, 246, 255)); // #636def on #f7f6ff
    t.formats[Element::FrontmatterBlock] = fgBgFormat(QColor(120, 144, 156), QColor(245, 245, 245));
    t.formats[Element::Callout] = fgFormat(QColor(68, 138, 255)); // #448aff

    // --- Checkboxes ---
    t.formats[Element::CheckboxUnchecked] = fgFormat(QColor(120, 120, 120));
    QTextCharFormat checkedFmt;
    checkedFmt.setForeground(QColor(76, 175, 80)); // #4caf50 green
    checkedFmt.setFontStrikeOut(true);
    t.formats[Element::CheckboxChecked] = checkedFmt;

    // --- Code syntax ---
    QColor codeBg(237, 252, 237);
    t.formats[Element::CodeKeyword] = fgBgFormat(QColor(249, 38, 114), codeBg);  // #f92672
    t.formats[Element::CodeString]  = fgBgFormat(QColor(59, 162, 63), codeBg);   // #3ba23f
    t.formats[Element::CodeComment] = fgBgFormat(QColor(144, 139, 116), codeBg); // #908b74
    t.formats[Element::CodeType]    = fgBgFormat(QColor(99, 109, 239), codeBg);  // #636def
    t.formats[Element::CodeNumLiteral] = fgBgFormat(QColor(181, 124, 80), codeBg); // #b57c50
    t.formats[Element::CodeBuiltIn] = fgBgFormat(QColor(0, 134, 179), codeBg);   // #0086b3
    t.formats[Element::CodeOther]   = fgBgFormat(QColor(80, 80, 80), codeBg);

    // --- Misc ---
    t.formats[Element::MaskedSyntax] = fgFormat(QColor(200, 200, 200));

    QTextCharFormat trailingFmt;
    trailingFmt.setBackground(QColor(235, 235, 235));
    t.formats[Element::TrailingSpace] = trailingFmt;

    return t;
}

Theme Theme::defaultDark()
{
    Theme t;
    t.textFont = QFont();
    t.textFont.setPointSize(14);
    t.codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    t.codeFont.setPointSize(14);

    // --- Base ---
    t.formats[Element::Text] = fgBgFormat(QColor(210, 210, 210), QColor(40, 44, 52));

    QTextCharFormat currentLineFmt;
    currentLineFmt.setBackground(QColor(50, 55, 65));
    t.formats[Element::CurrentLineBackground] = currentLineFmt;

    // --- Headings ---
    QColor headFg(130, 170, 255);
    QColor headBg(45, 50, 60);
    t.formats[Element::H1] = headingFormat(headFg, headBg, t.textFont, 200);
    t.formats[Element::H2] = headingFormat(headFg, headBg, t.textFont, 160);
    t.formats[Element::H3] = headingFormat(headFg, headBg, t.textFont, 130);
    t.formats[Element::H4] = headingFormat(headFg, headBg, t.textFont, 100);
    t.formats[Element::H5] = headingFormat(headFg, headBg, t.textFont, 90);
    t.formats[Element::H6] = headingFormat(headFg, headBg, t.textFont, 90);

    // --- Inline formatting ---
    QTextCharFormat boldFmt;
    boldFmt.setFontWeight(QFont::Bold);
    boldFmt.setForeground(QColor(230, 230, 230));
    t.formats[Element::Bold] = boldFmt;

    QTextCharFormat italicFmt;
    italicFmt.setFontItalic(true);
    italicFmt.setForeground(QColor(220, 220, 220));
    t.formats[Element::Italic] = italicFmt;

    QTextCharFormat strikeFmt;
    strikeFmt.setFontStrikeOut(true);
    strikeFmt.setForeground(QColor(120, 120, 120));
    t.formats[Element::Strikethrough] = strikeFmt;

    QTextCharFormat inlineCodeFmt;
    inlineCodeFmt.setForeground(QColor(130, 200, 130));
    inlineCodeFmt.setBackground(QColor(50, 60, 50));
    inlineCodeFmt.setFont(t.codeFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
    t.formats[Element::InlineCode] = inlineCodeFmt;

    QTextCharFormat highlightFmt;
    highlightFmt.setBackground(QColor(100, 90, 40));
    highlightFmt.setForeground(QColor(255, 249, 196));
    t.formats[Element::Highlight] = highlightFmt;

    QTextCharFormat commentFmt;
    commentFmt.setForeground(QColor(100, 100, 100));
    t.formats[Element::Comment] = commentFmt;

    t.formats[Element::Tag] = fgFormat(QColor(255, 152, 0)); // #ff9800

    // --- Links ---
    QTextCharFormat linkFmt;
    linkFmt.setForeground(QColor(255, 152, 60));
    linkFmt.setFontUnderline(true);
    t.formats[Element::Link] = linkFmt;

    QTextCharFormat wikiLinkFmt;
    wikiLinkFmt.setForeground(QColor(77, 208, 191));
    wikiLinkFmt.setFontUnderline(true);
    t.formats[Element::WikiLink] = wikiLinkFmt;

    QTextCharFormat brokenLinkFmt;
    brokenLinkFmt.setForeground(QColor(239, 83, 80));
    brokenLinkFmt.setFontUnderline(true);
    t.formats[Element::BrokenLink] = brokenLinkFmt;

    t.formats[Element::Image] = fgFormat(QColor(77, 208, 191));

    // --- Block-level ---
    QTextCharFormat codeBlockFmt;
    codeBlockFmt.setForeground(QColor(130, 200, 130));
    codeBlockFmt.setBackground(QColor(50, 60, 50));
    codeBlockFmt.setFont(t.codeFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
    t.formats[Element::CodeBlock] = codeBlockFmt;

    t.formats[Element::BlockQuote] = fgFormat(QColor(160, 200, 80));
    t.formats[Element::HorizontalRule] = fgBgFormat(QColor(100, 100, 100), QColor(55, 60, 68));
    t.formats[Element::ListMarker] = fgFormat(QColor(200, 120, 200));
    t.formats[Element::Table] = fgBgFormat(QColor(150, 160, 255), QColor(45, 48, 60));
    t.formats[Element::FrontmatterBlock] = fgBgFormat(QColor(140, 155, 165), QColor(45, 48, 55));
    t.formats[Element::Callout] = fgFormat(QColor(100, 160, 255));

    // --- Checkboxes ---
    t.formats[Element::CheckboxUnchecked] = fgFormat(QColor(160, 160, 160));
    QTextCharFormat checkedFmt;
    checkedFmt.setForeground(QColor(100, 200, 100));
    checkedFmt.setFontStrikeOut(true);
    t.formats[Element::CheckboxChecked] = checkedFmt;

    // --- Code syntax ---
    QColor codeBg(50, 60, 50);
    t.formats[Element::CodeKeyword] = fgBgFormat(QColor(249, 38, 114), codeBg);
    t.formats[Element::CodeString]  = fgBgFormat(QColor(152, 195, 121), codeBg);
    t.formats[Element::CodeComment] = fgBgFormat(QColor(92, 99, 112), codeBg);
    t.formats[Element::CodeType]    = fgBgFormat(QColor(150, 160, 255), codeBg);
    t.formats[Element::CodeNumLiteral] = fgBgFormat(QColor(209, 154, 102), codeBg);
    t.formats[Element::CodeBuiltIn] = fgBgFormat(QColor(86, 182, 194), codeBg);
    t.formats[Element::CodeOther]   = fgBgFormat(QColor(190, 190, 190), codeBg);

    // --- Misc ---
    t.formats[Element::MaskedSyntax] = fgFormat(QColor(80, 80, 80));

    QTextCharFormat trailingFmt;
    trailingFmt.setBackground(QColor(55, 60, 68));
    t.formats[Element::TrailingSpace] = trailingFmt;

    return t;
}

Theme Theme::fromSchemeFile(const QString &path)
{
    Q_UNUSED(path)
    // Stub: full QOwnNotes INI parser implemented in Task 10
    return defaultLight();
}

} // namespace Markoff
```

- [ ] **Step 6: Run tests**

```bash
cd /home/clinton/dev/Corbomite/build && ctest -R tst_markoff_theme --output-on-failure
```

Expected: All 7 tests pass. If any fail, fix the Theme.cpp implementation (not the tests).

- [ ] **Step 7: Commit**

```bash
git add libs/markoff/src/Theme.cpp libs/markoff/tests/tst_theme.cpp libs/markoff/CMakeLists.txt libs/markoff/tests/CMakeLists.txt
git commit -m "api: implement Theme with defaultLight/defaultDark factories"
```

---

### Task 3: Create ResourceProvider interface and FilesystemResourceProvider

**Files:**
- Create: `libs/markoff/include/markoff/ResourceProvider.h`
- Create: `libs/markoff/src/ResourceProvider.cpp`
- Create: `libs/markoff/tests/tst_resourceprovider.cpp`
- Modify: `libs/markoff/CMakeLists.txt` — add `src/ResourceProvider.cpp`
- Modify: `libs/markoff/tests/CMakeLists.txt` — add test target

- [ ] **Step 1: Create ResourceProvider.h**

```cpp
// libs/markoff/include/markoff/ResourceProvider.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_RESOURCEPROVIDER_H
#define MARKOFF_RESOURCEPROVIDER_H

#include <QUrl>
#include <QString>
#include <optional>

namespace Markoff {

class ResourceProvider {
public:
    virtual ~ResourceProvider() = default;

    virtual QUrl resolveImage(const QString &name) const = 0;
    virtual std::optional<QString> resolveEmbed(const QString &name) const = 0;
    virtual QUrl resolveLink(const QString &target) const = 0;
    virtual bool linkExists(const QString &target) const = 0;
};

class FilesystemResourceProvider : public ResourceProvider {
public:
    explicit FilesystemResourceProvider(const QString &basePath);

    QUrl resolveImage(const QString &name) const override;
    std::optional<QString> resolveEmbed(const QString &name) const override;
    QUrl resolveLink(const QString &target) const override;
    bool linkExists(const QString &target) const override;

private:
    QString m_basePath;
};

} // namespace Markoff

#endif // MARKOFF_RESOURCEPROVIDER_H
```

- [ ] **Step 2: Write failing test**

```cpp
// libs/markoff/tests/tst_resourceprovider.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include "markoff/ResourceProvider.h"

class TestResourceProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testResolveImageExists();
    void testResolveImageNotFound();
    void testResolveEmbedExists();
    void testResolveEmbedNotFound();
    void testLinkExistsWithExtension();
    void testLinkExistsWithoutExtension();
    void testLinkNotExists();
};

void TestResourceProvider::testResolveImageExists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(dir.filePath(QStringLiteral("photo.png")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake png");
    f.close();

    Markoff::FilesystemResourceProvider provider(dir.path());
    QUrl url = provider.resolveImage(QStringLiteral("photo.png"));
    QVERIFY(url.isValid());
    QVERIFY(url.isLocalFile());
    QVERIFY(url.toLocalFile().endsWith(QStringLiteral("photo.png")));
}

void TestResourceProvider::testResolveImageNotFound()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Markoff::FilesystemResourceProvider provider(dir.path());
    QUrl url = provider.resolveImage(QStringLiteral("missing.png"));
    QVERIFY(url.isEmpty());
}

void TestResourceProvider::testResolveEmbedExists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(dir.filePath(QStringLiteral("Other Note.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    QTextStream ts(&f);
    ts << "# Other Note\n\nSome content.\n";
    f.close();

    Markoff::FilesystemResourceProvider provider(dir.path());
    auto content = provider.resolveEmbed(QStringLiteral("Other Note"));
    QVERIFY(content.has_value());
    QVERIFY(content->contains(QStringLiteral("Some content.")));
}

void TestResourceProvider::testResolveEmbedNotFound()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Markoff::FilesystemResourceProvider provider(dir.path());
    auto content = provider.resolveEmbed(QStringLiteral("Missing Note"));
    QVERIFY(!content.has_value());
}

void TestResourceProvider::testLinkExistsWithExtension()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(dir.filePath(QStringLiteral("MyNote.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    Markoff::FilesystemResourceProvider provider(dir.path());
    QVERIFY(provider.linkExists(QStringLiteral("MyNote.md")));
}

void TestResourceProvider::testLinkExistsWithoutExtension()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(dir.filePath(QStringLiteral("MyNote.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    Markoff::FilesystemResourceProvider provider(dir.path());
    QVERIFY(provider.linkExists(QStringLiteral("MyNote")));
}

void TestResourceProvider::testLinkNotExists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Markoff::FilesystemResourceProvider provider(dir.path());
    QVERIFY(!provider.linkExists(QStringLiteral("Nope")));
}

QTEST_MAIN(TestResourceProvider)
#include "tst_resourceprovider.moc"
```

- [ ] **Step 3: Add to CMakeLists files**

Add `src/ResourceProvider.cpp` to the library's `add_library(markoff STATIC ...)`.

Add to `libs/markoff/tests/CMakeLists.txt`:
```cmake
add_executable(tst_markoff_resourceprovider tst_resourceprovider.cpp)
add_test(NAME tst_markoff_resourceprovider COMMAND tst_markoff_resourceprovider)
target_link_libraries(tst_markoff_resourceprovider PRIVATE Qt6::Test markoff)
set_tests_properties(tst_markoff_resourceprovider PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 4: Implement ResourceProvider.cpp**

```cpp
// libs/markoff/src/ResourceProvider.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/ResourceProvider.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace Markoff {

FilesystemResourceProvider::FilesystemResourceProvider(const QString &basePath)
    : m_basePath(basePath)
{
}

QUrl FilesystemResourceProvider::resolveImage(const QString &name) const
{
    QFileInfo fi(QDir(m_basePath), name);
    if (fi.exists())
        return QUrl::fromLocalFile(fi.absoluteFilePath());
    return {};
}

std::optional<QString> FilesystemResourceProvider::resolveEmbed(const QString &name) const
{
    QString fileName = name;
    if (!fileName.endsWith(QStringLiteral(".md")))
        fileName += QStringLiteral(".md");

    QFile file(QDir(m_basePath).filePath(fileName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;

    QTextStream stream(&file);
    return stream.readAll();
}

QUrl FilesystemResourceProvider::resolveLink(const QString &target) const
{
    // Strip heading fragment for file resolution
    QString filePart = target;
    int hashPos = target.indexOf(QLatin1Char('#'));
    if (hashPos >= 0)
        filePart = target.left(hashPos);

    if (filePart.isEmpty())
        return {};

    QString fileName = filePart;
    if (!fileName.endsWith(QStringLiteral(".md")))
        fileName += QStringLiteral(".md");

    QFileInfo fi(QDir(m_basePath), fileName);
    if (fi.exists())
        return QUrl::fromLocalFile(fi.absoluteFilePath());
    return {};
}

bool FilesystemResourceProvider::linkExists(const QString &target) const
{
    QString filePart = target;
    int hashPos = target.indexOf(QLatin1Char('#'));
    if (hashPos >= 0)
        filePart = target.left(hashPos);

    if (filePart.isEmpty())
        return false;

    QString fileName = filePart;
    if (!fileName.endsWith(QStringLiteral(".md")))
        fileName += QStringLiteral(".md");

    return QFileInfo::exists(QDir(m_basePath).filePath(fileName));
}

} // namespace Markoff
```

- [ ] **Step 5: Build and run tests**

```bash
cd /home/clinton/dev/Corbomite && cmake -B build -DCORBOMITE_DEV_BUILD=ON && cmake --build build --target tst_markoff_resourceprovider && cd build && ctest -R tst_markoff_resourceprovider --output-on-failure
```

Expected: All 7 tests pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff/include/markoff/ResourceProvider.h libs/markoff/src/ResourceProvider.cpp libs/markoff/tests/tst_resourceprovider.cpp libs/markoff/CMakeLists.txt libs/markoff/tests/CMakeLists.txt
git commit -m "api: add ResourceProvider interface and FilesystemResourceProvider"
```

---

### Task 4: Add Document query API

**Files:**
- Modify: `libs/markoff/include/markoff/Document.h` — add info structs and query methods
- Modify: `libs/markoff/src/Document.cpp` — implement query methods
- Create: `libs/markoff/tests/tst_document_queries.cpp`
- Modify: `libs/markoff/tests/CMakeLists.txt`

The Document class has access to its parsed `Block` list via `d->blocks`. Each `Block` has `type`, `headingLevel`, `sourceOffset`, and `inlines` (list of `InlineRun`). Each `InlineRun` has `linkHref`, `wikiTarget`, `imageSrc`, `isTag`, `text`, `sourceOffset`. See `libs/markoff/src/DocumentBuilder_p.h` for the complete structure.

- [ ] **Step 1: Write failing test**

```cpp
// libs/markoff/tests/tst_document_queries.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "markoff/Document.h"

class TestDocumentQueries : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testHeadings();
    void testHeadingsEmpty();
    void testLinks();
    void testWikiLinks();
    void testTags();
    void testWordCount();
    void testCharacterCount();
    void testFootnotes();
};

void TestDocumentQueries::testHeadings()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "# First\n\nParagraph.\n\n## Second\n\n### Third\n"));
    auto headings = doc->headings();
    QCOMPARE(headings.size(), 3);
    QCOMPARE(headings[0].level, 1);
    QCOMPARE(headings[0].text, QStringLiteral("First"));
    QCOMPARE(headings[1].level, 2);
    QCOMPARE(headings[1].text, QStringLiteral("Second"));
    QCOMPARE(headings[2].level, 3);
    QCOMPARE(headings[2].text, QStringLiteral("Third"));
}

void TestDocumentQueries::testHeadingsEmpty()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral("Just a paragraph."));
    QVERIFY(doc->headings().isEmpty());
}

void TestDocumentQueries::testLinks()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "A [link](https://example.com) and [[WikiNote]] here.\n"));
    auto links = doc->links();
    QVERIFY(links.size() >= 2);
    // Should contain at least one Standard and one Wiki link
    bool hasStandard = false, hasWiki = false;
    for (const auto &l : links) {
        if (l.type == Markoff::LinkInfo::Standard) hasStandard = true;
        if (l.type == Markoff::LinkInfo::Wiki) hasWiki = true;
    }
    QVERIFY(hasStandard);
    QVERIFY(hasWiki);
}

void TestDocumentQueries::testWikiLinks()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "See [[Note One]] and [[Note Two|display]] and [standard](url).\n"));
    auto wikiLinks = doc->wikiLinks();
    QCOMPARE(wikiLinks.size(), 2);
    QCOMPARE(wikiLinks[0].type, Markoff::LinkInfo::Wiki);
    QCOMPARE(wikiLinks[1].type, Markoff::LinkInfo::Wiki);
}

void TestDocumentQueries::testTags()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "Some text with #project and #status/active tags.\n"));
    auto tags = doc->tags();
    QVERIFY(tags.size() >= 2);
    QStringList tagNames;
    for (const auto &t : tags)
        tagNames << t.name;
    QVERIFY(tagNames.contains(QStringLiteral("project")));
    QVERIFY(tagNames.contains(QStringLiteral("status/active")));
}

void TestDocumentQueries::testWordCount()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral("One two three four five."));
    QCOMPARE(doc->wordCount(), 5);
}

void TestDocumentQueries::testCharacterCount()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral("Hello"));
    QCOMPARE(doc->characterCount(), 5);
}

void TestDocumentQueries::testFootnotes()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "Text with a reference[^1].\n\n[^1]: This is the footnote content.\n"));
    auto footnotes = doc->footnotes();
    QCOMPARE(footnotes.size(), 1);
    QCOMPARE(footnotes[0].number, 1);
    QCOMPARE(footnotes[0].label, QStringLiteral("1"));
    QVERIFY(footnotes[0].content.contains(QStringLiteral("footnote content")));
}

QTEST_MAIN(TestDocumentQueries)
#include "tst_document_queries.moc"
```

- [ ] **Step 2: Add test target to CMakeLists**

Add to `libs/markoff/tests/CMakeLists.txt`:
```cmake
add_executable(tst_markoff_document_queries tst_document_queries.cpp)
add_test(NAME tst_markoff_document_queries COMMAND tst_markoff_document_queries)
target_link_libraries(tst_markoff_document_queries PRIVATE Qt6::Test markoff)
set_tests_properties(tst_markoff_document_queries PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cd /home/clinton/dev/Corbomite && cmake -B build -DCORBOMITE_DEV_BUILD=ON && cmake --build build --target tst_markoff_document_queries 2>&1 | tail -10
```

Expected: Compile error — `HeadingInfo` not defined, `headings()` not declared.

- [ ] **Step 4: Update Document.h with info structs and query methods**

Add the following to `libs/markoff/include/markoff/Document.h`, inside `namespace Markoff`, before the `Document` class:

```cpp
struct HeadingInfo {
    int level;
    QString text;
    int sourceOffset;
};

struct LinkInfo {
    enum Type { Standard, Wiki, Image, Embed };
    Type type;
    QString target;
    QString displayText;
    int sourceOffset;
};

struct TagInfo {
    QString name;
    int sourceOffset;
};

struct FootnoteInfo {
    int number;
    QString label;
    QString content;
};
```

Add these public method declarations to the `Document` class (after the existing public methods):

```cpp
    QList<HeadingInfo> headings() const;
    QList<LinkInfo> links() const;
    QList<LinkInfo> wikiLinks() const;
    QList<TagInfo> tags() const;
    QList<FootnoteInfo> footnotes() const;
    int wordCount() const;
    int characterCount() const;
```

Add `#include <QList>` to the includes at the top of Document.h.

- [ ] **Step 5: Implement query methods in Document.cpp**

Add these implementations to `libs/markoff/src/Document.cpp`. The implementations walk `d->blocks` (the parsed AST — a `QList<Block>`) and `d->footnotes` (a `QList<Footnote>`).

The internal `Block` type is defined in `DocumentBuilder_p.h`. Key fields:
- `Block::type` — `MD_BLOCKTYPE` enum (`MD_BLOCK_H` for headings, `MD_BLOCK_P` for paragraphs, etc.)
- `Block::headingLevel` — 1-6 for headings
- `Block::sourceOffset` — byte offset in UTF-8 source
- `Block::inlines` — `QList<InlineRun>` with text/formatting
- `Block::children` — nested `QList<Block>`
- `InlineRun::linkHref`, `InlineRun::wikiTarget`, `InlineRun::imageSrc` — link data
- `InlineRun::isTag` — tag flag
- `InlineRun::text` — inline text content
- `InlineRun::sourceOffset` — byte offset

The methods should recursively walk `blocks` and their `children`. Use a helper lambda or static function to avoid repetition.

For `wordCount()`, count by splitting `d->source` (or `markdownContent()`) on whitespace, excluding frontmatter. For `characterCount()`, return `markdownContent().length()`.

For `footnotes()`, convert the existing internal `d->footnotes` list to `QList<FootnoteInfo>`.

```cpp
// Add these includes at the top of Document.cpp:
#include <md4c.h>

// Add these implementations after the existing methods:

// Helper: walk blocks recursively collecting results
static void collectFromBlocks(const QList<Block> &blocks,
                               QList<HeadingInfo> &headings,
                               QList<LinkInfo> &links,
                               QList<TagInfo> &tags)
{
    for (const Block &block : blocks) {
        if (block.type == MD_BLOCK_H) {
            HeadingInfo h;
            h.level = block.headingLevel;
            h.sourceOffset = block.sourceOffset;
            // Concatenate inline text for heading text
            QString text;
            for (const InlineRun &run : block.inlines)
                text += run.text;
            h.text = text.trimmed();
            headings.append(h);
        }

        for (const InlineRun &run : block.inlines) {
            if (!run.wikiTarget.isEmpty()) {
                LinkInfo li;
                li.type = run.imageSrc.isEmpty() ? LinkInfo::Wiki : LinkInfo::Embed;
                li.target = run.wikiTarget;
                li.displayText = run.text;
                li.sourceOffset = run.sourceOffset;
                links.append(li);
            } else if (!run.linkHref.isEmpty()) {
                LinkInfo li;
                li.type = LinkInfo::Standard;
                li.target = run.linkHref;
                li.displayText = run.text;
                li.sourceOffset = run.sourceOffset;
                links.append(li);
            } else if (!run.imageSrc.isEmpty()) {
                LinkInfo li;
                li.type = LinkInfo::Image;
                li.target = run.imageSrc;
                li.displayText = run.text;
                li.sourceOffset = run.sourceOffset;
                links.append(li);
            }

            if (run.isTag) {
                TagInfo ti;
                ti.name = run.text;
                // Strip leading # if present
                if (ti.name.startsWith(QLatin1Char('#')))
                    ti.name = ti.name.mid(1);
                ti.sourceOffset = run.sourceOffset;
                tags.append(ti);
            }
        }

        collectFromBlocks(block.children, headings, links, tags);
    }
}

QList<HeadingInfo> Document::headings() const
{
    QList<HeadingInfo> result;
    QList<LinkInfo> unusedLinks;
    QList<TagInfo> unusedTags;
    collectFromBlocks(d->blocks, result, unusedLinks, unusedTags);
    return result;
}

QList<LinkInfo> Document::links() const
{
    QList<HeadingInfo> unusedHeadings;
    QList<LinkInfo> result;
    QList<TagInfo> unusedTags;
    collectFromBlocks(d->blocks, unusedHeadings, result, unusedTags);
    return result;
}

QList<LinkInfo> Document::wikiLinks() const
{
    QList<LinkInfo> all = links();
    QList<LinkInfo> result;
    for (const LinkInfo &li : all) {
        if (li.type == LinkInfo::Wiki || li.type == LinkInfo::Embed)
            result.append(li);
    }
    return result;
}

QList<TagInfo> Document::tags() const
{
    QList<HeadingInfo> unusedHeadings;
    QList<LinkInfo> unusedLinks;
    QList<TagInfo> result;
    collectFromBlocks(d->blocks, unusedHeadings, unusedLinks, result);
    return result;
}

QList<FootnoteInfo> Document::footnotes() const
{
    QList<FootnoteInfo> result;
    for (const Footnote &fn : d->footnotes) {
        FootnoteInfo fi;
        fi.number = fn.number;
        fi.label = fn.label;
        fi.content = fn.content;
        result.append(fi);
    }
    return result;
}

int Document::wordCount() const
{
    const QString text = markdownContent();
    if (text.isEmpty())
        return 0;
    // Split on whitespace, skip empty parts
    const QStringList words = text.split(QRegularExpression(QStringLiteral("\\s+")),
                                          Qt::SkipEmptyParts);
    return words.size();
}

int Document::characterCount() const
{
    return markdownContent().length();
}
```

- [ ] **Step 6: Build and run tests**

```bash
cd /home/clinton/dev/Corbomite && cmake --build build && cd build && ctest -R tst_markoff_document --output-on-failure
```

Expected: Both `tst_markoff_document` (existing) and `tst_markoff_document_queries` (new) pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff/include/markoff/Document.h libs/markoff/src/Document.cpp libs/markoff/tests/tst_document_queries.cpp libs/markoff/tests/CMakeLists.txt
git commit -m "api: add Document query API (headings, links, tags, footnotes, word count)"
```

---

### Task 5: Migrate RenderSettings (remove baseFontSizePt and basePath)

**Files:**
- Modify: `libs/markoff/include/markoff/RenderSettings.h` — remove `baseFontSizePt` and `basePath`
- Modify: `libs/markoff/src/Renderer.cpp` — stop using `baseFontSizePt` and `basePath`
- Modify: `libs/core/src/MarkoffRenderEngine.cpp` — adapt to removed fields
- Modify: `libs/core/include/corbomite/core/MarkoffRenderEngine.h` — if needed

The spec says `baseFontSizePt` moves to `Theme::textFont` point size, and `basePath` moves to `ResourceProvider`. For now, in the Renderer (which is internal and will eventually be replaced), we hardcode sensible fallbacks. Corbomite's `MarkoffRenderEngine` stops setting these removed fields.

- [ ] **Step 1: Update RenderSettings.h**

Remove `baseFontSizePt` and `basePath` from the struct. The file should become:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_RENDERSETTINGS_H
#define MARKOFF_RENDERSETTINGS_H

namespace Markoff {

struct RenderSettings {
    int maxWidthPx = 0;        // 0 = fill container
    int marginPx = 16;
    bool showFrontmatter = false;
    bool renderImages = true;
    bool renderCodeHighlighting = true;
};

} // namespace Markoff

#endif // MARKOFF_RENDERSETTINGS_H
```

- [ ] **Step 2: Fix Renderer.cpp compile errors**

In `libs/markoff/src/Renderer.cpp`, the `renderToTextDocument` method references `s.baseFontSizePt` (line ~425) and `renderInlines` uses `settings.basePath` (line ~155-158). Fix:

1. Replace `s.baseFontSizePt` with a hardcoded `14` in the CSS body style.
2. In `renderInlines`, remove the `settings.basePath` image path resolution — just use the `src` as-is (the ResourceProvider pattern will handle this at the widget level in future tasks; the Renderer is internal/temporary). Keep checking for `http` and `data:` prefixes, but skip the `QDir(settings.basePath)` resolution.

- [ ] **Step 3: Fix MarkoffRenderEngine.cpp compile errors**

In `libs/core/src/MarkoffRenderEngine.cpp`, remove the lines that set:
- `settings.baseFontSizePt = ...` (line ~28)
- Any reference to `basePath` if present

The `RenderOptions` struct in Corbomite may still have `baseFontSizePt` — that's fine, Corbomite just stops passing it through to markoff. The font size will come from Theme in the future.

- [ ] **Step 4: Build everything**

```bash
cd /home/clinton/dev/Corbomite && cmake --build build 2>&1 | tail -20
```

Expected: Clean build. No remaining references to the removed fields.

- [ ] **Step 5: Run all markoff tests**

```bash
cd /home/clinton/dev/Corbomite/build && ctest -R tst_markoff --output-on-failure
```

Expected: All pass. The `tst_renderer` test may need adjustment if it was setting `baseFontSizePt` or `basePath`.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff/include/markoff/RenderSettings.h libs/markoff/src/Renderer.cpp libs/core/src/MarkoffRenderEngine.cpp
git commit -m "api: remove baseFontSizePt and basePath from RenderSettings"
```

---

### Task 6: Wire Theme into MarkdownHighlighter

**Files:**
- Modify: `libs/markoff/src/MarkdownHighlighter.h` — add `setTheme(const Theme &)` method, remove individual format members
- Modify: `libs/markoff/src/MarkdownHighlighter.cpp` — apply theme formats instead of hardcoded ones

Currently, `MarkdownHighlighter` has ~17 individual `QTextCharFormat` members hardcoded in the constructor. We replace these with a `Theme` that provides all the formats.

- [ ] **Step 1: Add Theme include and method to MarkdownHighlighter.h**

Add `#include "markoff/Theme.h"` at the top.

Add a public method:
```cpp
void setTheme(const Theme &theme);
```

Replace the individual format members (`m_headingFormat[6]`, `m_boldFormat`, `m_italicFormat`, etc.) with:
```cpp
Theme m_theme;
```

Keep the `blockquoteColor()` accessor but change its implementation to read from `m_theme.formats`.

- [ ] **Step 2: Update MarkdownHighlighter.cpp**

The constructor currently sets up hardcoded colors for each format. Replace that with `setTheme(Theme::defaultLight())` as a default.

Implement `setTheme()`:
```cpp
void MarkdownHighlighter::setTheme(const Theme &theme)
{
    m_theme = theme;
    rehighlight();
}
```

In `highlightBlock()` and `applySpanFormat()`, replace references to individual format members with lookups into `m_theme.formats`. The mapping is:

| Old member | Theme Element |
|-----------|---------------|
| `m_headingFormat[0..5]` | `Element::H1` through `Element::H6` |
| `m_boldFormat` | `Element::Bold` |
| `m_italicFormat` | `Element::Italic` |
| `m_strikethroughFormat` | `Element::Strikethrough` |
| `m_inlineCodeFormat` | `Element::InlineCode` |
| `m_linkFormat` | `Element::Link` |
| `m_wikilinkFormat` | `Element::WikiLink` |
| `m_blockquoteFormat` | `Element::BlockQuote` |
| `m_listMarkerFormat` | `Element::ListMarker` |
| `m_codeBlockFormat` | `Element::CodeBlock` |
| `m_horizontalRuleFormat` | `Element::HorizontalRule` |
| `m_mathFormat` | `Element::InlineCode` (or add Element::Math later) |
| `m_highlightFormat` | `Element::Highlight` |
| `m_commentFormat` | `Element::Comment` |
| `m_tagFormat` | `Element::Tag` |
| `m_frontmatterFormat` | `Element::FrontmatterBlock` |
| `m_calloutFormat` | `Element::Callout` |

Read `libs/markoff/src/MarkdownHighlighter.cpp` carefully before editing — it has complex logic for delimiter hiding and code block highlighting that must be preserved. Only change the format lookups, not the logic.

Helper pattern for format lookup:
```cpp
QTextCharFormat format = m_theme.formats.value(Element::Bold);
```

- [ ] **Step 3: Build and run tests**

```bash
cd /home/clinton/dev/Corbomite && cmake --build build && cd build && ctest -R tst_markoff --output-on-failure
```

Expected: All tests pass. The visual output may look slightly different (colors may differ from the old hardcoded ones), but the tests test behavior, not colors.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff/src/MarkdownHighlighter.h libs/markoff/src/MarkdownHighlighter.cpp
git commit -m "api: wire Theme into MarkdownHighlighter replacing hardcoded formats"
```

---

### Task 7: Expand Editor public API

**Files:**
- Modify: `libs/markoff/include/markoff/Editor.h` — full API expansion
- Modify: `libs/markoff/src/Editor.cpp` — implement new methods

This is the largest task. The Editor gets:
1. Configuration methods (setTheme, setEditorSettings, setRenderSettings, setResourceProvider)
2. Document accessor
3. Editing actions (undo, redo, cut, copy, paste, selectAll)
4. Formatting actions (toggleBold, toggleItalic, etc.)
5. Cursor/navigation (cursorLine, cursorColumn, goToLine, scrollToHeading)
6. Search (findText, replaceText, replaceAll)
7. Signals (cursorPositionChanged, undoAvailable, linkClicked, wikiLinkTrigger, headingsChanged, etc.)

- [ ] **Step 1: Update Editor.h**

Replace the entire `libs/markoff/include/markoff/Editor.h` with the full API from the spec. Include the necessary headers:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_EDITOR_H
#define MARKOFF_EDITOR_H

#include <QGraphicsView>
#include <QTextDocument>
#include <markoff/Theme.h>
#include <markoff/EditorSettings.h>
#include <markoff/RenderSettings.h>
#include <markoff/Document.h>

class QTimer;

namespace Markoff {

class SelectionScene;
class SceneCoordinator;
class MarkdownTextItem;
class ResourceProvider;

class Editor : public QGraphicsView {
    Q_OBJECT
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)

public:
    enum class Mode { Source, LivePreview };
    Q_ENUM(Mode)

    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    // --- Content ---
    void setPlainText(const QString &text);
    void clear();
    QString toPlainText() const;
    const Document *document() const;

    // --- Configuration ---
    void setTheme(const Theme &theme);
    Theme theme() const;

    void setEditorSettings(const EditorSettings &settings);
    EditorSettings editorSettings() const;

    void setRenderSettings(const RenderSettings &settings);
    RenderSettings renderSettings() const;

    void setResourceProvider(ResourceProvider *provider);

    // --- Mode ---
    void setMode(Mode mode);
    Mode mode() const;

    // --- Editing actions ---
    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void selectAll();

    // --- Formatting actions ---
    void toggleBold();
    void toggleItalic();
    void toggleStrikethrough();
    void toggleInlineCode();
    void insertLink();
    void insertWikiLink();
    void insertImage();
    void insertCodeBlock();
    void insertBlockQuote();
    void insertHorizontalRule();
    void insertTable(int rows, int cols);
    void increaseHeadingLevel();
    void decreaseHeadingLevel();
    void toggleCheckbox();
    void insertCallout(const QString &type);

    // --- Cursor & navigation ---
    int cursorLine() const;
    int cursorColumn() const;
    QRect cursorScreenRect() const;
    void goToLine(int line);
    void scrollToHeading(const HeadingInfo &heading);

    // --- Search ---
    bool findText(const QString &text, QTextDocument::FindFlags flags = {});
    bool replaceText(const QString &find, const QString &replace,
                     QTextDocument::FindFlags flags = {});
    int replaceAll(const QString &find, const QString &replace,
                   QTextDocument::FindFlags flags = {});

Q_SIGNALS:
    void textChanged();
    void modeChanged(Markoff::Editor::Mode mode);
    void cursorPositionChanged(int line, int column);
    void undoAvailable(bool available);
    void redoAvailable(bool available);
    void modificationChanged(bool modified);
    void linkClicked(const QString &target);
    void linkHovered(const QString &target);
    void wikiLinkTrigger(int cursorPosition);
    void tagTrigger(int cursorPosition);
    void completionDismissHint();
    void headingsChanged(const QList<Markoff::HeadingInfo> &headings);
    void linksChanged(const QList<Markoff::LinkInfo> &links);
    void tagsChanged(const QList<Markoff::TagInfo> &tags);
    void wordCountChanged(int count);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    void rebuildScene();
    void ensureFocusedCursorVisible();
    void startAutoScroll(int mouseY);
    void stopAutoScroll();
    void doAutoScroll();
    void jumpToDocumentEdge(bool toStart, bool select);
    void pageUpDown(bool up, bool select);
    MarkdownTextItem *focusedTextItem() const;
    void onDocumentReparsed();
    void detectCompletionTriggers(const QString &insertedText);
    void wrapSelection(const QString &before, const QString &after);
    void insertAtCursor(const QString &text);

    SelectionScene *m_scene = nullptr;
    SceneCoordinator *m_coordinator = nullptr;
    Mode m_mode = Mode::Source;
    QString m_sourceText;
    int m_fontSize = 14;
    QTimer *m_autoScrollTimer = nullptr;
    int m_autoScrollDelta = 0;
    bool m_autoScrollActive = false;

    Theme m_theme;
    EditorSettings m_editorSettings;
    RenderSettings m_renderSettings;
    ResourceProvider *m_resourceProvider = nullptr;
    std::unique_ptr<Document> m_document;
};

} // namespace Markoff

#endif // MARKOFF_EDITOR_H
```

- [ ] **Step 2: Implement new methods in Editor.cpp**

This step requires significant code. Read the existing `Editor.cpp` carefully — the existing methods (`setPlainText`, `toPlainText`, `setMode`, `setFontSize`, `resizeEvent`, `mouseMoveEvent`, `mouseReleaseEvent`, `contextMenuEvent`, `keyPressEvent`, `wheelEvent`, `rebuildScene`, `ensureFocusedCursorVisible`) must be preserved. Add new implementations around them.

Key implementation notes:

**Configuration methods:**
- `setTheme()`: Store theme, pass it to the SceneCoordinator's highlighter (via a new method on SceneCoordinator — see below), update font size from `theme.textFont`. This replaces `setFontSize()`.
- `setEditorSettings()`: Store settings. Apply line wrap/tab size to the coordinator's text items.
- `setRenderSettings()`: Store settings. Will be used by ReadingView and future Renderer integration.
- `setResourceProvider()`: Store the pointer (non-owning).

**Document accessor:**
- `document()`: Return `m_document.get()`. Reparse `m_document` on `textChanged()` signal (debounced — the SceneCoordinator already has a reparse timer). Connect to the SceneCoordinator's `textChanged()` signal to trigger `onDocumentReparsed()`.

**`onDocumentReparsed()`:**
```cpp
void Editor::onDocumentReparsed()
{
    m_document = Document::fromMarkdown(toPlainText());
    Q_EMIT headingsChanged(m_document->headings());
    Q_EMIT linksChanged(m_document->links());
    Q_EMIT tagsChanged(m_document->tags());
    Q_EMIT wordCountChanged(m_document->wordCount());
}
```

**Editing actions** — delegate to the focused text item's TextControl:
```cpp
void Editor::undo()
{
    if (auto *ti = focusedTextItem()) ti->textControl()->undo();
}
// Same pattern for redo, cut, copy, paste, selectAll
```

**Formatting actions** — use `wrapSelection()` helper:
```cpp
void Editor::wrapSelection(const QString &before, const QString &after)
{
    auto *ti = focusedTextItem();
    if (!ti) return;
    auto *tc = ti->textControl();
    QTextCursor cursor = tc->textCursor();
    if (cursor.hasSelection()) {
        QString selected = cursor.selectedText();
        // Check if already wrapped — toggle off
        // (simplified: just wrap/unwrap)
        cursor.insertText(before + selected + after);
    } else {
        cursor.insertText(before + after);
        cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, after.length());
    }
    tc->setTextCursor(cursor);
}

void Editor::toggleBold() { wrapSelection(QStringLiteral("**"), QStringLiteral("**")); }
void Editor::toggleItalic() { wrapSelection(QStringLiteral("*"), QStringLiteral("*")); }
void Editor::toggleStrikethrough() { wrapSelection(QStringLiteral("~~"), QStringLiteral("~~")); }
void Editor::toggleInlineCode() { wrapSelection(QStringLiteral("`"), QStringLiteral("`")); }
```

For `insertLink`, `insertWikiLink`, `insertImage`, `insertCodeBlock`, `insertBlockQuote`, `insertHorizontalRule`, `insertTable`, `increaseHeadingLevel`, `decreaseHeadingLevel`, `toggleCheckbox`, `insertCallout` — implement each as markdown text manipulation on the focused text item's cursor. Use `insertAtCursor()` for simple insertions.

**Cursor info:**
```cpp
int Editor::cursorLine() const
{
    auto *ti = focusedTextItem();
    if (!ti) return 1;
    // Count blocks in items before this one + block within this item
    int line = 1;
    for (auto *item : m_coordinator->items()) {
        if (item == static_cast<SelectableItem *>(ti)) break;
        if (item->isTextItem()) {
            auto *textItem = static_cast<MarkdownTextItem *>(item);
            line += textItem->document()->blockCount();
        } else {
            line += 1; // non-text items count as 1 line
        }
    }
    line += ti->textControl()->textCursor().blockNumber();
    return line;
}

int Editor::cursorColumn() const
{
    auto *ti = focusedTextItem();
    if (!ti) return 1;
    return ti->textControl()->textCursor().columnNumber() + 1;
}
```

**Search:** For `findText`, iterate through all text items and search their QTextDocuments. For `replaceText`/`replaceAll`, find the match first, then replace.

**Completion trigger detection** — add to `keyPressEvent` after the base class handles the key:
```cpp
void Editor::detectCompletionTriggers(const QString &insertedText)
{
    if (insertedText.isEmpty()) return;
    auto *ti = focusedTextItem();
    if (!ti) return;

    QTextCursor cursor = ti->textControl()->textCursor();
    int pos = cursor.positionInBlock();
    QString blockText = cursor.block().text();

    QChar ch = insertedText.at(0);
    if (ch == QLatin1Char('[') && pos >= 2 &&
        blockText.mid(pos - 2, 2) == QStringLiteral("[[")) {
        Q_EMIT wikiLinkTrigger(cursor.position());
    }
    if (ch == QLatin1Char('#') && pos > 1) {
        Q_EMIT tagTrigger(cursor.position());
    }
}
```

Call `detectCompletionTriggers(e->text())` at the end of `keyPressEvent` after the base class call.

**cursorPositionChanged signal:** Connect in the constructor. When a text item gains focus, connect its cursor changes to emit the signal. This requires tracking the focused item and connecting/disconnecting.

- [ ] **Step 3: Update SceneCoordinator to accept Theme**

Add a `setTheme(const Theme &theme)` method to `SceneCoordinator` that passes the theme to each `MarkdownTextItem`'s `MarkdownHighlighter`. This replaces the per-item format setup.

In `libs/markoff/src/SceneCoordinator.h`, add:
```cpp
void setTheme(const Theme &theme);
```

In `libs/markoff/src/SceneCoordinator.cpp`, implement it by iterating items and calling `highlighter->setTheme(theme)` on each text item's highlighter.

- [ ] **Step 4: Build**

```bash
cd /home/clinton/dev/Corbomite && cmake --build build 2>&1 | tail -30
```

Fix any compile errors. The most likely issues will be:
- Missing includes for new types
- SceneCoordinator/MarkdownTextItem not having the methods you're calling
- TextControl method names

Read `libs/markoff/src/TextControl.h` to verify method names for undo/redo/cut/copy/paste/textCursor/setTextCursor.

- [ ] **Step 5: Run tests**

```bash
cd /home/clinton/dev/Corbomite/build && ctest -R tst_markoff --output-on-failure
```

Expected: All pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff/include/markoff/Editor.h libs/markoff/src/Editor.cpp libs/markoff/src/SceneCoordinator.h libs/markoff/src/SceneCoordinator.cpp
git commit -m "api: expand Editor with full public API (config, formatting, signals, search)"
```

---

### Task 8: Expand ReadingView public API

**Files:**
- Modify: `libs/markoff/include/markoff/ReadingView.h`
- Modify: `libs/markoff/src/ReadingView.cpp`

- [ ] **Step 1: Update ReadingView.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_READINGVIEW_H
#define MARKOFF_READINGVIEW_H

#include <QWidget>
#include <memory>
#include <markoff/Theme.h>
#include <markoff/RenderSettings.h>
#include <markoff/Document.h>

namespace Markoff {

class ResourceProvider;

class ReadingView : public QWidget {
    Q_OBJECT
public:
    explicit ReadingView(QWidget *parent = nullptr);
    ~ReadingView() override;

    // Content
    void setDocument(const Document &doc);
    void setMarkdown(const QString &markdown);
    const Document *document() const;

    // Configuration
    void setTheme(const Theme &theme);
    Theme theme() const;

    void setRenderSettings(const RenderSettings &settings);
    RenderSettings renderSettings() const;

    void setResourceProvider(ResourceProvider *provider);

    // Scroll
    qreal scrollFraction() const;
    void setScrollFraction(qreal fraction);
    void scrollToHeading(const HeadingInfo &heading);

    // Size hint for embedding contexts
    int naturalHeight(int width) const;

Q_SIGNALS:
    void linkClicked(const QString &target);
    void linkHovered(const QString &target);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff

#endif // MARKOFF_READINGVIEW_H
```

- [ ] **Step 2: Update ReadingView.cpp**

Update the `Private` struct to store `Theme`, `RenderSettings`, `ResourceProvider*`, and `Document`:

```cpp
struct ReadingView::Private {
    QTextBrowser *browser = nullptr;
    Markoff::Renderer renderer;
    Theme theme;
    RenderSettings renderSettings;
    ResourceProvider *resourceProvider = nullptr;
    std::unique_ptr<Document> document;
};
```

Implement new methods:

```cpp
void ReadingView::setMarkdown(const QString &markdown)
{
    d->document = Document::fromMarkdown(markdown);
    setDocument(*d->document);
}

const Document *ReadingView::document() const
{
    return d->document.get();
}

void ReadingView::setTheme(const Theme &theme)
{
    d->theme = theme;
    // Apply base font to the browser
    QFont font = theme.textFont;
    d->browser->setFont(font);
}

Theme ReadingView::theme() const
{
    return d->theme;
}

void ReadingView::setRenderSettings(const RenderSettings &settings)
{
    d->renderSettings = settings;
    d->renderer.setSettings(settings);
}

RenderSettings ReadingView::renderSettings() const
{
    return d->renderSettings;
}

void ReadingView::setResourceProvider(ResourceProvider *provider)
{
    d->resourceProvider = provider;
}

void ReadingView::scrollToHeading(const HeadingInfo &heading)
{
    // Find the heading text in the rendered HTML and scroll to it
    auto *sb = d->browser->verticalScrollBar();
    if (!sb) return;

    // Use QTextDocument::find to locate the heading text
    QTextDocument *doc = d->browser->document();
    QTextCursor cursor = doc->find(heading.text);
    if (!cursor.isNull()) {
        d->browser->setTextCursor(cursor);
        d->browser->ensureCursorVisible();
    }
}

int ReadingView::naturalHeight(int width) const
{
    QTextDocument *doc = d->browser->document();
    if (!doc) return 0;
    doc->setTextWidth(width);
    return qRound(doc->size().height());
}
```

For `linkHovered`, connect to QTextBrowser's `highlighted(const QUrl &)` signal in the constructor:
```cpp
connect(d->browser, &QTextBrowser::highlighted, this, [this](const QUrl &url) {
    if (!url.isEmpty())
        Q_EMIT linkHovered(url.toString());
});
```

Update the existing `setSettings()` call to `setRenderSettings()` internally if needed, or keep both for now (the old `setSettings()` can become a wrapper).

- [ ] **Step 3: Build and run tests**

```bash
cd /home/clinton/dev/Corbomite && cmake --build build && cd build && ctest -R tst_markoff --output-on-failure
```

Expected: All pass.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff/include/markoff/ReadingView.h libs/markoff/src/ReadingView.cpp
git commit -m "api: expand ReadingView with Theme, ResourceProvider, setMarkdown, naturalHeight"
```

---

### Task 9: Make Renderer.h internal

**Files:**
- Move: `libs/markoff/include/markoff/Renderer.h` → `libs/markoff/src/Renderer.h`
- Modify: `libs/markoff/src/Renderer.cpp` — update include path
- Modify: `libs/markoff/src/ReadingView.cpp` — update include path
- Modify: `libs/markoff/CMakeLists.txt` — remove from public includes if listed
- Modify: `libs/core/src/MarkoffRenderEngine.cpp` — this file directly includes `<markoff/Renderer.h>`. It needs to either use the internal path (if it's in the same build tree) or be refactored.

The Renderer is internal scaffolding. Corbomite's `MarkoffRenderEngine` currently uses it directly. For now, we keep it working by adding the markoff `src/` directory to the core library's include path (it's in the same build tree). Long-term, Corbomite should use ReadingView or a new public rendering API.

- [ ] **Step 1: Move the file**

```bash
mv libs/markoff/include/markoff/Renderer.h libs/markoff/src/Renderer.h
```

- [ ] **Step 2: Update includes**

In `libs/markoff/src/Renderer.cpp`, change:
```cpp
#include "markoff/Renderer.h"
```
to:
```cpp
#include "Renderer.h"
```

In `libs/markoff/src/ReadingView.cpp`, change:
```cpp
#include "markoff/Renderer.h"
```
to:
```cpp
#include "Renderer.h"
```

In `libs/core/src/MarkoffRenderEngine.cpp`, change:
```cpp
#include <markoff/Renderer.h>
```
to:
```cpp
#include "Renderer.h"
```

And add the markoff src directory to the core library's include path. In `libs/core/CMakeLists.txt`, add to the `target_include_directories` for the core library:
```cmake
PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../markoff/src
```

- [ ] **Step 3: Update CMakeLists.txt**

In `libs/markoff/CMakeLists.txt`, if `include/markoff/Renderer.h` is listed in the `add_library(markoff STATIC ...)` sources, remove it (or change to `src/Renderer.h`).

- [ ] **Step 4: Build**

```bash
cd /home/clinton/dev/Corbomite && cmake -B build -DCORBOMITE_DEV_BUILD=ON && cmake --build build 2>&1 | tail -20
```

Expected: Clean build.

- [ ] **Step 5: Run all tests**

```bash
cd /home/clinton/dev/Corbomite/build && ctest --output-on-failure
```

Expected: All pass (including Corbomite's tests, not just markoff's).

- [ ] **Step 6: Commit**

```bash
git add -A libs/markoff/include/markoff/ libs/markoff/src/Renderer.h libs/markoff/src/Renderer.cpp libs/markoff/src/ReadingView.cpp libs/markoff/CMakeLists.txt libs/core/CMakeLists.txt libs/core/src/MarkoffRenderEngine.cpp
git commit -m "api: move Renderer.h to internal (src/), no longer public API"
```

---

### Task 10: Implement Theme::fromSchemeFile() for QOwnNotes INI parsing

**Files:**
- Modify: `libs/markoff/src/Theme.cpp` — implement `fromSchemeFile()`
- Modify: `libs/markoff/tests/tst_theme.cpp` — add scheme file loading tests

The QOwnNotes scheme format is QSettings INI with sections like `[EditorColorSchema-<uuid>]`. Colors are stored as `@Variant(...)` which is Qt's native QSettings serialization. We can use QSettings to read them directly.

- [ ] **Step 1: Write failing test**

Add to `libs/markoff/tests/tst_theme.cpp`:

```cpp
void TestTheme::testFromSchemeFileLoadsColors();
```

Add the declaration to the class and implement:

```cpp
void TestTheme::testFromSchemeFileLoadsColors()
{
    // The QOwnNotes schemes file
    QString schemePath = QStringLiteral("/home/clinton/src/QOwnNotes/src/configurations/schemes.conf");
    if (!QFile::exists(schemePath))
        QSKIP("QOwnNotes schemes.conf not found — skipping");

    auto theme = Markoff::Theme::fromSchemeFile(schemePath);

    // Should have loaded Text format
    QVERIFY(theme.formats.contains(Markoff::Element::Text));
    QColor fg = theme.formats[Markoff::Element::Text].foreground().color();
    QVERIFY(fg.isValid());

    // Should have headings
    QVERIFY(theme.formats.contains(Markoff::Element::H1));

    // H1 should be bold
    QVERIFY(theme.formats[Markoff::Element::H1].fontWeight() >= QFont::Bold);
}
```

- [ ] **Step 2: Run test to verify it fails**

The current stub returns `defaultLight()`, which will actually pass these tests. Update the test to verify a distinctive property of the loaded scheme vs. the default. For example, check that `fromSchemeFile` with the QOwnNotes path loads the first scheme (Light) and produces the same result as calling `defaultLight()` is not sufficient. Instead, check that it reads a second scheme (Dark) if we pass a schema key parameter.

Actually, for a simpler approach: `fromSchemeFile()` should load the first schema in the file. Verify it produces valid output and move on. The real test is that it doesn't crash and produces a complete theme.

- [ ] **Step 3: Implement fromSchemeFile()**

```cpp
Theme Theme::fromSchemeFile(const QString &path)
{
    QSettings settings(path, QSettings::IniFormat);

    // Find the first schema key
    QString schemaList = settings.value(QStringLiteral("Editor/DefaultColorSchemes")).toString();
    if (schemaList.isEmpty())
        return defaultLight();

    QString schemaKey = schemaList.split(QStringLiteral(",")).first().trimmed();
    if (schemaKey.isEmpty())
        return defaultLight();

    Theme t;
    t.textFont = QFont();
    t.textFont.setPointSize(14);
    t.codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    t.codeFont.setPointSize(14);

    settings.beginGroup(schemaKey);

    // Map QOwnNotes indices to Markoff elements
    struct IndexMapping {
        int qonIndex;
        Element element;
    };
    static const IndexMapping mappings[] = {
        {-1, Element::Text},
        {0,  Element::Link},
        {4,  Element::CodeBlock},
        {7,  Element::Italic},
        {8,  Element::Bold},
        {9,  Element::ListMarker},
        {11, Element::Comment},
        {12, Element::H1},
        {13, Element::H2},
        {14, Element::H3},
        {15, Element::H4},
        {16, Element::H5},
        {17, Element::H6},
        {18, Element::BlockQuote},
        {21, Element::HorizontalRule},
        {22, Element::Table},
        {23, Element::InlineCode},
        {24, Element::MaskedSyntax},
        {25, Element::CurrentLineBackground},
        {26, Element::BrokenLink},
        {27, Element::FrontmatterBlock},
        {28, Element::TrailingSpace},
        {29, Element::CheckboxUnchecked},
        {30, Element::CheckboxChecked},
        {1000, Element::CodeKeyword},
        {1001, Element::CodeString},
        {1002, Element::CodeComment},
        {1003, Element::CodeType},
        {1004, Element::CodeOther},
        {1005, Element::CodeNumLiteral},
        {1006, Element::CodeBuiltIn},
    };

    for (const auto &m : mappings) {
        QTextCharFormat fmt;
        QString idx = QString::number(m.qonIndex);

        // Foreground
        bool fgEnabled = settings.value(
            QStringLiteral("ForegroundColorEnabled_%1").arg(idx), false).toBool();
        if (fgEnabled) {
            QColor color = settings.value(
                QStringLiteral("ForegroundColor_%1").arg(idx)).value<QColor>();
            if (color.isValid())
                fmt.setForeground(color);
        }

        // Background
        bool bgEnabled = settings.value(
            QStringLiteral("BackgroundColorEnabled_%1").arg(idx), false).toBool();
        if (bgEnabled) {
            QColor color = settings.value(
                QStringLiteral("BackgroundColor_%1").arg(idx)).value<QColor>();
            if (color.isValid())
                fmt.setBackground(color);
        }

        // Bold
        if (settings.value(QStringLiteral("Bold_%1").arg(idx), false).toBool())
            fmt.setFontWeight(QFont::Bold);

        // Italic
        if (settings.value(QStringLiteral("Italic_%1").arg(idx), false).toBool())
            fmt.setFontItalic(true);

        // Underline
        if (settings.value(QStringLiteral("Underline_%1").arg(idx), false).toBool())
            fmt.setFontUnderline(true);

        // Font size adaptation (for headings)
        int sizeAdapt = settings.value(
            QStringLiteral("FontSizeAdaption_%1").arg(idx), 100).toInt();
        if (sizeAdapt != 100) {
            QFont font = t.textFont;
            font.setPointSize(qRound(t.textFont.pointSize() * sizeAdapt / 100.0));
            fmt.setFont(font, QTextCharFormat::FontPropertiesSpecifiedOnly);
            // Re-apply bold if it was set (setFont may clear it)
            if (settings.value(QStringLiteral("Bold_%1").arg(idx), false).toBool())
                fmt.setFontWeight(QFont::Bold);
        }

        // Code-related elements get monospace font
        if (m.qonIndex == 4 || m.qonIndex == 23 ||
            (m.qonIndex >= 1000 && m.qonIndex <= 1006)) {
            fmt.setFont(t.codeFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
        }

        t.formats[m.element] = fmt;
    }

    settings.endGroup();

    // Elements not in QOwnNotes — fill with sensible defaults
    if (!t.formats.contains(Element::Highlight)) {
        QTextCharFormat hlFmt;
        hlFmt.setBackground(QColor(255, 249, 196));
        t.formats[Element::Highlight] = hlFmt;
    }
    if (!t.formats.contains(Element::Tag))
        t.formats[Element::Tag] = fgFormat(QColor(230, 81, 0));
    if (!t.formats.contains(Element::WikiLink)) {
        QTextCharFormat wlFmt;
        wlFmt.setForeground(QColor(0, 137, 123));
        wlFmt.setFontUnderline(true);
        t.formats[Element::WikiLink] = wlFmt;
    }
    if (!t.formats.contains(Element::Image))
        t.formats[Element::Image] = fgFormat(QColor(0, 137, 123));
    if (!t.formats.contains(Element::Callout))
        t.formats[Element::Callout] = fgFormat(QColor(68, 138, 255));
    if (!t.formats.contains(Element::Strikethrough)) {
        QTextCharFormat sFmt;
        sFmt.setFontStrikeOut(true);
        sFmt.setForeground(QColor(150, 150, 150));
        t.formats[Element::Strikethrough] = sFmt;
    }

    return t;
}
```

Add `#include <QSettings>` to the top of Theme.cpp.

- [ ] **Step 4: Build and run tests**

```bash
cd /home/clinton/dev/Corbomite && cmake --build build && cd build && ctest -R tst_markoff_theme --output-on-failure
```

Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff/src/Theme.cpp libs/markoff/tests/tst_theme.cpp
git commit -m "api: implement Theme::fromSchemeFile() for QOwnNotes INI parsing"
```

---

### Task 11: Update CMakeLists.txt for all new files

This task is a checkpoint to ensure all new files from previous tasks are properly listed in CMakeLists.txt and the full build is clean.

**Files:**
- Modify: `libs/markoff/CMakeLists.txt`

- [ ] **Step 1: Verify CMakeLists.txt has all new sources**

Ensure `libs/markoff/CMakeLists.txt`'s `add_library(markoff STATIC ...)` includes:
- `src/Theme.cpp`
- `src/ResourceProvider.cpp`

And includes all new public headers (they don't need to be listed since they're in the `include/` directory which is a public include path, but listing them helps IDEs):
- `include/markoff/Theme.h`
- `include/markoff/EditorSettings.h`
- `include/markoff/ResourceProvider.h`

- [ ] **Step 2: Full rebuild from clean**

```bash
cd /home/clinton/dev/Corbomite && cmake -B build -DCORBOMITE_DEV_BUILD=ON && cmake --build build 2>&1 | tail -20
```

Expected: Clean build, no warnings related to markoff.

- [ ] **Step 3: Run all tests**

```bash
cd /home/clinton/dev/Corbomite/build && ctest -R tst_markoff --output-on-failure
```

Expected: All markoff tests pass.

- [ ] **Step 4: Run Corbomite tests too**

```bash
cd /home/clinton/dev/Corbomite/build && ctest --output-on-failure
```

Expected: All tests pass.

- [ ] **Step 5: Commit if any CMakeLists changes were needed**

```bash
git add libs/markoff/CMakeLists.txt libs/markoff/tests/CMakeLists.txt
git commit -m "chore: ensure all new markoff API files are in CMakeLists"
```

---

## Summary of public API after all tasks

```
include/markoff/
├── Document.h           # Document + HeadingInfo, LinkInfo, TagInfo, FootnoteInfo + query methods
├── Editor.h             # Full editor widget (config, formatting, signals, search)
├── EditorSettings.h     # Behavioral config struct
├── ReadingView.h        # Read-only view (theme, resource provider, setMarkdown, naturalHeight)
├── RenderSettings.h     # Layout config (no baseFontSizePt, no basePath)
├── ResourceProvider.h   # Abstract interface + FilesystemResourceProvider
└── Theme.h              # Element enum, Theme struct, defaultLight/Dark, fromSchemeFile
```

`Renderer.h` is now internal (`src/Renderer.h`).

## Testing checklist

| Test file | What it covers |
|-----------|---------------|
| `tst_theme.cpp` | Theme factories, scheme loading |
| `tst_resourceprovider.cpp` | FilesystemResourceProvider resolution |
| `tst_document_queries.cpp` | Headings, links, tags, word count, footnotes |
| `tst_document.cpp` | Existing: parsing, frontmatter, subpath extraction |
| `tst_renderer.cpp` | Existing: renderer output |
| `tst_selection.cpp` | Existing: cross-boundary selection |
| `tst_splitter.cpp` | Existing: markdown splitting |
| `tst_table.cpp` | Existing: table handling |
