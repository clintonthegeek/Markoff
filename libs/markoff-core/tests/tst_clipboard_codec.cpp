// SPDX-License-Identifier: GPL-3.0-or-later
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QTest>

#include <markoff/core/ClipboardCodec.h>

using namespace Markoff::ClipboardCodec;

class TstClipboardCodec : public QObject {
    Q_OBJECT

private slots:
    void markdownToPlain_stripsBoldItalicStrikeCodeHeadings();
    void markdownToPlain_keepsListMarkersAndLinkText();
    void markdownToHtml_emitsSemanticTagsNotThemedCss();
    void markdownToRtf_roundTripsBoldItalicViaQTextDocument();
    void htmlToMarkdown_boldItalicLinkListHeading();
    void htmlToMarkdown_tableToGfmPipes();
    void htmlToMarkdown_stripsScriptAndStyle();
    void rtfToMarkdown_boldItalicPar();
    void markdownFromMime_prefersMarkoffBlocksThenMarkdownThenHtmlThenRtfThenPlain();
    void markdownFromMime_plainModeIgnoresHtml();
    void mimeFromMarkdown_allFlavorsHasFiveFormats();
    void mimeFromMarkdown_exclusiveHtmlHasNoPlain();
};

void TstClipboardCodec::markdownToPlain_stripsBoldItalicStrikeCodeHeadings()
{
    const QByteArray md =
        "# Title\n\n"
        "Hello **bold** and *italic* and ~~strike~~ and `code`.\n";
    const QString plain = markdownToPlain(md);
    QVERIFY2(!plain.contains(QLatin1Char('#')), qPrintable(plain));
    QVERIFY2(!plain.contains(QStringLiteral("**")), qPrintable(plain));
    QVERIFY2(!plain.contains(QLatin1Char('`')), qPrintable(plain));
    QVERIFY2(!plain.contains(QStringLiteral("~~")), qPrintable(plain));
    QVERIFY2(plain.contains(QStringLiteral("Title")), qPrintable(plain));
    QVERIFY2(plain.contains(QStringLiteral("bold")), qPrintable(plain));
    QVERIFY2(plain.contains(QStringLiteral("italic")), qPrintable(plain));
    QVERIFY2(plain.contains(QStringLiteral("strike")), qPrintable(plain));
    QVERIFY2(plain.contains(QStringLiteral("code")), qPrintable(plain));
}

void TstClipboardCodec::markdownToPlain_keepsListMarkersAndLinkText()
{
    const QByteArray md =
        "- first [label](https://example.com)\n"
        "- second\n"
        "1. numbered\n";
    const QString plain = markdownToPlain(md);
    QVERIFY2(plain.contains(QStringLiteral("- first")), qPrintable(plain));
    QVERIFY2(plain.contains(QStringLiteral("- second")), qPrintable(plain));
    QVERIFY2(plain.contains(QStringLiteral("1. numbered"))
                 || plain.contains(QStringLiteral("1) numbered")),
             qPrintable(plain));
    QVERIFY2(plain.contains(QStringLiteral("label")), qPrintable(plain));
    QVERIFY2(!plain.contains(QStringLiteral("https://example.com")),
             qPrintable(plain));
    QVERIFY2(!plain.contains(QStringLiteral("[label]")), qPrintable(plain));
}

void TstClipboardCodec::markdownToHtml_emitsSemanticTagsNotThemedCss()
{
    const QString html = markdownToHtml(QByteArray("**bold** and *italic*\n"));
    QVERIFY2(html.contains(QStringLiteral("<strong>")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral("</strong>")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral("<em>"))
                 || html.contains(QStringLiteral("<i>")),
             qPrintable(html));
    QVERIFY2(!html.contains(QStringLiteral("**")), qPrintable(html));
    QVERIFY2(!html.contains(QStringLiteral("qtextdocument"), Qt::CaseInsensitive),
             qPrintable(html));
    QVERIFY2(!html.contains(QStringLiteral("background-color")), qPrintable(html));
    QVERIFY2(!html.contains(QStringLiteral("QFont")), qPrintable(html));
}

