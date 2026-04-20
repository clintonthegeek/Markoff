// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#include "markoff/CodeBlockProcessorRegistry.h"

#include <QtTest/QtTest>

using namespace Markoff;

class tst_MarkoffCodeBlockRegistry : public QObject
{
    Q_OBJECT

private slots:
    void dispatchHitsProcessor();
    void dispatchMissReturnsFalse();
    void hasLanguageReflectsRegistration();
};

void tst_MarkoffCodeBlockRegistry::dispatchHitsProcessor()
{
    CodeBlockProcessorRegistry reg;
    int calls = 0;
    QString capturedSource;
    reg.registerLanguage(
        QStringLiteral("mermaid"),
        [&](const QString &src, void *, const CodeBlockContext &) {
            ++calls;
            capturedSource = src;
            return true;
        });
    CodeBlockContext ctx;
    const bool handled = reg.dispatch(QStringLiteral("mermaid"),
                                      QStringLiteral("graph TD"),
                                      nullptr, ctx);
    QVERIFY(handled);
    QCOMPARE(calls, 1);
    QCOMPARE(capturedSource, QStringLiteral("graph TD"));
}

void tst_MarkoffCodeBlockRegistry::dispatchMissReturnsFalse()
{
    CodeBlockProcessorRegistry reg;
    CodeBlockContext ctx;
    QCOMPARE(reg.dispatch(QStringLiteral("nope"), QString{}, nullptr, ctx),
             false);
}

void tst_MarkoffCodeBlockRegistry::hasLanguageReflectsRegistration()
{
    CodeBlockProcessorRegistry reg;
    QVERIFY(!reg.hasLanguage(QStringLiteral("mermaid")));
    reg.registerLanguage(
        QStringLiteral("mermaid"),
        [](const QString &, void *, const CodeBlockContext &) { return true; });
    QVERIFY(reg.hasLanguage(QStringLiteral("mermaid")));
    QVERIFY(!reg.hasLanguage(QStringLiteral("Mermaid"))); // case-sensitive
}

QTEST_MAIN(tst_MarkoffCodeBlockRegistry)
#include "tst_markoff_codeblock_registry.moc"
