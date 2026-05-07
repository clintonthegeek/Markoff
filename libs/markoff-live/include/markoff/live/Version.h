// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>

#include <QtGlobal>

/// `Markoff::Live` is the namespace for the live-render library.
/// The library is delivered as a Qt6 QML module under URI
/// `org.markoff.live 1.0`.
///
/// Architecture spec: docs/specs/2026-05-02-live-render-restoration-design.md
namespace Markoff::Live {

/// Library version number, integer-encoded `major * 10000 + minor * 100 + patch`.
/// R1 ships at 0 (= 0.0.0).
MARKOFF_LIVE_EXPORT quint32 version() noexcept;

}  // namespace Markoff::Live
