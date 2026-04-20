// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.
//
// Phase 3b rewrite:
//   - the Phase 3a HTML-escape + `inlineToHtml` helper was replaced with a
//     CharacterStyle-driven SpanRenderer (see src/SpanRenderer.{h,cpp})
//     which walks the inline-span tree and emits QTextCharFormats directly
//     via `cursor.insertText(...)`, mirroring Markoff's
//     MarkdownTextItem.cpp pattern.
//   - five new content-type handlers land here: tables (GFM),
//     inline images, wiki-link spans (carry target in char-format user
//     data), math (inline via QTextObject + display as a QGraphicsPixmapItem)
//     and Mermaid fenced blocks (via `mmdr` + QGraphicsSvgItem).

#include "corbomite/readingview/SectionLayout.h"

#include "MermaidRenderer.h"
#include "ReadingMathObject.h"
#include "SpanRenderer.h"
#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/ReadingViewConstants.h"
#include "corbomite/readingview/VaultResourceProvider.h"
#include "corbomite/readingview/styling/StyleManager.h"

#include <QAbstractTextDocumentLayout>
#include <QBrush>
#include <QColor>
#include <QCryptographicHash>
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsItemGroup>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QtSvgWidgets/QGraphicsSvgItem>
#include <QGraphicsTextItem>
#include <QImage>
#include <QPen>
#include <QGraphicsPolygonItem>
#include <QPixmap>
#include <QPolygonF>
#include <QRegularExpression>
#include <QStringList>
#include <QSvgRenderer>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLength>
#include <QTextList>
#include <QTextListFormat>
#include <QTextOption>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableFormat>
#include <QUrl>

#include <jkqtmathtext/jkqtmathtext.h>

