// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <crdt/Anchor.h>

#include <markoff-foundation/CompletionTrigger.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

struct MARKOFF_FOUNDATION_EXPORT CompletionContext {
    CompletionTrigger        trigger = CompletionTrigger::None;
    QString                  prefix;
    CollabText::Crdt::Anchor triggerStart;
    CollabText::Crdt::Anchor cursorAnchor;
    QString                  anchorContext;
    bool isActive() const { return trigger != CompletionTrigger::None; }
};

}  // namespace Markoff
