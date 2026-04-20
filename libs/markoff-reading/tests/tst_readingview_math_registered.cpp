// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QTest>

#include "corbomite/core/CodeBlockProcessorRegistry.h"
#include "corbomite/readingview/ReadingView.h"

using namespace Corbomite::ReadingView;

/// Cluster J phase 5 — verify `math` / `latex` are registered onto
/// ReadingView's `CodeBlockProcessorRegistry` at construction. Delegation
/// target is the existing JKQTMathText bridge.
class TstReadingViewMathRegistered : public QObject
{
    Q_OBJECT

private slots:
    void testMathIsRegistered();
    void testLatexIsRegisteredSameHandler();
    void testEmptySourceFallsThrough();
};

void TstReadingViewMathRegistered::testMathIsRegistered()
{
    ReadingView rv;
    const auto *reg = rv.codeBlockProcessorRegistry();
    QVERIFY(reg != nullptr);

    Corbomite::Core::CodeBlockContext ctx;
    const bool handled =
        reg->dispatch(QStringLiteral("math"),
                      QStringLiteral("\\frac{a}{b}"), nullptr, ctx);
    QVERIFY2(handled,
             "math processor must be registered and the JKQTMathText "
             "bridge must accept a trivial LaTeX fraction");
}

void TstReadingViewMathRegistered::testLatexIsRegisteredSameHandler()
{
    ReadingView rv;
    const auto *reg = rv.codeBlockProcessorRegistry();
    QVERIFY(reg != nullptr);

    Corbomite::Core::CodeBlockContext ctx;
    const bool handled =
        reg->dispatch(QStringLiteral("latex"),
                      QStringLiteral("x^2 + y^2 = z^2"), nullptr, ctx);
    QVERIFY2(handled,
             "latex alias must route through the same math handler");
}

void TstReadingViewMathRegistered::testEmptySourceFallsThrough()
{
    ReadingView rv;
    const auto *reg = rv.codeBlockProcessorRegistry();
    QVERIFY(reg != nullptr);

    Corbomite::Core::CodeBlockContext ctx;
    const bool handled =
        reg->dispatch(QStringLiteral("math"),
                      QStringLiteral("   \n  "), nullptr, ctx);
    QVERIFY2(!handled,
             "empty/whitespace math input must report not-handled so the "
             "caller can suppress the block");
}

QTEST_MAIN(TstReadingViewMathRegistered)
#include "tst_readingview_math_registered.moc"
