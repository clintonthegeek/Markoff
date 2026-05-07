// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/BlockSerializer.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/BlockAttrsMap.h>

#include <variant>

namespace Markoff {

// ============================================================================
// BuiltinBlockSerializerRegistry — singleton + registration
// ============================================================================

BuiltinBlockSerializerRegistry &BuiltinBlockSerializerRegistry::instance()
{
    static BuiltinBlockSerializerRegistry s_instance;
    return s_instance;
}

void BuiltinBlockSerializerRegistry::registerSerializer(BlockKind kind, BlockSerializer fn)
{
    m_serializers.insert(static_cast<uint8_t>(kind), std::move(fn));
}

BlockSerializer BuiltinBlockSerializerRegistry::get(BlockKind kind) const
{
    auto it = m_serializers.constFind(static_cast<uint8_t>(kind));
    if (it != m_serializers.cend())
        return *it;
    // Fallback: passthrough
    return [](BlockKind, const QHash<AttrName, AttrValue> &, const QByteArray &content) {
        return content;
    };
}

// ============================================================================
// Built-in serializers
// ============================================================================

namespace {

// --- Task 8.2: paragraph, heading, code-block, list-item ---

QByteArray serializeParagraph(BlockKind, const QHash<AttrName, AttrValue> &,
                               const QByteArray &content)
{
    return content;
}

QByteArray serializeHeading(BlockKind, const QHash<AttrName, AttrValue> &attrs,
                             const QByteArray &content)
{
    int level = 1;
    auto it = attrs.constFind("level");
    if (it != attrs.cend()) {
        if (const int *p = std::get_if<int>(&it.value()))
            level = *p;
    }
    return QByteArray(level, '#') + " " + content;
}

QByteArray serializeCodeBlock(BlockKind, const QHash<AttrName, AttrValue> &attrs,
                               const QByteArray &content)
{
    QByteArray info;
    auto it = attrs.constFind("infoString");
    if (it != attrs.cend()) {
        if (const QString *p = std::get_if<QString>(&it.value()))
            info = p->toUtf8();
    }
    return "```" + info + "\n" + content + "\n```";
}

QByteArray serializeListItem(BlockKind, const QHash<AttrName, AttrValue> &attrs,
                              const QByteArray &content)
{
    QByteArray marker = "-";
    auto it = attrs.constFind("marker");
    if (it != attrs.cend()) {
        if (const QString *p = std::get_if<QString>(&it.value()))
            marker = p->toUtf8();
    }
    return marker + " " + content;
}

// --- Task 8.3: blockquote, hr, image, math, mermaid, html-block, table ---

QByteArray serializeBlockQuote(BlockKind, const QHash<AttrName, AttrValue> &,
                                const QByteArray &content)
{
    // v1: simple prefix — works for single-line content; multi-line handled by
    // round-trip via load-time bytes for untouched blocks.
    return "> " + content;
}

QByteArray serializeHorizontalRule(BlockKind, const QHash<AttrName, AttrValue> &,
                                    const QByteArray &)
{
    return "---";
}

// Passthrough serializers: round-trip via load-time bytes for untouched blocks;
// these handle the case where a block was born post-load (no load-time bytes).
QByteArray serializePassthrough(BlockKind, const QHash<AttrName, AttrValue> &,
                                 const QByteArray &content)
{
    return content;
}

}  // anonymous namespace

void BuiltinBlockSerializerRegistry::registerBuiltins()
{
    if (m_builtinsRegistered) return;
    m_builtinsRegistered = true;

    registerSerializer(BlockKind::Paragraph,      serializeParagraph);
    registerSerializer(BlockKind::Heading,         serializeHeading);
    registerSerializer(BlockKind::CodeBlock,       serializeCodeBlock);
    registerSerializer(BlockKind::ListItem,        serializeListItem);
    registerSerializer(BlockKind::BlockQuote,      serializeBlockQuote);
    registerSerializer(BlockKind::HorizontalRule,  serializeHorizontalRule);
    registerSerializer(BlockKind::Image,           serializePassthrough);
    registerSerializer(BlockKind::Math,            serializePassthrough);
    registerSerializer(BlockKind::Mermaid,         serializePassthrough);
    registerSerializer(BlockKind::HtmlBlock,       serializePassthrough);
    registerSerializer(BlockKind::Table,           serializePassthrough);
}

}  // namespace Markoff
