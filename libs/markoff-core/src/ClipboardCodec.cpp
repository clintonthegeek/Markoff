// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/ClipboardCodec.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/parser/Document.h>
#include <markoff/parser/SourceSpan.h>
#include <markoff/parser/TreeSitterParser.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSet>
#include <QStack>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QTextTable>
#include <QUrl>

namespace Markoff::ClipboardCodec {
namespace {

QByteArray bodyUtf8(const Document &doc)
{
    return doc.markdownContent().toUtf8();
}

QByteArray blockSlice(const QByteArray &body, const TopLevelBlock &block)
{
    const int from = qBound(0, block.byteStart, body.size());
    const int to = qBound(from, block.byteEnd, body.size());
    return body.mid(from, to - from);
}

QList<SourceSpan> spansFor(const TopLevelBlock &block, const QByteArray &slice)
{
    if (!block.inlineSpans.isEmpty())
        return block.inlineSpans;
    if (block.kind == TopLevelBlock::Kind::FencedCodeBlock
        || block.kind == TopLevelBlock::Kind::IndentedCodeBlock
        || block.kind == TopLevelBlock::Kind::ThematicBreak)
        return {};
    return inlineSpansFor(slice);
}

QString stripDelimiters(const QByteArray &src, const QList<SourceSpan> &spans)
{
    if (src.isEmpty())
        return {};
    QByteArray skip(src.size(), '\0');
    for (const SourceSpan &s : spans) {
        if (!s.isDelimiter)
            continue;
        const int from = qBound(0, s.utf8Offset, src.size());
        const int to = qBound(from, s.utf8Offset + s.utf8Length, src.size());
        for (int i = from; i < to; ++i)
            skip[i] = 1;
    }
    QByteArray out;
    out.reserve(src.size());
    for (int i = 0; i < src.size(); ++i) {
        if (!skip[i])
            out.append(src[i]);
    }
    return QString::fromUtf8(out).trimmed();
}

QString listMarker(const TopLevelBlock &block)
{
    if (block.kind != TopLevelBlock::Kind::ListItem)
        return {};
    if (block.markerStyle == QLatin1String("dot"))
        return QString::number(qMax(1, block.markerNumber)) + QStringLiteral(". ");
    if (block.markerStyle == QLatin1String("paren"))
        return QString::number(qMax(1, block.markerNumber)) + QStringLiteral(") ");
    if (block.markerStyle == QLatin1String("plus"))
        return QStringLiteral("+ ");
    if (block.markerStyle == QLatin1String("star"))
        return QStringLiteral("* ");
    if (block.markerStyle == QLatin1String("task"))
        return block.checked ? QStringLiteral("- [x] ") : QStringLiteral("- [ ] ");
    return QStringLiteral("- ");
}

bool isOrderedList(const TopLevelBlock &block)
{
    return block.markerStyle == QLatin1String("dot")
        || block.markerStyle == QLatin1String("paren");
}

QString htmlEscape(const QString &s)
{
    QString o;
    o.reserve(s.size());
    for (QChar c : s) {
        if (c == QLatin1Char('&'))
            o += QStringLiteral("&amp;");
        else if (c == QLatin1Char('<'))
            o += QStringLiteral("&lt;");
        else if (c == QLatin1Char('>'))
            o += QStringLiteral("&gt;");
        else if (c == QLatin1Char('"'))
            o += QStringLiteral("&quot;");
        else
            o += c;
    }
    return o;
}

struct InlineFlags {
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool code = false;
    bool link = false;
    QString href;
    bool operator==(const InlineFlags &) const = default;
};

InlineFlags flagsAt(int byte, const QList<SourceSpan> &spans)
{
    InlineFlags f;
    for (const SourceSpan &s : spans) {
        if (s.isDelimiter)
            continue;
        if (byte < s.utf8Offset || byte >= s.utf8Offset + s.utf8Length)
            continue;
        f.bold = f.bold || s.bold;
        f.italic = f.italic || s.italic;
        f.strike = f.strike || s.strikethrough;
        f.code = f.code || s.code;
        if (s.isLink || s.isWikilink) {
            f.link = true;
            if (f.href.isEmpty()) {
                if (!s.linkTarget.url.isEmpty())
                    f.href = s.linkTarget.url;
                else if (!s.linkTarget.page.isEmpty())
                    f.href = s.linkTarget.page;
                else if (!s.linkTarget.alias.isEmpty())
                    f.href = s.linkTarget.alias;
            }
        }
    }
    return f;
}

void closeFlags(QString &out, const InlineFlags &f)
{
    if (f.link)
        out += QStringLiteral("</a>");
    if (f.code)
        out += QStringLiteral("</code>");
    if (f.strike)
        out += QStringLiteral("</s>");
    if (f.italic)
        out += QStringLiteral("</em>");
    if (f.bold)
        out += QStringLiteral("</strong>");
}

void openFlags(QString &out, const InlineFlags &f)
{
    if (f.bold)
        out += QStringLiteral("<strong>");
    if (f.italic)
        out += QStringLiteral("<em>");
    if (f.strike)
        out += QStringLiteral("<s>");
    if (f.code)
        out += QStringLiteral("<code>");
    if (f.link) {
        out += QStringLiteral("<a href=\"");
        out += htmlEscape(f.href);
        out += QStringLiteral("\">");
    }
}

QString rtfEscape(const QString &s)
{
    QString o;
    o.reserve(s.size());
    for (QChar c : s) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('{') || c == QLatin1Char('}')) {
            o += QLatin1Char('\\');
            o += c;
        } else if (c.unicode() > 127) {
            o += QStringLiteral("\\u%1?").arg(int(c.unicode()));
        } else {
            o += c;
        }
    }
    return o;
}

