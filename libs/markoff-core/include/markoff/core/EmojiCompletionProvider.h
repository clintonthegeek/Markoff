// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/CompletionProvider.h>

namespace Markoff {

class MARKOFF_CORE_EXPORT EmojiCompletionProvider : public CompletionProvider {
    Q_OBJECT
public:
    explicit EmojiCompletionProvider(QObject *parent = nullptr);

    QSet<CompletionTrigger> handledTriggers() const override;
    QList<CompletionCandidate>
        candidatesFor(const CompletionContext &, quint64 requestId) override;
};

}  // namespace Markoff
