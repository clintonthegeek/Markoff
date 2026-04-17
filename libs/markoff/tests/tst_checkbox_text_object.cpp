// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/Theme.h>
#include "CheckboxTextObject.h"

using namespace Markoff;

class TstCheckboxTextObject : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void intrinsicSizeMatchesDocFontPointSize();
    void intrinsicSizeFallsBackWhenDocNull();
    void intrinsicSizeFallsBackWhenFontSizeNonPositive();
    void checkedPropertyRoundTripsThroughFormat();
    void drawObjectCheckedDiffersFromUnchecked();
    void drawObjectHonoursRectBounds();
    void setThemeSwapsCheckboxColors();
};

void TstCheckboxTextObject::intrinsicSizeMatchesDocFontPointSize()
{
    QTextDocument doc;
    QFont f = doc.defaultFont();
    f.setPointSize(18);
    doc.setDefaultFont(f);

    CheckboxTextObject cb;
    const QSizeF s = cb.intrinsicSize(&doc, 0, QTextCharFormat());
    QCOMPARE(s, QSizeF(18.0, 18.0));
}

void TstCheckboxTextObject::intrinsicSizeFallsBackWhenDocNull()
{
    CheckboxTextObject cb;
    const QSizeF s = cb.intrinsicSize(nullptr, 0, QTextCharFormat());
    QCOMPARE(s, QSizeF(14.0, 14.0));
}

void TstCheckboxTextObject::intrinsicSizeFallsBackWhenFontSizeNonPositive()
{
    // A pixel-sized QFont reports pointSizeF() == -1. The fallback branch
    // in checkboxSize() (`s > 0 ? s : 14.0`) should kick in.
    QTextDocument doc;
    QFont f = doc.defaultFont();
    f.setPixelSize(20);
    doc.setDefaultFont(f);
    QVERIFY(doc.defaultFont().pointSizeF() <= 0);

    CheckboxTextObject cb;
    const QSizeF s = cb.intrinsicSize(&doc, 0, QTextCharFormat());
    QCOMPARE(s, QSizeF(14.0, 14.0));
}

void TstCheckboxTextObject::checkedPropertyRoundTripsThroughFormat()
{
    QTextCharFormat fmt;
    fmt.setObjectType(CheckboxTextObject::TypeId);
    fmt.setProperty(CheckboxTextObject::CheckedProperty, true);
    QVERIFY(fmt.property(CheckboxTextObject::CheckedProperty).toBool());

    fmt.setProperty(CheckboxTextObject::CheckedProperty, false);
    QVERIFY(!fmt.property(CheckboxTextObject::CheckedProperty).toBool());
}

// Render the same rect in both states and confirm they differ meaningfully.
// Checked: filled green box. Unchecked: outline only. Pixel counts must diverge.
void TstCheckboxTextObject::drawObjectCheckedDiffersFromUnchecked()
{
    const QRectF rect(0, 0, 40, 40);
    QImage checkedImg(40, 40, QImage::Format_ARGB32_Premultiplied);
    QImage uncheckedImg(40, 40, QImage::Format_ARGB32_Premultiplied);
    checkedImg.fill(Qt::transparent);
    uncheckedImg.fill(Qt::transparent);

    CheckboxTextObject cb;
    QTextCharFormat fmtChecked;
    fmtChecked.setProperty(CheckboxTextObject::CheckedProperty, true);
    QTextCharFormat fmtUnchecked;
    fmtUnchecked.setProperty(CheckboxTextObject::CheckedProperty, false);

    {
        QPainter p(&checkedImg);
        cb.drawObject(&p, rect, nullptr, 0, fmtChecked);
    }
    {
        QPainter p(&uncheckedImg);
        cb.drawObject(&p, rect, nullptr, 0, fmtUnchecked);
    }

    QVERIFY(checkedImg != uncheckedImg);

    // Checked state uses a green fill; unchecked uses only an outline. The
    // checked image should therefore have substantially more opaque pixels.
    auto opaqueCount = [](const QImage &img) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 0) ++n;
        return n;
    };
    QVERIFY(opaqueCount(checkedImg) > opaqueCount(uncheckedImg) * 2);
}

// Pixels outside the passed rect must remain untouched.
void TstCheckboxTextObject::drawObjectHonoursRectBounds()
{
    QImage img(100, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::red);

    CheckboxTextObject cb;
    QTextCharFormat fmt;
    fmt.setProperty(CheckboxTextObject::CheckedProperty, true);

    {
        QPainter p(&img);
        cb.drawObject(&p, QRectF(10, 10, 20, 20), nullptr, 0, fmt);
    }

    // A pixel far outside the draw rect must still be red.
    QCOMPARE(img.pixel(80, 80), qRgb(255, 0, 0));
    QCOMPARE(img.pixel(5, 5), qRgb(255, 0, 0));
}

// Swapping the theme should change the painted fill color. Proves the
// Theme → CheckboxTextObject wiring actually flows through to drawObject.
void TstCheckboxTextObject::setThemeSwapsCheckboxColors()
{
    const QRectF rect(0, 0, 40, 40);
    QImage defaultImg(40, 40, QImage::Format_ARGB32_Premultiplied);
    QImage themedImg(40, 40, QImage::Format_ARGB32_Premultiplied);
    defaultImg.fill(Qt::transparent);
    themedImg.fill(Qt::transparent);

    QTextCharFormat fmtChecked;
    fmtChecked.setProperty(CheckboxTextObject::CheckedProperty, true);

    CheckboxTextObject cb;
    {
        QPainter p(&defaultImg);
        cb.drawObject(&p, rect, nullptr, 0, fmtChecked);
    }

    // Construct a theme that forces the checked fill to bright red.
    Theme t = Theme::defaultLight();
    t.paint.checkboxCheckedFill = QColor(255, 0, 0);
    cb.setTheme(t);
    {
        QPainter p(&themedImg);
        cb.drawObject(&p, rect, nullptr, 0, fmtChecked);
    }

    QVERIFY(defaultImg != themedImg);

    // Sample a point inside the fill that's well clear of the checkmark
    // stroke — (8, 8) is in the upper-left of the fill, nowhere near the
    // diagonal path.
    const QRgb pixel = themedImg.pixel(8, 8);
    QCOMPARE(qRed(pixel), 255);
    QVERIFY2(qGreen(pixel) < 50, qPrintable(QStringLiteral("green=%1").arg(qGreen(pixel))));
    QVERIFY2(qBlue(pixel) < 50, qPrintable(QStringLiteral("blue=%1").arg(qBlue(pixel))));
}

QTEST_MAIN(TstCheckboxTextObject)
#include "tst_checkbox_text_object.moc"
