// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/RenderedBlock.h>

namespace Markoff {

class Theme;

class MARKOFF_CORE_EXPORT CodeBlockProcessor {
public:
    virtual ~CodeBlockProcessor() = default;
    virtual QString       language() const = 0;
    virtual RenderedBlock render(const QByteArray &contentUtf8,
                                  const Theme &theme) = 0;
};

}  // namespace Markoff
