// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/CompletionDetector.h>
#include <markoff-foundation/MarkoffDocument.h>

using namespace Markoff;

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
            doc.textAnchorAt(8, /*rightBias*/ false));
        QCOMPARE(ctx.trigger, CompletionTrigger::WikiLink);
        QCOMPARE(ctx.prefix, QStringLiteral("no"));
    }

    void emoji_after_colon() {
        MarkoffDocument doc(1);
        seed(doc, "hi :smi");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.textAnchorAt(7, /*rightBias*/ false));
        QCOMPARE(ctx.trigger, CompletionTrigger::Emoji);
        QCOMPARE(ctx.prefix, QStringLiteral("smi"));
    }

    void tag_after_hash_in_body() {
        MarkoffDocument doc(1);
        seed(doc, "body #ta");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.textAnchorAt(8, /*rightBias*/ false));
        QCOMPARE(ctx.trigger, CompletionTrigger::Tag);
        QCOMPARE(ctx.prefix, QStringLiteral("ta"));
    }

    void heading_marker_not_a_tag() {
        MarkoffDocument doc(1);
        seed(doc, "#h");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.textAnchorAt(2, /*rightBias*/ false));
        QCOMPARE(ctx.trigger, CompletionTrigger::None);
    }

    void inside_fenced_code_block_suppresses_triggers() {
        MarkoffDocument doc(1);
        seed(doc, "```\n[[no");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.textAnchorAt(8, /*rightBias*/ false));
        QCOMPARE(ctx.trigger, CompletionTrigger::None);
    }

    void escaped_double_bracket_not_a_wikilink() {
        MarkoffDocument doc(1);
        seed(doc, "see \\[[no");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.textAnchorAt(9, /*rightBias*/ false));
        QCOMPARE(ctx.trigger, CompletionTrigger::None);
    }

    void footnote_marker_recognized() {
        MarkoffDocument doc(1);
        seed(doc, "see [^fn");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.textAnchorAt(8, /*rightBias*/ false));
        QCOMPARE(ctx.trigger, CompletionTrigger::Footnote);
        QCOMPARE(ctx.prefix, QStringLiteral("fn"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationCompletionDetector)
#include "tst_foundation_completion_detector.moc"
