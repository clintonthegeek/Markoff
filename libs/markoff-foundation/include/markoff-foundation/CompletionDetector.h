// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <crdt/Anchor.h>

#include <markoff-foundation/CompletionContext.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

class MarkoffDocument;

class MARKOFF_FOUNDATION_EXPORT CompletionDetector {
public:
    static CompletionContext
        detect(const MarkoffDocument *, const CollabText::Crdt::Anchor &cursor);
};

}  // namespace Markoff
