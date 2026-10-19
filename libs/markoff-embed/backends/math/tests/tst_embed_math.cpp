// SPDX-License-Identifier: GPL-3.0-or-later
//
// Math backend contract: it is a CodeBlockProcessor like any other; nothing
// here knows about documents or views.
#include <QGuiApplication>
#include <QTest>

#include <markoff/core/CodeBlockProcessor.h>
#include <markoff/embed/math/MathBackend.h>

using namespace Markoff;

namespace {

RenderRequest mathRequest(const QString &source, qreal dpr = 1.0, qreal fontPx = 20.0)
{
    RenderRequest request;
    request.language = QStringLiteral("math");
    request.source = source;
    request.devicePixelRatio = dpr;
    request.fontPixelSize = fontPx;
    request.foreground = QColor(20, 20, 20);
    request.background = QColor(255, 255, 255);
    request.themeKey = QStringLiteral("test");
    return request;
}

}  // namespace

class TstEmbedMath : public QObject {
    Q_OBJECT

private slots:
    void stripsDisplayAndInlineDelimiters()
    {
        QCOMPARE(Embed::stripMathDelimiters(QStringLiteral("$$x^2$$")),
                 QStringLiteral("x^2"));
        QCOMPARE(Embed::stripMathDelimiters(QStringLiteral("  $a+b$ ")),
                 QStringLiteral("a+b"));
        QCOMPARE(Embed::stripMathDelimiters(QStringLiteral("\\frac{1}{2}")),
                 QStringLiteral("\\frac{1}{2}"));
    }

    void rendersAnImageAtTheRequestedDpr()
    {
        Embed::MathBackend backend;
        QVERIFY(backend.languages().contains(QStringLiteral("math")));
        const RenderedBlock one = backend.render(mathRequest(QStringLiteral("$$x^2$$"), 1.0));
        QCOMPARE(one.kind, RenderedBlock::Kind::Image);
        QVERIFY(!one.image.isNull());
        QCOMPARE(one.image.devicePixelRatio(), 1.0);
        QVERIFY(one.preferredSize.width() > 0.0);

        const RenderedBlock two = backend.render(mathRequest(QStringLiteral("$$x^2$$"), 2.0));
        QCOMPARE(two.kind, RenderedBlock::Kind::Image);
        QCOMPARE(two.image.devicePixelRatio(), 2.0);
        // More pixels at the same DIP size (metrics round at different
        // sizes, so assert the trend and the DIP equivalence, not exact 2x).
        QVERIFY2(two.image.width() >= one.image.width() * 3 / 2,
                 qPrintable(QStringLiteral("dpr1=%1 dpr2=%2")
                                .arg(one.image.width()).arg(two.image.width())));
        QVERIFY(qAbs(two.preferredSize.width() - one.preferredSize.width()) <= 1.0);
    }

    void emptySourceIsAnErrorArtifactNotACrash()
    {
        Embed::MathBackend backend;
        const RenderedBlock empty = backend.render(mathRequest(QStringLiteral("$$$$")));
        QCOMPARE(empty.kind, RenderedBlock::Kind::Error);
        QVERIFY(!empty.error.isEmpty());
        QCOMPARE(empty.fallbackText, QStringLiteral("$$$$"));
        // jkqtmathtext's LaTeX parser is deliberately tolerant, so a
        // malformed-but-non-empty expression still renders something; the
        // front-end's fallback is exercised by the empty case above.
    }

    void theBackendIsAPlainContractImplementation()
    {
        Embed::MathBackend backend;
        QCOMPARE(backend.contractVersion(), kEmbedContractVersion);
        QVERIFY(backend.languages().contains(QStringLiteral("latex")));
    }
};

QTEST_MAIN(TstEmbedMath)
#include "tst_embed_math.moc"
