// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include <QLatin1Char>
#include <QString>

namespace Markoff {

/// Recursion-depth cap for embed dispatch. Restored 2026-05-20 driven by
/// Corbomite port pull on EmbedRegistry. Hosts that walk embeds recursively
/// (e.g. `![[Page]]` where Page itself contains `![[Other]]`) use this to
/// short-circuit cycles and very deep trees.
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

} // namespace Markoff
