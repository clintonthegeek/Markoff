// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace Markoff::Live {

/// Coarser dispatch key over BlockKind: the `delegateClass` value that
/// `LiveView.qml`'s DelegateChooser uses to pick a delegate. Within-class
/// kind changes (e.g. paragraph→heading, both "text-inline") produce a
/// `dataChanged` on the kind role instead of a Delete+Insert pair, so the
/// same delegate instance survives the transition. Cross-class changes
/// (e.g. paragraph→hr) still go through Delete+Insert.
///
/// Spec §4.2 (docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md).
QString delegateClassFor(const QString &kind);

}  // namespace Markoff::Live
