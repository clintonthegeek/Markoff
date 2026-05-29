// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/parser/TreeSitterParser.h>
#include <markoff/parser/SourceSpan.h>
#include <markoff/parser/Document.h>
#include <markoff/parser/PerfProbe.h>
#include "WikilinkDecomposition.h"

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

// True if the byte range [start, end) contains any character that could
// be the start of an inline-markdown construct: `*` `_` `` ` `` `[` `~`
// `=` `$` `#`. Used as a fast-path skip for `pipe_table_cell` ranges —
// every cell in a table would otherwise trigger its own inline-parser
// invocation per block reparse (24+ for a 5×4 table), making cell typing
// O(cells) in tree-sitter setup cost. Cells with no inline syntax just
// don't need to be parsed; the block walker's anonymous spans inside
// them are still filtered out by the inlineRegions overlap check.
//
// False positives are acceptable (cell gets parsed, just slower). False
// negatives would lose inline formatting in cells, so the trigger set
// covers every inline construct the inline grammar recognizes.
static bool rangeContainsInlineTrigger(const QByteArray &utf8,
                                       int startByte, int endByte)
{
    if (startByte < 0 || endByte > utf8.size() || startByte >= endByte)
        return false;
    const char *p = utf8.constData() + startByte;
    const int n = endByte - startByte;
    for (int i = 0; i < n; ++i) {
        const char c = p[i];
        switch (c) {
        case '*': case '_': case '`': case '[':
        case '~': case '=': case '$': case '#':
        case '\\':  // backslash escapes
            return true;
        default:
            break;
        }
    }
    return false;
}

