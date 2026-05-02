// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/BlockKindDescriptor.h>

#include <QHash>
#include <QStringList>

namespace Markoff::LiveRender {

/// Registry of block kind descriptors. Built-in kinds are registered in
/// the constructor. Plugin authors register custom kinds via register_().
/// LiveListModelBinding owns one instance; passes a pointer to downstream
/// components (LiveCursorState, LiveStructuralKeyHandler) in R3+.
class MARKOFF_LIVE_RENDER_EXPORT BlockKindRegistry {
public:
    BlockKindRegistry();  ///< Registers all five built-in kinds.

    void register_(BlockKindDescriptor descriptor);
    const BlockKindDescriptor *find(const QString &id) const;
    QStringList kinds() const;

private:
    void registerBuiltins();
    QHash<QString, BlockKindDescriptor> m_descriptors;
};

}  // namespace Markoff::LiveRender