void closeRtfFlags(QString &out, const InlineFlags &f)
{
    if (f.link)
        out += QLatin1Char('}');
    if (f.code)
        out += QLatin1Char('}');
    if (f.strike)
        out += QLatin1Char('}');
    if (f.italic)
        out += QLatin1Char('}');
    if (f.bold)
        out += QLatin1Char('}');
}

void openRtfFlags(QString &out, const InlineFlags &f)
{
    if (f.bold)
        out += QStringLiteral("{\\b ");
    if (f.italic)
        out += QStringLiteral("{\\i ");
    if (f.strike)
        out += QStringLiteral("{\\strike ");
    if (f.code)
        out += QStringLiteral("{\\f1 ");
    if (f.link)
        out += QStringLiteral("{");
}

QString inlinesToRtf(const QByteArray &src, const QList<SourceSpan> &spans)
{
    QByteArray skip(src.size(), '\0');
    for (const SourceSpan &s : spans) {
        if (!s.isDelimiter)
            continue;
        const int from = qBound(0, s.utf8Offset, src.size());
        const int to = qBound(from, s.utf8Offset + s.utf8Length, src.size());
        for (int i = from; i < to; ++i)
            skip[i] = 1;
    }
    QString out;
    InlineFlags current;
    int i = 0;
    while (i < src.size()) {
        if (skip[i]) {
            ++i;
            continue;
        }
        const InlineFlags next = flagsAt(i, spans);
        if (next != current) {
            closeRtfFlags(out, current);
            openRtfFlags(out, next);
            current = next;
        }
        const unsigned char c = static_cast<unsigned char>(src[i]);
        int len = 1;
        if ((c & 0xE0) == 0xC0)
            len = 2;
        else if ((c & 0xF0) == 0xE0)
            len = 3;
        else if ((c & 0xF8) == 0xF0)
            len = 4;
        len = qBound(1, len, src.size() - i);
        out += rtfEscape(QString::fromUtf8(src.constData() + i, len));
        i += len;
    }
    closeRtfFlags(out, current);
    return out.trimmed();
}

