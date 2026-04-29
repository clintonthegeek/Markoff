// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_DOCUMENT_H
#define MARKOFF_DOCUMENT_H

#include <memory>
#include <optional>
#include <utility>
#include <QString>
#include <QList>
#include <QVariant>
#include <markoff-parser/YamlValue.h>

namespace Markoff {

struct HeadingInfo {
    int level;
    QString text;
    int sourceOffset;
};

struct LinkInfo {
    enum Type { Standard, Wiki, Image, Embed };
    Type type;
    QString target;
    QString displayText;
    int sourceOffset;
};

struct TagInfo {
    QString name;
    int sourceOffset;
};

struct FootnoteInfo {
    int number;
    QString label;
    QString content;
};

/// @deprecated Use YamlValue-based parsedFrontmatter() instead.
struct FrontmatterProperty {
    QString key;
    QVariant value;
};

/// Result of walking a parsed tree to extract structured queries. Defined
/// in <markoff-parser/TreeSitterParser.h>.
struct DocumentQueryResult;

/// Frontmatter-aware extraction output. After this call:
///   - `frontmatter` holds the YAML body between the --- delimiters
///     (without the delimiters or the surrounding newlines).
///   - `body` is the source verbatim with the frontmatter block removed
///     (== source.mid(frontmatterBlockEnd) when frontmatter is present,
///     == source otherwise). Footnote references and definition lines
///     remain in `body` exactly as written.
///   - `footnotes` is the canonical definition list (label → content,
///     numbered by first-reference order).
///
/// Used by long-lived parsers (e.g., foundation's IncrementalParseSession)
/// that want to share Document::extract()'s logic without going through the
/// fromMarkdown() one-shot path.
struct ExtractedSource {
    QString             body;
    QString             frontmatter;
    int                 frontmatterBlockStart = -1;
    int                 frontmatterBlockEnd = -1;
    bool                frontmatterEofClose = false;
    QList<FootnoteInfo> footnotes;  // numbered, in reference order
};

class Document
{
public:
    ~Document();

    /// One-shot parse: extracts metadata, runs a fresh TreeSitterParser,
    /// bakes a snapshot. Convenience for callers that don't need a
    /// long-lived parser.
    static std::unique_ptr<Document> fromMarkdown(const QString &source);

    /// Extract frontmatter + footnote pre-processing from a raw markdown
    /// source. Pure function over `source`; no tree-sitter involved. The
    /// returned `body` is what callers should feed to TreeSitterParser
    /// (whether for a one-shot parse() or an incremental parseIncremental()).
    static ExtractedSource extract(const QString &source);

    /// Bake a Document from pre-computed components — used by long-lived
    /// parsers (foundation's IncrementalParseSession) so that a single
    /// TreeSitterParser instance can persist across calls and feed
    /// successive Document snapshots without re-parsing.
    ///
    /// `queries` should be the result of buildDocumentQueries() called on
    /// the parser whose tree was built against `extracted.body`.
    static std::unique_ptr<Document> fromComponents(
        QString source,
        ExtractedSource extracted,
        const DocumentQueryResult &queries);

    QString sourceText() const;
    bool isEmpty() const;
    QString extractSubpath(const QString &subpath) const;

    // --- Frontmatter API ---

    /// Raw YAML text between the --- delimiters (byte-exact, no normalization).
    QString frontmatterRaw() const;

    /// @deprecated Alias for frontmatterRaw() for backward compatibility.
    QString frontmatter() const;

    /// Byte span {startByte, endByte} of the complete frontmatter block
    /// including both delimiter lines. nullopt if no frontmatter present.
    std::optional<std::pair<int,int>> frontmatterSpan() const;

    /// True if the closing --- is at EOF with no trailing newline.
    bool frontmatterHasEofClose() const;

    /// Structured frontmatter as a YamlValue tree (preserves key order, nesting,
    /// node styles). Returns empty YamlValue on missing/malformed YAML.
    YamlValue parsedFrontmatter() const;

    /// Diagnostic string for the most recent parse error, or empty.
    QString frontmatterParseError() const;

    /// Rebuild the full file content with the old frontmatter replaced by the
    /// given value. If the document had no frontmatter and value is non-empty,
    /// a new block is prepended. If value is null/empty, frontmatter is stripped.
    QString withFrontmatter(const YamlValue &value) const;

    /// @deprecated Use YamlValue-based parsedFrontmatter() instead.
    QList<FrontmatterProperty> parsedFrontmatterLegacy() const;

    // Returns the markdown content without frontmatter
    QString markdownContent() const;

    // Footnote access for the renderer
    int footnoteCount() const;
    QString footnoteContent(int number) const;  // 1-based

    // Query API
    QList<HeadingInfo> headings() const;
    QList<LinkInfo> links() const;
    QList<LinkInfo> wikiLinks() const;
    QList<TagInfo> tags() const;
    QList<FootnoteInfo> footnotes() const;
    int wordCount() const;
    int characterCount() const;

private:
    Document();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff

#endif // MARKOFF_DOCUMENT_H
