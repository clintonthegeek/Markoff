// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-parser/TreeSitterParser.h>
#include <markoff-parser/SourceSpan.h>
#include <markoff-parser/Document.h>

#include <tree_sitter/api.h>
#include <tree-sitter/tree-sitter-markdown.h>
#include <tree-sitter/tree-sitter-markdown-inline.h>

#include <QStringList>
#include <QPair>
#include <vector>
#include <algorithm>

namespace Markoff {

// ---------------------------------------------------------------------------
// Collect inline ranges from block tree
// ---------------------------------------------------------------------------

struct HeadingRange {
    int startByte, endByte;
    int level;
};

static void collectHeadingRanges(TSNode node, std::vector<HeadingRange> &headings)
{
    const char *type = ts_node_type(node);
    if (strcmp(type, "atx_heading") == 0) {
        HeadingRange hr;
        hr.startByte = ts_node_start_byte(node);
        hr.endByte = ts_node_end_byte(node);
        hr.level = 1;
        // Determine level from marker child
        for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
            TSNode child = ts_node_child(node, i);
            const char *ct = ts_node_type(child);
            if (strncmp(ct, "atx_h", 5) == 0 && strstr(ct, "_marker"))
                hr.level = ct[5] - '0';
        }
        headings.push_back(hr);
        return;
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); ++i)
        collectHeadingRanges(ts_node_child(node, i), headings);
}

/// Collect byte ranges of formatting parent nodes (emphasis, strong_emphasis,
/// code_span, strikethrough, highlight, obsidian_comment) from the inline tree.
/// Each entry maps a byte range to the parent's byte range.
struct ParentRange {
    int childStartByte, childEndByte;
    int parentStartByte, parentEndByte;
};

static void collectParentRanges(TSNode node, std::vector<ParentRange> &parents)
{
    const char *type = ts_node_type(node);
    bool isFormattingParent =
        strcmp(type, "emphasis") == 0 ||
        strcmp(type, "strong_emphasis") == 0 ||
        strcmp(type, "strikethrough") == 0 ||
        strcmp(type, "code_span") == 0 ||
        strcmp(type, "highlight") == 0 ||
        strcmp(type, "obsidian_comment") == 0 ||
        strcmp(type, "latex_span") == 0 ||
        strcmp(type, "wiki_link") == 0 ||
        strcmp(type, "inline_link") == 0 ||
        strcmp(type, "shortcut_link") == 0 ||
        strcmp(type, "full_reference_link") == 0 ||
        strcmp(type, "collapsed_reference_link") == 0 ||
        strcmp(type, "image") == 0;

    if (isFormattingParent) {
        int pStart = ts_node_start_byte(node);
        int pEnd = ts_node_end_byte(node);
        // All children (especially delimiters) get this parent range
        for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
            TSNode child = ts_node_child(node, i);
            ParentRange pr;
            pr.childStartByte = ts_node_start_byte(child);
            pr.childEndByte = ts_node_end_byte(child);
            pr.parentStartByte = pStart;
            pr.parentEndByte = pEnd;
            parents.push_back(pr);
        }
    }

    for (uint32_t i = 0; i < ts_node_child_count(node); ++i)
        collectParentRanges(ts_node_child(node, i), parents);
}

struct BlockQuoteRange {
    int startByte, endByte;
    int depth; // nesting level
};

static void collectBlockQuoteRanges(TSNode node, std::vector<BlockQuoteRange> &quotes, int depth = 0)
{
    const char *type = ts_node_type(node);
    if (strcmp(type, "block_quote") == 0) {
        BlockQuoteRange bq;
        bq.startByte = ts_node_start_byte(node);
        bq.endByte = ts_node_end_byte(node);
        bq.depth = depth + 1;
        quotes.push_back(bq);
        // Recurse into children with incremented depth
        for (uint32_t i = 0; i < ts_node_child_count(node); ++i)
            collectBlockQuoteRanges(ts_node_child(node, i), quotes, depth + 1);
        return;
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); ++i)
        collectBlockQuoteRanges(ts_node_child(node, i), quotes, depth);
}

static void collectInlineRanges(TSNode node, std::vector<TSRange> &ranges)
{
    const char *type = ts_node_type(node);

    // "inline" nodes contain the inline text content of paragraphs,
    // headings, etc. These are the regions the inline parser should parse.
    if (strcmp(type, "inline") == 0) {
        TSRange range;
        range.start_point = ts_node_start_point(node);
        range.end_point = ts_node_end_point(node);
        range.start_byte = ts_node_start_byte(node);
        range.end_byte = ts_node_end_byte(node);
        ranges.push_back(range);
        return; // don't recurse into inline nodes
    }

    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        collectInlineRanges(ts_node_child(node, i), ranges);
}

