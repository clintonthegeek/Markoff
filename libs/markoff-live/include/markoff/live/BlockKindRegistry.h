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
        // FALSIFIABILITY PROOF — REVERTS NEXT COMMIT.
        // The block-only kind treatment lives entirely behind this predicate.
        // Making it false should turn the chokepoint, navigation, and
        // structural-key paths back into pre-spec behaviour and break every
        // R-rule test. Mirrors commits 2d609ba (takeFocus stub) and 20dcaee
        // (establishFocus stub) — see docs/specs/2026-05-13-block-only-kinds-design.md §7.
        (void)kind;
        return false;
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
