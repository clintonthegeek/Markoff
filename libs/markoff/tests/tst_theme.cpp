// libs/markoff/tests/tst_theme.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QFile>
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
    void testFromSchemeFileLoadsColors();

    // Paint colors (non-QTextCharFormat surfaces: checkboxes, code-block
    // backdrop, search highlights, callouts, image placeholder, block
    // selection overlay).
    void testDefaultLightHasPaintColors();
    void testDefaultDarkHasPaintColors();
    void testPaintColorsDifferBetweenLightAndDark();
    void testCalloutColorResolvesKnownTypes();
    void testCalloutColorFallsBackToDefault();
    void testFromSchemeFileInheritsPaintColors();
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

void TestTheme::testFromSchemeFileLoadsColors()
{
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

void TestTheme::testDefaultLightHasPaintColors()
{
    auto t = Markoff::Theme::defaultLight();
    QVERIFY(t.paint.codeBlockBg.isValid());
    QVERIFY(t.paint.codeBlockBorder.isValid());
    QVERIFY(t.paint.codeBlockLanguageLabel.isValid());
    QVERIFY(t.paint.searchMatchBg.isValid());
    QVERIFY(t.paint.searchCurrentMatchBg.isValid());
    QVERIFY(t.paint.checkboxCheckedFill.isValid());
    QVERIFY(t.paint.checkboxCheckMark.isValid());
    QVERIFY(t.paint.checkboxUncheckedOutline.isValid());
    QVERIFY(t.paint.imagePlaceholderBg.isValid());
    QVERIFY(t.paint.imagePlaceholderBorder.isValid());
    QVERIFY(t.paint.imagePlaceholderText.isValid());
    QVERIFY(t.paint.blockSelectionOverlay.isValid());
    QVERIFY(t.paint.calloutDefault.isValid());
    QVERIFY(!t.paint.calloutAccents.isEmpty());
}

void TestTheme::testDefaultDarkHasPaintColors()
{
    auto t = Markoff::Theme::defaultDark();
    QVERIFY(t.paint.codeBlockBg.isValid());
    QVERIFY(t.paint.searchMatchBg.isValid());
    QVERIFY(t.paint.checkboxCheckedFill.isValid());
    QVERIFY(t.paint.blockSelectionOverlay.isValid());
    QVERIFY(!t.paint.calloutAccents.isEmpty());
}

void TestTheme::testPaintColorsDifferBetweenLightAndDark()
{
    auto light = Markoff::Theme::defaultLight();
    auto dark = Markoff::Theme::defaultDark();
    // Code-block backdrop is the most visually divergent — light uses near-white,
    // dark uses a dark olive. They must not coincide.
    QVERIFY(light.paint.codeBlockBg != dark.paint.codeBlockBg);
    QVERIFY(light.paint.imagePlaceholderBg != dark.paint.imagePlaceholderBg);
}

void TestTheme::testCalloutColorResolvesKnownTypes()
{
    auto t = Markoff::Theme::defaultLight();
    // Obsidian-compatible types. At minimum, these must be present and
    // must not all collapse to the default (they drive section accenting).
    QColor note = t.calloutColor(QStringLiteral("note"));
    QColor warning = t.calloutColor(QStringLiteral("warning"));
    QColor danger = t.calloutColor(QStringLiteral("danger"));
    QVERIFY(note.isValid());
    QVERIFY(warning.isValid());
    QVERIFY(danger.isValid());
    QVERIFY(note != warning);
    QVERIFY(warning != danger);

    // Lookup is case-insensitive.
    QCOMPARE(t.calloutColor(QStringLiteral("NOTE")), note);
    QCOMPARE(t.calloutColor(QStringLiteral("Note")), note);
}

void TestTheme::testCalloutColorFallsBackToDefault()
{
    auto t = Markoff::Theme::defaultLight();
    QColor unknown = t.calloutColor(QStringLiteral("thisTypeDoesNotExist"));
    QCOMPARE(unknown, t.paint.calloutDefault);
}

// QOwnNotes INI files don't describe the non-QTextCharFormat paint colors.
// `fromSchemeFile` should fill them in from the light defaults so host
// apps never hand a partially-initialized Theme to the editor.
void TestTheme::testFromSchemeFileInheritsPaintColors()
{
    QString schemePath = QStringLiteral("/home/clinton/src/QOwnNotes/src/configurations/schemes.conf");
    if (!QFile::exists(schemePath))
        QSKIP("QOwnNotes schemes.conf not found — skipping");

    auto t = Markoff::Theme::fromSchemeFile(schemePath);
    QVERIFY(t.paint.codeBlockBg.isValid());
    QVERIFY(t.paint.checkboxCheckedFill.isValid());
    QVERIFY(!t.paint.calloutAccents.isEmpty());
}

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
