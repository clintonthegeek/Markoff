// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <markoff/MarkoffCoreExport.h>

namespace Markoff {

enum class CursorBias { Left, Right };

class MARKOFF_CORE_EXPORT CanonicalBuffer {
public:
    virtual ~CanonicalBuffer() = default;

    virtual const QString &toMarkdown() const = 0;
    virtual qsizetype      length() const = 0;
    virtual QString        substring(qsizetype offset, qsizetype len) const = 0;

    virtual void applyDelta(qsizetype offset,
                            qsizetype removedLength,
                            const QString &inserted) = 0;
    virtual void reset(const QString &newContent) = 0;

    virtual quint64   createAnchor(qsizetype offset, CursorBias bias) = 0;
    virtual qsizetype resolveAnchor(quint64 handle) const = 0;
    virtual void      releaseAnchor(quint64 handle) = 0;
};

} // namespace Markoff
