// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/FoldingTypes.h>
#include <markoff-parser/Document.h>
#include <QHash>
#include <QRegularExpression>

namespace Markoff {

QString normalizeHeadingText(const QString &raw) {
    QString text = raw.trimmed();
    // Strip common inline markdown: **bold**, *italic*, _emph_, `code`,
    // ~~strike~~. Keep link text (drop brackets/URLs).
    static const QRegularExpression re(
        R"((\*\*|__|\*|_|`|~~)(.+?)\1|\[([^\]]+)\]\([^)]+\)|\[\[([^\]|]+)(?:\|([^\]]+))?\]\])");
    QString out;
    out.reserve(text.size());
    int pos = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        out += text.mid(pos, m.capturedStart() - pos);
        // Prefer the innermost captured text.
        for (int g = 2; g <= 5; ++g) {
            if (!m.captured(g).isNull()) { out += m.captured(g); break; }
        }
        pos = m.capturedEnd();
    }
    out += text.mid(pos);
    return out.trimmed();
}

QList<FoldRegionKey> computeHeadingPaths(const QList<HeadingInfo> &headings) {
    QList<FoldRegionKey> result;
    result.reserve(headings.size());

    // Stack of (level, normalized-text) capturing current ancestor chain.
    struct Frame { int level; QString text; };
    QList<Frame> ancestors;

    // Map of (parent-path-join, level, text) -> occurrence count, to assign #N.
    QHash<QString, int> occurrences;

    for (const auto &h : headings) {
        // Pop ancestors at >= current level.
        while (!ancestors.isEmpty() && ancestors.last().level >= h.level)
            ancestors.removeLast();

        QStringList path;
        for (const auto &a : ancestors) path << a.text;
        QString normText = normalizeHeadingText(h.text);

        QString key;
        for (const auto &p : path) { key += p; key += QLatin1Char('\x1f'); }
        key += QString::number(h.level);
        key += QLatin1Char('\x1f');
        key += normText;

        int &count = occurrences[key];
        ++count;

        QString finalText = (count == 1)
            ? normText
            : QStringLiteral("%1#%2").arg(normText).arg(count);

        path << finalText;
        result << path;
        ancestors.push_back(Frame{h.level, finalText});
    }
    return result;
}

} // namespace Markoff
