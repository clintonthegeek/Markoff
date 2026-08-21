// SPDX-License-Identifier: GPL-3.0-or-later
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTest>
#include <QTextCursor>

#include <markoff/core/ClipboardCodec.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/styled/Editor.h>

using Markoff::ClipboardCodec::kMarkdownMime;
using Markoff::ClipboardCodec::kRtfMime;
using Markoff::MarkoffDocument;
using Markoff::Styled::Editor;

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
    QTextCursor c = ed.textEdit()->textCursor();
    c.select(QTextCursor::Document);
    ed.textEdit()->setTextCursor(c);
}

}  // namespace

class TstStyledRichClipboard : public QObject {
    Q_OBJECT
private slots:
    void copy_writesMarkdownNotThemedOnly();
    void copyAsPlain_stripsMarkers();
    void readingMode_copyStillWorks();
    void paste_htmlBecomesMarkdown();
    void pasteAsPlain_stripsIncomingHtml();
    void copy_loadedBlockQuote_includesMarkerAndExportsHtml();
    void copy_listItem_includesMarker();
};

void TstStyledRichClipboard::copy_writesMarkdownNotThemedOnly()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello **world**\n");
    Editor ed;
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    selectAll(ed);
    ed.copy();

    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime);
    // Default plain is raw markdown (not stripped, not themed Qt soup).
    QVERIFY2(mime->text().contains(QStringLiteral("**world**")),
             qPrintable(mime->text()));
    QVERIFY(mime->hasFormat(QString::fromUtf8(kMarkdownMime)));
    QVERIFY(mime->hasFormat(QString::fromUtf8(kRtfMime)));
    QVERIFY2(mime->html().contains(QStringLiteral("<strong>"))
                 || QString::fromUtf8(mime->data(QStringLiteral("text/html")))
                        .contains(QStringLiteral("<strong>")),
             qPrintable(mime->html()));
    // Themed Qt HTML often ships Meta-Tag Generator / span style noise;
    // semantic export must not look like a full QTextDocument dump.
    QVERIFY2(!mime->html().contains(QStringLiteral("meta name=\"qrichtext\"")),
             qPrintable(mime->html()));
}

void TstStyledRichClipboard::copyAsPlain_stripsMarkers()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello **world**\n");
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
}

void TstStyledRichClipboard::readingMode_copyStillWorks()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Read **me**\n");
    Editor ed;
    ed.setDocument(&doc);
    ed.setReadOnly(true);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    selectAll(ed);
    ed.copy();

    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime);
    QVERIFY2(mime->text().contains(QStringLiteral("**me**")),
             qPrintable(mime->text()));

    // Paste must stay blocked while read-only.
    auto *incoming = new QMimeData;
    incoming->setHtml(QStringLiteral("<p><strong>nope</strong></p>"));
    QGuiApplication::clipboard()->setMimeData(incoming);
    ed.paste();
    QVERIFY2(!QString::fromUtf8(flat(doc)).contains(QStringLiteral("**nope**")),
             qPrintable(QString::fromUtf8(flat(doc))));
}

void TstStyledRichClipboard::paste_htmlBecomesMarkdown()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("X\n");
    Editor ed;
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    auto *mime = new QMimeData;
    mime->setHtml(QStringLiteral("<p><strong>bold</strong> and "
                                 "<a href=\"https://ex.test\">link</a></p>"));
    QGuiApplication::clipboard()->setMimeData(mime);

    QTextCursor c = ed.textEdit()->textCursor();
    c.movePosition(QTextCursor::End);
    ed.textEdit()->setTextCursor(c);
    ed.paste();

    QTRY_VERIFY2(QString::fromUtf8(flat(doc)).contains(QStringLiteral("**bold**")),
                 qPrintable(QString::fromUtf8(flat(doc))));
    QVERIFY2(QString::fromUtf8(flat(doc)).contains(QStringLiteral("[link](https://ex.test)")),
             qPrintable(QString::fromUtf8(flat(doc))));
}

void TstStyledRichClipboard::pasteAsPlain_stripsIncomingHtml()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("X\n");
    Editor ed;
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    auto *mime = new QMimeData;
    mime->setHtml(QStringLiteral("<p><strong>bold</strong></p>"));
    mime->setText(QStringLiteral("PLAIN"));
    QGuiApplication::clipboard()->setMimeData(mime);

    QTextCursor c = ed.textEdit()->textCursor();
    c.movePosition(QTextCursor::End);
    ed.textEdit()->setTextCursor(c);
    ed.pasteAsPlain();

    QTRY_VERIFY2(QString::fromUtf8(flat(doc)).contains(QStringLiteral("PLAIN")),
                 qPrintable(QString::fromUtf8(flat(doc))));
    QVERIFY2(!QString::fromUtf8(flat(doc)).contains(QStringLiteral("**")),
             qPrintable(QString::fromUtf8(flat(doc))));
}

void TstStyledRichClipboard::copy_loadedBlockQuote_includesMarkerAndExportsHtml()
{
    // Styled's counterpart to the canvas "BlockQuote typed-vs-loaded buffer
    // asymmetry" bug: loaded quotes render via a QTextBlock left-margin
    // quote-bar (StyleApplier::applyBlockquote), not literal "> " text, so
    // raw QTextCursor::selectedText() was content-only for this kind —
    // found during Cluster N pre-merge cleanup, falsifying Phase 5's
    // "Reading-mode copy is markdown-faithful" eyeball claim for quotes.
    // Fixed in StructuralTextEdit::selectedMarkdown() (marker restore via
    // MarkoffDocument::blockQuoteDisplayMarker(), same fix shape as canvas).
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("> a quoted line\n");
    Editor ed;
    ed.setDocument(&doc);
    ed.setReadOnly(true);  // matches Corbomite's actual Reading-mode usage
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    selectAll(ed);
    ed.copy();

    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime);
    QVERIFY2(mime->text().contains(QStringLiteral(">")), qPrintable(mime->text()));
    const QString html = mime->html().isEmpty()
        ? QString::fromUtf8(mime->data(QStringLiteral("text/html")))
        : mime->html();
    QVERIFY2(html.contains(QStringLiteral("<blockquote")), qPrintable(html));
}

void TstStyledRichClipboard::copy_listItem_includesMarker()
{
    // Same bug shape as the BlockQuote test above, for ListItem: bullets
    // render via native QTextList decoration (StyleApplier's
    // manageListMembership), not literal "- " text, so selectedText() was
    // also content-only for this kind (Styled had neither the ListItem
    // nor the BlockQuote marker-restore canvas already had for the
    // former) — same fix, same StructuralTextEdit::selectedMarkdown().
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("- first item\n- second item\n");
    Editor ed;
    ed.setDocument(&doc);
    ed.setReadOnly(true);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(50);

    selectAll(ed);
    ed.copy();

    const QString text = QGuiApplication::clipboard()->text();
    QVERIFY2(text.contains(QStringLiteral("- first item")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("- second item")), qPrintable(text));
}

QTEST_MAIN(TstStyledRichClipboard)
#include "tst_styled_rich_clipboard.moc"