void TstClipboardCodec::markdownToRtf_roundTripsBoldItalicViaQTextDocument()
{
    const QByteArray rtf = markdownToRtf(QByteArray("**bold** and *italic*\n"));
    QVERIFY2(!rtf.isEmpty(), "markdownToRtf produced no bytes");
    const QByteArray lower = rtf.toLower();
    QVERIFY2(lower.contains("\\b") || lower.contains("\\b0") || lower.contains("bold"),
             rtf.constData());
    QVERIFY2(lower.contains("bold"), rtf.constData());
    QVERIFY2(lower.contains("italic"), rtf.constData());
}

void TstClipboardCodec::htmlToMarkdown_boldItalicLinkListHeading()
{
    const QString html = QStringLiteral(
        "<h2>Head</h2>"
        "<p><strong>bold</strong> and <em>italic</em> "
        "<a href=\"https://ex.test\">link</a></p>"
        "<ul><li>one</li><li>two</li></ul>");
    const QByteArray md = htmlToMarkdown(html);
    const QString s = QString::fromUtf8(md);
    QVERIFY2(s.contains(QStringLiteral("## Head"))
                 || s.contains(QStringLiteral("##Head")),
             qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("**bold**")), qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("*italic*"))
                 || s.contains(QStringLiteral("_italic_")),
             qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("[link](https://ex.test)")), qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("- one"))
                 || s.contains(QStringLiteral("* one")),
             qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("- two"))
                 || s.contains(QStringLiteral("* two")),
             qPrintable(s));
}

void TstClipboardCodec::htmlToMarkdown_tableToGfmPipes()
{
    const QString html = QStringLiteral(
        "<table><thead><tr><th>A</th><th>B</th></tr></thead>"
        "<tbody><tr><td>1</td><td>2</td></tr></tbody></table>");
    const QByteArray md = htmlToMarkdown(html);
    const QString s = QString::fromUtf8(md);
    QVERIFY2(s.contains(QLatin1Char('|')), qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("A")), qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("B")), qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("1")), qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("2")), qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("---")), qPrintable(s));
}

void TstClipboardCodec::htmlToMarkdown_stripsScriptAndStyle()
{
    const QString html = QStringLiteral(
        "<p>keep</p><script>alert(1)</script><style>body{color:red}</style>");
    const QByteArray md = htmlToMarkdown(html);
    const QString s = QString::fromUtf8(md);
    QVERIFY2(s.contains(QStringLiteral("keep")), qPrintable(s));
    QVERIFY2(!s.contains(QStringLiteral("alert")), qPrintable(s));
    QVERIFY2(!s.contains(QStringLiteral("color:red")), qPrintable(s));
}

void TstClipboardCodec::rtfToMarkdown_boldItalicPar()
{
    const QByteArray rtf =
        "{\\rtf1\\ansi{\\b bold} {\\i italic}\\par more}";
    const QByteArray md = rtfToMarkdown(rtf);
    const QString s = QString::fromUtf8(md);
    QVERIFY2(s.contains(QStringLiteral("**bold**")), qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("*italic*"))
                 || s.contains(QStringLiteral("_italic_")),
             qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("more")), qPrintable(s));
}

