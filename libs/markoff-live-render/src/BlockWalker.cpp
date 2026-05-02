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
        out.append(rec);
    }

    return out;
}

}  // namespace Markoff::LiveRender