// ---------------------------------------------------------------------------
// UTF-8 ↔ QString char mapping
// ---------------------------------------------------------------------------

static QList<int> buildByteToCharMap(const QByteArray &utf8)
{
    QList<int> map(utf8.size() + 1, 0);
    int charIdx = 0;
    int i = 0;
    while (i < utf8.size()) {
        map[i] = charIdx;
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        int seqLen;
        if (c < 0x80)       seqLen = 1;
        else if (c < 0xE0)  seqLen = 2;
        else if (c < 0xF0)  seqLen = 3;
        else                 seqLen = 4;
        for (int j = 1; j < seqLen && (i + j) < utf8.size(); ++j)
            map[i + j] = charIdx;
        charIdx += (seqLen == 4) ? 2 : 1;
        i += seqLen;
    }
    map[utf8.size()] = charIdx;
    return map;
}

// ---------------------------------------------------------------------------
// TreeSitterParser
// ---------------------------------------------------------------------------

TreeSitterParser::TreeSitterParser()
{
    m_blockParser = ts_parser_new();
    ts_parser_set_language(m_blockParser, tree_sitter_markdown());

    m_inlineParser = ts_parser_new();
    ts_parser_set_language(m_inlineParser, tree_sitter_markdown_inline());
}

TreeSitterParser::~TreeSitterParser()
{
    if (m_blockTree) ts_tree_delete(m_blockTree);
    for (TSTree *t : m_inlineTrees) ts_tree_delete(t);
    if (m_blockParser) ts_parser_delete(m_blockParser);
    if (m_inlineParser) ts_parser_delete(m_inlineParser);
}

bool TreeSitterParser::parse(const QString &text)
{
    m_utf8 = text.toUtf8();
    m_byteToChar = buildByteToCharMap(m_utf8);

    // Phase 1: parse block structure
    if (m_blockTree) ts_tree_delete(m_blockTree);
    m_blockTree = ts_parser_parse_string(m_blockParser, nullptr,
                                          m_utf8.constData(),
                                          static_cast<uint32_t>(m_utf8.size()));
    if (!m_blockTree)
        return false;

    // Phase 2: parse inline content — ONE parse per inline region.
    // Each `inline` node in the block tree (heading text, paragraph text,
    // etc.) is parsed separately so each gets its own CST. This avoids
    // the boundary-crossing problem where set_included_ranges with multiple
    // ranges produces gap spans that span across heading/paragraph boundaries.
    for (TSTree *t : m_inlineTrees) ts_tree_delete(t);
    m_inlineTrees.clear();

    TSNode root = ts_tree_root_node(m_blockTree);
    std::vector<TSRange> inlineRanges;
    collectInlineRanges(root, inlineRanges);

    for (const TSRange &range : inlineRanges) {
        ts_parser_set_included_ranges(m_inlineParser, &range, 1);
        TSTree *tree = ts_parser_parse_string(m_inlineParser, nullptr,
                                               m_utf8.constData(),
                                               static_cast<uint32_t>(m_utf8.size()));
        if (tree)
            m_inlineTrees.append(tree);
        ts_parser_set_included_ranges(m_inlineParser, nullptr, 0);
    }

    return true;
}

int TreeSitterParser::utf8ToCharOffset(int byteOffset) const
{
    if (byteOffset < 0) return 0;
    if (byteOffset >= m_byteToChar.size()) return m_byteToChar.last();
    return m_byteToChar[byteOffset];
}

// ---------------------------------------------------------------------------
// CST → SourceSpan conversion
// ---------------------------------------------------------------------------

