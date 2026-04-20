// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QTest>

#include "corbomite/core/CodeBlockProcessorRegistry.h"
#include "corbomite/readingview/ReadingView.h"

using namespace Corbomite::ReadingView;

/// Cluster J phase 5 — verify Mermaid is registered onto ReadingView's
/// `CodeBlockProcessorRegistry` at construction. Test contract:
/// dispatching `"mermaid"` must return true (processor reached and the
/// bridge handled the input); dispatching an unknown language must
/// return false.
class TstReadingViewMermaidRegistered : public QObject
{
    Q_OBJECT

private slots:
    void testMermaidIsRegistered();
    void testUnknownLanguageFallsThrough();
};

void TstReadingViewMermaidRegistered::testMermaidIsRegistered()
{
    ReadingView rv;
    const auto *reg = rv.codeBlockProcessorRegistry();
    QVERIFY(reg != nullptr);

    Corbomite::Core::CodeBlockContext ctx;
    const bool handled =
        reg->dispatch(QStringLiteral("mermaid"),
                      QStringLiteral("graph TD;\nA-->B;\n"), nullptr, ctx);
    QVERIFY2(handled,
             "mermaid processor must be registered and the Rust-FFI bridge "
             "must return non-empty SVG for a trivial graph");
}

void TstReadingViewMermaidRegistered::testUnknownLanguageFallsThrough()
{
    ReadingView rv;
    const auto *reg = rv.codeBlockProcessorRegistry();
    QVERIFY(reg != nullptr);

    Corbomite::Core::CodeBlockContext ctx;
    const bool handled =
        reg->dispatch(QStringLiteral("no-such-lang-42"),
                      QStringLiteral("nothing"), nullptr, ctx);
    QVERIFY2(!handled,
             "unregistered language must report fallthrough so the caller "
             "can route to the default handler");
}

QTEST_MAIN(TstReadingViewMermaidRegistered)
#include "tst_readingview_mermaid_registered.moc"
