// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/Cursor.h>
#include <markoff/live/MarkoffLiveExport.h>

namespace Markoff::Live {

/// View-layer selection: two Cursor endpoints. Collapsed when anchor == active.
/// Spec §3.1 Selection.
struct MARKOFF_LIVE_EXPORT LiveRenderSelection {
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

}  // namespace Markoff::Live
