// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

#include <markoff-foundation/MarkoffFoundationExport.h>
#include <markoff-foundation/RenderedBlock.h>

namespace Markoff {

class Theme;

class MARKOFF_FOUNDATION_EXPORT CodeBlockProcessor {
public:
    virtual ~CodeBlockProcessor() = default;
    virtual QString       language() const = 0;
    virtual RenderedBlock render(const QByteArray &contentUtf8,
                                  const Theme &theme) = 0;
};

}  // namespace Markoff