/// Map a tree-sitter node type name to SourceSpan formatting flags
static void applyNodeType(SourceSpan &span, const char *type)
{
    // Block-level delimiters
    if (strcmp(type, "atx_h1_marker") == 0) { span.isDelimiter = true; span.isHeading = true; span.headingLevel = 1; }
    else if (strcmp(type, "atx_h2_marker") == 0) { span.isDelimiter = true; span.isHeading = true; span.headingLevel = 2; }
    else if (strcmp(type, "atx_h3_marker") == 0) { span.isDelimiter = true; span.isHeading = true; span.headingLevel = 3; }
    else if (strcmp(type, "atx_h4_marker") == 0) { span.isDelimiter = true; span.isHeading = true; span.headingLevel = 4; }
    else if (strcmp(type, "atx_h5_marker") == 0) { span.isDelimiter = true; span.isHeading = true; span.headingLevel = 5; }
    else if (strcmp(type, "atx_h6_marker") == 0) { span.isDelimiter = true; span.isHeading = true; span.headingLevel = 6; }
    else if (strcmp(type, "block_quote_marker") == 0) { span.isDelimiter = true; span.isBlockquoteMarker = true; }
    else if (strcmp(type, "block_continuation") == 0) { span.isDelimiter = true; span.isBlockquoteMarker = true; }
    else if (strcmp(type, "fenced_code_block_delimiter") == 0) { span.isCodeBlockFence = true; span.isDelimiter = true; }
    else if (strcmp(type, "info_string") == 0) { span.isCodeBlockFence = true; span.isDelimiter = true; }
    else if (strcmp(type, "language") == 0) { span.isCodeBlockFence = true; span.isDelimiter = true; }
    else if (strcmp(type, "code_fence_content") == 0) { span.isCodeBlockContent = true; }
    else if (strcmp(type, "fenced_code_block") == 0) { span.isCodeBlockContent = true; }
    else if (strcmp(type, "thematic_break") == 0) { span.isHorizontalRule = true; }
    else if (strcmp(type, "minus_metadata") == 0 || strcmp(type, "plus_metadata") == 0) { span.isFrontmatter = true; }
    else if (strcmp(type, "list_marker_dot") == 0 || strcmp(type, "list_marker_minus") == 0 ||
             strcmp(type, "list_marker_plus") == 0 || strcmp(type, "list_marker_star") == 0 ||
             strcmp(type, "list_marker_parenthesis") == 0) { span.isListMarker = true; }

    // Inline delimiters
    else if (strcmp(type, "emphasis_delimiter") == 0) { span.isDelimiter = true; }
    else if (strcmp(type, "code_span_delimiter") == 0) { span.isDelimiter = true; span.code = true; }
    else if (strcmp(type, "strikethrough_delimiter") == 0) { span.isDelimiter = true; span.strikethrough = true; }
    else if (strcmp(type, "latex_span_delimiter") == 0 || strcmp(type, "latex_block_delimiter") == 0)
        { span.isDelimiter = true; span.math = true; }

    // Inline content (non-delimiter)
    else if (strcmp(type, "emphasis") == 0) { span.italic = true; }
    else if (strcmp(type, "strong_emphasis") == 0) { span.bold = true; }
    else if (strcmp(type, "code_span") == 0) { span.code = true; }
    else if (strcmp(type, "strikethrough") == 0) { span.strikethrough = true; }
    else if (strcmp(type, "highlight") == 0) { span.highlight = true; }
    else if (strcmp(type, "highlight_delimiter") == 0) { span.isDelimiter = true; span.highlight = true; }
    else if (strcmp(type, "obsidian_comment") == 0) { span.comment = true; }
    else if (strcmp(type, "comment_delimiter") == 0) { span.isDelimiter = true; span.comment = true; }
    // Note: tree-sitter-markdown does NOT have a separate `latex_span` node
    // type — both `$x^2$` and `$$x^2$$` are parsed as `latex_block`. The
    // mathDisplay flag is set by the caller via the source-byte inspection
    // helper below, since this node-type mapper has no access to the bytes.
    else if (strcmp(type, "latex_span") == 0) { span.math = true; }
    else if (strcmp(type, "latex_block") == 0) { span.math = true; }
    else if (strcmp(type, "wiki_link") == 0) { span.isWikilink = true; }
    // Links: tree-sitter uses inline_link, shortcut_link, full_reference_link, collapsed_reference_link
    else if (strcmp(type, "inline_link") == 0 || strcmp(type, "shortcut_link") == 0 ||
             strcmp(type, "full_reference_link") == 0 || strcmp(type, "collapsed_reference_link") == 0 ||
             strcmp(type, "uri_autolink") == 0) { span.isLink = true; }
    else if (strcmp(type, "link_text") == 0) { span.isLink = true; }  // text inside links
    else if (strcmp(type, "link_destination") == 0) { span.isLink = true; span.isDelimiter = true; }  // URL part — hide in live preview
    else if (strcmp(type, "image") == 0) { span.isImage = true; }
    else if (strcmp(type, "tag") == 0) { span.isTag = true; }

    // Task list markers — isListMarker for coloring, isTaskMarker for checkbox substitution
    else if (strcmp(type, "task_list_marker_checked") == 0) { span.isListMarker = true; span.isTaskMarker = true; }
    else if (strcmp(type, "task_list_marker_unchecked") == 0) { span.isListMarker = true; span.isTaskMarker = true; }
    else if (strcmp(type, "task_list_marker_extended") == 0) { span.isListMarker = true; span.isTaskMarker = true; }

    // Heading content inherits heading level from parent
    else if (strcmp(type, "atx_heading") == 0) { span.isHeading = true; }
    else if (strcmp(type, "setext_heading") == 0) { span.isHeading = true; }
}

