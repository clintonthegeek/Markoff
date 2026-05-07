// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-parser/TreeSitterParser.h>
#include <markoff-parser/SourceSpan.h>
#include <markoff-parser/Document.h>

#include <tree_sitter/api.h>
#include <tree-sitter/tree-sitter-markdown.h>
#include <tree-sitter/tree-sitter-markdown-inline.h>

#include <QStringList>
#include <QPair>
#include <chrono>
#include <optional>
#include <unordered_map>
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
    m_lastInlineReuseCount = 0;
    m_lastBlockChangedBytes = -1;
    m_lastParseBlockNs  = 0;
    m_lastParseInlineNs = 0;
    m_lastChangedRanges.clear();
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
    m_inlineRanges.clear();

    TSNode root = ts_tree_root_node(m_blockTree);
    std::vector<TSRange> inlineRanges;
    collectInlineRanges(root, inlineRanges);
    m_inlineRanges.reserve(inlineRanges.size());

    for (const TSRange &range : inlineRanges) {
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

bool TreeSitterParser::parseIncremental(const QList<ByteEdit> &edits,
                                        const QByteArray &newUtf8)
{
    m_lastParseBlockNs  = 0;
    m_lastParseInlineNs = 0;
    m_lastChangedRanges.clear();

    // No prior tree → full parse of the new buffer. Callers don't need
    // a first-parse branch.
    if (!m_blockTree) {
        return parse(QString::fromUtf8(newUtf8));
    }

    using Clock = std::chrono::steady_clock;
    auto toNs = [](Clock::duration d) {
        return static_cast<quint64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
    };
    const auto tStart = Clock::now();

    // Snapshot inline state from before any edit, so we can reuse trees
    // whose post-edit byte range is unchanged. m_inlineRanges is cached
    // from the previous parse / parseIncremental and mirrors m_inlineTrees,
    // so we don't need to re-walk the old block tree here.
    const std::vector<ByteRange> oldInlineRanges = m_inlineRanges;
    QList<TSTree *> oldInlineTrees = m_inlineTrees;
    m_inlineTrees.clear();
    const auto tInlinePrepDone = Clock::now();

    QList<ByteEdit> sortedEdits;

    if (edits.isEmpty()) {
        // No edits but caller still asked for incremental: nothing to do
        // for the block tree (it's already valid). Refresh the buffer-of-
        // record (caller may have replaced it with an identical-looking
        // newUtf8) and fall through to the inline reuse pass — every
        // inline region's range will match exactly, so all trees reuse.
        m_lastBlockChangedBytes = 0;
        m_utf8       = newUtf8;
        m_byteToChar = buildByteToCharMap(m_utf8);
    } else {
        // Sort by ascending oldStart, then apply ts_tree_edit in DESCENDING
        // order. Each ts_tree_edit shifts node positions to a new frame; by
        // editing right-to-left, each edit's old-frame offsets remain valid
        // against the tree's then-current state (because we haven't touched
        // anything to the right yet).
        sortedEdits = edits;
        std::sort(sortedEdits.begin(), sortedEdits.end(),
                  [](const ByteEdit &a, const ByteEdit &b) {
                      return a.oldStart < b.oldStart;
                  });

        for (auto it = sortedEdits.rbegin(); it != sortedEdits.rend(); ++it) {
            const ByteEdit &e = *it;
            TSInputEdit ed{};
            ed.start_byte    = e.oldStart;
            ed.old_end_byte  = e.oldEnd;
            ed.new_end_byte  = e.oldStart + e.newLength;
            // Points are unused downstream (we read trees by byte only),
            // so leave them zero. tree-sitter uses points for some
            // decisions but byte offsets dominate; this is the documented
            // safe shortcut for byte-only consumers.
            ts_tree_edit(m_blockTree, &ed);
            // Apply the same edit to all old inline trees so that their
            // node byte positions are updated to the new frame. Trees that
            // are later reused will then emit spans with correct offsets.
            for (TSTree *inlineTree : oldInlineTrees)
                ts_tree_edit(inlineTree, &ed);
        }

        // Now reparse the block tree against the new buffer, supplying the
        // edited prior tree. tree-sitter reuses unchanged subtrees.
        m_utf8       = newUtf8;
        m_byteToChar = buildByteToCharMap(m_utf8);

        TSTree *newTree = ts_parser_parse_string(m_blockParser, m_blockTree,
                                                  m_utf8.constData(),
                                                  static_cast<uint32_t>(m_utf8.size()));
        if (!newTree) {
            // Block reparse failed. Restore the old inline-tree handles so
            // the parser stays in a valid state, and report failure.
            m_inlineTrees = oldInlineTrees;
            return false;
        }
        {
            uint32_t nRanges = 0;
            TSRange *ranges = ts_tree_get_changed_ranges(m_blockTree, newTree, &nRanges);
            quint64 totalBytes = 0;
            m_lastChangedRanges.reserve(nRanges + sortedEdits.size());
            for (uint32_t i = 0; i < nRanges; ++i) {
                totalBytes += static_cast<quint64>(ranges[i].end_byte) - ranges[i].start_byte;
                m_lastChangedRanges.push_back(
                    ByteRange{ ranges[i].start_byte, ranges[i].end_byte });
            }
            if (ranges) free(ranges);
            m_lastBlockChangedBytes = static_cast<int>(qMin<quint64>(totalBytes, INT_MAX));

            // Tree-sitter reports STRUCTURAL changes on the block tree only:
            // an inline-only edit (typing inside a heading or paragraph)
            // often produces no block-tree changed-range. The edits
            // themselves are the authoritative lower bound on changed
            // bytes; project each edit into the new frame and union it
            // into m_lastChangedRanges so the queries layer sees inline
            // edits too.
            qint64 delta = 0;
            for (const ByteEdit &e : sortedEdits) {
                const quint32 newStart = static_cast<quint32>(qint64(e.oldStart) + delta);
                const quint32 newEnd   = newStart + e.newLength;
                m_lastChangedRanges.push_back(ByteRange{ newStart, newEnd });
                delta += qint64(e.newLength) - qint64(e.oldEnd - e.oldStart);
            }
        }
        ts_tree_delete(m_blockTree);
        m_blockTree = newTree;
    }

    const auto tBlockEnd = Clock::now();

    // Phase 2: reuse inline trees for regions whose post-edit byte range
    // is unchanged. Shift each old range through the sorted edits to
    // derive its post-edit byte range, or mark it invalid if any edit
    // overlaps. Index valid shifted ranges by packed (start,end) so new
    // ranges look up matches in O(1) rather than scanning O(N).
    TSNode root = ts_tree_root_node(m_blockTree);
    std::vector<TSRange> newInlineRanges;
    collectInlineRanges(root, newInlineRanges);

    m_lastInlineReuseCount = 0;

    // Build a hash map: packed (startByte, endByte) → oldIdx, for valid
    // shifted ranges only. Since old ranges were disjoint pre-edit and
    // edits shift each contiguous span by the same delta, the post-edit
    // ranges are also pairwise distinct — so a plain unordered_map works
    // (no collisions among valid entries).
    std::unordered_map<quint64, int> shiftedByKey;
    shiftedByKey.reserve(oldInlineRanges.size());
    for (size_t i = 0; i < oldInlineRanges.size(); ++i) {
        const quint32 s = oldInlineRanges[i].startByte;
        const quint32 e = oldInlineRanges[i].endByte;
        bool valid = true;
        qint64 delta = 0;
        for (const ByteEdit &ed : sortedEdits) {
            if (ed.oldEnd <= s) {
                delta += static_cast<qint64>(ed.newLength)
                       - static_cast<qint64>(ed.oldEnd - ed.oldStart);
            } else if (ed.oldStart >= e) {
                // edit lies entirely past this range — no impact.
            } else {
                valid = false;
                break;
            }
        }
        if (!valid) continue;
        const quint32 ns = static_cast<quint32>(static_cast<qint64>(s) + delta);
        const quint32 ne = static_cast<quint32>(static_cast<qint64>(e) + delta);
        const quint64 key = (static_cast<quint64>(ns) << 32) | ne;
        shiftedByKey.emplace(key, static_cast<int>(i));
    }

    std::vector<bool> consumed(oldInlineTrees.size(), false);
    m_inlineRanges.clear();
    m_inlineRanges.reserve(newInlineRanges.size());

    for (const TSRange &nr : newInlineRanges) {
        const quint64 key = (static_cast<quint64>(nr.start_byte) << 32) | nr.end_byte;
        auto it = shiftedByKey.find(key);
        int reuseIdx = -1;
        if (it != shiftedByKey.end() && !consumed[it->second]) {
            reuseIdx = it->second;
        }
        if (reuseIdx >= 0) {
            m_inlineTrees.append(oldInlineTrees[reuseIdx]);
            consumed[reuseIdx] = true;
            ++m_lastInlineReuseCount;
        } else {
            ts_parser_set_included_ranges(m_inlineParser, &nr, 1);
            TSTree *tree = ts_parser_parse_string(m_inlineParser, nullptr,
                                                   m_utf8.constData(),
                                                   static_cast<uint32_t>(m_utf8.size()));
            if (tree)
                m_inlineTrees.append(tree);
            ts_parser_set_included_ranges(m_inlineParser, nullptr, 0);
        }
        m_inlineRanges.push_back(ByteRange{nr.start_byte, nr.end_byte});
    }

    for (size_t i = 0; i < oldInlineTrees.size(); ++i) {
        if (!consumed[i])
            ts_tree_delete(oldInlineTrees[i]);
    }

    const auto tEnd = Clock::now();
    m_lastParseBlockNs  = toNs(tBlockEnd - tInlinePrepDone);
    m_lastParseInlineNs = toNs(tInlinePrepDone - tStart) + toNs(tEnd - tBlockEnd);

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

using ByteRange = TreeSitterParser::ByteRange;

bool rangesOverlap(quint32 aStart, quint32 aEnd,
                   const std::vector<ByteRange> &ranges)
{
    for (const auto &r : ranges) {
        if (r.startByte < aEnd && r.endByte > aStart) return true;
    }
    return false;
}

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

// ---------- pruned walks (used by buildDocumentQueries(prior, edits)) ------

// Emit gate: an entry is emitted by the pruned walk when its full byte
// range overlaps any changed range. This is the inverse of the carry-over
// keep gate (which keeps entries whose range does NOT overlap), so for
// any individual entry exactly one of carry-over and pruned-walk emits it.

void collectHeadingsForQueryInRanges(TSNode node, const QByteArray &utf8,
                                     const std::vector<ByteRange> &ranges,
                                     QList<HeadingInfo> &headings)
{
    const quint32 ns = ts_node_start_byte(node);
    const quint32 ne = ts_node_end_byte(node);
    if (!rangesOverlap(ns, ne, ranges)) return;
    HeadingInfo h;
    if (extractHeadingFromNode(node, utf8, h)) {
        if (rangesOverlap(quint32(h.sourceOffset),
                          quint32(h.sourceOffset + h.sourceLength), ranges))
            headings.append(h);
        return;
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        collectHeadingsForQueryInRanges(ts_node_child(node, i), utf8, ranges, headings);
}

void collectInlineQueriesInRanges(TSNode node, const QByteArray &utf8,
                                  const std::vector<ByteRange> &ranges,
                                  QList<LinkInfo> &links, QList<TagInfo> &tags)
{
    const quint32 ns = ts_node_start_byte(node);
    const quint32 ne = ts_node_end_byte(node);
    if (!rangesOverlap(ns, ne, ranges)) return;
    LinkInfo li;
    if (extractLinkFromNode(node, utf8, li)) {
        if (rangesOverlap(quint32(li.sourceOffset),
                          quint32(li.sourceOffset + li.sourceLength), ranges))
            links.append(li);
        return;
    }
    TagInfo ti;
    if (extractTagFromNode(node, utf8, ti)) {
        if (rangesOverlap(quint32(ti.sourceOffset),
                          quint32(ti.sourceOffset + ti.sourceLength), ranges))
            tags.append(ti);
        return;
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        collectInlineQueriesInRanges(ts_node_child(node, i), utf8, ranges, links, tags);
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
static bool isListLoose(TSNode listNode, const QByteArray &utf8)
{
    const uint32_t start = ts_node_start_byte(listNode);
    const uint32_t end   = ts_node_end_byte(listNode);
    if (end <= start + 1 || static_cast<uint32_t>(utf8.size()) < end) return false;
    for (uint32_t i = start; i + 1 < end; ++i) {
        if (utf8[i] == '\n' && utf8[i + 1] == '\n') return true;
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
        // Strip trailing '\n' from content end
        while (contentEnd > contentStart
               && static_cast<uint32_t>(contentEnd - 1) < static_cast<uint32_t>(utf8.size())
               && utf8[contentEnd - 1] == '\n') {
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
                                  bool currentLooseRun = false)
{
    const char *type = ts_node_type(node);

    // Containers: descend into named children, do not emit.
    if (strcmp(type, "document") == 0 || strcmp(type, "section") == 0) {
        uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            collectTopLevelBlocks(child, utf8, out, currentIndent, currentLooseRun);
        }
        return;
    }

    // List: recurse into list_item children (one TLB per item).
    if (strcmp(type, "list") == 0) {
        const bool loose = isListLoose(node, utf8);
        const uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            collectTopLevelBlocks(child, utf8, out, currentIndent, loose);
        }
        return;
    }

    // List item: emit one TLB, then recurse into any nested list children.
    if (strcmp(type, "list_item") == 0) {
        TopLevelBlock b;
        b.kind = TopLevelBlock::Kind::ListItem;
        b.indentDepth = currentIndent;
        b.looseRun = currentLooseRun;
        harvestListItem(node, utf8, b);
        out.append(b);
        // Recurse into nested list children at increased indent depth
        const uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            if (strcmp(ts_node_type(child), "list") == 0) {
                collectTopLevelBlocks(child, utf8, out, currentIndent + 1, currentLooseRun);
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

    // Walk block tree for top-level blocks (linearised, in document order)
    collectTopLevelBlocks(blockRoot, m_utf8, result.topLevelBlocks);

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

DocumentQueryResult TreeSitterParser::buildDocumentQueries(
    const DocumentQueryResult &prior,
    const QList<ByteEdit> &edits) const
{
    if (!m_blockTree)
        return {};

    // Sort edits by oldStart so we can apply shift math left-to-right per
    // entry. (parseIncremental does its own sort internally; we must match
    // the shape of edits the caller used.)
    QList<ByteEdit> sortedEdits = edits;
    std::sort(sortedEdits.begin(), sortedEdits.end(),
              [](const ByteEdit &a, const ByteEdit &b) {
                  return a.oldStart < b.oldStart;
              });

    // Map an old-frame byte offset to the new-frame. Returns std::nullopt
    // if the offset falls strictly inside an edit's old range (entry was
    // overwritten and must be re-discovered, not carried over).
    auto shiftOldOffset = [&sortedEdits](int oldOff) -> std::optional<int> {
        qint64 cur = oldOff;
        for (const ByteEdit &e : sortedEdits) {
            if (qint64(e.oldStart) > cur) break;            // edit lies past us
            if (qint64(e.oldEnd) <= cur) {                  // edit lies entirely before us
                cur += qint64(e.newLength) - qint64(e.oldEnd - e.oldStart);
            } else {                                        // cur is inside [oldStart, oldEnd)
                return std::nullopt;
            }
        }
        return int(cur);
    };

    DocumentQueryResult result;

    // 1. Carry over prior entries whose byte range did not intersect any
    //    changed range. The entry's full range — [sourceOffset, sourceOffset
    //    + sourceLength) in old-frame coords — must shift cleanly (start byte
    //    survives; we conservatively require the start to shift) AND must
    //    not overlap any changed range in the new frame.
    auto carry = [&](auto &outList, const auto &priorList) {
        for (const auto &item : priorList) {
            const std::optional<int> nf = shiftOldOffset(item.sourceOffset);
            if (!nf) continue;
            const int len = item.sourceLength > 0 ? item.sourceLength : 1;
            if (rangesOverlap(quint32(*nf), quint32(*nf + len), m_lastChangedRanges))
                continue;
            auto shifted = item;
            shifted.sourceOffset = *nf;
            outList.append(shifted);
        }
    };
    carry(result.headings, prior.headings);
    carry(result.links,    prior.links);
    carry(result.tags,     prior.tags);

    // 2. Walk only the subtrees of the new tree that overlap a changed
    //    range, emitting entries whose start byte sits in a changed range.
    //    If there are no changed ranges (e.g. parseIncremental({}, sameBuf))
    //    this is a no-op and the carry-over above is the entire result.
    if (!m_lastChangedRanges.empty()) {
        TSNode blockRoot = ts_tree_root_node(m_blockTree);
        collectHeadingsForQueryInRanges(blockRoot, m_utf8, m_lastChangedRanges,
                                         result.headings);
        for (TSTree *tree : m_inlineTrees) {
            TSNode inlineRoot = ts_tree_root_node(tree);
            collectInlineQueriesInRanges(inlineRoot, m_utf8, m_lastChangedRanges,
                                          result.links, result.tags);
        }
    }

    // 3. Sort by source offset for stable order and equality with the
    //    fresh-walk reference.
    std::sort(result.headings.begin(), result.headings.end(),
              [](const HeadingInfo &a, const HeadingInfo &b) {
                  return a.sourceOffset < b.sourceOffset;
              });
    std::sort(result.links.begin(), result.links.end(),
              [](const LinkInfo &a, const LinkInfo &b) {
                  return a.sourceOffset < b.sourceOffset;
              });
    std::sort(result.tags.begin(), result.tags.end(),
              [](const TagInfo &a, const TagInfo &b) {
                  return a.sourceOffset < b.sourceOffset;
              });

    // Top-level blocks: inherently order-dependent (consumers expect a
    // linear document-order sequence) and there are typically far fewer
    // of them than headings/links/tags. A fresh full walk is simpler
    // and correct; revisit if profiling shows it as a hot spot.
    {
        TSNode blockRoot = ts_tree_root_node(m_blockTree);
        collectTopLevelBlocks(blockRoot, m_utf8, result.topLevelBlocks);
    }

    // Bake per-block inline spans (R1B).
    bakeInlineSpansIntoBlocks(buildSpanMap(), result.topLevelBlocks, m_utf8);

    return result;
}

// ---------------------------------------------------------------------------
// inlineSpansFor — standalone per-block inline parse entry point (Phase 10)
// ---------------------------------------------------------------------------

QList<SourceSpan> inlineSpansFor(const QByteArray &blockContent)
{
    TreeSitterParser parser;
    parser.parse(QString::fromUtf8(blockContent));
    return parser.buildSpanMap();
}

} // namespace Markoff
