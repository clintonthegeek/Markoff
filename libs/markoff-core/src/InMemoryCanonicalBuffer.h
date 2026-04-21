// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/CanonicalBuffer.h>
#include <QHash>

namespace Markoff {

class InMemoryCanonicalBuffer final : public CanonicalBuffer {
public:
    InMemoryCanonicalBuffer();
    ~InMemoryCanonicalBuffer() override;

    const QString &toMarkdown() const override;
    qsizetype      length() const override;
    QString        substring(qsizetype offset, qsizetype len) const override;

    void applyDelta(qsizetype offset,
                    qsizetype removedLength,
                    const QString &inserted) override;
    void reset(const QString &newContent) override;

    quint64   createAnchor(qsizetype offset, CursorBias bias) override;
    qsizetype resolveAnchor(quint64 handle) const override;
    void      releaseAnchor(quint64 handle) override;

private:
    struct Anchor {
        qsizetype  offset;
        CursorBias bias;
    };

    QString                m_text;
    QHash<quint64, Anchor> m_anchors;
    quint64                m_nextHandle = 1;
};

} // namespace Markoff
