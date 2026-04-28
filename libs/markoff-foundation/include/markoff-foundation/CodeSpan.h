// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

#include <markoff-foundation/CodeTokenKind.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

struct MARKOFF_FOUNDATION_EXPORT CodeSpan {
    quint32       offset = 0;     // UTF-8 byte offset within the code-block content
    quint32       length = 0;     // UTF-8 byte length
    CodeTokenKind kind = CodeTokenKind::Default;
};

}  // namespace Markoff
