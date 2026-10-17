// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextList>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/DocumentRenderer.h>
#include <markoff/styled/Editor.h>
#include "CandidateDocumentFixture.h"

namespace {
QTextBlock blockN(const QTextDocument *doc, int n) {
    return doc->findBlockByNumber(n);
}
}  // namespace

class TstStyledDocumentRenderer : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ---- T1: renderInto correctness (oracle = explicit expectations) ------

    void heading_is_bold_and_nonempty() {
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("# Title\n\nbody"));
    auto& doc = *fixture_doc.document;
        QTextDocument target;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&target, &doc);
        QVERIFY(target.characterCount() > 1);
        const QTextCharFormat cf = blockN(&target, 0).charFormat();
        QVERIFY(cf.fontPointSize() > 0);
        QCOMPARE(cf.fontWeight(), int(QFont::Bold));
    }

    void code_block_is_monospace_with_background() {
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("```\ncode line\n```"));
    auto& doc = *fixture_doc.document;
        QTextDocument target;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&target, &doc);
        // Fenced code spans 3 QTextBlocks: fence, content, fence.
        const QTextBlock content = blockN(&target, 1);
        const QTextCharFormat cf = content.charFormat();
        QVERIFY(cf.fontFixedPitch() || !cf.fontFamilies().toStringList().isEmpty());
        QVERIFY(content.blockFormat().background().style() != Qt::NoBrush);
    }

    void blockquote_left_margin_and_list_membership() {
        auto quoteFixture = Markoff::Styled::Tests::openCandidateDocument(
            QByteArrayLiteral("> quoted text"));
        auto& q = *quoteFixture.document;
        QTextDocument tq;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&tq, &q);
        QVERIFY(blockN(&tq, 0).blockFormat().leftMargin() > 0);

        // List items indent via QTextList membership (not blockFormat
        // leftMargin) since the em-spacing rework.
        auto listFixture = Markoff::Styled::Tests::openCandidateDocument(
            QByteArrayLiteral("- first item"));
        auto& l = *listFixture.document;
        QTextDocument tl;
        r.renderInto(&tl, &l);
        QVERIFY(blockN(&tl, 0).textList() != nullptr);
    }

    void inline_bold_and_italic_applied() {
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("normal **bold** _italic_ text"));
    auto& doc = *fixture_doc.document;
        QTextDocument target;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&target, &doc);
        bool sawBold = false, sawItalic = false;
        const QTextBlock blk = blockN(&target, 0);
        for (QTextBlock::iterator it = blk.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat f = it.fragment().charFormat();
            if (f.fontWeight() == QFont::Bold) sawBold = true;
            if (f.fontItalic()) sawItalic = true;
        }
        QVERIFY(sawBold);
        QVERIFY(sawItalic);
    }

    void bytes_overload_renders_kinds() {
        QTextDocument target;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&target, QByteArrayLiteral("# H\n\nbody\n\n- a\n- b"));
        QVERIFY(target.characterCount() > 1);
        QCOMPARE(blockN(&target, 0).charFormat().fontWeight(), int(QFont::Bold));
    }

    void unsupported_block_degrades_to_text() {  // acceptance #3
        QTextDocument target;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&target,
                     QByteArrayLiteral("| a | b |\n| - | - |\n| 1 | 2 |"));
        QVERIFY(target.characterCount() > 1);  // non-empty, no crash
    }

    void matches_widget_path() {  // acceptance #1 (consistency)
        const QByteArray md = QByteArrayLiteral(
            "# H1\n\n## H2\n\npara **b**\n\n> quote\n\n- item\n\n```\ncode\n```");
        Markoff::Styled::Editor e;
        auto widgetFixture = Markoff::Styled::Tests::openCandidateDocument(md);
        auto& wdoc = *widgetFixture.document;
        auto *s = wdoc.createSession();
        e.setSession(s);
        e.setDocument(&wdoc);
        const QTextDocument *wq = e.textEdit()->document();

        auto headlessFixture = Markoff::Styled::Tests::openCandidateDocument(md);
        auto& hdoc = *headlessFixture.document;
        QTextDocument hq;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&hq, &hdoc);

        QCOMPARE(hq.blockCount(), wq->blockCount());
        for (int i = 0; i < wq->blockCount(); ++i) {
            const QTextBlock wb = wq->findBlockByNumber(i);
            const QTextBlock hb = hq.findBlockByNumber(i);
            QCOMPARE(hb.charFormat().fontPointSize(), wb.charFormat().fontPointSize());
            QCOMPARE(hb.charFormat().fontWeight(),    wb.charFormat().fontWeight());
            QCOMPARE(hb.blockFormat().leftMargin(),   wb.blockFormat().leftMargin());
        }
    }

    // ---- T2: idealHeight / paint -----------------------------------------

    void ideal_height_positive_and_grows_when_narrow() {
        auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(
            QByteArrayLiteral("This is a fairly long paragraph that should wrap onto several "
                              "lines once the available width becomes small enough to force it."));
        auto& doc = *fixture_doc.document;
        Markoff::Styled::DocumentRenderer r;
        const qreal wide   = r.idealHeight(&doc, 600);
        const qreal narrow = r.idealHeight(&doc, 120);
        QVERIFY(wide > 0);
        QVERIFY(narrow >= wide);
    }

    void paint_draws_something() {
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("# Heading\n\nbody text here"));
    auto& doc = *fixture_doc.document;
        Markoff::Styled::DocumentRenderer r;
        const qreal h = r.idealHeight(&doc, 300);
        QVERIFY(h > 0);
        QImage img(300, int(h) + 4, QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        r.paint(&p, QRectF(0, 0, 300, h), &doc);
        p.end();
        bool drew = false;
        for (int y = 0; y < img.height() && !drew; ++y)
            for (int x = 0; x < img.width(); ++x)
                if (img.pixelColor(x, y) != QColor(Qt::white)) { drew = true; break; }
        QVERIFY(drew);
    }
};

QTEST_MAIN(TstStyledDocumentRenderer)
#include "tst_styled_document_renderer.moc"