QString inlinesToHtml(const QByteArray &src, const QList<SourceSpan> &spans)
{
    QByteArray skip(src.size(), '\0');
    for (const SourceSpan &s : spans) {
        if (!s.isDelimiter)
            continue;
        const int from = qBound(0, s.utf8Offset, src.size());
        const int to = qBound(from, s.utf8Offset + s.utf8Length, src.size());
        for (int i = from; i < to; ++i)
            skip[i] = 1;
    }

    QString out;
    InlineFlags current;
    int i = 0;
    while (i < src.size()) {
        if (skip[i]) {
            ++i;
            continue;
        }
        const InlineFlags next = flagsAt(i, spans);
        if (next != current) {
            closeFlags(out, current);
            openFlags(out, next);
            current = next;
        }
        // Consume a UTF-8 character.
        const unsigned char c = static_cast<unsigned char>(src[i]);
        int len = 1;
        if ((c & 0x80) == 0)
            len = 1;
        else if ((c & 0xE0) == 0xC0)
            len = 2;
        else if ((c & 0xF0) == 0xE0)
            len = 3;
        else if ((c & 0xF8) == 0xF0)
            len = 4;
        len = qBound(1, len, src.size() - i);
        out += htmlEscape(QString::fromUtf8(src.constData() + i, len));
        i += len;
    }
    closeFlags(out, current);
    return out.trimmed();
}

QString blockToPlain(const TopLevelBlock &block, const QByteArray &slice)
{
    if (block.kind == TopLevelBlock::Kind::ThematicBreak)
        return {};
    if (block.kind == TopLevelBlock::Kind::FencedCodeBlock
        || block.kind == TopLevelBlock::Kind::IndentedCodeBlock) {
        return block.codeText;
    }
    const QString inner = stripDelimiters(slice, spansFor(block, slice));
    if (block.kind == TopLevelBlock::Kind::ListItem)
        return listMarker(block) + inner;
    return inner;
}

}  // namespace

QString markdownToPlain(const QByteArray &markdown)
{
    if (markdown.isEmpty())
        return {};
    const auto doc = Document::fromMarkdown(QString::fromUtf8(markdown));
    if (!doc)
        return {};
    const QByteArray body = bodyUtf8(*doc);
    const QList<TopLevelBlock> blocks = doc->topLevelBlocks();
    QStringList parts;
    parts.reserve(blocks.size());
    for (const TopLevelBlock &block : blocks) {
        const QString part = blockToPlain(block, blockSlice(body, block));
        if (!part.isEmpty())
            parts << part;
    }
    return parts.join(QLatin1Char('\n'));
}

QString markdownToHtml(const QByteArray &markdown)
{
    if (markdown.isEmpty())
        return {};
    const auto doc = Document::fromMarkdown(QString::fromUtf8(markdown));
    if (!doc)
        return {};
    const QByteArray body = bodyUtf8(*doc);
    QString html;
    enum { NoList, Ul, Ol } openList = NoList;

    auto closeList = [&]() {
        if (openList == Ul)
            html += QStringLiteral("</ul>");
        else if (openList == Ol)
            html += QStringLiteral("</ol>");
        openList = NoList;
    };

    for (const TopLevelBlock &block : doc->topLevelBlocks()) {
        const QByteArray slice = blockSlice(body, block);
        if (block.kind == TopLevelBlock::Kind::ListItem) {
            const auto want = isOrderedList(block) ? Ol : Ul;
            if (openList != want) {
                closeList();
                html += (want == Ol) ? QStringLiteral("<ol>")
                                     : QStringLiteral("<ul>");
                openList = want;
            }
            html += QStringLiteral("<li>");
            html += inlinesToHtml(slice, spansFor(block, slice));
            html += QStringLiteral("</li>");
            continue;
        }
        closeList();
        switch (block.kind) {
        case TopLevelBlock::Kind::AtxHeading:
        case TopLevelBlock::Kind::SetextHeading: {
            const int level = qBound(1, block.headingLevel, 6);
            html += QStringLiteral("<h%1>").arg(level);
            html += inlinesToHtml(slice, spansFor(block, slice));
            html += QStringLiteral("</h%1>").arg(level);
            break;
        }
        case TopLevelBlock::Kind::FencedCodeBlock:
        case TopLevelBlock::Kind::IndentedCodeBlock: {
            html += QStringLiteral("<pre><code");
            if (!block.codeLanguage.isEmpty()) {
                html += QStringLiteral(" class=\"language-");
                html += htmlEscape(block.codeLanguage);
                html += QLatin1Char('"');
            }
            html += QLatin1Char('>');
            html += htmlEscape(block.codeText);
            html += QStringLiteral("</code></pre>");
            break;
        }
        case TopLevelBlock::Kind::ThematicBreak:
            html += QStringLiteral("<hr>");
            break;
        case TopLevelBlock::Kind::BlockQuote: {
            html += QStringLiteral("<blockquote>");
            html += inlinesToHtml(slice, spansFor(block, slice));
            html += QStringLiteral("</blockquote>");
            break;
        }
        default: {
            const QString inner = inlinesToHtml(slice, spansFor(block, slice));
            if (!inner.isEmpty()) {
                html += QStringLiteral("<p>");
                html += inner;
                html += QStringLiteral("</p>");
            }
            break;
        }
        }
    }
    closeList();
    return html;
}

