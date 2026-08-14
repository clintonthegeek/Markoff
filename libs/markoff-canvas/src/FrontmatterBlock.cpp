// SPDX-License-Identifier: GPL-3.0-or-later
#include "FrontmatterBlock.h"

namespace Markoff::Canvas::Detail {

QList<FrontmatterProperty> parseFrontmatterProperties(const QString &rawYaml)
{
    QList<FrontmatterProperty> out;

    const QStringList lines = rawYaml.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        // A top-level scalar key has no leading whitespace (nested maps and
        // continuation lines are indented) and is not a list item.
        if (rawLine.isEmpty() || rawLine.at(0).isSpace())
            continue;
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char('-')))
            continue;

        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = line.left(colon).trimmed();
        if (key.isEmpty())
            continue;
        QString value = line.mid(colon + 1).trimmed();
        // Strip a single layer of matching quotes, YAML's simplest scalar
        // quoting form.
        if (value.size() >= 2
            && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
             || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\'')))) {
            value = value.mid(1, value.size() - 2);
        }
        out.append({key, value});
    }
    return out;
}

}  // namespace Markoff::Canvas::Detail