void TstClipboardCodec::markdownFromMime_prefersMarkoffBlocksThenMarkdownThenHtmlThenRtfThenPlain()
{
    QJsonObject payload;
    payload[QStringLiteral("version")] = 1;
    QJsonArray blocks;
    QJsonObject b;
    b[QStringLiteral("kind")] = QStringLiteral("paragraph");
    b[QStringLiteral("text")] = QStringLiteral("FROM-BLOCKS");
    blocks.append(b);
    payload[QStringLiteral("blocks")] = blocks;

    auto fill = [](QMimeData *mime) {
        mime->setText(QStringLiteral("FROM-PLAIN"));
        mime->setData(QString::fromUtf8(kMarkdownMime), QByteArray("FROM-MARKDOWN"));
        mime->setHtml(QStringLiteral("<p>FROM-HTML</p>"));
        mime->setData(QString::fromUtf8(kRtfMime),
                      QByteArray("{\\rtf1 FROM-RTF}"));
    };

    {
        QMimeData mime;
        fill(&mime);
        mime.setData(QString::fromUtf8(kBlocksMime),
                     QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QCOMPARE(QString::fromUtf8(markdownFromMime(&mime, PasteMode::Smart)),
                 QStringLiteral("FROM-BLOCKS"));
    }
    {
        QMimeData mime;
        fill(&mime);
        QCOMPARE(QString::fromUtf8(markdownFromMime(&mime, PasteMode::Smart)),
                 QStringLiteral("FROM-MARKDOWN"));
    }
    {
        QMimeData mime;
        mime.setText(QStringLiteral("FROM-PLAIN"));
        mime.setHtml(QStringLiteral("<p>FROM-HTML</p>"));
        mime.setData(QString::fromUtf8(kRtfMime), QByteArray("{\\rtf1 FROM-RTF}"));
        const QString got = QString::fromUtf8(markdownFromMime(&mime, PasteMode::Smart));
        QVERIFY2(got.contains(QStringLiteral("FROM-HTML")), qPrintable(got));
        QVERIFY2(!got.contains(QStringLiteral("FROM-PLAIN")), qPrintable(got));
    }
    {
        QMimeData mime;
        mime.setText(QStringLiteral("FROM-PLAIN"));
        mime.setData(QString::fromUtf8(kRtfMime),
                     QByteArray("{\\rtf1 FROM-RTF}"));
        const QString got = QString::fromUtf8(markdownFromMime(&mime, PasteMode::Smart));
        QVERIFY2(got.contains(QStringLiteral("FROM-RTF")), qPrintable(got));
        QVERIFY2(!got.contains(QStringLiteral("FROM-PLAIN")), qPrintable(got));
    }
    {
        QMimeData mime;
        mime.setText(QStringLiteral("FROM-PLAIN"));
        QCOMPARE(QString::fromUtf8(markdownFromMime(&mime, PasteMode::Smart)),
                 QStringLiteral("FROM-PLAIN"));
    }
}

void TstClipboardCodec::markdownFromMime_plainModeIgnoresHtml()
{
    QMimeData mime;
    mime.setText(QStringLiteral("PLAIN-FALLBACK"));
    mime.setHtml(QStringLiteral("<p><strong>HTML-BOLD</strong></p>"));
    const QString got =
        QString::fromUtf8(markdownFromMime(&mime, PasteMode::Plain));
    QCOMPARE(got, QStringLiteral("PLAIN-FALLBACK"));
    QVERIFY(!got.contains(QStringLiteral("**")));
}

void TstClipboardCodec::mimeFromMarkdown_allFlavorsHasFiveFormats()
{
    QJsonObject payload;
    payload[QStringLiteral("version")] = 1;
    QJsonArray blocks;
    QJsonObject b;
    b[QStringLiteral("kind")] = QStringLiteral("paragraph");
    b[QStringLiteral("text")] = QStringLiteral("Hello **world**");
    blocks.append(b);
    payload[QStringLiteral("blocks")] = blocks;

    QScopedPointer<QMimeData> mime(
        mimeFromMarkdown(QByteArray("Hello **world**\n"),
                         QJsonDocument(payload),
                         Flavor::All));
    QVERIFY(mime);
    QVERIFY(mime->hasText());
    QVERIFY(mime->hasHtml());
    QVERIFY(mime->hasFormat(QString::fromUtf8(kMarkdownMime)));
    QVERIFY(mime->hasFormat(QString::fromUtf8(kRtfMime)));
    QVERIFY(mime->hasFormat(QString::fromUtf8(kBlocksMime)));
    QCOMPARE(mime->text(), QStringLiteral("Hello **world**\n"));
}

void TstClipboardCodec::mimeFromMarkdown_exclusiveHtmlHasNoPlain()
{
    QScopedPointer<QMimeData> mime(
        mimeFromMarkdown(QByteArray("**bold**\n"),
                         QJsonDocument(),
                         Flavor::Html));
    QVERIFY(mime);
    QVERIFY(mime->hasHtml() || mime->hasFormat(QStringLiteral("text/html")));
    QVERIFY2(!mime->hasText(),
             "exclusive HTML must not also offer text/plain "
             "(Word/LibreOffice would ignore the HTML otherwise)");
    QVERIFY(!mime->hasFormat(QString::fromUtf8(kMarkdownMime)));
    QVERIFY(!mime->hasFormat(QString::fromUtf8(kRtfMime)));
}

QTEST_MAIN(TstClipboardCodec)
#include "tst_clipboard_codec.moc"