/// Determine heading level from an atx_heading node
static int headingLevelFromNode(TSNode node)
{
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        TSNode child = ts_node_child(node, i);
        const char *type = ts_node_type(child);
        if (strncmp(type, "atx_h", 5) == 0 && strstr(type, "_marker"))
            return type[5] - '0';  // "atx_h2_marker" → 2
    }
    return 1;
}

void TreeSitterParser::walkNode(TSNode node, QList<SourceSpan> &spans) const
{
    const char *type = ts_node_type(node);
    uint32_t startByte = ts_node_start_byte(node);
    uint32_t endByte = ts_node_end_byte(node);
    uint32_t childCount = ts_node_child_count(node);
    bool isNamed = ts_node_is_named(node);

    // Leaf nodes (no children) → emit a span
    if (childCount == 0) {
        int spanStartByte = static_cast<int>(startByte);
        int spanEndByte = static_cast<int>(endByte);

        // Extend heading markers to include the trailing space
        // (## Heading → hide "## " not just "##")
        if (strncmp(type, "atx_h", 5) == 0 && strstr(type, "_marker")) {
            if (spanEndByte < m_utf8.size() && m_utf8[spanEndByte] == ' ')
                ++spanEndByte;
        }

        SourceSpan span;
        span.utf8Offset = spanStartByte;
        span.utf8Length = spanEndByte - spanStartByte;
        span.charOffset = utf8ToCharOffset(spanStartByte);
        span.charLength = utf8ToCharOffset(spanEndByte) - span.charOffset;

        applyNodeType(span, type);

        // latex_block can be either inline `$...$` or display `$$...$$`.
        // The grammar uses one node type for both; disambiguate from source.
        if (span.math
            && spanStartByte + 1 < m_utf8.size()
            && m_utf8[spanStartByte] == '$'
            && m_utf8[spanStartByte + 1] == '$') {
            span.mathDisplay = true;
        }

        // Inherit formatting from parent context
        // (handled by the caller propagating parent formatting)

        if (span.charLength > 0)
            spans.append(span);
        return;
    }

    // For container nodes, propagate formatting context to children
    // First, check if this node adds formatting
    SourceSpan parentFmt;
    applyNodeType(parentFmt, type);

    // Same disambiguation as the leaf path: latex_block covers both inline
    // and display math; check for `$$` at the node's start byte.
    if (parentFmt.math
        && static_cast<int>(startByte) + 1 < m_utf8.size()
        && m_utf8[startByte] == '$'
        && m_utf8[startByte + 1] == '$') {
        parentFmt.mathDisplay = true;
    }

    // Determine heading level for heading nodes
    int headingLevel = 0;
    if (parentFmt.isHeading && strcmp(type, "atx_heading") == 0)
        headingLevel = headingLevelFromNode(node);

    // Walk children and emit spans for implicit text gaps between them.
    // Tree-sitter doesn't create child nodes for text content between
    // named children (e.g., "bold text" between ** delimiters in
    // strong_emphasis). We emit content spans for those gaps.
    uint32_t prevEnd = startByte;
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(node, i);
        uint32_t childStart = ts_node_start_byte(child);

        // Gap before this child = implicit text content
        if (childStart > prevEnd) {
            SourceSpan gap;
            gap.utf8Offset = static_cast<int>(prevEnd);
            gap.utf8Length = static_cast<int>(childStart - prevEnd);
            gap.charOffset = utf8ToCharOffset(prevEnd);
            gap.charLength = utf8ToCharOffset(childStart) - gap.charOffset;
            // Inherit parent formatting
            gap.bold = parentFmt.bold;
            gap.italic = parentFmt.italic;
            gap.strikethrough = parentFmt.strikethrough;
            gap.code = parentFmt.code;
            gap.math = parentFmt.math;
            gap.mathDisplay = parentFmt.mathDisplay;
            gap.isLink = parentFmt.isLink;
            gap.isWikilink = parentFmt.isWikilink;
            gap.isImage = parentFmt.isImage;
            gap.isHeading = parentFmt.isHeading;
            gap.highlight = parentFmt.highlight;
            gap.comment = parentFmt.comment;
            gap.isTag = parentFmt.isTag;
            gap.isCodeBlockContent = parentFmt.isCodeBlockContent;
            if (headingLevel > 0) gap.headingLevel = headingLevel;
            if (gap.charLength > 0)
                spans.append(gap);
        }

        walkNode(child, spans);
        prevEnd = ts_node_end_byte(child);
    }

    // Gap after last child
    if (prevEnd < endByte) {
        SourceSpan gap;
        gap.utf8Offset = static_cast<int>(prevEnd);
        gap.utf8Length = static_cast<int>(endByte - prevEnd);
        gap.charOffset = utf8ToCharOffset(prevEnd);
        gap.charLength = utf8ToCharOffset(endByte) - gap.charOffset;
        gap.bold = parentFmt.bold;
        gap.italic = parentFmt.italic;
        gap.strikethrough = parentFmt.strikethrough;
        gap.code = parentFmt.code;
        gap.math = parentFmt.math;
        gap.mathDisplay = parentFmt.mathDisplay;
        gap.isLink = parentFmt.isLink;
        gap.isWikilink = parentFmt.isWikilink;
        gap.isImage = parentFmt.isImage;
        gap.isHeading = parentFmt.isHeading;
        gap.highlight = parentFmt.highlight;
        gap.comment = parentFmt.comment;
        gap.isTag = parentFmt.isTag;
        gap.isCodeBlockContent = parentFmt.isCodeBlockContent;
        if (headingLevel > 0) gap.headingLevel = headingLevel;
        if (gap.charLength > 0)
            spans.append(gap);
    }

    // Also propagate parent formatting to child spans (delimiter nodes
    // from children need the parent's formatting context too)
    if (parentFmt.bold || parentFmt.italic || parentFmt.strikethrough ||
        parentFmt.code || parentFmt.math || parentFmt.isLink ||
        parentFmt.isWikilink || parentFmt.isImage || parentFmt.isHeading ||
        parentFmt.highlight || parentFmt.comment || parentFmt.isCodeBlockContent) {
        for (int i = spans.size() - 1; i >= 0; --i) {
            SourceSpan &s = spans[i];
            if (s.utf8Offset < static_cast<int>(startByte))
                break;
            if (s.utf8Offset >= static_cast<int>(endByte))
                continue;

            if (parentFmt.bold) s.bold = true;
            if (parentFmt.italic) s.italic = true;
            if (parentFmt.strikethrough) s.strikethrough = true;
            if (parentFmt.code) s.code = true;
            if (parentFmt.math) s.math = true;
            if (parentFmt.mathDisplay) s.mathDisplay = true;
            if (parentFmt.isLink) s.isLink = true;
            if (parentFmt.isWikilink) s.isWikilink = true;
            if (parentFmt.isImage) s.isImage = true;
            if (parentFmt.highlight) s.highlight = true;
            if (parentFmt.comment) s.comment = true;
            if (parentFmt.isCodeBlockContent) s.isCodeBlockContent = true;
            if (parentFmt.isHeading) {
                s.isHeading = true;
                if (headingLevel > 0)
                    s.headingLevel = headingLevel;
            }
        }
    }

    // For link-type parents: mark everything except link_text as delimiter.
    // This hides brackets, parens, URLs, pipe characters in live preview.
    bool isLinkParent = parentFmt.isLink || parentFmt.isWikilink || parentFmt.isImage;
    if (isLinkParent) {
        // Collect the visible text ranges for this link.
        // - Standard links: link_text is visible, link_destination (URL) is hidden
        // - Wikilinks with display: link_text is visible, link_destination is hidden
        // - Wikilinks without display: link_destination is visible
        QList<QPair<int,int>> textRanges;
        bool hasLinkText = false;
        bool hasLinkDest = false;
        int destStart = -1, destEnd = -1;

        for (uint32_t i = 0; i < childCount; ++i) {
            TSNode child = ts_node_child(node, i);
            const char *childType = ts_node_type(child);
            if (strcmp(childType, "link_text") == 0) {
                hasLinkText = true;
                textRanges.append({static_cast<int>(ts_node_start_byte(child)),
                                   static_cast<int>(ts_node_end_byte(child))});
            } else if (strcmp(childType, "link_destination") == 0) {
                hasLinkDest = true;
                destStart = static_cast<int>(ts_node_start_byte(child));
                destEnd = static_cast<int>(ts_node_end_byte(child));
            }
        }

        // Wikilinks without explicit display text: show the destination
        if (parentFmt.isWikilink && !hasLinkText && hasLinkDest) {
            textRanges.append({destStart, destEnd});
        }

        // Mark spans as delimiter or content based on textRanges.
        // Spans within a textRange are visible content (un-mark as delimiter).
        // Spans outside textRanges are delimiters (mark as delimiter).
        for (int i = spans.size() - 1; i >= 0; --i) {
            SourceSpan &s = spans[i];
            if (s.utf8Offset < static_cast<int>(startByte))
                break;
            if (s.utf8Offset >= static_cast<int>(endByte))
                continue;

            bool isVisibleText = false;
            for (const auto &[tStart, tEnd] : textRanges) {
                if (s.utf8Offset >= tStart && (s.utf8Offset + s.utf8Length) <= tEnd) {
                    isVisibleText = true;
                    break;
                }
            }
            if (isVisibleText) {
                s.isDelimiter = false; // ensure visible, even if previously marked
            } else {
                s.isDelimiter = true;
            }
        }
    }
}

