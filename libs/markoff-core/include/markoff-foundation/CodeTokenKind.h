// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

enum class CodeTokenKind {
    Default,
    Keyword, ControlFlow, Builtin,
    Type, Function, Variable, Constant,
    Operator, Punctuation,
    String, Number, Boolean,
    Comment, Documentation,
    Preprocessor, Annotation,
};

}  // namespace Markoff