namespace Corbomite::ReadingView {

namespace {

// -- Block-level breakdown --------------------------------------------------

enum class BlockKind {
    Heading,
    Paragraph,
    CodeBlock,
    Mermaid,
    UnorderedList,
    OrderedList,
    HorizontalRule,
    Blockquote,
    Table,
    DisplayMath,
};

struct CodeBlockInfo {
    QString language;
    QString content;
};

struct ListItem {
    int indent = 0;
    QString text;
    bool ordered = false;
};

struct TableInfo {
    QStringList headers;
    QList<Qt::Alignment> alignments;
    QList<QStringList> rows;
};

struct BlockRecord {
    BlockKind kind;
    int headingLevel = 0;
    QString text;
    CodeBlockInfo code;
    QList<ListItem> listItems;
    TableInfo table;
};

// Parse a pipe-table alignment cell. Returns Qt::AlignLeft by default.
Qt::Alignment parseTableAlignment(const QString &cell)
{
    const QString t = cell.trimmed();
    if (t.length() < 3) return Qt::AlignLeft;
    const bool leftColon  = t.startsWith(QLatin1Char(':'));
    const bool rightColon = t.endsWith(QLatin1Char(':'));
    if (leftColon && rightColon) return Qt::AlignHCenter;
    if (rightColon)              return Qt::AlignRight;
    return Qt::AlignLeft;
}

QStringList splitTableRow(const QString &line)
{
    QString trimmed = line.trimmed();
    if (trimmed.startsWith(QLatin1Char('|')))
        trimmed = trimmed.mid(1);
    if (trimmed.endsWith(QLatin1Char('|')))
        trimmed.chop(1);
    QStringList out;
    QString current;
    for (int i = 0; i < trimmed.length(); ++i) {
        const QChar c = trimmed.at(i);
        if (c == QLatin1Char('\\') && i + 1 < trimmed.length()
            && trimmed.at(i + 1) == QLatin1Char('|')) {
            current += QLatin1Char('|');
            ++i;
            continue;
        }
        if (c == QLatin1Char('|')) {
            out << current.trimmed();
            current.clear();
        } else {
            current += c;
        }
    }
    out << current.trimmed();
    return out;
}

bool isTableSeparatorRow(const QString &line)
{
    QString trimmed = line.trimmed();
    if (!trimmed.contains(QLatin1Char('|'))) return false;
    // Each cell must be dashes and optional colons/spaces only.
    const QStringList cells = splitTableRow(trimmed);
    if (cells.isEmpty()) return false;
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*:?-{3,}:?\s*$)"));
    for (const QString &c : cells) {
        if (!re.match(c).hasMatch()) return false;
    }
    return true;
}

QList<BlockRecord> parseBlocks(const QString &md)
{
    QList<BlockRecord> out;
    const QStringList lines = md.split(QLatin1Char('\n'));

    static const QRegularExpression headingRe(
        QStringLiteral(R"(^ {0,3}(#{1,6})\s+(.*?)\s*#*\s*$)"));
    static const QRegularExpression hrRe(
        QStringLiteral(R"(^ {0,3}([-*_])(?:\s*\1){2,}\s*$)"));
    static const QRegularExpression fenceOpenRe(
        QStringLiteral(R"(^ {0,3}(`{3,}|~{3,})\s*([A-Za-z0-9_+\-.]*)\s*$)"));
    static const QRegularExpression ulRe(
        QStringLiteral(R"(^(\s*)[-*+]\s+(.*)$)"));
    static const QRegularExpression olRe(
        QStringLiteral(R"(^(\s*)\d+[.)]\s+(.*)$)"));
    static const QRegularExpression bqRe(
        QStringLiteral(R"(^ {0,3}>\s?(.*)$)"));

    int i = 0;
    const int n = lines.size();
    while (i < n) {
        const QString &ln = lines.at(i);

        if (ln.trimmed().isEmpty()) { ++i; continue; }

        // Fenced code / mermaid
        auto fm = fenceOpenRe.match(ln);
        if (fm.hasMatch()) {
            const QString fence = fm.captured(1);
            const QString lang = fm.captured(2);
            CodeBlockInfo cb;
            cb.language = lang;
            QStringList body;
            ++i;
            while (i < n) {
                const QString &cl = lines.at(i);
                const QString ts = cl.trimmed();
                if (ts.startsWith(fence.left(1))
                    && ts.count(fence.at(0)) >= fence.size()
                    && ts.length() == ts.count(fence.at(0))) {
                    ++i;
                    break;
                }
                body << cl;
                ++i;
            }
            cb.content = body.join(QLatin1Char('\n'));
            BlockRecord br;
            br.kind = (lang == QLatin1String("mermaid"))
                        ? BlockKind::Mermaid
                        : BlockKind::CodeBlock;
            br.code = cb;
            out.push_back(br);
            continue;
        }

        // Heading
        auto hm = headingRe.match(ln);
        if (hm.hasMatch()) {
            BlockRecord br;
            br.kind = BlockKind::Heading;
            br.headingLevel = hm.captured(1).size();
            br.text = hm.captured(2);
            out.push_back(br);
            ++i;
            continue;
        }

        // Horizontal rule
        if (hrRe.match(ln).hasMatch()) {
            BlockRecord br;
            br.kind = BlockKind::HorizontalRule;
            out.push_back(br);
            ++i;
            continue;
        }

        // Display math: $$...$$ possibly multi-line, if the block starts with $$.
        {
            const QString t = ln.trimmed();
            if (t.startsWith(QStringLiteral("$$"))) {
                QString collected;
                QString first = t.mid(2);
                bool closedSameLine = false;
                if (first.endsWith(QStringLiteral("$$")) && first.length() >= 2) {
                    collected = first.left(first.length() - 2);
                    closedSameLine = true;
                }
                if (closedSameLine) {
                    BlockRecord br;
                    br.kind = BlockKind::DisplayMath;
                    br.text = collected;
                    out.push_back(br);
                    ++i;
                    continue;
                } else {
                    // Multi-line: gather until closing $$.
                    collected = first;
                    int k = i + 1;
                    bool found = false;
                    while (k < n) {
                        const QString &cl = lines.at(k);
                        const QString ts = cl.trimmed();
                        if (ts.endsWith(QStringLiteral("$$"))) {
                            collected += QLatin1Char('\n')
                                       + ts.left(ts.length() - 2);
                            found = true;
                            ++k;
                            break;
                        }
                        collected += QLatin1Char('\n') + cl;
                        ++k;
                    }
                    if (found) {
                        BlockRecord br;
                        br.kind = BlockKind::DisplayMath;
                        br.text = collected;
                        out.push_back(br);
                        i = k;
                        continue;
                    }
                    // Unclosed — fall through to paragraph.
                }
            }
        }

        // Blockquote
        auto bqm = bqRe.match(ln);
        if (bqm.hasMatch()) {
            QStringList bq;
            bq << bqm.captured(1);
            ++i;
            while (i < n) {
                auto m2 = bqRe.match(lines.at(i));
                if (!m2.hasMatch()) break;
                bq << m2.captured(1);
                ++i;
            }
            BlockRecord br;
            br.kind = BlockKind::Blockquote;
            br.text = bq.join(QLatin1Char('\n'));
            out.push_back(br);
            continue;
        }

        // List
        auto ulm = ulRe.match(ln);
        auto olm = olRe.match(ln);
        if (ulm.hasMatch() || olm.hasMatch()) {
            const bool ordered = olm.hasMatch();
            BlockRecord br;
            br.kind = ordered ? BlockKind::OrderedList
                              : BlockKind::UnorderedList;
            while (i < n) {
                auto a = ulRe.match(lines.at(i));
                auto b = olRe.match(lines.at(i));
                if (!a.hasMatch() && !b.hasMatch()) break;
                const QRegularExpressionMatch &m =
                    a.hasMatch() ? a : b;
                ListItem li;
                li.indent = m.captured(1).size() / 2;
                li.text = m.captured(2);
                li.ordered = b.hasMatch();
                br.listItems.push_back(li);
                ++i;
            }
            out.push_back(br);
            continue;
        }

        // Table detection — a pipe-table header line followed by a
        // separator `|---|:---:|---:|` line.
        if (ln.contains(QLatin1Char('|')) && i + 1 < n
            && isTableSeparatorRow(lines.at(i + 1))) {
            TableInfo ti;
            ti.headers = splitTableRow(ln);
            const QStringList sepCells = splitTableRow(lines.at(i + 1));
            for (const QString &c : sepCells)
                ti.alignments.push_back(parseTableAlignment(c));
            i += 2;
            while (i < n) {
                const QString &row = lines.at(i);
                if (row.trimmed().isEmpty() || !row.contains(QLatin1Char('|')))
                    break;
                ti.rows.push_back(splitTableRow(row));
                ++i;
            }
            BlockRecord br;
            br.kind = BlockKind::Table;
            br.table = ti;
            out.push_back(br);
            continue;
        }

        // Paragraph
        QStringList para;
        while (i < n) {
            const QString &pl = lines.at(i);
            if (pl.trimmed().isEmpty()) break;
            if (headingRe.match(pl).hasMatch()) break;
            if (hrRe.match(pl).hasMatch()) break;
            if (fenceOpenRe.match(pl).hasMatch()) break;
            if (bqRe.match(pl).hasMatch()) break;
            if (ulRe.match(pl).hasMatch() || olRe.match(pl).hasMatch()) break;
            if (pl.trimmed().startsWith(QStringLiteral("$$"))) break;
            if (pl.contains(QLatin1Char('|')) && i + 1 < n
                && isTableSeparatorRow(lines.at(i + 1))) break;
            para << pl;
            ++i;
        }
        if (!para.isEmpty()) {
            BlockRecord br;
            br.kind = BlockKind::Paragraph;
            br.text = para.join(QLatin1Char('\n'));
            out.push_back(br);
        }
    }
    return out;
}

// -- Graphics-item factories ------------------------------------------------

QTextCharFormat defaultCharFormat(const QFont &font, const QColor &color)
{
    QTextCharFormat cf;
    cf.setFont(font);
    cf.setForeground(QBrush(color));
    return cf;
}

/// Build a `QGraphicsTextItem` whose document has been populated via the
/// SpanRenderer. If the renderer emitted inline-math or inline-image runs
/// they're attached to the document via this helper's post-pass.
/// Caller supplies ownership for the created `ReadingMathObject` handler
/// (if any) by pushing it into `objectSink`.
QGraphicsTextItem *buildRichTextItem(const QString &markdown,
                                     const QFont &font,
                                     const QColor &color,
                                     qreal width,
                                     StyleManager &styles,
                                     VaultResourceProvider *provider,
                                     QList<QObject *> &objectSink,
                                     bool *hasInlineObjects = nullptr)
{
    auto *item = new QGraphicsTextItem;
    item->setFont(font);
    item->setDefaultTextColor(color);
    item->setTextWidth(width);
    QTextDocument *doc = item->document();
    doc->setDefaultFont(font);

    // Register the math-object handler on the per-item document so inline
    // replacement characters render as rasterised LaTeX.
    auto *mathHandler = new ReadingMathObject(item);
    doc->documentLayout()->registerHandler(ReadingMathObject::TypeId, mathHandler);
    objectSink.push_back(mathHandler);

    QTextCursor cursor(doc);
    QTextCharFormat base = defaultCharFormat(font, color);

    SpanRenderer sr;
    const bool anyObjects = sr.renderInline(cursor, markdown, base, styles);
    if (hasInlineObjects) *hasInlineObjects = anyObjects;

    if (anyObjects) {
        // Walk the document and re-stamp the math-object formats so the
        // document layout treats them as drawable handlers. SpanRenderer
        // set `objectType == MathObjectMarker` (0x100010) — map to our
        // registered TypeId.
        for (QTextBlock blk = doc->begin(); blk.isValid(); blk = blk.next()) {
            for (auto it = blk.begin(); it != blk.end(); ++it) {
                if (!it.fragment().isValid()) continue;
                QTextCharFormat cf = it.fragment().charFormat();
                if (cf.objectType() == SpanRenderer::MathObjectMarker) {
                    QTextCharFormat nf = cf;
                    nf.setObjectType(ReadingMathObject::TypeId);
                    const QString latex =
                        cf.property(SpanRenderer::InlineMathSourceProperty)
                          .toString();
                    const bool display =
                        cf.property(QTextFormat::UserProperty).toBool();
                    nf.setProperty(ReadingMathObject::SourceProperty, latex);
                    nf.setProperty(ReadingMathObject::DisplayProperty, display);
                    // Re-apply by selecting the fragment and setting the
                    // new format.
                    const int start = it.fragment().position();
                    const int len = it.fragment().length();
                    QTextCursor fc(doc);
                    fc.setPosition(start);
                    fc.setPosition(start + len, QTextCursor::KeepAnchor);
                    fc.setCharFormat(nf);
                }
                else if (cf.objectType() == SpanRenderer::ImageObjectMarker) {
                    // Swap the object-ref char for an inline image. We do it
                    // here because SpanRenderer doesn't know about the
                    // provider. We convert the fragment to HTML-image-style
                    // QTextImageFormat, which QTextDocument natively renders.
                    const QString path =
                        cf.property(SpanRenderer::InlineImagePathProperty)
                          .toString();
                    const QString alt =
                        cf.property(SpanRenderer::InlineImageAltProperty)
                          .toString();
                    QPixmap px;
                    bool loaded = false;
                    if (provider) {
                        QByteArray bytes = provider->loadImageBytes(path);
                        if (!bytes.isEmpty()) {
                            QImage img;
                            loaded = img.loadFromData(bytes);
                            if (loaded) px = QPixmap::fromImage(img);
                        }
                    }
                    const int start = it.fragment().position();
                    const int len = it.fragment().length();
                    QTextCursor fc(doc);
                    fc.setPosition(start);
                    fc.setPosition(start + len, QTextCursor::KeepAnchor);
                    if (loaded && !px.isNull()) {
                        const QString resUrl = QStringLiteral("rv-img:") + path;
                        doc->addResource(QTextDocument::ImageResource,
                                         QUrl(resUrl), QVariant(px));
                        QTextImageFormat img;
                        img.setName(resUrl);
                        img.setWidth(px.width());
                        img.setHeight(px.height());
                        fc.removeSelectedText();
                        fc.insertImage(img);
                    } else {
                        fc.removeSelectedText();
                        QTextCharFormat fallback = base;
                        styles.resolvedCharacterStyle(
                            QStringLiteral("ImageCaption"))
                                .applyFormat(fallback);
                        fc.insertText(QStringLiteral("[") + alt
                                           + QStringLiteral("]"),
                                       fallback);
                    }
                }
            }
        }
    }

    item->setTextWidth(width);
    return item;
}

} // namespace

