// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/CompletionContext.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/TextAnchor.h>

namespace Markoff {

class MarkoffDocument;

class MARKOFF_CORE_EXPORT CompletionDetector {
public:
    static CompletionContext
        detect(const MarkoffDocument *, TextAnchor cursor);
};

}  // namespace Markoff
