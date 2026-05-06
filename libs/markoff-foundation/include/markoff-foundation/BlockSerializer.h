// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/BlockAttrsMap.h>
#include <markoff-foundation/MarkoffFoundationExport.h>
#include <QByteArray>
#include <QHash>
#include <functional>

namespace Markoff {

/// Signature for a per-kind block serializer. Given the block kind, its attrs,
/// and the current buffer text (UTF-8), returns the reconstructed Markdown
/// source for that block. The returned bytes must not include trailing inter-
/// block separators — those are added by serializeForSave().
using BlockSerializer = std::function<QByteArray(BlockKind,
                                                  const QHash<AttrName, AttrValue> &,
                                                  const QByteArray &content)>;

/// Singleton registry mapping BlockKind → BlockSerializer. Built-in serializers
/// are registered by registerBuiltins(); callers may override or extend.
class MARKOFF_FOUNDATION_EXPORT BuiltinBlockSerializerRegistry {
public:
    static BuiltinBlockSerializerRegistry &instance();

    /// Register a serializer for the given kind. Overwrites any previous entry.
    void registerSerializer(BlockKind kind, BlockSerializer fn);

    /// Look up the serializer for the given kind. Returns the Unknown/passthrough
    /// fallback if no serializer is registered for that kind.
    BlockSerializer get(BlockKind kind) const;

    /// Register all built-in serializers (paragraph, heading, code-block,
    /// list-item, blockquote, hr, image, math, mermaid, html-block, table).
    /// Called once by MarkoffDocument::serializeForSave() on first use.
    void registerBuiltins();

private:
    QHash<uint8_t, BlockSerializer> m_serializers;
    bool m_builtinsRegistered = false;
};

}  // namespace Markoff