QByteArray markdownToRtf(const QByteArray &markdown)
{
    // QTextDocumentWriter has no RTF backend on Linux; emit a small
    // semantic subset ourselves from the same parse the HTML path uses.
    if (markdown.isEmpty())
        return {};
    const auto doc = Document::fromMarkdown(QString::fromUtf8(markdown));
    if (!doc)
        return {};
    const QByteArray body = bodyUtf8(*doc);
    QString rtf = QStringLiteral("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times;}{\\f1 Courier;}}");
    bool first = true;
    for (const TopLevelBlock &block : doc->topLevelBlocks()) {
        if (!first)
            rtf += QStringLiteral("\\par ");
        first = false;
        const QByteArray slice = blockSlice(body, block);
        if (block.kind == TopLevelBlock::Kind::AtxHeading
            || block.kind == TopLevelBlock::Kind::SetextHeading) {
            rtf += QStringLiteral("{\\b ");
            rtf += inlinesToRtf(slice, spansFor(block, slice));
            rtf += QLatin1Char('}');
        } else if (block.kind == TopLevelBlock::Kind::ListItem) {
            rtf += QStringLiteral("\\bullet ");
            rtf += inlinesToRtf(slice, spansFor(block, slice));
        } else if (block.kind == TopLevelBlock::Kind::FencedCodeBlock
                   || block.kind == TopLevelBlock::Kind::IndentedCodeBlock) {
            rtf += QStringLiteral("{\\f1 ");
            rtf += rtfEscape(block.codeText);
            rtf += QLatin1Char('}');
        } else if (block.kind == TopLevelBlock::Kind::ThematicBreak) {
            rtf += QStringLiteral("\\emdash\\emdash\\emdash");
        } else {
            rtf += inlinesToRtf(slice, spansFor(block, slice));
        }
    }
    rtf += QLatin1Char('}');
    return rtf.toUtf8();
}

namespace {

bool isMonospace(const QTextCharFormat &fmt)
{
    if (fmt.fontFixedPitch())
        return true;
    if (fmt.fontStyleHint() == QFont::TypeWriter)
        return true;
    const QStringList families = fmt.fontFamilies().toStringList();
    for (const QString &fam : families) {
        const QString lower = fam.toLower();
        if (lower.contains(QLatin1String("mono"))
            || lower.contains(QLatin1String("courier"))
            || lower.contains(QLatin1String("consolas")))
            return true;
    }
    return false;
}

QString wrapMd(const QString &text, const QTextCharFormat &fmt, bool skipDefaultBold)
{
    QString t = text;
    t.replace(QChar::LineSeparator, QLatin1Char('\n'));
    t.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    t.replace(QChar::Nbsp, QLatin1Char(' '));
    if (t.isEmpty())
        return t;
    if (fmt.isImageFormat()) {
        const QTextImageFormat img = fmt.toImageFormat();
        return QStringLiteral("![](%1)").arg(img.name());
    }
    if (isMonospace(fmt) && !t.contains(QLatin1Char('\n')))
        t = QLatin1Char('`') + t + QLatin1Char('`');
    if (fmt.fontStrikeOut())
        t = QStringLiteral("~~") + t + QStringLiteral("~~");
    if (fmt.fontItalic())
        t = QLatin1Char('_') + t + QLatin1Char('_');
    if (!skipDefaultBold && fmt.fontWeight() >= QFont::Bold)
        t = QStringLiteral("**") + t + QStringLiteral("**");
    if (fmt.isAnchor() && !fmt.anchorHref().isEmpty()) {
        t = QStringLiteral("[%1](%2)").arg(t, fmt.anchorHref());
    }
    return t;
}

QString blockInnerMarkdown(const QTextBlock &block, bool skipDefaultBold = false)
{
    QString out;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        if (!frag.isValid())
            continue;
        out += wrapMd(frag.text(), frag.charFormat(), skipDefaultBold);
    }
    if (out.isEmpty())
        out = block.text();
    return out;
}

