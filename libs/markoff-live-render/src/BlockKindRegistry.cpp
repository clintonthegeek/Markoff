// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/BlockKind.h>

namespace Markoff::LiveRender {

BlockKindRegistry::BlockKindRegistry()
{
    registerBuiltins();
}

void BlockKindRegistry::registerBuiltins()
{
    // Paragraph: text-bearing, TextCaret (R3), Enter/Backspace/Delete (R5).
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Paragraph;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/ParagraphDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // Heading: text-bearing, TextCaret.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Heading;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/HeadingDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // CodeBlock: text-bearing, TextCaret. Tab inserts literal tab (R5).
    {
        BlockKindDescriptor d;
        d.id = BlockKind::CodeBlock;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/CodeBlockDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // HorizontalRule: non-text, BlockSelected only.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::HorizontalRule;
        d.acceptsTextRoleUpdates = false;
        d.supportedCursorVariants = { QStringLiteral("BlockSelected") };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/HorizontalRuleDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // Image: non-text, BlockSelected (default) + optional alt-edit (post-R6).
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Image;
        d.acceptsTextRoleUpdates = false;
        d.supportedCursorVariants = { QStringLiteral("BlockSelected") };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/ImageDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
}

void BlockKindRegistry::register_(BlockKindDescriptor descriptor)
{
    m_descriptors.insert(descriptor.id, std::move(descriptor));
}

const BlockKindDescriptor *BlockKindRegistry::find(const QString &id) const
{
    auto it = m_descriptors.find(id);
    return (it != m_descriptors.end()) ? &it.value() : nullptr;
}

QStringList BlockKindRegistry::kinds() const
{
    return QStringList(m_descriptors.keys());
}

}  // namespace Markoff::LiveRender
