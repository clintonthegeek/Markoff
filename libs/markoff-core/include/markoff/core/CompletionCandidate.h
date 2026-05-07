// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

struct MARKOFF_CORE_EXPORT CompletionCandidate {
    QString display;
    QString insertion;
    QString detail;
    QString iconName;
    int     priority = 0;
};

}  // namespace Markoff