QList<SourceSpan> TreeSitterParser::buildSpanMap() const
{
    QList<SourceSpan> spans;

    if (!m_blockTree)
        return spans;

    // Collect inline region ranges (used for splitting and filtering)
    QList<QPair<int,int>> inlineRegions;
    {
        std::vector<TSRange> ranges;
        collectInlineRanges(ts_tree_root_node(m_blockTree), ranges);
        for (const auto &r : ranges)
            inlineRegions.append({static_cast<int>(r.start_byte),
                                  static_cast<int>(r.end_byte)});
    }

    // Walk the block tree for block-level structure
    TSNode blockRoot = ts_tree_root_node(m_blockTree);
    walkNode(blockRoot, spans);

    // Walk each inline tree separately for inline formatting.
    // Each tree covers one inline region (heading text, paragraph text, etc.)
    // so spans never cross block boundaries.
    if (!m_inlineTrees.isEmpty()) {
        QList<SourceSpan> inlineSpans;
        for (TSTree *tree : m_inlineTrees) {
            TSNode inlineRoot = ts_tree_root_node(tree);
            walkNode(inlineRoot, inlineSpans);
        }

        // Remove block spans that fall within inline regions (the block
        // tree has anonymous * and ` characters without formatting info).
        // Keep block_quote_marker and block_continuation spans — they exist
        // inside inline regions but carry important delimiter info.
        spans.erase(std::remove_if(spans.begin(), spans.end(),
            [&](const SourceSpan &s) {
                // Keep blockquote markers and other block-level delimiters
                if (s.isBlockquoteMarker || s.isListMarker || s.isHorizontalRule)
                    return false;
                for (const auto &[start, end] : inlineRegions) {
                    if (s.utf8Offset >= start && (s.utf8Offset + s.utf8Length) <= end)
                        return true;
                }
                return false;
            }), spans.end());

        spans.append(inlineSpans);
    }

    // Post-process 1: propagate heading level from block tree to inline spans
    {
        std::vector<HeadingRange> headings;
        collectHeadingRanges(ts_tree_root_node(m_blockTree), headings);
        for (auto &s : spans) {
            for (const auto &h : headings) {
                if (s.utf8Offset >= h.startByte && (s.utf8Offset + s.utf8Length) <= h.endByte) {
                    s.isHeading = true;
                    if (s.headingLevel == 0)
                        s.headingLevel = h.level;
                    // Heading delimiters (## markers) should show when
                    // cursor is anywhere in the heading line
                    if (s.isDelimiter && s.parentCharStart < 0) {
                        s.parentCharStart = utf8ToCharOffset(h.startByte);
                        s.parentCharEnd = utf8ToCharOffset(h.endByte);
                    }
                }
            }
        }
    }

    // Post-process 1b: propagate blockquote context from block tree
    {
        std::vector<BlockQuoteRange> quotes;
        collectBlockQuoteRanges(ts_tree_root_node(m_blockTree), quotes);
        for (auto &s : spans) {
            for (const auto &bq : quotes) {
                if (s.utf8Offset >= bq.startByte && (s.utf8Offset + s.utf8Length) <= bq.endByte) {
                    s.isBlockquote = true;
                    if (bq.depth > s.blockquoteDepth)
                        s.blockquoteDepth = bq.depth;
                    // Blockquote delimiters (> markers) should show when
                    // cursor is anywhere in the blockquote
                    if (s.isBlockquoteMarker && s.parentCharStart < 0) {
                        s.parentCharStart = utf8ToCharOffset(bq.startByte);
                        s.parentCharEnd = utf8ToCharOffset(bq.endByte);
                    }
                }
            }
        }
    }

    // Post-process 2: set parent ranges on delimiter spans so the highlighter
    // knows to show delimiters when cursor is anywhere in the parent element
    if (!m_inlineTrees.isEmpty()) {
        std::vector<ParentRange> parents;
        for (TSTree *tree : m_inlineTrees)
            collectParentRanges(ts_tree_root_node(tree), parents);
        for (auto &s : spans) {
            if (!s.isDelimiter) continue;
            for (const auto &pr : parents) {
                if (s.utf8Offset >= pr.childStartByte && (s.utf8Offset + s.utf8Length) <= pr.childEndByte) {
                    s.parentCharStart = utf8ToCharOffset(pr.parentStartByte);
                    s.parentCharEnd = utf8ToCharOffset(pr.parentEndByte);
                    break;
                }
            }
        }
    }

    // Post-process 3: detect footnote references [^N] and mark them.
    // Tree-sitter parses these as shortcut_link with link_text starting with ^.
    // We mark the [, ^, and ] as delimiters, and the number as footnoteRef.
    for (auto &s : spans) {
        if (!s.isLink || s.isDelimiter || s.isWikilink)
            continue;
        // Check if this content span's text starts with ^
        if (s.utf8Offset >= 0 && s.utf8Offset < m_utf8.size()) {
            int off = s.utf8Offset;
            if (off < m_utf8.size() && m_utf8[off] == '^') {
                // This is a footnote reference — mark the ^ as delimiter
                // Find the ^ span and the number span
                s.isFootnoteRef = true;
                s.isLink = false; // don't style as link
            }
        }
    }

    // Sort by offset
    std::sort(spans.begin(), spans.end(),
              [](const SourceSpan &a, const SourceSpan &b) {
                  return a.utf8Offset < b.utf8Offset;
              });

    return spans;
}

