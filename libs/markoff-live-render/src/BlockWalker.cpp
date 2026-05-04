// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockWalker.h"

#include <markoff/live-render/BlockKind.h>
#include <markoff-parser/Document.h>

#include <QByteArray>
#include <QString>

namespace Markoff::LiveRender {

namespace {

QString mapKind(Markoff::TopLevelBlock::Kind k)
{
    using K = Markoff::TopLevelBlock::Kind;
    switch (k) {
        case K::AtxHeading:
        case K::SetextHeading:      return BlockKind::Heading;
        case K::FencedCodeBlock:
        case K::IndentedCodeBlock:  return BlockKind::CodeBlock;
        case K::ThematicBreak:      return BlockKind::HorizontalRule;
        // Everything else collapses to Paragraph in R2.
        // Lists/blockquotes/tables gain dedicated kinds in R7.
        case K::Paragraph:
        case K::BlockQuote:
        case K::ListTight:
        case K::ListLoose:
        case K::HtmlBlock:
        case K::LinkReferenceDefinition:
        case K::Table:
        case K::Other:
        default:                    return BlockKind::Paragraph;
    }
}

}  // namespace

QList<BlockRecord> BlockWalker::walk(const Markoff::Document *parsed)
{
    QList<BlockRecord> out;
    if (!parsed) return out;

    const QList<Markoff::TopLevelBlock> blocks = parsed->topLevelBlocks();
    if (blocks.isEmpty()) return out;

    const QString    body     = parsed->markdownContent();
    const QByteArray bodyUtf8 = body.toUtf8();

    out.reserve(blocks.size());

    // Single-pass UTF-8 byte → UTF-16 char-offset translation.
    // Maintains a running cursor advancing forward through the body.
    int byteCursor = 0;
    int charCursor = 0;

    auto advanceTo = [&](int targetByte) {
        if (targetByte < byteCursor) {
            byteCursor = 0;
            charCursor = 0;
        }
        const int end = qMin(targetByte, static_cast<int>(bodyUtf8.size()));
        while (byteCursor < end) {
            const unsigned char c = static_cast<unsigned char>(bodyUtf8[byteCursor]);
            int seqLen;
            if      ((c & 0x80) == 0x00) seqLen = 1;
            else if ((c & 0xE0) == 0xC0) seqLen = 2;
            else if ((c & 0xF0) == 0xE0) seqLen = 3;
            else if ((c & 0xF8) == 0xF0) seqLen = 4;
            else                          seqLen = 1;
            charCursor += (seqLen == 4) ? 2 : 1;
            byteCursor += seqLen;
        }
    };

    for (const auto &tlb : blocks) {
        BlockRecord rec;
        rec.kind         = mapKind(tlb.kind);
        rec.headingLevel = tlb.headingLevel;
        rec.codeLanguage = tlb.codeLanguage;
        rec.inlineSpans  = tlb.inlineSpans;  // pre-baked by parser (R1B)
        // blockAnchor: filled by LiveListModelBinding.

        advanceTo(tlb.byteStart);
        const int charStart = charCursor;
        advanceTo(tlb.byteEnd);
        const int charEnd   = charCursor;

        rec.text = body.mid(charStart, charEnd - charStart);
        // Tree-sitter's paragraph (and similar block) byte range includes
        // the trailing newline of the block's last line — that newline is
        // logically the boundary between this block and the next, NOT user-
        // editable content. Without trimming, qtPos = text.length() lands
        // ONE byte INSIDE the "\n\n" paragraph separator: a user typing at
        // the end of a paragraph inserts between the two `\n`s, turning
        // "para1\n\npara2" into "para1\nXpara2" (one paragraph with a soft
        // break, eating the next paragraph). Trimming makes byte
        // position blockStart + text.size() == byte position of the
        // separator's first `\n`, which is the correct insert point.
        if (rec.text.endsWith(QLatin1Char('\n')))
            rec.text.chop(1);
        out.append(rec);
    }

    return out;
}

}  // namespace Markoff::LiveRender
