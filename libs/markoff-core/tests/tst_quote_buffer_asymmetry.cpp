// SPDX-License-Identifier: GPL-3.0-or-later
// KNOWN BUG PINS (Cluster N N5 / canvas BlockQuote asymmetry) — these
// assert today's broken loaded-vs-typed behavior so a fix has to flip them:
//   - load → content-only buffer (correct)
//   - selectedText-like copy of loaded quote → no <blockquote> in HTML
//   - kind=BlockQuote with "> " still in buffer → serialize doubles to "> >"
// Canvas fix: strip on promote + restore marker in selectedText (ListItem).
// See Corbomite punch-list "BlockQuote typed-vs-loaded buffer asymmetry".
#include <QTest>
#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/ClipboardCodec.h>
#include <markoff/core/KindInference.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

class TstQuoteBufferAsymmetry : public QObject {
    Q_OBJECT
private slots:
    void load_stripsMarker_contentOnly()
    {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("> Single-paragraph quote — hello\n");
        const auto ids = doc.iterateBlocks();
        QCOMPARE(ids.size(), size_t(1));
        QCOMPARE(doc.blockKind(ids[0]), BlockKind::BlockQuote);
        const QByteArray text = doc.blockText(ids[0]);
        QVERIFY2(!text.startsWith(">"), qPrintable(QString::fromUtf8(text)));
        QVERIFY2(text.contains("hello"), qPrintable(QString::fromUtf8(text)));
    }

    void canvasSelectedText_withoutMarkerRestore_breaksHtmlExport()
    {
        // Mimic View::selectedText for BlockQuote: raw buffer, no "> " prepend
        // (ListItem gets marker restore; BlockQuote does not).
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("> a quoted line\n");
        const QByteArray sel = doc.blockText(doc.iterateBlocks()[0]);
        const QString html = ClipboardCodec::markdownToHtml(sel + "\n");
        QVERIFY2(!html.contains(QStringLiteral("<blockquote>")),
                 qPrintable(html + " — content-only sel must NOT look quoted to codec"));
    }

    void serialize_withMarkerStillInBuffer_doubles()
    {
        // Mimic typed promotion: kind=BlockQuote but buffer still has "> ".
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("para\n");
        const auto id = doc.iterateBlocks()[0];
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 0, uint32_t(doc.blockText(id).size()),
                                  QByteArray("> typed quote"), t);
            doc.d2SetBlockKind(id, BlockKind::BlockQuote, t);
            doc.d2SetBlockAttr(id, AttrNames::BlockQuoteDepth, 1, t);
            doc.d2SetBlockAttr(id, AttrNames::BlockQuoteRunId, 1, t);
        }
        doc.flushPendingD2Changed();
        QCOMPARE(doc.blockText(id), QByteArray("> typed quote"));
        const QByteArray saved = doc.serializeForSave();
        QVERIFY2(saved.startsWith(">>") || saved.startsWith("> >"),
                 qPrintable(QString::fromUtf8(saved)));
    }

    void selectedText_withMarkerInBuffer_htmlExportsBlockquote()
    {
        const QByteArray sel = QByteArray("> typed quote\n");
        const QString html = ClipboardCodec::markdownToHtml(sel);
        QVERIFY2(html.contains(QStringLiteral("<blockquote>")), qPrintable(html));
    }

    void inferPromotesButDoesNotDescribeStrip()
    {
        const auto inf = inferBlockKind(QStringLiteral("> hello"));
        QCOMPARE(inf.kind, BlockKind::BlockQuote);
    }
};

QTEST_MAIN(TstQuoteBufferAsymmetry)
#include "tst_quote_buffer_asymmetry.moc"
