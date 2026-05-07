// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-foundation/CompletionContext.h>
#include <markoff-foundation/MarkoffFoundationExport.h>
#include <markoff-foundation/TextAnchor.h>

namespace Markoff {

class MarkoffDocument;

class MARKOFF_FOUNDATION_EXPORT CompletionDetector {
public:
    static CompletionContext
        detect(const MarkoffDocument *, TextAnchor cursor);
};

}  // namespace Markoff
