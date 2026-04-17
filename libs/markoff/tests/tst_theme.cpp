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

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
