// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

struct MARKOFF_FOUNDATION_EXPORT CompletionCandidate {
    QString display;
    QString insertion;
    QString detail;
    QString iconName;
    int     priority = 0;
};

}  // namespace Markoff
