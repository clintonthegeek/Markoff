// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <markoff-foundation/BlockId.h>
#include <markoff-foundation/CompletionTrigger.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

struct MARKOFF_FOUNDATION_EXPORT CompletionContext {
    CompletionTrigger trigger         = CompletionTrigger::None;
    QString           prefix;
    BlockId           triggerBlock;
    uint32_t          triggerByteOffset = 0;
    BlockId           cursorBlock;
    uint32_t          cursorByteOffset  = 0;
    QString           anchorContext;
    bool isActive() const { return trigger != CompletionTrigger::None; }
};

}  // namespace Markoff
