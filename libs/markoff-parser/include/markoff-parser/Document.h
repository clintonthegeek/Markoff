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

class Document
{
public:
    ~Document();

    static std::unique_ptr<Document> fromMarkdown(const QString &source);

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
