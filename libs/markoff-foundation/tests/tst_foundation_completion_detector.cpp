// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/CompletionDetector.h>
#include <markoff-foundation/MarkoffDocument.h>

using namespace Markoff;
using CollabText::Crdt::Bias;

namespace {
void seed(MarkoffDocument &doc, const QByteArray &text) {
    QList<MarkoffEdit> ed;
    MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = text;
    ed << i;
    doc.applyLocalEdit(ed);
}
}

class TstFoundationCompletionDetector : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void wikilink_after_double_open_bracket() {
        MarkoffDocument doc(1);
        seed(doc, "see [[no");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.anchorAt(8, Bias::Left));
        QCOMPARE(ctx.trigger, CompletionTrigger::WikiLink);
        QCOMPARE(ctx.prefix, QStringLiteral("no"));
    }

    void emoji_after_colon() {
        MarkoffDocument doc(1);
        seed(doc, "hi :smi");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.anchorAt(7, Bias::Left));
        QCOMPARE(ctx.trigger, CompletionTrigger::Emoji);
        QCOMPARE(ctx.prefix, QStringLiteral("smi"));
    }

    void tag_after_hash_in_body() {
        MarkoffDocument doc(1);
        seed(doc, "body #ta");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.anchorAt(8, Bias::Left));
        QCOMPARE(ctx.trigger, CompletionTrigger::Tag);
        QCOMPARE(ctx.prefix, QStringLiteral("ta"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationCompletionDetector)
#include "tst_foundation_completion_detector.moc"
