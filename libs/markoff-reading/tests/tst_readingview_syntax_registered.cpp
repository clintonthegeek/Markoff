// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QTest>

#include "corbomite/core/CodeBlockProcessorRegistry.h"
#include "corbomite/readingview/ReadingView.h"

using namespace Corbomite::ReadingView;

/// Cluster J phase 5 — verify the default syntax-highlighting fallback is
/// registered onto ReadingView's `CodeBlockProcessorRegistry`. The handler
/// exercises the KF6::SyntaxHighlighting repository via
/// `CodeBlockHighlighter` construction. Obsidian's parity here is the
/// "fallback to default highlighting when no per-language processor
/// claimed the block" path — this registration is the surface a plugin
/// author can hook to replace that default.
class TstReadingViewSyntaxRegistered : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultSyntaxIsRegistered();
    void testPerLanguageProcessorStillPreferred();
};

void TstReadingViewSyntaxRegistered::testDefaultSyntaxIsRegistered()
{
    ReadingView rv;
    const auto *reg = rv.codeBlockProcessorRegistry();
    QVERIFY(reg != nullptr);

    Corbomite::Core::CodeBlockContext ctx;
    const bool handled =
        reg->dispatch(QStringLiteral("default"),
                      QStringLiteral("int x = 42;"), nullptr, ctx);
    QVERIFY2(handled,
             "default syntax-highlighting handler must be registered and "
             "must succeed when KF6::SyntaxHighlighting is available");
}

void TstReadingViewSyntaxRegistered::testPerLanguageProcessorStillPreferred()
{
    // Registering a custom handler for e.g. `python` at runtime must take
    // precedence over dispatching to `default`. This test documents the
    // contract: the registry dispatches by exact language key, and the
    // caller (SectionLayout / plugin layer) chooses whether to route
    // fallthrough cases to `default`.
    ReadingView rv;
    auto *reg = rv.codeBlockProcessorRegistry();
    QVERIFY(reg != nullptr);

    bool pythonCalled = false;
    reg->registerLanguage(
        QStringLiteral("python"),
        [&pythonCalled](const QString &,
                        void *,
                        const Corbomite::Core::CodeBlockContext &) {
            pythonCalled = true;
            return true;
        });

    Corbomite::Core::CodeBlockContext ctx;
    QVERIFY(reg->dispatch(QStringLiteral("python"),
                          QStringLiteral("print('hi')"), nullptr, ctx));
    QVERIFY(pythonCalled);
}

QTEST_MAIN(TstReadingViewSyntaxRegistered)
#include "tst_readingview_syntax_registered.moc"
