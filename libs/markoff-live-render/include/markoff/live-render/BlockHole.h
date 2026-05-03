// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff-foundation/TextAnchor.h>

#include <QString>
#include <QtGlobal>

namespace Markoff::LiveRender {

/// Kind of phantom row (v2: paragraph only; v3+ extends).
enum class HoleKind {
    Paragraph,
};

/// Layer-local hole identity. Disambiguates view-side phantom rows
/// from CRDT-anchored parser blocks. Per spec §3.1 amendment A1.
struct MARKOFF_LIVE_RENDER_EXPORT HoleBlockId {
    quint64 holeId;                  ///< zero == invalid

    bool operator==(const HoleBlockId &) const = default;
};

/// A view-side phantom row owned by LiveHoleLayer. The layer manages
/// its lifecycle; consumers refer to it via `holeId`. The reifyAnchor
/// is the CRDT byte position where the hole's `bufferText` will be
/// committed to source on reification (one applyLocalEdit of
/// "\n\n" + bufferText at reifyAnchor).
struct MARKOFF_LIVE_RENDER_EXPORT BlockHole {
    HoleKind kind = HoleKind::Paragraph;
    Markoff::TextAnchor reifyAnchor;
    QString bufferText;
    quint64 holeId = 0;
};

}  // namespace Markoff::LiveRender
