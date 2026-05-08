// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/BlockAnchor.h>
#include <markoff/core/TextAnchor.h>

#include <variant>
#include <QString>
#include <QMetaType>
#include <QtGlobal>

namespace Markoff {

/// Opaque block identity used in cursor variants.
using BlockId = Markoff::BlockAnchor;

/// Caret inside a text-bearing block at a CRDT-anchored byte position.
/// Rendered as a blinking I-beam.
struct MARKOFF_CORE_EXPORT TextCaret {
    Markoff::BlockId    block;                   ///< CRDT-anchored parser block.
    Markoff::TextAnchor positionAnchor;          ///< CRDT anchor; survives remote edits.
    quint32             cachedByteOffset = 0;    ///< Resolved byte offset; refreshed on use.

    bool operator==(const TextCaret &o) const noexcept {
        return block == o.block && positionAnchor == o.positionAnchor
            && cachedByteOffset == o.cachedByteOffset;
    }
};

/// Block focused as a unit — no caret. Rendered as a focus ring.
struct MARKOFF_CORE_EXPORT BlockSelected {
    Markoff::BlockId block;
    bool operator==(const BlockSelected &o) const noexcept { return block == o.block; }
};

/// Block in its own internal-edit mode. Deferred to R8 (math block).
struct MARKOFF_CORE_EXPORT BlockInternalEdit {
    Markoff::BlockId block;
    QString          mode;  ///< Block-kind-defined token, e.g. "editing-latex".
    bool operator==(const BlockInternalEdit &o) const noexcept {
        return block == o.block && mode == o.mode;
    }
};

/// No-focus sentinel — cursor is not placed anywhere.
struct MARKOFF_CORE_EXPORT NoCursor {
    bool operator==(const NoCursor &) const noexcept { return true; }
};

/// The canonical cursor value. Discriminated union over the four variants.
using Cursor = std::variant<NoCursor, TextCaret, BlockSelected, BlockInternalEdit>;

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::Cursor)