static void collectBlockBoundaries(TSNode node,
                                    const TreeSitterParser *parser,
                                    QList<TreeSitterParser::BlockBoundary> &boundaries)
{
    uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "pipe_table") == 0) {
            TreeSitterParser::BlockBoundary b;
            b.type = TreeSitterParser::BlockBoundary::Table;
            b.startByte = static_cast<int>(ts_node_start_byte(child));
            b.endByte = static_cast<int>(ts_node_end_byte(child));
            b.startChar = parser->utf8ToCharOffset(b.startByte);
            b.endChar = parser->utf8ToCharOffset(b.endByte);
            boundaries.append(b);
        } else if (strcmp(type, "paragraph") == 0) {
            // Check if this paragraph is a standalone image (the only
            // content is an image syntax: ![alt](url) or ![[embed]])
            int startB = static_cast<int>(ts_node_start_byte(child));
            int endB = static_cast<int>(ts_node_end_byte(child));
            const QByteArray slice = parser->utf8Source().mid(startB, endB - startB);
            const QByteArray trimmed = slice.trimmed();
            bool isImage = false;
            if (trimmed.startsWith("![[") && trimmed.endsWith("]]"))
                isImage = true;
            else if (trimmed.startsWith("![") && trimmed.contains("](") && trimmed.endsWith(")"))
                isImage = true;

            if (isImage) {
                TreeSitterParser::BlockBoundary b;
                b.type = TreeSitterParser::BlockBoundary::Image;
                b.startByte = startB;
                b.endByte = endB;
                b.startChar = parser->utf8ToCharOffset(startB);
                b.endChar = parser->utf8ToCharOffset(endB);
                boundaries.append(b);
            } else {
                collectBlockBoundaries(child, parser, boundaries);
            }
        } else {
            // Recurse into sections and other container nodes
            collectBlockBoundaries(child, parser, boundaries);
        }
    }
}

