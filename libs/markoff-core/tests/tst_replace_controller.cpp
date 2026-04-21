// SPDX-License-Identifier: GPL-3.0-or-later
// PhaseC3: Whole file pending rewrite against Phase-C3 API in Task 8.
// Current API uses Phase-A setPlainText/plainText removed in Task 6.
// Rewrite mappings:
//   setPlainText(text) → resetContent(text, Origin::FirstOpen) (or TestFixture)
//   plainText() → toMarkdown()
//   textDocument() → removed; use toMarkdown() / parsedDocument()
#include <QTest>
#include <QTextDocument>

#include <markoff/MarkoffDocument.h>
#include <markoff/ReplaceController.h>
#include <markoff/SearchAdapter.h>

using namespace Markoff;

namespace {
class StubAdapter : public SearchAdapter {
public:
    int cursorSourceOffset() const override { return 0; }
    void highlightMatches(QVector<TextSpan> s) override { highlighted = s; }
    void clearMatchHighlight() override { highlighted.clear(); }
    void scrollMatchIntoView(TextSpan) override {}
    bool supportsReplace() const override { return replace; }

    QVector<TextSpan> highlighted;
    bool replace = true;
};
}  // namespace

class TstReplaceController : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void replaceCurrentMutates() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("foo bar foo"));
        StubAdapter adapter;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("foo"));
        QCOMPARE(c.matchCount(), 2);
        c.replaceCurrent(QStringLiteral("baz"));
        QCOMPARE(doc.plainText(), QStringLiteral("baz bar foo"));
    }

    void replaceAllIsAtomic() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a a a"));
        StubAdapter adapter;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        QCOMPARE(c.replaceAll(QStringLiteral("zz")), 3);
        QCOMPARE(doc.plainText(), QStringLiteral("zz zz zz"));
        // One undo reverts all three.
        doc.textDocument()->undo();
        QCOMPARE(doc.plainText(), QStringLiteral("a a a"));
    }

    void refusesWhenAdapterRejects() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a"));
        StubAdapter adapter;
        adapter.replace = false;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        const QString before = doc.plainText();
        c.replaceCurrent(QStringLiteral("b"));
        QCOMPARE(doc.plainText(), before);
        QCOMPARE(c.replaceAll(QStringLiteral("b")), 0);
        QCOMPARE(doc.plainText(), before);
    }

    void replaceAllHandlesOverlappingGrowth() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a a"));
        StubAdapter adapter;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        QCOMPARE(c.replaceAll(QStringLiteral("aa")), 2);
        QCOMPARE(doc.plainText(), QStringLiteral("aa aa"));
    }
};

QTEST_MAIN(TstReplaceController)
#include "tst_replace_controller.moc"
