// SPDX-License-Identifier: GPL-3.0-or-later
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTest>
#include <QTextCursor>

#include <markoff/core/ClipboardCodec.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/source/Editor.h>
#include "CandidateDocumentFixture.h"

using Markoff::ClipboardCodec::kMarkdownMime;
using Markoff::ClipboardCodec::kRtfMime;
using Markoff::MarkoffDocument;
using Markoff::Source::Editor;

namespace {

QByteArray flat(MarkoffDocument &doc)
{
    QByteArray out;
    for (const auto id : doc.iterateBlocks())
        out += doc.blockText(id);
    return out;
}

void selectAll(Editor &ed)
{
    QTextCursor c = ed.textCursor();
    c.select(QTextCursor::Document);
    ed.setTextCursor(c);
}

}  // namespace

class TstSourceRichClipboard : public QObject {
    Q_OBJECT
private slots:
    void copy_writesHtmlAndMarkdownMime();
    void copy_restores_list_and_quote_markers();
    void copyAsPlain_stripsMarkers();
    void copyAsHtml_exclusiveNoPlain();
    void paste_htmlBecomesMarkdown();
    void pasteAsPlain_stripsIncomingHtml();
};

void TstSourceRichClipboard::copy_writesHtmlAndMarkdownMime()
{
    auto fixtureDocument = Markoff::Source::Tests::openCandidateDocument("Hello **world**\n");
    auto& doc = *fixtureDocument.document;
    Editor ed;
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    selectAll(ed);
    ed.copy();

    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime);
    QVERIFY(mime->hasText());
    QVERIFY(mime->hasHtml() || mime->hasFormat(QStringLiteral("text/html")));
    QVERIFY(mime->hasFormat(QString::fromUtf8(kMarkdownMime)));
    QVERIFY(mime->hasFormat(QString::fromUtf8(kRtfMime)));
    QVERIFY2(mime->text().contains(QStringLiteral("**world**")),
             qPrintable(mime->text()));
    QVERIFY2(mime->html().contains(QStringLiteral("<strong>"))
                 || QString::fromUtf8(mime->data(QStringLiteral("text/html")))
                        .contains(QStringLiteral("<strong>")),
             qPrintable(mime->html()));
}

// ASK-M24: the leaf paints list/quote markers outside the text, so a selection
// is content-only; Copy must restore the markers or the pasted markdown loses
// its List/BlockQuote structure.
void TstSourceRichClipboard::copy_restores_list_and_quote_markers()
{
    auto fixtureDocument =
        Markoff::Source::Tests::openCandidateDocument("- one\n- two\n");
    auto& doc = *fixtureDocument.document;
    Editor ed;
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    selectAll(ed);
    ed.copy();

    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime);
    const QString md =
        QString::fromUtf8(mime->data(QString::fromUtf8(kMarkdownMime)));
    QVERIFY2(md.contains(QStringLiteral("- one")), qPrintable(md));
    QVERIFY2(md.contains(QStringLiteral("- two")), qPrintable(md));
}

void TstSourceRichClipboard::copyAsPlain_stripsMarkers()
{
    auto fixtureDocument = Markoff::Source::Tests::openCandidateDocument("Hello **world**\n");
    auto& doc = *fixtureDocument.document;
    Editor ed;
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    selectAll(ed);
    ed.copyAsPlain();

    const QString plain = QGuiApplication::clipboard()->text();
    QVERIFY2(plain.contains(QStringLiteral("world")), qPrintable(plain));
    QVERIFY2(!plain.contains(QStringLiteral("**")), qPrintable(plain));
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime);
    QVERIFY(!mime->hasHtml());
}

void TstSourceRichClipboard::copyAsHtml_exclusiveNoPlain()
{
    auto fixtureDocument = Markoff::Source::Tests::openCandidateDocument("**bold**\n");
    auto& doc = *fixtureDocument.document;
    Editor ed;
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    selectAll(ed);
    ed.copyAsHtml();

    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime);
    QVERIFY(mime->hasHtml() || mime->hasFormat(QStringLiteral("text/html")));
    QVERIFY(!mime->hasText());
    QVERIFY(!mime->hasFormat(QString::fromUtf8(kMarkdownMime)));
}

void TstSourceRichClipboard::paste_htmlBecomesMarkdown()
{
    auto fixtureDocument = Markoff::Source::Tests::openCandidateDocument("X\n");
    auto& doc = *fixtureDocument.document;
    Editor ed;
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    auto *mime = new QMimeData;
    mime->setHtml(QStringLiteral("<p><strong>bold</strong> and "
                                 "<a href=\"https://ex.test\">link</a></p>"));
    QGuiApplication::clipboard()->setMimeData(mime);

    // Caret at end.
    QTextCursor c = ed.textCursor();
    c.movePosition(QTextCursor::End);
    ed.setTextCursor(c);
    ed.paste();

    QTRY_VERIFY2(QString::fromUtf8(flat(doc)).contains(QStringLiteral("**bold**")),
                 qPrintable(QString::fromUtf8(flat(doc))));
    QVERIFY2(QString::fromUtf8(flat(doc)).contains(QStringLiteral("[link](https://ex.test)")),
             qPrintable(QString::fromUtf8(flat(doc))));
}

void TstSourceRichClipboard::pasteAsPlain_stripsIncomingHtml()
{
    auto fixtureDocument = Markoff::Source::Tests::openCandidateDocument("X\n");
    auto& doc = *fixtureDocument.document;
    Editor ed;
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    auto *mime = new QMimeData;
    mime->setHtml(QStringLiteral("<p><strong>bold</strong></p>"));
    mime->setText(QStringLiteral("PLAIN"));
    QGuiApplication::clipboard()->setMimeData(mime);

    QTextCursor c = ed.textCursor();
    c.movePosition(QTextCursor::End);
    ed.setTextCursor(c);
    ed.pasteAsPlain();

    QTRY_VERIFY2(QString::fromUtf8(flat(doc)).contains(QStringLiteral("PLAIN")),
                 qPrintable(QString::fromUtf8(flat(doc))));
    QVERIFY2(!QString::fromUtf8(flat(doc)).contains(QStringLiteral("**")),
             qPrintable(QString::fromUtf8(flat(doc))));
}

QTEST_MAIN(TstSourceRichClipboard)
#include "tst_source_rich_clipboard.moc"