QList<TreeSitterParser::BlockBoundary> TreeSitterParser::findBlockBoundaries() const
{
    QList<BlockBoundary> boundaries;
    if (!m_blockTree)
        return boundaries;

    TSNode root = ts_tree_root_node(m_blockTree);
    collectBlockBoundaries(root, this, boundaries);

    // Sort by start position (should already be in order, but be safe)
    std::sort(boundaries.begin(), boundaries.end(),
              [](const BlockBoundary &a, const BlockBoundary &b) {
                  return a.startByte < b.startByte;
              });

    return boundaries;
}

// ---------------------------------------------------------------------------
// buildDocumentQueries — extract headings, links, tags from the CST
// ---------------------------------------------------------------------------

namespace {

/// Recursively walk block tree for atx_heading nodes
void collectHeadingsForQuery(TSNode node, const QByteArray &utf8,
                             QList<HeadingInfo> &headings)
{
    const char *type = ts_node_type(node);

    if (strcmp(type, "atx_heading") == 0) {
        HeadingInfo h;
        h.level = 1;
        h.sourceOffset = static_cast<int>(ts_node_start_byte(node));
        QString text;

        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_child(node, i);
            const char *ct = ts_node_type(child);

            if (strncmp(ct, "atx_h", 5) == 0 && strstr(ct, "_marker")) {
                h.level = ct[5] - '0';
            } else if (strcmp(ct, "inline") == 0) {
                int startByte = static_cast<int>(ts_node_start_byte(child));
                int endByte = static_cast<int>(ts_node_end_byte(child));
                text = QString::fromUtf8(utf8.mid(startByte, endByte - startByte));
            }
        }

        h.text = text.trimmed();
        headings.append(h);
        return; // don't recurse into children of atx_heading
    }

    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        collectHeadingsForQuery(ts_node_child(node, i), utf8, headings);
}

