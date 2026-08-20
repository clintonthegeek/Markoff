// SPDX-License-Identifier: GPL-3.0-or-later
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/ClipboardCodec.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;
using Markoff::ClipboardCodec::kMarkdownMime;
using Markoff::ClipboardCodec::kRtfMime;
using Markoff::MarkoffDocument;

namespace {

void selectAllViaShortcut(View &view)
{
    QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
}

QByteArray flat(MarkoffDocument &doc)
{
    QByteArray out;
    for (const BlockId id : doc.iterateBlocks())
        out += doc.blockText(id);
    return out;
}

}  // namespace

class TstCanvasRichClipboard : public QObject {
    Q_OBJECT
private slots:
    void copy_writesHtmlAndMarkdownMime();
    void copyAsPlain_stripsMarkers();
    void paste_htmlBecomesMarkdown();
    void pasteAsPlain_stripsIncomingHtml();
    void copy_listItemIncludesMarker();
};

void TstCanvasRichClipboard::copy_writesHtmlAndMarkdownMime()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Hello **world**\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    selectAllViaShortcut(view);
    QVERIFY(view.hasSelection());
    QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);

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

void TstCanvasRichClipboard::copyAsPlain_stripsMarkers()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Hello **world**\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    selectAllViaShortcut(view);
    view.copyAsPlain();

    const QString plain = QGuiApplication::clipboard()->text();
    QVERIFY2(plain.contains(QStringLiteral("world")), qPrintable(plain));
    QVERIFY2(!plain.contains(QStringLiteral("**")), qPrintable(plain));
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime);
    QVERIFY(!mime->hasHtml());
}

void TstCanvasRichClipboard::paste_htmlBecomesMarkdown()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("X\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *mime = new QMimeData;
    mime->setHtml(QStringLiteral("<p><strong>bold</strong> and "
                                 "<a href=\"https://ex.test\">link</a></p>"));
    QGuiApplication::clipboard()->setMimeData(mime);

    view.setCaretPosition(doc.iterateBlocks().front(),
                          int(doc.blockText(doc.iterateBlocks().front()).size()));
    view.paste();

    const QString got = QString::fromUtf8(flat(doc));
    QVERIFY2(got.contains(QStringLiteral("**bold**")), qPrintable(got));
    QVERIFY2(got.contains(QStringLiteral("[link](https://ex.test)")), qPrintable(got));
}

void TstCanvasRichClipboard::pasteAsPlain_stripsIncomingHtml()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("X\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *mime = new QMimeData;
    mime->setHtml(QStringLiteral("<p><strong>bold</strong></p>"));
    mime->setText(QStringLiteral("PLAIN"));
    QGuiApplication::clipboard()->setMimeData(mime);

    view.setCaretPosition(doc.iterateBlocks().front(),
                          int(doc.blockText(doc.iterateBlocks().front()).size()));
    view.pasteAsPlain();

    const QString got = QString::fromUtf8(flat(doc));
    QVERIFY2(got.contains(QStringLiteral("PLAIN")), qPrintable(got));
    QVERIFY2(!got.contains(QStringLiteral("**")), qPrintable(got));
}

void TstCanvasRichClipboard::copy_listItemIncludesMarker()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("- first\n- second\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    selectAllViaShortcut(view);
    QVERIFY(view.hasSelection());
    QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);

    const QString text = QGuiApplication::clipboard()->text();
    QVERIFY2(text.contains(QStringLiteral("- first")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("- second")), qPrintable(text));
}

QTEST_MAIN(TstCanvasRichClipboard)
#include "tst_canvas_rich_clipboard.moc"
