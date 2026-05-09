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

// Drops up to 6 leading `#` characters and one optional space after them.
// Returns the remainder. Used by serializeHeading to defend against the
// case where the heading buffer already carries an ATX prefix (the load-
// time convention) so re-serialisation doesn't double-prefix.
QByteArray stripLeadingHashes(const QByteArray &content)
{
    int i = 0;
    while (i < 6 && i < content.size() && content[i] == '#') ++i;
    if (i == 0) return content;
    if (i < content.size() && content[i] == ' ') ++i;
    return content.mid(i);
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

    // Setext form: buffer already contains `text\n<underline>`. Emit
    // verbatim. Only valid for level 1 / 2 per CommonMark; fall through
    // to ATX otherwise (defensive).
    auto fmIt = attrs.constFind("headingForm");
    if (fmIt != attrs.cend()) {
        if (const QString *p = std::get_if<QString>(&fmIt.value())) {
            if (*p == QStringLiteral("setext") && (level == 1 || level == 2))
                return content;
        }
    }

    // ATX form. Strip any leading `# ` markers from content first so a
    // buffer like "## Heading" round-trips as "## Heading", not
    // "## ## Heading".
    return QByteArray(level, '#') + " " + stripLeadingHashes(content);
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
