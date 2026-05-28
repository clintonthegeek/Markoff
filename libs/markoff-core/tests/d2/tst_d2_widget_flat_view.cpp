// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

class TstD2WidgetFlatView : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void canonical_two_paragraphs_join_with_single_newline() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        QCOMPARE(doc.flatView(),       QByteArrayLiteral("Hello\n\nWorld"));
        QCOMPARE(doc.widgetFlatView(), QByteArrayLiteral("Hello\nWorld"));
    }

    void empty_block_between_content_renders_as_one_extra_newline() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        QCOMPARE(doc.iterateBlocks().size(), 3u);
        QCOMPARE(doc.flatView(),       QByteArrayLiteral("Hello\n\n\n\nWorld"));
        QCOMPARE(doc.widgetFlatView(), QByteArrayLiteral("Hello\n\nWorld"));
    }

    void single_block_no_separator() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello"));
        QCOMPARE(doc.widgetFlatView(), QByteArrayLiteral("Hello"));
    }

    void empty_document_returns_empty() {
        Markoff::MarkoffDocument doc(1);
        QCOMPARE(doc.widgetFlatView(), QByteArrayLiteral(""));
    }

    void trailing_empty_block_emits_one_trailing_newline() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello"));
        doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        QCOMPARE(doc.iterateBlocks().size(), 2u);
        QCOMPARE(doc.widgetFlatView(), QByteArrayLiteral("Hello\n"));
    }
};

QTEST_APPLESS_MAIN(TstD2WidgetFlatView)
#include "tst_d2_widget_flat_view.moc"
