// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/BlockKindRegistry.h>
#include <markoff/live/BlockKind.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/BlockAttrsMap.h>

namespace Markoff::Live {

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
        d.consumedStructuralKeys = {
            Qt::Key_Return, Qt::Key_Enter,
            Qt::Key_Backspace, Qt::Key_Delete,
        };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/ParagraphDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // Heading: text-bearing, TextCaret. Same structural keys as paragraph.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Heading;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.consumedStructuralKeys = {
            Qt::Key_Return, Qt::Key_Enter,
            Qt::Key_Backspace, Qt::Key_Delete,
        };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/HeadingDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // CodeBlock: text-bearing, TextCaret. Only edge-merge keys consumed;
    // Enter passes through to TextEdit's native \n insertion.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::CodeBlock;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.consumedStructuralKeys = { Qt::Key_Backspace, Qt::Key_Delete, Qt::Key_Tab };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/CodeBlockDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // HorizontalRule: non-text, BlockSelected only. block-only (no TextCaret ever).
    {
        BlockKindDescriptor d;
        d.id = BlockKind::HorizontalRule;
        d.acceptsTextRoleUpdates = false;
        d.isBlockOnly = true;
        d.supportedCursorVariants = { QStringLiteral("BlockSelected") };
        d.consumedStructuralKeys = {
            Qt::Key_Delete, Qt::Key_Backspace,
            Qt::Key_Up, Qt::Key_Down,
            Qt::Key_Return, Qt::Key_Enter,
        };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/HorizontalRuleDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // Image: non-text, BlockSelected (default) + optional alt-edit. block-only.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Image;
        d.acceptsTextRoleUpdates = false;
        d.isBlockOnly = true;
        d.supportedCursorVariants = {
            QStringLiteral("BlockSelected"),
            QStringLiteral("BlockInternalEdit"),
        };
        d.internalEditModes = { QStringLiteral("alt-edit") };
        d.consumedStructuralKeys = {
            Qt::Key_Delete, Qt::Key_Backspace,
            Qt::Key_Up, Qt::Key_Down,
            Qt::Key_Return, Qt::Key_Enter,
        };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/ImageDelegate.qml");
        m_descriptors.insert(d.id, d);
    }
    // ListItem: text-bearing, TextCaret. Structural keys: Enter/Backspace/Delete/Tab.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::ListItem;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.consumedStructuralKeys = {
            Qt::Key_Return, Qt::Key_Enter,
            Qt::Key_Backspace, Qt::Key_Delete,
            Qt::Key_Tab,
        };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/ListItemDelegate.qml");
        d.serializer = [](const QByteArray &text,
                          const QHash<Markoff::AttrName, Markoff::AttrValue> &) {
            return text;
        };
        m_descriptors.insert(d.id, d);
    }
    // Blockquote: text-bearing, TextCaret. Enter/Backspace/Delete consumed.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Blockquote;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = { QStringLiteral("TextCaret") };
        d.consumedStructuralKeys = {
            Qt::Key_Return, Qt::Key_Enter,
            Qt::Key_Backspace, Qt::Key_Delete,
        };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/BlockquoteDelegate.qml");
        d.serializer = [](const QByteArray &text,
                          const QHash<Markoff::AttrName, Markoff::AttrValue> &) {
            return text;
        };
        m_descriptors.insert(d.id, d);
    }
    // Math: non-text initially, BlockSelected + BlockInternalEdit for latex editing.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Math;
        d.acceptsTextRoleUpdates = true;
        d.supportedCursorVariants = {
            QStringLiteral("BlockSelected"),
            QStringLiteral("BlockInternalEdit"),
        };
        d.internalEditModes = { QStringLiteral("editing-latex") };
        d.consumedStructuralKeys = {
            Qt::Key_Return, Qt::Key_Enter,
            Qt::Key_Backspace, Qt::Key_Delete,
            Qt::Key_F2,
        };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/MathDelegate.qml");
        d.serializer = [](const QByteArray &text,
                          const QHash<Markoff::AttrName, Markoff::AttrValue> &) {
            return text;
        };
        m_descriptors.insert(d.id, d);
    }
    // Table: first multi-cell interactive atomic block. BlockSelected for
    // whole-block selection/delete; BlockInternalEdit for in-cell editing.
    // Buffer IS the pipe-table markdown source (passthrough serializer).
    // Spec: docs/specs/2026-05-22-e4-tables-design.md §5.1.
    {
        BlockKindDescriptor d;
        d.id = BlockKind::Table;
        d.acceptsTextRoleUpdates = true;
        d.isBlockOnly = false;
        d.supportedCursorVariants = {
            QStringLiteral("BlockSelected"),
            QStringLiteral("BlockInternalEdit"),
        };
        d.internalEditModes = { QStringLiteral("editing-cell") };
        d.consumedStructuralKeys = {
            Qt::Key_Delete, Qt::Key_Backspace,
            Qt::Key_Up, Qt::Key_Down,
            Qt::Key_Return, Qt::Key_Enter,
            Qt::Key_Tab, Qt::Key_Backtab,
            Qt::Key_Escape,
        };
        d.delegateUrl = QStringLiteral(
            "qrc:/qt/qml/org/markoff/live/render/delegates/TableDelegate.qml");
        d.serializer = [](const QByteArray &text,
                          const QHash<Markoff::AttrName, Markoff::AttrValue> &) {
            return text;
        };
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

QByteArray BlockKindRegistry::serialize(Markoff::BlockKind kind,
                                         const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs,
                                         const QByteArray &content) const
{
    using BK = Markoff::BlockKind;
    QString kindStr;
    switch (kind) {
    case BK::Heading:        kindStr = BlockKind::Heading;        break;
    case BK::CodeBlock:      kindStr = BlockKind::CodeBlock;      break;
    case BK::HorizontalRule: kindStr = BlockKind::HorizontalRule; break;
    case BK::Image:          kindStr = BlockKind::Image;          break;
    case BK::ListItem:       kindStr = BlockKind::ListItem;       break;
    case BK::BlockQuote:     kindStr = BlockKind::Blockquote;     break;
    case BK::Math:           kindStr = BlockKind::Math;           break;
    default:                 kindStr = BlockKind::Paragraph;      break;
    }
    const auto *desc = find(kindStr);
    if (desc && desc->serializer)
        return desc->serializer(content, attrs);
    return content;  // passthrough fallback
}

}  // namespace Markoff::Live
