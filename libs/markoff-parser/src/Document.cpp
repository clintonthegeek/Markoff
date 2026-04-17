// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-parser/Document.h>
#include <markoff-parser/YamlValue.h>
#include <markoff-parser/TreeSitterParser.h>

#include <QStringList>
#include <QRegularExpression>

namespace Markoff {

struct Footnote {
    QString label;
    QString content;
    int number = 0;  // assigned sequentially
};

struct Document::Private {
    QString source;
    QString frontmatter;        // YAML frontmatter content (without --- delimiters)
    int frontmatterBlockStart = -1;   // byte offset of first char of opening ---
    int frontmatterBlockEnd = -1;     // byte offset past closing --- line (or EOF)
    bool eofClose = false;            // closing --- is at EOF with no trailing newline
    TreeSitterParser parser;
    QList<Footnote> footnotes;

    // Lazy-parsed
    mutable YamlValue cachedParsed;
    mutable QString cachedParseError;
    mutable bool parsedOnce = false;
};

Document::Document()
    : d(std::make_unique<Private>())
{
}

Document::~Document() = default;

std::unique_ptr<Document> Document::fromMarkdown(const QString &source)
{
    auto doc = std::unique_ptr<Document>(new Document());
    doc->d->source = source;

    // Extract frontmatter before parsing — track byte spans per Cluster A contract
    QString markdown = source;
    if (source.startsWith(QStringLiteral("---\n")) || source.startsWith(QStringLiteral("---\r\n"))) {
        int endPos = source.indexOf(QStringLiteral("\n---"), 3);
        if (endPos >= 0) {
            int fmStart = source.indexOf(QLatin1Char('\n')) + 1;
            doc->d->frontmatter = source.mid(fmStart, endPos - fmStart);
            doc->d->frontmatterBlockStart = 0;

            // Skip past the closing ---\n (or ---\r\n or ---<EOF>)
            int afterFm = endPos + 4; // "\n---"
            if (afterFm < source.size() && source[afterFm] == QLatin1Char('\r'))
                ++afterFm;
            if (afterFm < source.size() && source[afterFm] == QLatin1Char('\n'))
                ++afterFm;

            doc->d->eofClose = (afterFm >= source.size());
            doc->d->frontmatterBlockEnd = afterFm;
            markdown = source.mid(afterFm);
        } else {
            // Opening --- but no closing --- found — check for EOF-close:
            // entire rest of file after opening line is frontmatter
            int fmStart = source.indexOf(QLatin1Char('\n')) + 1;
            // Check if there's a closing --- at the very end without trailing newline
            if (source.endsWith(QStringLiteral("\n---"))) {
                doc->d->frontmatter = source.mid(fmStart, source.size() - fmStart - 4);
                doc->d->frontmatterBlockStart = 0;
                doc->d->frontmatterBlockEnd = source.size();
                doc->d->eofClose = true;
                markdown = QString();
            }
        }
    }

    // Extract footnote definitions [^label]: content
    static const QRegularExpression footnoteDef(
        QStringLiteral(R"(^\[\^([^\]]+)\]:\s*(.+)$)"),
        QRegularExpression::MultilineOption);

    QHash<QString, Footnote> footnoteMap;
    auto it = footnoteDef.globalMatch(markdown);
    while (it.hasNext()) {
        auto match = it.next();
        Footnote fn;
        fn.label = match.captured(1);
        fn.content = match.captured(2);
        footnoteMap.insert(fn.label, fn);
    }

    // Remove footnote definitions from the markdown
    if (!footnoteMap.isEmpty())
        markdown.remove(footnoteDef);

    // Number footnotes in order of first reference
    int nextNum = 1;
    static const QRegularExpression footnoteRef(QStringLiteral(R"(\[\^([^\]]+)\])"));
    auto refIt = footnoteRef.globalMatch(markdown);
    while (refIt.hasNext()) {
        auto match = refIt.next();
        const QString label = match.captured(1);
        if (footnoteMap.contains(label) && footnoteMap[label].number == 0) {
            footnoteMap[label].number = nextNum++;
        }
    }

    // Replace [^label] references with superscript numbers
    if (!footnoteMap.isEmpty()) {
        // Use a copy to iterate while modifying
        QString processed;
        int pos = 0;
        auto refIt2 = footnoteRef.globalMatch(markdown);
        while (refIt2.hasNext()) {
            auto match = refIt2.next();
            processed += markdown.mid(pos, match.capturedStart() - pos);
            const QString label = match.captured(1);
            if (footnoteMap.contains(label)) {
                int num = footnoteMap[label].number;
                processed += QStringLiteral("<sup>%1</sup>").arg(num);
            } else {
                processed += match.captured(0); // leave unresolved refs as-is
            }
            pos = match.capturedEnd();
        }
        processed += markdown.mid(pos);
        markdown = processed;
    }

    // Store sorted footnotes
    QList<Footnote> sorted;
    for (auto &fn : footnoteMap) {
        if (fn.number > 0)
            sorted.append(fn);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const Footnote &a, const Footnote &b) { return a.number < b.number; });
    doc->d->footnotes = sorted;

