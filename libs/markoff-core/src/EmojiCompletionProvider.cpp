// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/EmojiCompletionProvider.h>

#include "EmojiData.h"

namespace Markoff {

EmojiCompletionProvider::EmojiCompletionProvider(QObject *parent)
    : CompletionProvider(parent) {}

QSet<CompletionTrigger> EmojiCompletionProvider::handledTriggers() const
{
    return { CompletionTrigger::Emoji };
}

QList<CompletionCandidate>
EmojiCompletionProvider::candidatesFor(const CompletionContext &ctx, quint64)
{
    QList<CompletionCandidate> out;
    if (ctx.trigger != CompletionTrigger::Emoji) return out;
    const QString prefix = ctx.prefix;
    for (const auto &e : Detail::kEmojis) {
        const QString sc = QString::fromLatin1(e.shortcode);
        if (sc.startsWith(prefix, Qt::CaseInsensitive)) {
            CompletionCandidate c;
            c.display   = QStringLiteral(":%1: %2").arg(sc,
                                              QString::fromUtf8(e.glyph));
            c.insertion = QStringLiteral(":%1:").arg(sc);
            c.detail    = sc;
            out << c;
        }
    }
    return out;
}

}  // namespace Markoff
