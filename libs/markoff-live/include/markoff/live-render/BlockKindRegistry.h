// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/BlockKindDescriptor.h>
#include <markoff/core/BlockSerializerRegistry.h>

#include <QHash>
#include <QStringList>

namespace Markoff::Live {

/// Registry of block kind descriptors. Built-in kinds are registered in
/// the constructor. Plugin authors register custom kinds via register_().
/// LiveListModelBinding owns one instance; passes a pointer to downstream
/// components (LiveCursorState, LiveStructuralKeyHandler) in R3+.
class MARKOFF_LIVE_RENDER_EXPORT BlockKindRegistry
    : public Markoff::BlockSerializerRegistry {
public:
    BlockKindRegistry();  ///< Registers all built-in kinds.

    void register_(BlockKindDescriptor descriptor);
    const BlockKindDescriptor *find(const QString &id) const;
    QStringList kinds() const;

    // Markoff::BlockSerializerRegistry implementation
    QByteArray serialize(Markoff::BlockKind kind,
                         const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs,
                         const QByteArray &content) const override;

private:
    void registerBuiltins();
    QHash<QString, BlockKindDescriptor> m_descriptors;
};

}  // namespace Markoff::Live
