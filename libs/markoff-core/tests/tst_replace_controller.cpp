// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QUndoStack>

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
        doc.resetContent(QStringLiteral("foo bar foo"), Origin::FirstOpen);
        StubAdapter adapter;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("foo"));
        QCOMPARE(c.matchCount(), 2);
        c.replaceCurrent(QStringLiteral("baz"));
        QCOMPARE(doc.toMarkdown(), QStringLiteral("baz bar foo"));
    }

    void replaceAllIsAtomic() {
        MarkoffDocument doc;
        doc.resetContent(QStringLiteral("a a a"), Origin::FirstOpen);
        StubAdapter adapter;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        QCOMPARE(c.replaceAll(QStringLiteral("zz")), 3);
        QCOMPARE(doc.toMarkdown(), QStringLiteral("zz zz zz"));
        // One undo reverts all three (replaceAll wraps in a beginMacro/endMacro).
        doc.undoStack()->undo();
        QCOMPARE(doc.toMarkdown(), QStringLiteral("a a a"));
    }

    void refusesWhenAdapterRejects() {
        MarkoffDocument doc;
        doc.resetContent(QStringLiteral("a"), Origin::FirstOpen);
        StubAdapter adapter;
        adapter.replace = false;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        const QString before = doc.toMarkdown();
        c.replaceCurrent(QStringLiteral("b"));
        QCOMPARE(doc.toMarkdown(), before);
        QCOMPARE(c.replaceAll(QStringLiteral("b")), 0);
        QCOMPARE(doc.toMarkdown(), before);
    }

    void replaceAllHandlesOverlappingGrowth() {
        MarkoffDocument doc;
        doc.resetContent(QStringLiteral("a a"), Origin::FirstOpen);
        StubAdapter adapter;
        ReplaceController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        QCOMPARE(c.replaceAll(QStringLiteral("aa")), 2);
        QCOMPARE(doc.toMarkdown(), QStringLiteral("aa aa"));
    }
};

QTEST_MAIN(TstReplaceController)
#include "tst_replace_controller.moc"