static void collectInlineRanges(TSNode node, std::vector<TSRange> &ranges,
                                const QByteArray &utf8)
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

    // `pipe_table_cell` nodes hold table-cell text. The block grammar
    // treats them as leaf-ish sequences of word/whitespace/punctuation
    // tokens (no `inline` wrapper), which would leave `**bold**`,
    // `[[wikilink]]`, etc. inside cells un-highlighted. Feeding the
    // cell byte range to the inline parser produces real bold/italic/
    // code/link spans whose offsets are document-absolute (set_included_ranges
    // preserves source byte coordinates). The block-walker's anonymous
    // child spans inside this range are then filtered out by the same
    // `inlineRegions` overlap check that handles paragraph content.
    //
    // Fast-path: skip cells with no inline-trigger characters. Each emitted
    // range costs one full ts_parser_parse_string call; for a 5×4 table
    // most cells are plain text and don't need to be parsed at all.
    if (strcmp(type, "pipe_table_cell") == 0) {
        const int s = ts_node_start_byte(node);
        const int e = ts_node_end_byte(node);
        if (rangeContainsInlineTrigger(utf8, s, e)) {
            TSRange range;
            range.start_point = ts_node_start_point(node);
            range.end_point = ts_node_end_point(node);
            range.start_byte = s;
            range.end_byte = e;
            ranges.push_back(range);
        }
        return;  // don't recurse into pipe_table_cell either way
    }

    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        collectInlineRanges(ts_node_child(node, i), ranges, utf8);
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
    MARKOFF_PERF_SCOPE("parser.TreeSitterParser::parse");
    m_utf8 = text.toUtf8();
    m_byteToChar = buildByteToCharMap(m_utf8);

    // Phase 1: parse block structure
    if (m_blockTree) ts_tree_delete(m_blockTree);
    {
        MARKOFF_PERF_SCOPE("parser.block_grammar");
        m_blockTree = ts_parser_parse_string(m_blockParser, nullptr,
                                              m_utf8.constData(),
                                              static_cast<uint32_t>(m_utf8.size()));
    }
    if (!m_blockTree)
        return false;

    // Phase 2: parse inline content — ONE parse per inline region.
    // Each `inline` node in the block tree (heading text, paragraph text,
    // etc.) is parsed separately so each gets its own CST. This avoids
    // the boundary-crossing problem where set_included_ranges with multiple
    // ranges produces gap spans that span across heading/paragraph boundaries.
    for (TSTree *t : m_inlineTrees) ts_tree_delete(t);
    m_inlineTrees.clear();
    m_inlineRanges.clear();

    TSNode root = ts_tree_root_node(m_blockTree);
    std::vector<TSRange> inlineRanges;
    collectInlineRanges(root, inlineRanges, m_utf8);
    m_inlineRanges.reserve(inlineRanges.size());

    for (const TSRange &range : inlineRanges) {
        MARKOFF_PERF_SCOPE("parser.inline_grammar_one_range");
        ts_parser_set_included_ranges(m_inlineParser, &range, 1);
        TSTree *tree = ts_parser_parse_string(m_inlineParser, nullptr,
                                               m_utf8.constData(),
                                               static_cast<uint32_t>(m_utf8.size()));
        if (tree) {
            m_inlineTrees.append(tree);
            m_inlineRanges.push_back(ByteRange{range.start_byte, range.end_byte});
        }
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
    MARKOFF_PERF_SCOPE("parser.buildSpanMap");
    QList<SourceSpan> spans;

    if (!m_blockTree)
        return spans;

    // Collect inline region ranges (used for splitting and filtering)
    QList<QPair<int,int>> inlineRegions;
    {
        std::vector<TSRange> ranges;
        collectInlineRanges(ts_tree_root_node(m_blockTree), ranges, m_utf8);
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

    // Post-process 4: populate linkTarget on wikilink and standard-link spans.
    // Walk inline trees to find link container nodes, extract the structured
    // target, then stamp it onto every span within that node's byte range.
    if (!m_inlineTrees.isEmpty()) {
        // Collect (startByte, endByte, LinkTarget) tuples for each link node.
        struct LinkRange {
            int startByte;
            int endByte;
            LinkTarget target;
        };
        QList<LinkRange> linkRanges;

        // Recursive lambda via std::function to walk the inline tree.
        std::function<void(TSNode)> collectLinkRanges = [&](TSNode node) {
            const char *type = ts_node_type(node);
            int startB = static_cast<int>(ts_node_start_byte(node));
            int endB   = static_cast<int>(ts_node_end_byte(node));

            if (strcmp(type, "wiki_link") == 0) {
                // Extract the raw text, strip [[ and ]] (or ![[  and ]]),
                // and decompose the inner content.
                QString raw = QString::fromUtf8(m_utf8.mid(startB, endB - startB));
                QString inner;
                if (raw.startsWith(QStringLiteral("![[")) && raw.endsWith(QStringLiteral("]]")))
                    inner = raw.mid(3, raw.size() - 5);
                else if (raw.startsWith(QStringLiteral("[[")) && raw.endsWith(QStringLiteral("]]")))
                    inner = raw.mid(2, raw.size() - 4);
                else
                    inner = raw;  // malformed; pass through as-is
                LinkRange lr;
                lr.startByte = startB;
                lr.endByte   = endB;
                lr.target    = Markoff::Detail::decomposeWikilinkInner(QStringView{inner});
                linkRanges.append(lr);
                return;  // don't recurse into wiki_link children
            }
            if (strcmp(type, "inline_link") == 0 ||
                strcmp(type, "full_reference_link") == 0 ||
                strcmp(type, "collapsed_reference_link") == 0) {
                // Extract the link_destination child for the URL.
                LinkRange lr;
                lr.startByte = startB;
                lr.endByte   = endB;
                uint32_t count = ts_node_child_count(node);
                for (uint32_t i = 0; i < count; ++i) {
                    TSNode child = ts_node_child(node, i);
                    if (strcmp(ts_node_type(child), "link_destination") == 0) {
                        int cs = static_cast<int>(ts_node_start_byte(child));
                        int ce = static_cast<int>(ts_node_end_byte(child));
                        lr.target.url = QString::fromUtf8(m_utf8.mid(cs, ce - cs));
                        break;
                    }
                }
                linkRanges.append(lr);
                return;  // don't recurse into link children
            }

            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; ++i)
                collectLinkRanges(ts_node_child(node, i));
        };

        for (TSTree *tree : m_inlineTrees)
            collectLinkRanges(ts_tree_root_node(tree));

        // Stamp linkTarget onto each span whose byte range falls within a
        // link node's range. A span may only belong to one link node —
        // wikilinks/links are not nestable — so first-match wins.
        for (auto &s : spans) {
            if (!s.isLink && !s.isWikilink) continue;
            for (const auto &lr : linkRanges) {
                if (s.utf8Offset >= lr.startByte
                    && (s.utf8Offset + s.utf8Length) <= lr.endByte) {
                    s.linkTarget = lr.target;
                    break;
                }
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

// Pure node-to-Info extractors. Return true if the node matched the expected
// shape. Used by both the full walk and the pruned-by-changed-ranges walk.

bool extractHeadingFromNode(TSNode node, const QByteArray &utf8, HeadingInfo &out)
{
    if (strcmp(ts_node_type(node), "atx_heading") != 0) return false;
    out.level = 1;
    const int sb = static_cast<int>(ts_node_start_byte(node));
    const int eb = static_cast<int>(ts_node_end_byte(node));
    out.sourceOffset = sb;
    out.sourceLength = eb - sb;
    QString text;
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        TSNode child = ts_node_child(node, i);
        const char *ct = ts_node_type(child);
        if (strncmp(ct, "atx_h", 5) == 0 && strstr(ct, "_marker")) {
            out.level = ct[5] - '0';
        } else if (strcmp(ct, "inline") == 0) {
            int csb = static_cast<int>(ts_node_start_byte(child));
            int ceb = static_cast<int>(ts_node_end_byte(child));
            text = QString::fromUtf8(utf8.mid(csb, ceb - csb));
        }
    }
    out.text = text.trimmed();
    return true;
}

bool extractLinkFromNode(TSNode node, const QByteArray &utf8, LinkInfo &out)
{
    const char *type = ts_node_type(node);
    if (strcmp(type, "wiki_link") == 0) {
        int sb = static_cast<int>(ts_node_start_byte(node));
        int eb = static_cast<int>(ts_node_end_byte(node));
        QString raw = QString::fromUtf8(utf8.mid(sb, eb - sb));
        out.sourceOffset = sb;
        out.sourceLength = eb - sb;
        const bool isEmbed = raw.startsWith(QStringLiteral("![["));
        out.type = isEmbed ? LinkInfo::Embed : LinkInfo::Wiki;
        QString inner = isEmbed ? raw.mid(3, raw.size() - 5)
                                : raw.mid(2, raw.size() - 4);
        const int pipeIdx = inner.indexOf(QLatin1Char('|'));
        if (pipeIdx >= 0) {
            out.target = inner.left(pipeIdx);
            out.displayText = inner.mid(pipeIdx + 1);
        } else {
            out.target = inner;
            out.displayText = inner;
        }
        out.structured = Markoff::Detail::decomposeWikilinkInner(QStringView{inner});
        return true;
    }
    if (strcmp(type, "inline_link") == 0 ||
        strcmp(type, "shortcut_link") == 0 ||
        strcmp(type, "full_reference_link") == 0 ||
        strcmp(type, "collapsed_reference_link") == 0) {
        out.type = LinkInfo::Standard;
        out.sourceOffset = static_cast<int>(ts_node_start_byte(node));
        out.sourceLength = static_cast<int>(ts_node_end_byte(node)) - out.sourceOffset;
        out.target.clear();
        out.displayText.clear();
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_child(node, i);
            const char *ct = ts_node_type(child);
            int cs = static_cast<int>(ts_node_start_byte(child));
            int ce = static_cast<int>(ts_node_end_byte(child));
            if (strcmp(ct, "link_text") == 0) {
                QString raw = QString::fromUtf8(utf8.mid(cs, ce - cs));
                if (raw.startsWith(QLatin1Char('[')) && raw.endsWith(QLatin1Char(']')))
                    raw = raw.mid(1, raw.size() - 2);
                out.displayText = raw;
            } else if (strcmp(ct, "link_destination") == 0) {
                out.target = QString::fromUtf8(utf8.mid(cs, ce - cs));
            }
        }
        if (out.target.isEmpty() && !out.displayText.isEmpty())
            out.target = out.displayText;
        out.structured.url = out.target;
        return true;
    }
    if (strcmp(type, "image") == 0) {
        out.type = LinkInfo::Image;
        out.sourceOffset = static_cast<int>(ts_node_start_byte(node));
        out.sourceLength = static_cast<int>(ts_node_end_byte(node)) - out.sourceOffset;
        out.target.clear();
        out.displayText.clear();
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_child(node, i);
            const char *ct = ts_node_type(child);
            int cs = static_cast<int>(ts_node_start_byte(child));
            int ce = static_cast<int>(ts_node_end_byte(child));
            if (strcmp(ct, "image_description") == 0 || strcmp(ct, "link_text") == 0) {
                out.displayText = QString::fromUtf8(utf8.mid(cs, ce - cs));
            } else if (strcmp(ct, "link_destination") == 0) {
                out.target = QString::fromUtf8(utf8.mid(cs, ce - cs));
            }
        }
        out.structured.url = out.target;
        return true;
    }
    return false;
}

bool extractTagFromNode(TSNode node, const QByteArray &utf8, TagInfo &out)
{
    if (strcmp(ts_node_type(node), "tag") != 0) return false;
    int sb = static_cast<int>(ts_node_start_byte(node));
    int eb = static_cast<int>(ts_node_end_byte(node));
    QString raw = QString::fromUtf8(utf8.mid(sb, eb - sb));
    out.sourceOffset = sb;
    out.sourceLength = eb - sb;
    out.name = raw.startsWith(QLatin1Char('#')) ? raw.mid(1) : raw;
    return true;
}

// ---------- full walks (used by buildDocumentQueries() no-arg) -------------

void collectHeadingsForQuery(TSNode node, const QByteArray &utf8,
                             QList<HeadingInfo> &headings)
{
    HeadingInfo h;
    if (extractHeadingFromNode(node, utf8, h)) {
        headings.append(h);
        return;  // don't recurse into atx_heading children
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        collectHeadingsForQuery(ts_node_child(node, i), utf8, headings);
}

void collectInlineQueries(TSNode node, const QByteArray &utf8,
                          QList<LinkInfo> &links, QList<TagInfo> &tags)
{
    LinkInfo li;
    if (extractLinkFromNode(node, utf8, li)) {
        links.append(li);
        return;
    }
    TagInfo ti;
    if (extractTagFromNode(node, utf8, ti)) {
        tags.append(ti);
        return;
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        collectInlineQueries(ts_node_child(node, i), utf8, links, tags);
}

// ---------- top-level block walker (used by buildDocumentQueries) ----------

static int setextHeadingLevelFromNode(TSNode node)
{
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        TSNode child = ts_node_child(node, i);
        const char *type = ts_node_type(child);
        if (strcmp(type, "setext_h1_underline") == 0) return 1;
        if (strcmp(type, "setext_h2_underline") == 0) return 2;
    }
    return 1;
}

static void fillFencedCodeFields(TSNode node, const QByteArray &utf8,
                                 TopLevelBlock &b)
{
    uint32_t count = ts_node_child_count(node);
    QByteArray collectedBody;
    bool firstContent = true;
    for (uint32_t i = 0; i < count; ++i) {
        TSNode child = ts_node_child(node, i);
        const char *ct = ts_node_type(child);
        if (strcmp(ct, "info_string") == 0) {
            // Prefer the `language` named child of info_string. If
            // not present, fall back to the trimmed info_string text.
            uint32_t ic = ts_node_child_count(child);
            QString lang;
            for (uint32_t j = 0; j < ic; ++j) {
                TSNode ic_child = ts_node_child(child, j);
                if (strcmp(ts_node_type(ic_child), "language") == 0) {
                    int s = static_cast<int>(ts_node_start_byte(ic_child));
                    int e = static_cast<int>(ts_node_end_byte(ic_child));
                    lang = QString::fromUtf8(utf8.mid(s, e - s));
                    break;
                }
            }
            if (lang.isEmpty()) {
                int s = static_cast<int>(ts_node_start_byte(child));
                int e = static_cast<int>(ts_node_end_byte(child));
                lang = QString::fromUtf8(utf8.mid(s, e - s)).trimmed();
            }
            b.codeLanguage = lang;
        } else if (strcmp(ct, "code_fence_content") == 0) {
            int s = static_cast<int>(ts_node_start_byte(child));
            int e = static_cast<int>(ts_node_end_byte(child));
            if (!firstContent) collectedBody.append('\n');
            collectedBody.append(utf8.mid(s, e - s));
            firstContent = false;
        }
    }
    b.codeText = QString::fromUtf8(collectedBody);
}

static TopLevelBlock::Kind classifyTopLevelKind(const char *type)
{
    if (strcmp(type, "paragraph") == 0)                 return TopLevelBlock::Kind::Paragraph;
    if (strcmp(type, "atx_heading") == 0)               return TopLevelBlock::Kind::AtxHeading;
    if (strcmp(type, "setext_heading") == 0)            return TopLevelBlock::Kind::SetextHeading;
    if (strcmp(type, "fenced_code_block") == 0)         return TopLevelBlock::Kind::FencedCodeBlock;
    if (strcmp(type, "indented_code_block") == 0)       return TopLevelBlock::Kind::IndentedCodeBlock;
    if (strcmp(type, "block_quote") == 0)               return TopLevelBlock::Kind::BlockQuote;
    if (strcmp(type, "list_item") == 0)                 return TopLevelBlock::Kind::ListItem;
    if (strcmp(type, "thematic_break") == 0)            return TopLevelBlock::Kind::ThematicBreak;
    if (strcmp(type, "html_block") == 0)                return TopLevelBlock::Kind::HtmlBlock;
    if (strcmp(type, "link_reference_definition") == 0) return TopLevelBlock::Kind::LinkReferenceDefinition;
    if (strcmp(type, "pipe_table") == 0)                return TopLevelBlock::Kind::Table;
    return TopLevelBlock::Kind::Other;
}

// Returns true if the list node contains a blank line between items
// (i.e., the list is "loose" in CommonMark terms).
//
// The scan range is bounded by the LAST list_item child's end byte, not
// the list node's overall end byte. Tree-sitter's list node range can
// extend past the last item to include trailing blank lines that belong
// to the document separator before the next block — those blank lines are
// not evidence of loose-ness and were the cause of a tight-to-loose
// serializer round-trip drift surfaced 2026-05-21 by Corbomite dogfood
// (Mike's Obsidian-Based Writing Workflow.md became loose on save).
static bool isListLoose(TSNode listNode, const QByteArray &utf8)
{
    // Tree-sitter includes trailing blank lines in list_item byte ranges:
    //   - In a loose list, every non-last item ends with "\n\n" (the
    //     inter-item separator is absorbed into the item's range).
    //   - In a tight list followed by another block, the LAST item ends
    //     with "\n\n" (the document separator before the next block is
    //     absorbed). Non-last items end with a single "\n".
    // So scanning every byte of the list node — or every byte up to the
    // last item's end — finds the trailing blank line in the tight case
    // and misclassifies it as loose (surfaced 2026-05-21 by Corbomite
    // dogfood: tight lists became loose on save round-trip). The correct
    // signal is `\n\n` inside any NON-LAST list_item.
    const uint32_t childCount = ts_node_named_child_count(listNode);
    // Find index of the last list_item child.
    int lastItemIdx = -1;
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_named_child(listNode, i);
        if (strcmp(ts_node_type(child), "list_item") == 0)
            lastItemIdx = static_cast<int>(i);
    }
    if (lastItemIdx < 0) return false;
    // Scan non-last list_item children for `\n\n`.
    for (uint32_t i = 0; i < childCount; ++i) {
        if (static_cast<int>(i) == lastItemIdx) continue;
        TSNode child = ts_node_named_child(listNode, i);
        if (strcmp(ts_node_type(child), "list_item") != 0) continue;
        const uint32_t cs = ts_node_start_byte(child);
        const uint32_t ce = ts_node_end_byte(child);
        if (ce <= cs + 1 || static_cast<uint32_t>(utf8.size()) < ce) continue;
        for (uint32_t j = cs; j + 1 < ce; ++j) {
            if (utf8[j] == '\n' && utf8[j + 1] == '\n') return true;
        }
    }
    return false;
}

// Harvest marker style/number, checked state, and content byte range
// from a list_item node into the TopLevelBlock b.
static void harvestListItem(TSNode item, const QByteArray &utf8,
                            TopLevelBlock &b)
{
    const uint32_t n = ts_node_named_child_count(item);
    int contentStart = -1;
    int contentEnd   = -1;

    for (uint32_t i = 0; i < n; ++i) {
        TSNode child = ts_node_named_child(item, i);
        const char *ctype = ts_node_type(child);

        bool isMarker = false;

        if (strcmp(ctype, "list_marker_dot") == 0) {
            b.markerStyle = QStringLiteral("dot"); isMarker = true;
        } else if (strcmp(ctype, "list_marker_parenthesis") == 0) {
            b.markerStyle = QStringLiteral("paren"); isMarker = true;
        } else if (strcmp(ctype, "list_marker_minus") == 0) {
            b.markerStyle = QStringLiteral("minus"); isMarker = true;
        } else if (strcmp(ctype, "list_marker_plus") == 0) {
            b.markerStyle = QStringLiteral("plus"); isMarker = true;
        } else if (strcmp(ctype, "list_marker_star") == 0) {
            b.markerStyle = QStringLiteral("star"); isMarker = true;
        } else if (strcmp(ctype, "task_list_marker_unchecked") == 0) {
            b.markerStyle = QStringLiteral("task");
            b.checked = false;
            isMarker = true;
        } else if (strcmp(ctype, "task_list_marker_checked") == 0) {
            b.markerStyle = QStringLiteral("task");
            b.checked = true;
            isMarker = true;
        } else if (strcmp(ctype, "list") == 0
                || strcmp(ctype, "block_continuation") == 0) {
            // nested list or continuation — not this item's direct content
        } else {
            // Real content child (paragraph, fenced_code_block, etc.)
            const int s = static_cast<int>(ts_node_start_byte(child));
            const int e = static_cast<int>(ts_node_end_byte(child));
            if (contentStart < 0) contentStart = s;
            contentEnd = e;
        }

        // Extract markerNumber from ordered marker text
        if (isMarker && (b.markerStyle == QStringLiteral("dot")
                      || b.markerStyle == QStringLiteral("paren"))) {
            const int ms = static_cast<int>(ts_node_start_byte(child));
            const int me = static_cast<int>(ts_node_end_byte(child));
            if (ms >= 0 && me > ms && me <= static_cast<int>(utf8.size())) {
                QByteArray digits;
                for (int j = ms; j < me; ++j) {
                    const char c = utf8[j];
                    if (c >= '0' && c <= '9') digits += c;
                    else break;
                }
                if (!digits.isEmpty())
                    b.markerNumber = digits.toInt();
            }
        }
    }

    if (contentStart < 0) {
        // Empty item — point both ends at the item's end byte
        b.byteStart = b.byteEnd = static_cast<int>(ts_node_end_byte(item));
    } else {
        // Strip trailing whitespace (newlines + spaces/tabs that tree-sitter
        // may include in the paragraph node as continuation-indent bytes of
        // the following sibling item).
        while (contentEnd > contentStart
               && static_cast<uint32_t>(contentEnd - 1) < static_cast<uint32_t>(utf8.size())
               && (utf8[contentEnd - 1] == '\n'
                   || utf8[contentEnd - 1] == '\r'
                   || utf8[contentEnd - 1] == ' '
                   || utf8[contentEnd - 1] == '\t')) {
            --contentEnd;
        }
        b.byteStart = contentStart;
        b.byteEnd   = contentEnd;
    }
}

// Walk container nodes (`document`, `section`) recursively, emitting a
// TopLevelBlock for every block-level child. Sections themselves are
// containers — their child heading and following blocks are flattened
// into the linear output sequence.
// currentIndent: nesting depth from enclosing list ancestors (0 = top-level).
// currentLooseRun: true iff the nearest enclosing list was loose.
static void collectTopLevelBlocks(TSNode node, const QByteArray &utf8,
                                  QList<TopLevelBlock> &out,
                                  int currentIndent = 0,
                                  bool currentLooseRun = false,
                                  int currentBlockQuoteDepth = 0,
                                  int currentBlockQuoteRunId = 0,
                                  quint32 *nextBlockQuoteRunId = nullptr)
{
    const char *type = ts_node_type(node);

    // Containers: descend into named children, do not emit.
    if (strcmp(type, "document") == 0 || strcmp(type, "section") == 0) {
        uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            collectTopLevelBlocks(child, utf8, out, currentIndent,
                                  currentLooseRun, currentBlockQuoteDepth,
                                  currentBlockQuoteRunId, nextBlockQuoteRunId);
        }
        return;
    }

    // BlockQuote: do NOT emit a TLB for the block_quote node itself.
    // Recurse into named children carrying depth+runId so each child
    // emits its own native-kind TLB tagged with the quote context.
    // Structural marker children (`block_quote_marker` = the literal `>`,
    // `block_continuation` = blank-quoted-line separating paragraphs)
    // are skipped — they carry no content and would otherwise generate
    // spurious Kind::Other TLBs. See docs/specs/2026-05-29-blockquote-
    // multi-paragraph-split-design.md §4.
    if (strcmp(type, "block_quote") == 0) {
        const int childDepth = currentBlockQuoteDepth + 1;
        // Fresh runId for this block_quote node's direct children;
        // any nested block_quote inside will take its own.
        Q_ASSERT(nextBlockQuoteRunId != nullptr);
        const int childRunId = static_cast<int>((*nextBlockQuoteRunId)++);
        const uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            const char *childType = ts_node_type(child);
            if (strcmp(childType, "block_quote_marker") == 0
             || strcmp(childType, "block_continuation") == 0) continue;
            collectTopLevelBlocks(child, utf8, out, currentIndent,
                                  currentLooseRun, childDepth, childRunId,
                                  nextBlockQuoteRunId);
        }
        return;
    }

    // List: recurse into list_item children (one TLB per item).
    if (strcmp(type, "list") == 0) {
        const bool loose = isListLoose(node, utf8);
        const uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            collectTopLevelBlocks(child, utf8, out, currentIndent, loose,
                                  currentBlockQuoteDepth,
                                  currentBlockQuoteRunId, nextBlockQuoteRunId);
        }
        return;
    }

    // List item: emit one TLB, then recurse into any nested list children.
    if (strcmp(type, "list_item") == 0) {
        TopLevelBlock b;
        b.kind = TopLevelBlock::Kind::ListItem;
        b.indentDepth = currentIndent;
        b.looseRun = currentLooseRun;
        b.blockQuoteDepth = currentBlockQuoteDepth;
        b.blockQuoteRunId = currentBlockQuoteRunId;
        harvestListItem(node, utf8, b);
        out.append(b);
        // Recurse into nested list children at increased indent depth
        const uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            if (strcmp(ts_node_type(child), "list") == 0) {
                collectTopLevelBlocks(child, utf8, out, currentIndent + 1,
                                      currentLooseRun, currentBlockQuoteDepth,
                                      currentBlockQuoteRunId, nextBlockQuoteRunId);
            }
        }
        return;
    }

    // Skip frontmatter metadata blocks if they ever appear (the parse
    // pipeline strips frontmatter before tree-sitter sees the body, so
    // this is defensive).
    if (strcmp(type, "minus_metadata") == 0 ||
        strcmp(type, "plus_metadata") == 0) {
        return;
    }

    // Block-level node — emit one TopLevelBlock.
    TopLevelBlock b;
    b.kind      = classifyTopLevelKind(type);
    b.byteStart = static_cast<int>(ts_node_start_byte(node));
    b.byteEnd   = static_cast<int>(ts_node_end_byte(node));
    b.blockQuoteDepth = currentBlockQuoteDepth;
    b.blockQuoteRunId = currentBlockQuoteRunId;

    switch (b.kind) {
    case TopLevelBlock::Kind::AtxHeading:
        b.headingLevel = headingLevelFromNode(node);
        break;
    case TopLevelBlock::Kind::SetextHeading:
        b.headingLevel = setextHeadingLevelFromNode(node);
        break;
    case TopLevelBlock::Kind::FencedCodeBlock:
        fillFencedCodeFields(node, utf8, b);
        break;
    case TopLevelBlock::Kind::IndentedCodeBlock:
        // v1: surface raw block source. Consumers de-indent if needed.
        b.codeText = QString::fromUtf8(utf8.mid(b.byteStart, b.byteEnd - b.byteStart));
        break;
    default:
        break;
    }

    out.append(b);
}