/// Recursively walk an inline tree for links, wikilinks, images, and tags
void collectInlineQueries(TSNode node, const QByteArray &utf8,
                          QList<LinkInfo> &links, QList<TagInfo> &tags)
{
    const char *type = ts_node_type(node);

    if (strcmp(type, "wiki_link") == 0) {
        // Wiki link: [[target]] or [[target|display]] or ![[embed]]
        int startByte = static_cast<int>(ts_node_start_byte(node));
        int endByte = static_cast<int>(ts_node_end_byte(node));
        QString raw = QString::fromUtf8(utf8.mid(startByte, endByte - startByte));

        LinkInfo li;
        li.sourceOffset = startByte;

        // Check for embed prefix
        bool isEmbed = raw.startsWith(QStringLiteral("![["));
        li.type = isEmbed ? LinkInfo::Embed : LinkInfo::Wiki;

        // Strip delimiters: ![[...]] or [[...]]
        QString inner;
        if (isEmbed)
            inner = raw.mid(3, raw.size() - 5); // strip "![[" and "]]"
        else
            inner = raw.mid(2, raw.size() - 4); // strip "[[" and "]]"

        // Split on | for display text
        int pipeIdx = inner.indexOf(QLatin1Char('|'));
        if (pipeIdx >= 0) {
            li.target = inner.left(pipeIdx);
            li.displayText = inner.mid(pipeIdx + 1);
        } else {
            li.target = inner;
            li.displayText = inner;
        }

        links.append(li);
        return; // don't recurse into wiki_link children
    }

    if (strcmp(type, "inline_link") == 0 ||
        strcmp(type, "shortcut_link") == 0 ||
        strcmp(type, "full_reference_link") == 0 ||
        strcmp(type, "collapsed_reference_link") == 0) {

        LinkInfo li;
        li.type = LinkInfo::Standard;
        li.sourceOffset = static_cast<int>(ts_node_start_byte(node));

        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_child(node, i);
            const char *ct = ts_node_type(child);
            int cStart = static_cast<int>(ts_node_start_byte(child));
            int cEnd = static_cast<int>(ts_node_end_byte(child));

            if (strcmp(ct, "link_text") == 0) {
                // link_text includes brackets, so strip them
                QString raw = QString::fromUtf8(utf8.mid(cStart, cEnd - cStart));
                if (raw.startsWith(QLatin1Char('[')) && raw.endsWith(QLatin1Char(']')))
                    raw = raw.mid(1, raw.size() - 2);
                li.displayText = raw;
            } else if (strcmp(ct, "link_destination") == 0) {
                li.target = QString::fromUtf8(utf8.mid(cStart, cEnd - cStart));
            }
        }

        // For shortcut_link: target = displayText (no separate destination)
        if (li.target.isEmpty() && !li.displayText.isEmpty())
            li.target = li.displayText;

        links.append(li);
        return;
    }

    if (strcmp(type, "image") == 0) {
        LinkInfo li;
        li.type = LinkInfo::Image;
        li.sourceOffset = static_cast<int>(ts_node_start_byte(node));

        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_child(node, i);
            const char *ct = ts_node_type(child);
            int cStart = static_cast<int>(ts_node_start_byte(child));
            int cEnd = static_cast<int>(ts_node_end_byte(child));

            if (strcmp(ct, "image_description") == 0 || strcmp(ct, "link_text") == 0) {
                li.displayText = QString::fromUtf8(utf8.mid(cStart, cEnd - cStart));
            } else if (strcmp(ct, "link_destination") == 0) {
                li.target = QString::fromUtf8(utf8.mid(cStart, cEnd - cStart));
            }
        }

        links.append(li);
        return;
    }

    if (strcmp(type, "tag") == 0) {
        int startByte = static_cast<int>(ts_node_start_byte(node));
        int endByte = static_cast<int>(ts_node_end_byte(node));
        QString raw = QString::fromUtf8(utf8.mid(startByte, endByte - startByte));

        TagInfo ti;
        ti.sourceOffset = startByte;
        // Strip leading #
        ti.name = raw.startsWith(QLatin1Char('#')) ? raw.mid(1) : raw;
        tags.append(ti);
        return;
    }

    // Recurse into children
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        collectInlineQueries(ts_node_child(node, i), utf8, links, tags);
}

} // anonymous namespace

DocumentQueryResult TreeSitterParser::buildDocumentQueries() const
{
    DocumentQueryResult result;

    if (!m_blockTree)
        return result;

    // Walk block tree for headings
    TSNode blockRoot = ts_tree_root_node(m_blockTree);
    collectHeadingsForQuery(blockRoot, m_utf8, result.headings);

    // Walk inline trees for links and tags
    for (TSTree *tree : m_inlineTrees) {
        TSNode inlineRoot = ts_tree_root_node(tree);
        collectInlineQueries(inlineRoot, m_utf8, result.links, result.tags);
    }

    return result;
}

} // namespace Markoff
