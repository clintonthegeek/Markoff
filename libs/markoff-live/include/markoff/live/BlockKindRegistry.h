// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/live/BlockKindDescriptor.h>
#include <markoff/core/BlockSerializerRegistry.h>

#include <QHash>
#include <QStringList>

namespace Markoff::Live {

/// Registry of block kind descriptors. Built-in kinds are registered in
/// the constructor. Plugin authors register custom kinds via register_().
/// LiveListModelBinding owns one instance; passes a pointer to downstream
/// components (LiveCursorState, LiveStructuralKeyHandler) in R3+.
class MARKOFF_LIVE_EXPORT BlockKindRegistry
    : public Markoff::BlockSerializerRegistry {
public:
    BlockKindRegistry();  ///< Registers all built-in kinds.

    void register_(BlockKindDescriptor descriptor);
    const BlockKindDescriptor *find(const QString &id) const;
    QStringList kinds() const;

    /// Returns true when the kind's descriptor has isBlockOnly set — i.e. the
    /// block can never host a text caret and UX is select-whole-block.
    bool isBlockOnly(const QString &kind) const {
        const auto *d = find(kind);
        return d && d->isBlockOnly;
    }

    // Markoff::BlockSerializerRegistry implementation
    QByteArray serialize(Markoff::BlockKind kind,
                         const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs,
                         const QByteArray &content) const override;

private:
    void registerBuiltins();
    QHash<QString, BlockKindDescriptor> m_descriptors;
};

}  // namespace Markoff::Live
