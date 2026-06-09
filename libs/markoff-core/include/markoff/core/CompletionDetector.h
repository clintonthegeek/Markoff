// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/CompletionContext.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/TextAnchor.h>

namespace Markoff {

class MarkoffDocument;

class MARKOFF_CORE_EXPORT CompletionDetector {
public:
    /// LEGACY coordinate space: reads `toMarkdown()`/`toMarkdownUtf8()` (the
    /// legacy flat buffer) and resolves the cursor anchor against it. On a
    /// D2-loaded document the legacy buffer is stale-or-empty, so detection
    /// fails. No production caller remains (the live leaf's completion went
    /// with the retired view); a D2-native rewrite over
    /// `iterateBlocks()`/`blockText()` is required before any new consumer
    /// wires this up. Tracked in docs/queue.md.
    static CompletionContext
        detect(const MarkoffDocument *, TextAnchor cursor);
};

}  // namespace Markoff