QString tableToGfm(QTextTable *table)
{
    if (!table)
        return {};
    const int rows = table->rows();
    const int cols = table->columns();
    if (rows <= 0 || cols <= 0)
        return {};
    QString out;
    for (int r = 0; r < rows; ++r) {
        out += QLatin1Char('|');
        for (int c = 0; c < cols; ++c) {
            const QTextTableCell cell = table->cellAt(r, c);
            QString text;
            for (auto it = cell.begin(); it != cell.end(); ++it) {
                const QTextBlock b = it.currentBlock();
                if (b.isValid()) {
                    if (!text.isEmpty())
                        text += QLatin1Char(' ');
                    text += b.text();
                }
            }
            text.replace(QLatin1Char('|'), QStringLiteral("\\|"));
            out += QLatin1Char(' ');
            out += text.trimmed();
            out += QStringLiteral(" |");
        }
        out += QLatin1Char('\n');
        if (r == 0) {
            out += QLatin1Char('|');
            for (int c = 0; c < cols; ++c)
                out += QStringLiteral(" --- |");
            out += QLatin1Char('\n');
        }
    }
    return out;
}

void collectTableBlocks(QTextTable *table, QSet<int> *skip)
{
    if (!table || !skip)
        return;
    for (int r = 0; r < table->rows(); ++r) {
        for (int c = 0; c < table->columns(); ++c) {
            const QTextTableCell cell = table->cellAt(r, c);
            for (auto it = cell.begin(); it != cell.end(); ++it) {
                const QTextBlock b = it.currentBlock();
                if (b.isValid())
                    skip->insert(b.blockNumber());
            }
        }
    }
}

}  // namespace

QByteArray htmlToMarkdown(const QString &html)
{
    if (html.isEmpty())
        return {};
    QTextDocument doc;
    doc.setHtml(html);
    QString md;
    QSet<int> skipBlocks;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        if (skipBlocks.contains(block.blockNumber()))
            continue;
        QTextCursor cur(block);
        if (QTextTable *table = cur.currentTable()) {
            md += tableToGfm(table);
            if (!md.endsWith(QLatin1Char('\n')))
                md += QLatin1Char('\n');
            md += QLatin1Char('\n');
            collectTableBlocks(table, &skipBlocks);
            continue;
        }
        const QTextBlockFormat bf = block.blockFormat();
        if (bf.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
            md += QStringLiteral("---\n\n");
            continue;
        }
        const bool isHeading = bf.headingLevel() > 0;
        QString inner = blockInnerMarkdown(block, isHeading);
        if (QTextList *list = block.textList()) {
            const QTextListFormat lf = list->format();
            const bool ordered =
                lf.style() == QTextListFormat::ListDecimal
                || lf.style() == QTextListFormat::ListUpperAlpha
                || lf.style() == QTextListFormat::ListLowerAlpha
                || lf.style() == QTextListFormat::ListUpperRoman
                || lf.style() == QTextListFormat::ListLowerRoman;
            const int indent = qMax(0, lf.indent() - 1);
            md += QString(indent * 2, QLatin1Char(' '));
            if (ordered) {
                const int n = list->itemNumber(block) + 1;
                md += QString::number(n);
                md += QStringLiteral(". ");
            } else {
                md += QStringLiteral("- ");
            }
            md += inner.trimmed();
            md += QLatin1Char('\n');
            continue;
        }
        const int heading = bf.headingLevel();
        if (heading > 0) {
            md += QString(heading, QLatin1Char('#'));
            md += QLatin1Char(' ');
            md += inner.trimmed();
            md += QStringLiteral("\n\n");
            continue;
        }
        if (inner.trimmed().isEmpty()) {
            if (!md.isEmpty() && !md.endsWith(QStringLiteral("\n\n")))
                md += QLatin1Char('\n');
            continue;
        }
        md += inner;
        md += QStringLiteral("\n\n");
    }
    return md.trimmed().toUtf8();
}

