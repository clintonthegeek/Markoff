// SPDX-License-Identifier: GPL-3.0-or-later
// Phase-A stub shim. See VaultResourceProvider.h for context.
#pragma once

#include <QString>

namespace Corbomite::Core {

class EmbedDepthGuard
{
public:
    static constexpr int kMaxDepth = 5;

    bool allow(int depth) const { return depth < kMaxDepth; }

    static QString placeholder(const QString &target)
    {
        return QStringLiteral("[embed depth cap: ") + target
               + QStringLiteral("]");
    }

    static QString placeholderTarget(const QString &placeholder)
    {
        const QString prefix = QStringLiteral("[embed depth cap: ");
        if (!placeholder.startsWith(prefix)) return {};
        if (!placeholder.endsWith(QLatin1Char(']'))) return {};
        return placeholder.mid(prefix.size(),
                               placeholder.size() - prefix.size() - 1);
    }
};

} // namespace Corbomite::Core
