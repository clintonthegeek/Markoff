// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/TextAnchor.h>

#include <variant>
#include <QString>
#include <QtGlobal>

namespace Markoff::LiveRender {

/// Opaque block identity. Spec §3.1: a CRDT-anchored parser block.
///
/// Historical note: spec §3.1 amendment A1 had widened this to a variant
/// over `Markoff::BlockAnchor` and a phantom-row id to support v2 phantom
/// rows. The marker-paragraph design (R5.5) retires phantom rows in
/// favour of a single source-of-truth marker block, so the variant is
/// reverted.
using BlockId = Markoff::BlockAnchor;

/// Caret inside a text-bearing block at a CRDT-anchored byte position.
/// Rendered as a blinking I-beam. Spec §3.1.
struct MARKOFF_LIVE_RENDER_EXPORT TextCaret {
    BlockId              block;           ///< CRDT-anchored parser block.
    Markoff::TextAnchor  positionAnchor;  ///< CRDT anchor; survives remote edits.
    quint32              cachedByteOffset = 0; ///< Resolved byte offset; refreshed on use.

    bool operator==(const TextCaret &o) const noexcept {
        return block == o.block && positionAnchor == o.positionAnchor
            && cachedByteOffset == o.cachedByteOffset;
    }
};

/// Block focused as a unit — no caret. Rendered as a focus ring.
/// Used by non-text blocks (hr, image) in their default state. Spec §3.1.
struct MARKOFF_LIVE_RENDER_EXPORT BlockSelected {
    BlockId block;                        ///< CRDT-anchored parser block.
    bool operator==(const BlockSelected &o) const noexcept { return block == o.block; }
};

/// Block in its own internal-edit mode. Deferred to R8 (math block). Spec §3.1.
struct MARKOFF_LIVE_RENDER_EXPORT BlockInternalEdit {
    BlockId block;                        ///< CRDT-anchored parser block.
    QString mode;  ///< Block-kind-defined token, e.g. "editing-latex".
    bool operator==(const BlockInternalEdit &o) const noexcept {
        return block == o.block && mode == o.mode;
    }
};

/// No-focus sentinel — cursor is not placed anywhere.
struct MARKOFF_LIVE_RENDER_EXPORT NoCursor {
    bool operator==(const NoCursor &) const noexcept { return true; }
};

/// The canonical cursor value. Discriminated union over the four variants.
/// Spec §3.1: "using Cursor = std::variant<TextCaret, BlockSelected, BlockInternalEdit>;"
/// NoCursor is our sentinel for "nothing focused" before first click.
using Cursor = std::variant<NoCursor, TextCaret, BlockSelected, BlockInternalEdit>;

/// View-layer selection: two Cursor endpoints. Collapsed when anchor == active.
/// Spec §3.1 Selection.
struct MARKOFF_LIVE_RENDER_EXPORT LiveRenderSelection {
    Cursor anchor;
    Cursor active;

    bool isCollapsed() const noexcept { return anchor == active; }
    bool isCaret() const noexcept {
        return isCollapsed() && std::holds_alternative<TextCaret>(anchor);
    }
    bool hasNoFocus() const noexcept {
        return std::holds_alternative<NoCursor>(anchor);
    }
};

}  // namespace Markoff::LiveRender