QString htmlToPlain(const QString &html)
{
    if (html.isEmpty())
        return {};
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText();
}

namespace {

int hexVal(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool isControlChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

QByteArray rtfBytes(const QMimeData *mime)
{
    if (!mime)
        return {};
    if (mime->hasFormat(QString::fromUtf8(kRtfMime)))
        return mime->data(QString::fromUtf8(kRtfMime));
    if (mime->hasFormat(QStringLiteral("application/rtf")))
        return mime->data(QStringLiteral("application/rtf"));
    if (mime->hasFormat(QStringLiteral("text/richtext")))
        return mime->data(QStringLiteral("text/richtext"));
    return {};
}

}  // namespace

QByteArray rtfToMarkdown(const QByteArray &rtf)
{
    if (rtf.isEmpty())
        return {};
    QString md;
    QString run;
    struct RtfState {
        bool bold = false;
        bool italic = false;
        bool strike = false;
    };
    RtfState st;
    QStack<RtfState> groups;
    int i = 0;
    const int n = rtf.size();

    auto flush = [&]() {
        if (run.isEmpty())
            return;
        QString t = run;
        run.clear();
        if (st.strike)
            t = QStringLiteral("~~") + t + QStringLiteral("~~");
        if (st.italic)
            t = QLatin1Char('_') + t + QLatin1Char('_');
        if (st.bold)
            t = QStringLiteral("**") + t + QStringLiteral("**");
        md += t;
    };

    auto skipGroup = [&]() {
        int depth = 1;
        while (i < n && depth > 0) {
            const char c = rtf[i++];
            if (c == '{')
                ++depth;
            else if (c == '}')
                --depth;
            else if (c == '\\' && i < n)
                ++i;
        }
    };

    while (i < n) {
        const char c = rtf[i];
        if (c == '{') {
            ++i;
            // Skip ignorable destinations ({\* ...}) and tables.
            if (i < n && rtf[i] == '\\') {
                const QByteArray rest = rtf.mid(i);
                if (rest.startsWith("\\*")
                    || rest.startsWith("\\fonttbl")
                    || rest.startsWith("\\colortbl")
                    || rest.startsWith("\\stylesheet")
                    || rest.startsWith("\\info")
                    || rest.startsWith("\\header")
                    || rest.startsWith("\\footer")) {
                    skipGroup();
                    continue;
                }
            }
            groups.push(st);
            continue;
        }
        if (c == '}') {
            ++i;
            flush();
            if (!groups.isEmpty())
                st = groups.pop();
            continue;
        }
        if (c == '\\') {
            ++i;
            if (i >= n)
                break;
            const char n1 = rtf[i];
            if (n1 == '\\' || n1 == '{' || n1 == '}') {
                flush();
                run += QLatin1Char(n1);
                ++i;
                continue;
            }
            if (n1 == '\'') {
                ++i;
                if (i + 1 < n) {
                    const int hi = hexVal(rtf[i]);
                    const int lo = hexVal(rtf[i + 1]);
                    i += 2;
                    if (hi >= 0 && lo >= 0) {
                        flush();
                        run += QChar(hi * 16 + lo);
                    }
                }
                continue;
            }
            if (n1 == '~') {
                flush();
                run += QLatin1Char(' ');
                ++i;
                continue;
            }
            if (n1 == '*') {
                // Ignorable destination without a wrapping group.
                ++i;
                continue;
            }
            QByteArray word;
            while (i < n && isControlChar(rtf[i]))
                word += rtf[i++];
            bool haveArg = false;
            int sign = 1;
            if (i < n && rtf[i] == '-') {
                sign = -1;
                ++i;
            }
            int val = 0;
            while (i < n && rtf[i] >= '0' && rtf[i] <= '9') {
                haveArg = true;
                val = val * 10 + (rtf[i] - '0');
                ++i;
            }
            const int arg = haveArg ? sign * val : 1;
            if (i < n && rtf[i] == ' ')
                ++i;

            if (word == "b") {
                flush();
                st.bold = !haveArg || arg != 0;
            } else if (word == "i") {
                flush();
                st.italic = !haveArg || arg != 0;
            } else if (word == "strike") {
                flush();
                st.strike = !haveArg || arg != 0;
            } else if (word == "par" || word == "line" || word == "pard") {
                flush();
                if (word != "pard")
                    md += QLatin1Char('\n');
            } else if (word == "bullet") {
                flush();
                md += QStringLiteral("- ");
            } else if (word == "tab") {
                flush();
                run += QLatin1Char('\t');
            } else if (word == "u" && haveArg) {
                flush();
                run += QChar(ushort(arg < 0 ? 0 : arg));
                // Optional ANSI fallback char.
                if (i < n && rtf[i] == '?')
                    ++i;
            }
            continue;
        }
        if (c == '\r' || c == '\n') {
            ++i;
            continue;
        }
        run += QLatin1Char(c);
        ++i;
    }
    flush();
    return md.trimmed().toUtf8();
}

QString rtfToPlain(const QByteArray &rtf)
{
    return markdownToPlain(rtfToMarkdown(rtf));
}

QMimeData *mimeFromMarkdown(const QByteArray &markdown,
                            const QJsonDocument &blocksPayload,
                            Flavor flavor)
{
    auto *mime = new QMimeData;
    switch (flavor) {
    case Flavor::All:
        mime->setText(QString::fromUtf8(markdown));
        mime->setData(QString::fromUtf8(kMarkdownMime), markdown);
        mime->setHtml(markdownToHtml(markdown));
        mime->setData(QString::fromUtf8(kRtfMime), markdownToRtf(markdown));
        if (!blocksPayload.isEmpty())
            mime->setData(QString::fromUtf8(kBlocksMime),
                          blocksPayload.toJson(QJsonDocument::Compact));
        break;
    case Flavor::Markdown:
        mime->setText(QString::fromUtf8(markdown));
        mime->setData(QString::fromUtf8(kMarkdownMime), markdown);
        break;
    case Flavor::Plain:
        mime->setText(markdownToPlain(markdown));
        break;
    case Flavor::Html:
        // setData, not setHtml: QMimeData::setHtml may also attach text/plain
        // on some Qt builds, which is exactly the Word-leak exclusive copy
        // must not do.
        mime->setData(QStringLiteral("text/html"), markdownToHtml(markdown).toUtf8());
        break;
    case Flavor::Rtf:
        mime->setData(QString::fromUtf8(kRtfMime), markdownToRtf(markdown));
        break;
    }
    return mime;
}

QByteArray markdownFromMime(const QMimeData *mime, PasteMode mode)
{
    if (!mime)
        return {};
    if (mode == PasteMode::Plain) {
        if (mime->hasText())
            return mime->text().toUtf8();
        if (mime->hasHtml())
            return htmlToPlain(mime->html()).toUtf8();
        const QByteArray rtf = rtfBytes(mime);
        if (!rtf.isEmpty())
            return rtfToPlain(rtf).toUtf8();
        return {};
    }

    if (mime->hasFormat(QString::fromUtf8(kBlocksMime))) {
        const QJsonDocument jdoc =
            QJsonDocument::fromJson(mime->data(QString::fromUtf8(kBlocksMime)));
        if (jdoc.isObject() && jdoc.object().value(QStringLiteral("version")).toInt() == 1) {
            return MarkoffDocument::reconstructFlatMarkdown(
                jdoc.object().value(QStringLiteral("blocks")).toArray());
        }
    }
    if (mime->hasFormat(QString::fromUtf8(kMarkdownMime)))
        return mime->data(QString::fromUtf8(kMarkdownMime));
    if (mime->hasHtml())
        return htmlToMarkdown(mime->html());
    {
        const QByteArray rtf = rtfBytes(mime);
        if (!rtf.isEmpty())
            return rtfToMarkdown(rtf);
    }
    if (mime->hasText())
        return mime->text().toUtf8();
    return {};
}

}  // namespace Markoff::ClipboardCodec