/// Bucket spans into top-level blocks by byte-range containment, translating
/// each span's offsets from document-absolute to block-relative.
///
/// Pre: spans are in document-absolute coordinates (output of buildSpanMap()).
/// Pre: blocks[i].byteStart/byteEnd are document-absolute UTF-8 byte ranges,
///      blocks ordered ascending.
/// Post: each block.inlineSpans contains spans whose utf8Offset is in
///       [block.byteStart, block.byteEnd), with offsets translated to
///       block-relative coordinates.
void bakeInlineSpansIntoBlocks(QList<SourceSpan> spans,
                               QList<TopLevelBlock> &blocks,
                               const QByteArray &utf8)
{
    if (blocks.isEmpty() || spans.isEmpty()) return;

    const QList<int> u8ToChar = buildUtf8ToCharMap(utf8);

    std::sort(spans.begin(), spans.end(),
              [](const SourceSpan &a, const SourceSpan &b) {
                  return a.utf8Offset < b.utf8Offset;
              });

    auto charOffsetAtByte = [&](int byte) -> int {
        if (byte < 0) return 0;
        if (byte >= u8ToChar.size()) return u8ToChar.isEmpty() ? 0 : u8ToChar.last();
        return u8ToChar[byte];
    };

    qsizetype spanIdx = 0;
    for (TopLevelBlock &block : blocks) {
        const int blockCharStart = charOffsetAtByte(block.byteStart);

        while (spanIdx < spans.size()
               && spans[spanIdx].utf8Offset < block.byteStart) {
            ++spanIdx;
        }

        for (qsizetype i = spanIdx; i < spans.size(); ++i) {
            const SourceSpan &s = spans[i];
            if (s.utf8Offset >= block.byteEnd) break;

            SourceSpan rel = s;
            rel.utf8Offset = s.utf8Offset - block.byteStart;
            rel.charOffset = s.charOffset - blockCharStart;
            if (s.parentCharStart >= 0) rel.parentCharStart = s.parentCharStart - blockCharStart;
            if (s.parentCharEnd   >= 0) rel.parentCharEnd   = s.parentCharEnd   - blockCharStart;
            block.inlineSpans.append(rel);
        }
    }
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

    // Walk block tree for top-level blocks (linearised, in document order).
    // `nextBlockQuoteRunId` is the doc-scoped counter for the per-`block_quote`
    // RunId stamped on quoted children — see
    // docs/specs/2026-05-29-blockquote-multi-paragraph-split-design.md §4.
    quint32 nextBlockQuoteRunId = 1;
    collectTopLevelBlocks(blockRoot, m_utf8, result.topLevelBlocks,
                          /*currentIndent=*/0,
                          /*currentLooseRun=*/false,
                          /*currentBlockQuoteDepth=*/0,
                          /*currentBlockQuoteRunId=*/0,
                          &nextBlockQuoteRunId);

    // Walk inline trees for links and tags
    for (TSTree *tree : m_inlineTrees) {
        TSNode inlineRoot = ts_tree_root_node(tree);
        collectInlineQueries(inlineRoot, m_utf8, result.links, result.tags);
    }

    // Bake per-block inline spans (R1B). buildSpanMap is O(N) over inline
    // trees; bucketing is O(spans + blocks) on top.
    bakeInlineSpansIntoBlocks(buildSpanMap(), result.topLevelBlocks, m_utf8);

    return result;
}