SectionLayout::SectionLayout() = default;

SectionLayout::~SectionLayout()
{
    qDeleteAll(m_highlighters);
    m_highlighters.clear();
    // m_textObjects are parented to their text items; QGraphicsItemGroup
    // ownership handles them via Qt parent-child semantics.
    m_textObjects.clear();
}

QGraphicsItemGroup *SectionLayout::layoutSection(ReadingSection &section,
                                                 const QString &sectionMarkdown,
                                                 const Context &ctx)
{
    if (!ctx.styles)
        return nullptr;

    auto *group = new QGraphicsItemGroup;
    qreal y = 0.0;
    const qreal contentWidth = ctx.contentWidth;

    QByteArray shapeSrc;

    const QList<BlockRecord> blocks = parseBlocks(sectionMarkdown);

    for (const BlockRecord &br : blocks) {
        QGraphicsItem *child = nullptr;
        qreal spaceAfter = 0.0;

        switch (br.kind) {
        case BlockKind::Heading: {
            const QString styleName =
                QStringLiteral("Heading%1").arg(br.headingLevel);
            ParagraphStyle ps = ctx.styles->resolvedParagraphStyle(styleName);
            QFont font;
            if (ps.hasFontFamily()) font.setFamily(ps.fontFamily());
            if (ps.hasFontSize()) font.setPointSizeF(ps.fontSize());
            if (ps.hasFontWeight()) font.setWeight(ps.fontWeight());
            if (ps.hasFontItalic()) font.setItalic(ps.fontItalic());
            const QColor color = ps.hasForeground() ? ps.foreground()
                                                    : QColor(Qt::black);
            auto *t = buildRichTextItem(br.text, font, color, contentWidth,
                                         *ctx.styles, ctx.vaultProvider,
                                         m_textObjects);
            child = t;
            spaceAfter = ps.hasSpaceAfter() ? ps.spaceAfter() : 10.0;
            shapeSrc += "H|";
            shapeSrc += QByteArray::number(br.headingLevel);
            shapeSrc += "|";
            shapeSrc += br.text.toUtf8();
            shapeSrc += ";";
            break;
        }

        case BlockKind::Paragraph: {
            ParagraphStyle ps =
                ctx.styles->resolvedParagraphStyle(QStringLiteral("Body"));
            QFont font;
            if (ps.hasFontFamily()) font.setFamily(ps.fontFamily());
            if (ps.hasFontSize()) font.setPointSizeF(ps.fontSize());
            const QColor color = ps.hasForeground() ? ps.foreground()
                                                    : QColor(Qt::black);
            QString joined = br.text;
            joined.replace(QLatin1Char('\n'), QLatin1Char(' '));
            auto *t = buildRichTextItem(joined, font, color, contentWidth,
                                         *ctx.styles, ctx.vaultProvider,
                                         m_textObjects);
            child = t;
            spaceAfter = ps.hasSpaceAfter() ? ps.spaceAfter() : 8.0;
            shapeSrc += "P|";
            shapeSrc += br.text.toUtf8();
            shapeSrc += ";";
            break;
        }

        case BlockKind::CodeBlock: {
            ParagraphStyle ps =
                ctx.styles->resolvedParagraphStyle(QStringLiteral("CodeBlock"));
            QFont font;
            font.setFamily(ps.hasFontFamily() ? ps.fontFamily()
                                              : QStringLiteral("monospace"));
            font.setPointSizeF(ps.hasFontSize() ? ps.fontSize() : 13.0);
            font.setFixedPitch(true);
            font.setStyleHint(QFont::Monospace);
            const QColor color = ps.hasForeground() ? ps.foreground()
                                                    : QColor(Qt::black);

            auto *t = new QGraphicsTextItem;
            t->setFont(font);
            t->setDefaultTextColor(color);
            t->setTextWidth(contentWidth);
            t->setPlainText(br.code.content);

            QTextDocument *doc = t->document();
            if (!br.code.language.isEmpty()) {
                QTextBlock blk = doc->begin();
                while (blk.isValid()) {
                    QTextCursor bc(blk);
                    QTextBlockFormat bf = blk.blockFormat();
                    bf.setProperty(QTextFormat::BlockCodeLanguage,
                                   br.code.language);
                    bc.setBlockFormat(bf);
                    blk = blk.next();
                }
            }

            auto *hl = new CodeBlockHighlighter(ctx.theme);
            hl->highlight(doc);
            m_highlighters.push_back(hl);

            if (ps.hasBackground()) {
                QRectF bbox = t->boundingRect();
                auto *bg = new QGraphicsRectItem(0, 0, contentWidth,
                                                  bbox.height());
                bg->setBrush(ps.background());
                bg->setPen(Qt::NoPen);
                bg->setZValue(-1);
                group->addToGroup(bg);
                bg->setPos(0, y);
            }

            child = t;
            spaceAfter = ps.hasSpaceAfter() ? ps.spaceAfter() : 10.0;
            shapeSrc += "C|";
            shapeSrc += br.code.language.toUtf8();
            shapeSrc += "|";
            shapeSrc += br.code.content.toUtf8();
            shapeSrc += ";";
            break;
        }

        case BlockKind::Mermaid: {
            const QByteArray svg = MermaidRenderer::renderSvg(br.code.content);
            if (svg.isEmpty()) {
                // Fallback: treat as a plain code block.
                BlockRecord fb;
                fb.kind = BlockKind::CodeBlock;
                fb.code = br.code;
                fb.code.language = QStringLiteral("mermaid");
                // Re-dispatch.
                auto *t = new QGraphicsTextItem;
                QFont f;
                f.setFamily(QStringLiteral("monospace"));
                t->setFont(f);
                t->setTextWidth(contentWidth);
                t->setPlainText(br.code.content);
                child = t;
                spaceAfter = 10.0;
            } else {
                auto *svgItem = new QGraphicsSvgItem;
                auto *renderer = new QSvgRenderer(svg, svgItem);
                svgItem->setSharedRenderer(renderer);
                // Scale down if too wide.
                QSizeF def = renderer->defaultSize();
                if (def.isEmpty()) def = QSizeF(600, 400);
                qreal scale = 1.0;
                if (def.width() > contentWidth && def.width() > 0)
                    scale = contentWidth / def.width();
                svgItem->setScale(scale);
                child = svgItem;
                spaceAfter = 12.0;
            }
            shapeSrc += "MER|";
            shapeSrc += br.code.content.toUtf8();
            shapeSrc += ";";
            break;
        }

        case BlockKind::UnorderedList:
        case BlockKind::OrderedList: {
            ParagraphStyle ps =
                ctx.styles->resolvedParagraphStyle(QStringLiteral("ListItem"));
            QFont font;
            if (ps.hasFontFamily()) font.setFamily(ps.fontFamily());
            if (ps.hasFontSize()) font.setPointSizeF(ps.fontSize());
            else font.setPointSizeF(14);
            const QColor color = ps.hasForeground() ? ps.foreground()
                                                    : QColor(Qt::black);
            const bool ordered = (br.kind == BlockKind::OrderedList);

            // Use a QTextDocument with QTextList; populate items via
            // SpanRenderer so inline formatting is applied per item.
            auto *t = new QGraphicsTextItem;
            t->setFont(font);
            t->setDefaultTextColor(color);
            t->setTextWidth(contentWidth);
            QTextDocument *doc = t->document();
            doc->setDefaultFont(font);

            auto *mathHandler = new ReadingMathObject(t);
            doc->documentLayout()->registerHandler(
                ReadingMathObject::TypeId, mathHandler);
            m_textObjects.push_back(mathHandler);

            QTextCursor cursor(doc);
            QTextListFormat lf;
            lf.setStyle(ordered ? QTextListFormat::ListDecimal
                                : QTextListFormat::ListDisc);
            cursor.createList(lf);
            QTextCharFormat base = defaultCharFormat(font, color);
            for (int k = 0; k < br.listItems.size(); ++k) {
                const auto &li = br.listItems.at(k);
                if (k > 0) {
                    cursor.insertBlock();
                }
                SpanRenderer sr;
                sr.renderInline(cursor, li.text, base, *ctx.styles);
            }
            child = t;
            spaceAfter = ps.hasSpaceAfter() ? ps.spaceAfter() : 8.0;
            shapeSrc += ordered ? "OL|" : "UL|";
            for (const auto &it : br.listItems) {
                shapeSrc += QByteArray::number(it.indent);
                shapeSrc += ":";
                shapeSrc += it.text.toUtf8();
                shapeSrc += "|";
            }
            shapeSrc += ";";
            break;
        }

        case BlockKind::HorizontalRule: {
            auto *line = new QGraphicsLineItem(0, 0, contentWidth, 0);
            QPen pen(ctx.theme == Theme::Dark ? QColor(80, 80, 80)
                                              : QColor(200, 200, 200));
            pen.setWidth(1);
            line->setPen(pen);
            child = line;
            spaceAfter = 12.0;
            shapeSrc += "HR;";
            break;
        }

        case BlockKind::Blockquote: {
            ParagraphStyle ps =
                ctx.styles->resolvedParagraphStyle(QStringLiteral("Blockquote"));
            QFont font;
            if (ps.hasFontFamily()) font.setFamily(ps.fontFamily());
            if (ps.hasFontSize()) font.setPointSizeF(ps.fontSize());
            else font.setPointSizeF(14);
            if (ps.hasFontItalic()) font.setItalic(ps.fontItalic());
            const QColor color = ps.hasForeground() ? ps.foreground()
                                                    : QColor(Qt::darkGray);

            auto *t = buildRichTextItem(br.text, font, color,
                                         contentWidth - 24,
                                         *ctx.styles, ctx.vaultProvider,
                                         m_textObjects);

            auto *bar = new QGraphicsRectItem(0, 0, 4, 1);
            bar->setBrush(QColor(ctx.theme == Theme::Dark
                                     ? QColor(120, 120, 120)
                                     : QColor(160, 160, 160)));
            bar->setPen(Qt::NoPen);

            if (ps.hasBackground()) {
                auto *bg = new QGraphicsRectItem(0, 0, contentWidth, 1);
                bg->setBrush(ps.background());
                bg->setPen(Qt::NoPen);
                bg->setZValue(-1);
                group->addToGroup(bg);
                const qreal h = t->boundingRect().height();
                bg->setRect(0, 0, contentWidth, h);
                bg->setPos(0, y);
            }

            const qreal h = t->boundingRect().height();
            bar->setRect(0, 0, 4, h);
            bar->setPos(0, y);
            group->addToGroup(bar);

            t->setPos(16, y);
            group->addToGroup(t);
            y += h + (ps.hasSpaceAfter() ? ps.spaceAfter() : 10.0);

            shapeSrc += "BQ|";
            shapeSrc += br.text.toUtf8();
            shapeSrc += ";";
            continue;
        }

        case BlockKind::Table: {
            // Render as a grid of QGraphicsTextItem cells inside a sub-
            // group. Each cell is its own text item so per-cell alignment
            // and word-wrap work naturally.
            auto *tableGroup = new QGraphicsItemGroup;

            const int cols = qMax(br.table.headers.size(),
                                  br.table.alignments.size());
            if (cols > 0) {
                const qreal cellWidth = contentWidth / cols;
                const qreal cellPadX = 8.0;
                const qreal cellPadY = 4.0;

                ParagraphStyle bodyPs =
                    ctx.styles->resolvedParagraphStyle(QStringLiteral("Body"));
                QFont cellFont;
                if (bodyPs.hasFontFamily()) cellFont.setFamily(bodyPs.fontFamily());
                if (bodyPs.hasFontSize()) cellFont.setPointSizeF(bodyPs.fontSize());
                else cellFont.setPointSizeF(14);
                const QColor cellColor = bodyPs.hasForeground()
                    ? bodyPs.foreground() : QColor(Qt::black);

                const QColor headerBg = (ctx.theme == Theme::Dark)
                    ? QColor(50, 50, 55)
                    : QColor(240, 242, 245);
                const QColor borderColor = (ctx.theme == Theme::Dark)
                    ? QColor(70, 70, 75)
                    : QColor(210, 213, 218);

                auto alignForCol = [&](int c) -> Qt::Alignment {
                    if (c >= 0 && c < br.table.alignments.size())
                        return br.table.alignments.at(c);
                    return Qt::AlignLeft;
                };

                auto mkCell = [&](const QString &text, int col, bool isHeader,
                                   qreal yRow) -> qreal {
                    QFont f = cellFont;
                    if (isHeader) f.setBold(true);
                    auto *cellItem = buildRichTextItem(
                        text, f, cellColor, cellWidth - 2 * cellPadX,
                        *ctx.styles, ctx.vaultProvider, m_textObjects);
                    auto *tdoc = cellItem->document();
                    QTextOption opt = tdoc->defaultTextOption();
                    Qt::Alignment a = alignForCol(col);
                    opt.setAlignment(a);
                    tdoc->setDefaultTextOption(opt);
                    // Re-apply alignment to existing blocks.
                    for (QTextBlock blk = tdoc->begin(); blk.isValid();
                         blk = blk.next()) {
                        QTextCursor bc(blk);
                        QTextBlockFormat bf = blk.blockFormat();
                        bf.setAlignment(a);
                        bc.setBlockFormat(bf);
                    }
                    const qreal cellH = cellItem->boundingRect().height()
                                       + 2 * cellPadY;
                    auto *bg = new QGraphicsRectItem(
                        col * cellWidth, yRow, cellWidth, cellH);
                    bg->setBrush(isHeader ? QBrush(headerBg)
                                           : QBrush(Qt::NoBrush));
                    bg->setPen(QPen(borderColor, 1.0));
                    tableGroup->addToGroup(bg);
                    cellItem->setPos(col * cellWidth + cellPadX,
                                      yRow + cellPadY);
                    tableGroup->addToGroup(cellItem);
                    return cellH;
                };

                qreal yRow = 0.0;
                qreal headerH = 0.0;
                for (int c = 0; c < cols; ++c) {
                    const QString text = (c < br.table.headers.size())
                        ? br.table.headers.at(c) : QString();
                    headerH = qMax(headerH, mkCell(text, c, true, yRow));
                }
                yRow += headerH;

                for (const auto &row : br.table.rows) {
                    qreal rowH = 0.0;
                    for (int c = 0; c < cols; ++c) {
                        const QString text = (c < row.size())
                            ? row.at(c) : QString();
                        rowH = qMax(rowH, mkCell(text, c, false, yRow));
                    }
                    yRow += rowH;
                }
            }

            child = tableGroup;
            spaceAfter = 10.0;
            shapeSrc += "TBL|";
            shapeSrc += QByteArray::number(br.table.headers.size());
            shapeSrc += "|";
            for (const auto &h : br.table.headers) {
                shapeSrc += h.toUtf8();
                shapeSrc += "^";
            }
            shapeSrc += "|";
            for (const auto &row : br.table.rows) {
                for (const auto &cell : row) {
                    shapeSrc += cell.toUtf8();
                    shapeSrc += "^";
                }
                shapeSrc += "#";
            }
            shapeSrc += ";";
            break;
        }

        case BlockKind::DisplayMath: {
            // Render $$...$$ as a standalone pixmap pixmap-item. We use
            // JKQTMathText directly rather than going through the inline
            // ReadingMathObject, since display math is a block.
            JKQTMathText mt;
            mt.useXITS();
            ParagraphStyle bodyPs =
                ctx.styles->resolvedParagraphStyle(QStringLiteral("Body"));
            const qreal sz = bodyPs.hasFontSize() ? bodyPs.fontSize() : 14.0;
            mt.setFontSize(sz * 1.25);
            const QString wrapped = QStringLiteral("$") + br.text
                                   + QStringLiteral("$");
            QImage img;
            if (mt.parse(wrapped)) {
                img = mt.drawIntoImage(false, Qt::transparent, 2, 2.0, 96);
                if (!img.isNull()) img.setDevicePixelRatio(2.0);
            }
            if (img.isNull()) {
                auto *fallback = new QGraphicsTextItem;
                QFont mono;
                mono.setFamily(QStringLiteral("monospace"));
                mono.setPointSizeF(sz);
                fallback->setFont(mono);
                fallback->setDefaultTextColor(QColor(Qt::darkRed));
                fallback->setPlainText(QStringLiteral("$$") + br.text
                                        + QStringLiteral("$$"));
                fallback->setTextWidth(contentWidth);
                child = fallback;
            } else {
                auto *pix = new QGraphicsPixmapItem(QPixmap::fromImage(img));
                // Center horizontally.
                const qreal w = img.width() / img.devicePixelRatio();
                pix->setPos((contentWidth - w) / 2.0, 0);
                child = pix;
            }
            spaceAfter = 12.0;
            shapeSrc += "DM|";
            shapeSrc += br.text.toUtf8();
            shapeSrc += ";";
            break;
        }
        }

        if (child) {
            // If child is already a group holding pre-positioned items
            // (the table branch), add it as-is but still compute its bbox
            // for y-advance.
            child->setPos(0, y);
            group->addToGroup(child);
            QRectF bb = child->boundingRect();
            if (bb.height() <= 0) bb.setHeight(1);
            y += bb.height() + spaceAfter;
        }
    }

    // Phase 4: the recycle key (section.renderedShape()) is now populated
    // pre-layout by ReadingPipeline from the source-byte slice. We retain
    // `shapeSrc` local state here so the post-layout digest can be
    // recomputed for diagnostics, but we only *write* it if the pipeline
    // left the section with an empty shape — callers that instantiate a
    // section directly (existing tests) rely on `renderedShape()` being
    // non-empty after layout.
    if (section.renderedShape().isEmpty()) {
        const QByteArray digest =
            QCryptographicHash::hash(shapeSrc, QCryptographicHash::Sha256);
        section.setRenderedShape(digest);
    }

    // Phase 6: heading sections get a clickable gutter arrow (▶ / ▼). The
    // arrow is a small triangle QGraphicsPolygonItem tagged with the
    // section index under `kFoldArrowSectionIdxProperty` so click-hit
    // testing in ReadingView::mousePressEvent can look up the owning
    // section without needing back-pointers from items to sections.
    if (ctx.headingCollapsedIndicator && section.headingLevel() > 0
        && ctx.sectionIndex >= 0) {
        QPolygonF poly;
        const qreal w = 8.0;
        const qreal h = 10.0;
        if (section.headingCollapsed()) {
            // ▶ pointing right.
            poly << QPointF(0, 0) << QPointF(w, h / 2.0)
                 << QPointF(0, h);
        } else {
            // ▼ pointing down.
            poly << QPointF(0, 0) << QPointF(w, 0)
                 << QPointF(w / 2.0, h);
        }
        auto *arrow = new QGraphicsPolygonItem(poly);
        arrow->setBrush(QColor(120, 120, 120));
        arrow->setPen(Qt::NoPen);
        // Position in the left gutter, vertically aligned to the first
        // heading block (y = 0 relative to the group; the heading is the
        // first block laid out). A -16 px x offset places the arrow off
        // the left of the content area.
        arrow->setPos(-16.0, 8.0);
        arrow->setData(kFoldArrowSectionIdxProperty, ctx.sectionIndex);
        group->addToGroup(arrow);
    }

    section.setGraphicsItem(group);
    return group;
}

} // namespace Corbomite::ReadingView
