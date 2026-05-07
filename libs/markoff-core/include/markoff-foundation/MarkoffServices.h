// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

class CodeBlockProcessorRegistry;
class CompletionRegistry;
class LinkService;
class SyntaxHighlightService;

struct MARKOFF_FOUNDATION_EXPORT MarkoffServices {
    SyntaxHighlightService     *syntax = nullptr;
    CodeBlockProcessorRegistry *codeProcessors = nullptr;
    LinkService                *links = nullptr;
    CompletionRegistry         *completion = nullptr;
};

}  // namespace Markoff
