// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/MarkoffDocument.h>

namespace Markoff::Cmd {

VNext::ActionPublicationResult changeKind(
    MarkoffDocument& doc, BlockId block, BlockKind newKind,
    const QList<QByteArray>& attrNames, const QList<AttrValue>& attrValues)
{
    if (attrNames.size() != attrValues.size())
        return {VNext::ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::malformed,
                "changeKind attribute names and values must have equal lengths")};
    return doc.changeBlockKind(block, newKind, attrNames, attrValues);
}

PasteMarkdownResult pasteMarkdown(MarkoffDocument& doc, BlockId targetBlock,
                                  uint32_t byteOffset, const QByteArray& source)
{
    const auto result = doc.pasteMarkdownAt(targetBlock, byteOffset, source);
    if (result.publication.state != VNext::ActionPublicationState::committed
        && result.publication.state != VNext::ActionPublicationState::committedHistoryPending)
        return {targetBlock, byteOffset};
    return {result.caretBlock, result.caretByteOffset};
}

} // namespace Markoff::Cmd
