// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_DOCUMENT_H
#define MARKOFF_DOCUMENT_H

#include <memory>
#include <optional>
#include <utility>
#include <QString>
#include <QList>
#include <QVariant>
#include <markoff-parser/SourceSpan.h>
#include <markoff-parser/YamlValue.h>

namespace Markoff {

struct HeadingInfo {
    int level;
    QString text;
    int sourceOffset;
    int sourceLength = 0;  ///< UTF-8 bytes covered by the atx_heading node.
};

struct LinkInfo {
    enum Type { Standard, Wiki, Image, Embed };
    Type type;
    QString target;
    QString displayText;
    int sourceOffset;
    int sourceLength = 0;  ///< UTF-8 bytes covered by the link node.
};

struct TagInfo {
    QString name;
    int sourceOffset;
    int sourceLength = 0;  ///< UTF-8 bytes covered by the tag node.
};

struct FootnoteInfo {
    int number;
    QString label;
    QString content;
};

struct FootnoteRefInfo {
    QString label;        // e.g. "1", "bignote"
    int     number;       // 1-based, assigned by first-reference order;
                          // 0 if the label has no matching definition.
    int     sourceOffset; // QString char offset of '[' in body.
};

/// @deprecated Use YamlValue-based parsedFrontmatter() instead.
struct FrontmatterProperty {
    QString key;
    QVariant value;
};

/// A single top-level block in the parsed Markdown body, with its
/// kind, byte range, and a few kind-specific extras (heading level,
/// fenced-code language/body, etc.).
///
/// This is a snapshot of the tree-sitter Markdown grammar's
/// block-level children. The parser wraps document content in a
/// `document` → `section` → block hierarchy; this API flattens that
/// hierarchy back to a linear sequence of blocks in document order
/// (sections themselves are not emitted — their child heading +
/// blocks are).
///
/// Byte ranges (`byteStart`, `byteEnd`) are half-open
/// `[byteStart, byteEnd)` and are in the *body* (post-frontmatter)
/// buffer's UTF-8 byte coordinates. Frontmatter is already stripped
/// by the parse pipeline before tree-sitter sees the text. To
/// translate to full-source bytes when frontmatter is present, add
/// `frontmatterSpan()->second` to byte offsets.
///
/// `Kind` is open: `Other` is the catch-all for any block kind not
/// yet recognised by this snapshot (future grammar additions).
struct TopLevelBlock {
    enum class Kind {
        Paragraph,
        AtxHeading,                 // # / ## / ### / ... headings
        SetextHeading,              // ==== / ---- underline headings
        FencedCodeBlock,
        IndentedCodeBlock,
        BlockQuote,
        ListTight,                  // top-level list (tight, default)
        ListLoose,                  // top-level list (loose)
        ThematicBreak,              // <hr> / horizontal rule
        HtmlBlock,
        LinkReferenceDefinition,
        Table,                      // pipe_table extension
        Other                       // any unrecognized top-level node type
    };

    Kind kind = Kind::Other;

    /// UTF-8 byte range in the (post-frontmatter) body that this block
    /// occupies. Half-open: [byteStart, byteEnd).
    int byteStart = 0;
    int byteEnd = 0;

    /// Heading level (1-6). Valid for kind == AtxHeading or
    /// SetextHeading; 0 otherwise.
    int headingLevel = 0;

    /// For FencedCodeBlock: the language info string (the trimmed
    /// `language` child of the opening fence's info_string). Empty
    /// if absent. Empty for IndentedCodeBlock and other kinds.
    QString codeLanguage;

    /// For FencedCodeBlock: the body of the code block (concatenated
    /// `code_fence_content` text, fences excluded). For
    /// IndentedCodeBlock: the raw block source as-is (consumers
    /// de-indent if needed; this is a v1 simplification). Empty for
    /// other kinds.
    QString codeText;

    /// For HtmlBlock with a single recognized image syntax (raw
    /// `<img>`): src/alt/title. v1 leaves these empty; consumers
    /// should detect images inside paragraphs via the link API.
    QString imageSrc;
    QString imageAlt;
    QString imageTitle;

    /// True if the block's source has any nested children that the
    /// consumer might care about (e.g. inline code, formatting).
    /// v1 leaves this false; consumers re-walk via buildSpanMap()
    /// for fine-grained inline info.
    bool hasInlineContent = false;

    /// Inline structural spans (bold, italic, code, link, etc.) within
    /// this block's source range. Offsets are *block-relative*:
    ///   - `utf8Offset` is in [0, byteEnd - byteStart)
    ///   - `charOffset` is in [0, block-char-length)
    ///   - `parentCharStart` / `parentCharEnd`, when non-`-1`, are
    ///     also block-relative.
    /// Empty for blocks whose kind has no inline content (FencedCodeBlock,
    /// ThematicBreak, etc.). Populated by TreeSitterParser::buildDocumentQueries.
    /// Consumers (e.g. InlineFormatHighlighter) use these directly without
    /// re-parsing — see restoration spec §11 R1B.
    QList<SourceSpan> inlineSpans;
};

/// Result of walking a parsed tree to extract structured queries. Defined
/// in <markoff-parser/TreeSitterParser.h>.
struct DocumentQueryResult;

/// Frontmatter-aware extraction output. After this call:
///   - `frontmatter` holds the YAML body between the --- delimiters
///     (without delimiters or the surrounding newline).
///   - `body` is the source verbatim, with the frontmatter block removed
///     (== source.mid(frontmatterBlockEnd) when frontmatter is present,
///     == source otherwise). Footnote references and definition lines
///     remain in `body` exactly as written; the parser sees them in
///     their original textual form.
///   - `footnotes` is the canonical definition list (label → content,
///     numbered by first-reference order).
///   - `refs` records every `[^label]` reference occurrence in `body`,
///     in order of appearance, with the same numbering scheme. Refs
///     whose label has no definition carry number 0.
///
/// Used by long-lived parsers (e.g., foundation's IncrementalParseSession)
/// that want to share Document::extract()'s logic without going through the
/// fromMarkdown() one-shot path.
struct ExtractedSource {
    QString                  body;
    QString                  frontmatter;
    int                      frontmatterBlockStart = -1;
    int                      frontmatterBlockEnd   = -1;
    bool                     frontmatterEofClose   = false;
    QList<FootnoteInfo>      footnotes;  // numbered, in reference order
    QList<FootnoteRefInfo>   refs;       // ordered by sourceOffset (== first-occurrence order)
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
    /// Footnote references in `body`, in order of occurrence. Each entry
    /// carries the label, the number assigned by first-reference order,
    /// and the QString char offset of the opening `[` in body coordinates.
    /// Refs whose label has no matching definition carry number 0.
    QList<FootnoteRefInfo> footnoteRefs() const;

    /// Iterate the parsed tree's top-level blocks in document order.
    /// Each returned block carries its kind, source byte range
    /// (half-open, in body coordinates), and kind-specific extras.
    /// See `TopLevelBlock` for coordinate-space details.
    QList<TopLevelBlock> topLevelBlocks() const;

    int wordCount() const;
    int characterCount() const;

private:
    Document();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff

#endif // MARKOFF_DOCUMENT_H
