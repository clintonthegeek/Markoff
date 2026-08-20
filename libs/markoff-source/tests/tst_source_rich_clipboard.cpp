// SPDX-License-Identifier: GPL-3.0-or-later
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTest>
#include <QTextCursor>

#include <markoff/core/ClipboardCodec.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/source/Editor.h>

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
    void copyAsPlain_stripsMarkers();
    void copyAsHtml_exclusiveNoPlain();
    void paste_htmlBecomesMarkdown();
    void pasteAsPlain_stripsIncomingHtml();
};

void TstSourceRichClipboard::copy_writesHtmlAndMarkdownMime()
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

void TstSourceRichClipboard::copyAsPlain_stripsMarkers()
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
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime);
    QVERIFY(!mime->hasHtml());
}

void TstSourceRichClipboard::copyAsHtml_exclusiveNoPlain()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("**bold**\n");
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