    // Parse with tree-sitter
    doc->d->parser.parse(markdown);

    return doc;
}

QString Document::sourceText() const
{
    return d->source;
}

bool Document::isEmpty() const
{
    return d->source.isEmpty();
}

QString Document::frontmatter() const
{
    return d->frontmatter;
}

QString Document::frontmatterRaw() const
{
    return d->frontmatter;
}

std::optional<std::pair<int,int>> Document::frontmatterSpan() const
{
    if (d->frontmatterBlockStart < 0)
        return std::nullopt;
    return std::make_pair(d->frontmatterBlockStart, d->frontmatterBlockEnd);
}

bool Document::frontmatterHasEofClose() const
{
    return d->eofClose;
}

QString Document::markdownContent() const
{
    // Return source with frontmatter stripped
    if (d->frontmatter.isEmpty())
        return d->source;

    // Find the end of frontmatter and return everything after
    int endPos = d->source.indexOf(QStringLiteral("\n---"), 3);
    if (endPos < 0)
        return d->source;
    int afterFm = endPos + 4;
    if (afterFm < d->source.size() && d->source[afterFm] == QLatin1Char('\n'))
        ++afterFm;
    return d->source.mid(afterFm);
}

int Document::footnoteCount() const
{
    return d->footnotes.size();
}

QString Document::footnoteContent(int number) const
{
    for (const auto &fn : d->footnotes) {
        if (fn.number == number)
            return fn.content;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Query API — delegates to TreeSitterParser::buildDocumentQueries()
// ---------------------------------------------------------------------------

QList<HeadingInfo> Document::headings() const
{
    return d->parser.buildDocumentQueries().headings;
}

QList<LinkInfo> Document::links() const
{
    return d->parser.buildDocumentQueries().links;
}

QList<LinkInfo> Document::wikiLinks() const
{
    QList<LinkInfo> all = links();
    QList<LinkInfo> result;
    for (const auto &l : all) {
        if (l.type == LinkInfo::Wiki || l.type == LinkInfo::Embed)
            result.append(l);
    }
    return result;
}

QList<TagInfo> Document::tags() const
{
    return d->parser.buildDocumentQueries().tags;
}

QList<FootnoteInfo> Document::footnotes() const
{
    QList<FootnoteInfo> result;
    for (const auto &fn : d->footnotes) {
        FootnoteInfo info;
        info.number = fn.number;
        info.label = fn.label;
        info.content = fn.content;
        result.append(info);
    }
    return result;
}

int Document::wordCount() const
{
    const QString content = markdownContent().trimmed();
    if (content.isEmpty())
        return 0;
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    return content.split(whitespace, Qt::SkipEmptyParts).size();
}

int Document::characterCount() const
{
    return markdownContent().length();
}

// ---------------------------------------------------------------------------
// extractSubpath
//
// Handles two formats:
//   "#^block-id"  -- paragraph containing ^block-id marker
//   "#heading"    -- section from matching heading to next same/higher heading
// ---------------------------------------------------------------------------

QString Document::extractSubpath(const QString &subpath) const
{
    if (subpath.isEmpty() || !subpath.startsWith(QLatin1Char('#')))
        return {};

    const QString fragment = subpath.mid(1); // strip leading '#'

    const QStringList lines = d->source.split(QLatin1Char('\n'));

    // -----------------------------------------------------------------------
    // Block-id mode: fragment starts with '^'
    // -----------------------------------------------------------------------
    if (fragment.startsWith(QLatin1Char('^'))) {
        const QString marker = fragment; // e.g. "^myblock"

        // Find the line containing the marker
        int markerLine = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].contains(marker)) {
                markerLine = i;
                break;
            }
        }
        if (markerLine < 0)
            return {};

        // Expand to contiguous non-empty paragraph (walk up and down)
        int start = markerLine;
        while (start > 0 && !lines[start - 1].trimmed().isEmpty())
            --start;

        int end = markerLine;
        while (end + 1 < lines.size() && !lines[end + 1].trimmed().isEmpty())
            ++end;

        // Collect lines, strip the marker token from whichever line it's on
        QStringList result;
        for (int i = start; i <= end; ++i) {
            QString line = lines[i];
            // Remove the marker (e.g. " ^myblock" including any leading space)
            line.remove(QRegularExpression(QStringLiteral("\\s*\\^[A-Za-z0-9_-]+")));
            result.append(line);
        }

        return result.join(QLatin1Char('\n')).trimmed();
    }

    // -----------------------------------------------------------------------
    // Heading mode
    // -----------------------------------------------------------------------
    // Normalise the fragment: hyphens -> spaces, lowercase
    QString needle = fragment;
    needle.replace(QLatin1Char('-'), QLatin1Char(' '));
    needle = needle.toLower().trimmed();

    // Find the heading line whose text matches the needle
    static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})\\s+(.*)$"));

    int headingLine = -1;
    int headingLevel = 0;

    for (int i = 0; i < lines.size(); ++i) {
        const QRegularExpressionMatch m = headingRe.match(lines[i]);
        if (!m.hasMatch())
            continue;

        QString headText = m.captured(2).toLower().trimmed();
        // Strip any trailing block-id marker (e.g. "My Heading ^abc")
        headText.remove(QRegularExpression(QStringLiteral("\\s*\\^[A-Za-z0-9_-]+$")));

        if (headText == needle) {
            headingLine = i;
            headingLevel = static_cast<int>(m.captured(1).size());
            break;
        }
    }

    if (headingLine < 0)
        return {};

    // Collect lines from the heading line until the next heading of same or
    // higher level (lower or equal '#' count), or EOF
    QStringList result;
    result.append(lines[headingLine]);

    for (int i = headingLine + 1; i < lines.size(); ++i) {
        const QRegularExpressionMatch m = headingRe.match(lines[i]);
        if (m.hasMatch()) {
            const int lvl = static_cast<int>(m.captured(1).size());
            if (lvl <= headingLevel)
                break;
        }
        result.append(lines[i]);
    }

    // Trim trailing blank lines
    while (!result.isEmpty() && result.last().trimmed().isEmpty())
        result.removeLast();

    return result.join(QLatin1Char('\n'));
}

