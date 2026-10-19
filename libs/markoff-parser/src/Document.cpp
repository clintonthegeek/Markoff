// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/parser/Document.h>
#include <markoff/parser/YamlValue.h>
#include <markoff/parser/TreeSitterParser.h>

#include <QStringList>
#include <QRegularExpression>

#include <mutex>

namespace Markoff {

struct Document::Private {
    QString source;
    QString frontmatter;        // YAML frontmatter content (without --- delimiters)
    int frontmatterBlockStart = -1;   // byte offset of first char of opening ---
    int frontmatterBlockEnd = -1;     // byte offset past closing --- line (or EOF)
    bool eofClose = false;            // closing --- is at EOF with no trailing newline
    QList<FootnoteInfo> footnotes;
    QList<FootnoteRefInfo> refs;

    DocumentQueryResult queries;

    // Lazy-parsed frontmatter. Guarded so concurrent readers cannot race, and
    // the value is written before the once flag publishes it (the previous
    // flag-then-fill order could hand a second reader an empty YamlValue).
    mutable std::once_flag parseOnce;
    mutable YamlValue cachedParsed;
    mutable QString cachedParseError;
};

Document::Document()
    : d(std::make_unique<Private>())
{
}

Document::~Document() = default;

namespace {

/// Index of the first newline-delimited `---` that is a complete closing
/// fence: the three dashes must be followed by '\n', "\r\n", or end of
/// string. A plain prefix search also accepted `----`, `--- ` and `---foo`,
/// which silently cut the body short on round-trip. Returns -1 when no
/// occurrence qualifies.
int findClosingFence(const QString &source, int from)
{
    const int size = source.size();
    int pos = from;
    while (pos >= 0 && pos < size) {
        pos = source.indexOf(QStringLiteral("\n---"), pos);
        if (pos < 0)
            return -1;
        const int after = pos + 4;
        const QChar next = (after < size) ? source.at(after) : QChar();
        if (after >= size || next == QLatin1Char('\n') || next == QLatin1Char('\r'))
            return pos;
        pos = after;  // `----`, `--- `, `---foo`: keep looking
    }
    return -1;
}

} // namespace

ExtractedSource Document::extract(const QString &source)
{
    ExtractedSource out;
    QString markdown = source;

    // Extract frontmatter — track byte spans per Cluster A contract.
    if (source.startsWith(QStringLiteral("---\n")) || source.startsWith(QStringLiteral("---\r\n"))) {
        const int endPos = findClosingFence(source, 3);
        if (endPos >= 0) {
            int fmStart = source.indexOf(QLatin1Char('\n')) + 1;
            // A closing fence immediately after the opening one means no
            // content: endPos - fmStart is negative there, and QString::mid
            // treats a negative length as "rest of the string".
            out.frontmatter = source.mid(fmStart, qMax(0, endPos - fmStart));
            out.frontmatterBlockStart = 0;

            int afterFm = endPos + 4;  // "\n---"
            if (afterFm < source.size() && source[afterFm] == QLatin1Char('\r'))
                ++afterFm;
            if (afterFm < source.size() && source[afterFm] == QLatin1Char('\n'))
                ++afterFm;

            // A closing fence at end of file with no trailing newline is
            // reached through this same path; there is no separate branch.
            out.frontmatterEofClose = (afterFm >= source.size());
            out.frontmatterBlockEnd = afterFm;
            markdown = source.mid(afterFm);
        }
    }

    // Extract footnote definitions [^label]: content
    static const QRegularExpression footnoteDef(
        QStringLiteral(R"(^\[\^([^\]]+)\]:\s*(.+)$)"),
        QRegularExpression::MultilineOption);

    QHash<QString, FootnoteInfo> footnoteMap;
    auto defIt = footnoteDef.globalMatch(markdown);
    while (defIt.hasNext()) {
        auto match = defIt.next();
        FootnoteInfo fn;
        fn.label = match.captured(1);
        fn.content = match.captured(2);
        fn.number = 0;
        footnoteMap.insert(fn.label, fn);
    }

    // Single-pass scan over `[^label]` occurrences. Skip definition prefixes
    // (a `[^label]` immediately followed by `:`). Number on first sighting,
    // record every occurrence into `out.refs`.
    int nextNum = 1;
    static const QRegularExpression footnoteRef(QStringLiteral(R"(\[\^([^\]]+)\])"));
    auto refIt = footnoteRef.globalMatch(markdown);
    while (refIt.hasNext()) {
        auto match = refIt.next();
        const int afterClose = match.capturedEnd();
        // Skip definition prefixes: `]` followed (after optional whitespace
        // that is not a newline) by `:`.
        int p = afterClose;
        while (p < markdown.size()) {
            const QChar c = markdown[p];
            if (c == QLatin1Char(' ') || c == QLatin1Char('\t')) {
                ++p;
                continue;
            }
            break;
        }
        if (p < markdown.size() && markdown[p] == QLatin1Char(':'))
            continue;

        const QString label = match.captured(1);
        FootnoteRefInfo ref;
        ref.label = label;
        ref.sourceOffset = match.capturedStart();
        ref.number = 0;
        if (footnoteMap.contains(label)) {
            if (footnoteMap[label].number == 0)
                footnoteMap[label].number = nextNum++;
            ref.number = footnoteMap[label].number;
        }
        out.refs.append(ref);
    }

    // Sort referenced footnotes by assigned number.
    for (auto &fn : footnoteMap) {
        if (fn.number > 0)
            out.footnotes.append(fn);
    }
    std::sort(out.footnotes.begin(), out.footnotes.end(),
              [](const FootnoteInfo &a, const FootnoteInfo &b) {
                  return a.number < b.number;
              });

    out.body = std::move(markdown);
    return out;
}

std::unique_ptr<Document> Document::fromMarkdown(const QString &source)
{
    ExtractedSource extracted = extract(source);
    TreeSitterParser parser;
    parser.parse(extracted.body);
    DocumentQueryResult q = parser.buildDocumentQueries();
    auto doc = std::unique_ptr<Document>(new Document());
    doc->d->source                = source;
    doc->d->frontmatter           = std::move(extracted.frontmatter);
    doc->d->frontmatterBlockStart = extracted.frontmatterBlockStart;
    doc->d->frontmatterBlockEnd   = extracted.frontmatterBlockEnd;
    doc->d->eofClose              = extracted.frontmatterEofClose;
    doc->d->footnotes             = std::move(extracted.footnotes);
    doc->d->refs                  = std::move(extracted.refs);
    doc->d->queries               = q;
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
    // Return source with frontmatter stripped. The extraction pass already
    // recorded the exact span; re-searching here is what let a `----` line
    // (accepted by the old prefix search) cut the body at the wrong place.
    // The predicate is the recorded span, not the raw text: an empty
    // frontmatter block ("---\n---\nBody") is present but has no text.
    if (d->frontmatterBlockStart < 0 || d->frontmatterBlockEnd < 0
        || d->frontmatterBlockEnd > d->source.size())
        return d->source;

    return d->source.mid(d->frontmatterBlockEnd);
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
// Query API — reads baked results from d->queries, populated at construction.
// ---------------------------------------------------------------------------

QList<HeadingInfo> Document::headings() const
{
    return d->queries.headings;
}

QList<LinkInfo> Document::links() const
{
    return d->queries.links;
}

QList<LinkInfo> Document::wikiLinks() const
{
    QList<LinkInfo> result;
    for (const auto &l : d->queries.links) {
        if (l.type == LinkInfo::Wiki || l.type == LinkInfo::Embed)
            result.append(l);
    }
    return result;
}

QList<TagInfo> Document::tags() const
{
    return d->queries.tags;
}

QList<FootnoteInfo> Document::footnotes() const
{
    return d->footnotes;
}

QList<FootnoteRefInfo> Document::footnoteRefs() const
{
    return d->refs;
}

QList<TopLevelBlock> Document::topLevelBlocks() const
{
    return d->queries.topLevelBlocks;
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
    std::call_once(d->parseOnce, [this] {
        const QString raw = d->frontmatter;
        if (raw.isEmpty())
            return;
        d->cachedParsed = YamlValue::parse(raw, &d->cachedParseError);
    });
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

} // namespace Markoff
