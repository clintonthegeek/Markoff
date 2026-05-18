// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/Theme.h>
#include <QtQmlIntegration>

namespace Markoff::Live {

/// QML_FOREIGN shim exposing Markoff::Theme (a Q_GADGET in markoff-core)
/// as the QML type `Theme` in the org.markoff.live module. Theme is value-
/// only — QML cannot construct one. The `Q_ENUM(Slot)` on Markoff::Theme
/// is reachable as `Theme.<name>` in QML.
struct ThemeForeign {
    Q_GADGET
    QML_FOREIGN(Markoff::Theme)
    QML_NAMED_ELEMENT(Theme)
    QML_UNCREATABLE("Theme is a value type; use LiveListModelBinding.theme")
};

}  // namespace Markoff::Live