YamlValue Document::parsedFrontmatter() const
{
    if (d->parsedOnce)
        return d->cachedParsed;

    d->parsedOnce = true;
    const QString raw = d->frontmatter;
    if (raw.isEmpty())
        return {};

    d->cachedParsed = YamlValue::parse(raw, &d->cachedParseError);
    return d->cachedParsed;
}

QString Document::frontmatterParseError() const
{
    // Trigger lazy parse if not done yet
    parsedFrontmatter();
    return d->cachedParseError;
}

QString Document::withFrontmatter(const YamlValue &value) const
{
    QString body = markdownContent();

    if (value.isNull()) {
        // Strip frontmatter entirely
        return body;
    }

    QString yamlBody = value.stringify();
    QString newFm = QStringLiteral("---\n") + yamlBody + QStringLiteral("\n---\n");
    return newFm + body;
}

// --- Legacy compatibility (deprecated) ---

static QVariant yamlValueToVariant(const YamlValue &val)
{
    switch (val.kind()) {
    case YamlValue::Kind::Null:
        return {};
    case YamlValue::Kind::Bool:
        return val.asBool();
    case YamlValue::Kind::Int:
        return static_cast<int>(val.asInt());
    case YamlValue::Kind::Double:
        return val.asDouble();
    case YamlValue::Kind::String:
        return val.asString();
    case YamlValue::Kind::Seq:
        return val.asStringList();
    case YamlValue::Kind::Map: {
        QVariantMap map;
        val.forEach([&](const QString &k, const YamlValue &v) {
            map.insert(k, yamlValueToVariant(v));
        });
        return map;
    }
    }
    return {};
}

static QVariant normalizeListValue(const QString &key, const QVariant &value)
{
    if ((key == QStringLiteral("tags") || key == QStringLiteral("aliases"))
        && value.typeId() == QMetaType::QString) {
        QStringList parts;
        for (const auto &s : value.toString().split(QLatin1Char(','),
                                                      Qt::SkipEmptyParts))
            parts.append(s.trimmed());
        return parts;
    }
    return value;
}

QList<FrontmatterProperty> Document::parsedFrontmatterLegacy() const
{
    YamlValue fm = parsedFrontmatter();
    if (!fm.isMap())
        return {};

    QList<FrontmatterProperty> result;
    fm.forEach([&](const QString &key, const YamlValue &val) {
        FrontmatterProperty prop;
        prop.key = key;
        prop.value = normalizeListValue(key, yamlValueToVariant(val));
        result.append(prop);
    });
    return result;
}

} // namespace Markoff
