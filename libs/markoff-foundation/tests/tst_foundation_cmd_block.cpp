// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/Cmd/Block.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>

using namespace Markoff;
using CollabText::Crdt::Bias;

namespace {
Selection rangeSel(const MarkoffDocument &doc, quint32 start, quint32 end) {
    Selection s;
    s.anchor = doc.anchorAt(start, Bias::Left);
    s.active = doc.anchorAt(end,   Bias::Right);
    return s;
}
void seed(MarkoffDocument &doc, const QByteArray &text) {
    QList<MarkoffEdit> ed;
    MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = text;
    ed << i;
    doc.applyLocalEdit(ed);
}
}

class TstFoundationCmdBlock : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void paragraph_to_h1() {
        MarkoffDocument doc(1);
        seed(doc, "title\nbody\n");
        Cmd::setHeading(doc, rangeSel(doc, 0, 5), 1);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("# title\nbody\n"));
    }

    void h2_to_h3() {
        MarkoffDocument doc(1);
        seed(doc, "## title\n");
        Cmd::setHeading(doc, rangeSel(doc, 3, 8), 3);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("### title\n"));
    }

    void h4_to_paragraph() {
        MarkoffDocument doc(1);
        seed(doc, "#### title\n");
        Cmd::setHeading(doc, rangeSel(doc, 5, 10), 0);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("title\n"));
    }

    void toggle_checkbox_unchecked_to_checked() {
        MarkoffDocument doc(1);
        seed(doc, "- [ ] task\n");
        Cmd::toggleCheckbox(doc, doc.anchorAt(7, Bias::Left));   // inside "task"
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("- [x] task\n"));
    }

    void toggle_checkbox_checked_to_none() {
        MarkoffDocument doc(1);
        seed(doc, "- [x] task\n");
        Cmd::toggleCheckbox(doc, doc.anchorAt(7, Bias::Left));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("- task\n"));
    }

    void block_quote_wraps_each_line() {
        MarkoffDocument doc(1);
        seed(doc, "one\ntwo\n");
        Cmd::blockQuote(doc, rangeSel(doc, 0, 7));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("> one\n> two\n"));
    }

    void block_quote_unwraps_when_all_lines_quoted() {
        MarkoffDocument doc(1);
        seed(doc, "> one\n> two\n");
        Cmd::blockQuote(doc, rangeSel(doc, 0, 11));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("one\ntwo\n"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdBlock)
#include "tst_foundation_cmd_block.moc"
