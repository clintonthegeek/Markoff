// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/Cmd/InlineFormat.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>

using namespace Markoff;
using CollabText::Crdt::Bias;

namespace {
Selection rangeSel(const MarkoffDocument &doc, quint32 start, quint32 end) {
    Selection s;
    s.anchor = doc.anchorAt(start, Bias::Left);
    s.active = doc.anchorAt(end,   Bias::Right);
    s.kind = Selection::Kind::Primary;
    return s;
}

void seed(MarkoffDocument &doc, const QByteArray &text) {
    QList<MarkoffEdit> ed;
    MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = text;
    ed << i;
    doc.applyLocalEdit(ed);
}
}

class TstFoundationCmdInlineFormat : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void toggle_bold_wraps_unstyled() {
        MarkoffDocument doc(1);
        seed(doc, "hello world");
        const Selection sel = rangeSel(doc, 0, 5);   // "hello"
        const auto edits = Cmd::editsForToggleBold(doc, sel);
        QVERIFY(!edits.isEmpty());
        Cmd::toggleBold(doc, sel);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("**hello** world"));
    }

    void toggle_bold_unwraps_styled() {
        MarkoffDocument doc(1);
        seed(doc, "**hello** world");
        // Inner range covers "hello"
        const Selection sel = rangeSel(doc, 2, 7);
        Cmd::toggleBold(doc, sel);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello world"));
    }

    void toggle_bold_empty_selection_noop() {
        MarkoffDocument doc(1);
        seed(doc, "hello");
        const Selection sel = rangeSel(doc, 3, 3);
        const auto edits = Cmd::editsForToggleBold(doc, sel);
        QVERIFY(edits.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdInlineFormat)
#include "tst_foundation_cmd_inline_format.moc"
