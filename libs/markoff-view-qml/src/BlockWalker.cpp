// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockWalker.h"

#include <markoff/view/qml/BlockKind.h>
#include <markoff-parser/Document.h>

#include <QByteArray>
#include <QString>

namespace Markoff::View::Qml {

namespace {

QString mapKind(Markoff::TopLevelBlock::Kind k)
{
    using K = Markoff::TopLevelBlock::Kind;
    switch (k) {
        case K::AtxHeading:
        case K::SetextHeading:
            return BlockKind::Heading;
        case K::FencedCodeBlock:
        case K::IndentedCodeBlock:
            return BlockKind::CodeBlock;
        case K::ThematicBreak:
            return BlockKind::HorizontalRule;
        // Everything else collapses to Paragraph for v1. The foundation's
        // parser doesn't surface dedicated kinds for blockquotes/lists/
        // tables that the view yet has delegates for, so they render as
        // raw markdown text — InlineFormatHighlighter still styles
        // inline formatting (bold/italic/code spans).
        case K::Paragraph:
        case K::BlockQuote:
        case K::ListItem:
        case K::HtmlBlock:
        case K::LinkReferenceDefinition:
        case K::Table:
        case K::Other:
        default:
            return BlockKind::Paragraph;
    }
}

}  // namespace

QList<BlockRecord> BlockWalker::walk(const Markoff::Document *parsed)
{
    QList<BlockRecord> out;
    if (!parsed) return out;

    const QList<Markoff::TopLevelBlock> blocks = parsed->topLevelBlocks();
    if (blocks.isEmpty()) return out;

    // The body buffer (post-frontmatter) is what `byteStart`/`byteEnd` index
    // into. Document::sourceText() is the full source; for byte-coords-to-
    // QString-coords we want the body bytes specifically. The foundation's
    // BlockAnchor byte ranges (used by LiveEditBinding) are also in body
    // coordinates after the parse pipeline strips frontmatter, so the
    // delegate's text and the binding's blockStart use a common origin.
    const QString body = parsed->markdownContent();
    const QByteArray bodyUtf8 = body.toUtf8();

    out.reserve(blocks.size());

    // Single-pass UTF-8 byte → UTF-16 char-offset translation. Maintain a
    // running cursor through the body's UTF-8 bytes and emit each block's
    // start/end as QString character offsets in lockstep.
    int  byteCursor = 0;
    int  charCursor = 0;
    auto advanceTo = [&](int targetByte) {
        // Walk forward from byteCursor to targetByte, updating charCursor.
        // Bytes outside [0, bodyUtf8.size()] are clamped.
        if (targetByte < byteCursor) {
            // Should not happen — topLevelBlocks() returns blocks in
            // document order — but guard defensively.
            byteCursor = 0;
            charCursor = 0;
        }
        const int end = std::min(targetByte, static_cast<int>(bodyUtf8.size()));
        while (byteCursor < end) {
            const unsigned char c = static_cast<unsigned char>(bodyUtf8[byteCursor]);
            int seqLen;
            if      ((c & 0x80) == 0x00) seqLen = 1;
            else if ((c & 0xE0) == 0xC0) seqLen = 2;
            else if ((c & 0xF0) == 0xE0) seqLen = 3;
            else if ((c & 0xF8) == 0xF0) seqLen = 4;
            else                          seqLen = 1; // malformed; skip 1
            // UTF-16 code units: 1 for BMP, 2 for supplementary (4-byte UTF-8).
            charCursor += (seqLen == 4) ? 2 : 1;
            byteCursor += seqLen;
        }
    };

    for (const auto &tlb : blocks) {
        BlockRecord rec;
        rec.kind         = mapKind(tlb.kind);
        rec.headingLevel = tlb.headingLevel;
        rec.codeLanguage = tlb.codeLanguage;
        // Legacy: codeText is no longer populated. CodeBlockDelegate now
        // renders the source-faithful `text` (which includes the fences).
        rec.codeText     = QString();
        // imageSrc/Alt/Title: the parser doesn't surface image-only-paragraph
        // detection through topLevelBlocks() in v1, so these stay empty.
        // Image syntax inside paragraphs renders as raw markdown text —
        // InlineFormatHighlighter handles the inline styling.

        advanceTo(tlb.byteStart);
        const int charStart = charCursor;
        advanceTo(tlb.byteEnd);
        const int charEnd   = charCursor;

        rec.text   = body.mid(charStart, charEnd - charStart);
        rec.source = rec.text;
        out.append(rec);
    }

    return out;
}

}  // namespace Markoff::View::Qml