// ---------------------------------------------------------------------------
// inlineSpansFor — standalone per-block inline parse entry point (Phase 10)
// ---------------------------------------------------------------------------

QList<SourceSpan> inlineSpansFor(const QByteArray &blockContent)
{
    MARKOFF_PERF_SCOPE("parser.inlineSpansFor(QByteArray)");
    // Tree-sitter's markdown block grammar only wraps a leaf marker
    // (atx_h*_marker, fenced delimiter, …) in its parent block node when the
    // construct is line-terminated. User-typed per-block content arrives here
    // without a trailing newline (LiveListModelBinding strips one if present),
    // so the parent block node is never produced, collectHeadingRanges /
    // collectBlockQuoteRanges return empty, and buildSpanMap's post-processing
    // leaves parentCharStart/parentCharEnd at -1 on the marker span — which
    // InlineHighlighter::delimiterShouldHide interprets as "always show".
    // The visible symptom: typing `# word` then pressing Enter leaves the `#`
    // marker permanently rendered in the heading delegate.
    //
    // Append a synthetic newline when missing. The extra span the parser
    // emits at offset == content length is filtered by the highlighter's
    // `relStart >= lineLen` bounds check.
    QByteArray terminated = blockContent;
    if (!terminated.endsWith('\n'))
        terminated.append('\n');
    TreeSitterParser parser;
    parser.parse(QString::fromUtf8(terminated));
    return parser.buildSpanMap();
}

} // namespace Markoff
