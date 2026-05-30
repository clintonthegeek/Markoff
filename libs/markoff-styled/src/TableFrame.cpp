// SPDX-License-Identifier: GPL-3.0-or-later
#include "TableFrame.h"

#include <QFont>
#include <QTextCharFormat>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextTable>
#include <QTextTableFormat>

#include <markoff/core/OpaqueBlockRenderer.h>  // OpaqueBlockKeyProperty

namespace Markoff::Styled {

namespace {

// Split a pipe-table row into trimmed cell strings, dropping the optional
// leading/trailing pipes. "| a | b |" → ["a","b"].
QStringList splitRow(const QString &line) {
    QString s = line.trimmed();
    if (s.startsWith(QLatin1Char('|'))) s.remove(0, 1);
    if (s.endsWith(QLatin1Char('|')))   s.chop(1);
    QStringList cells = s.split(QLatin1Char('|'));
    for (QString &c : cells) c = c.trimmed();
    return cells;
}

Qt::Alignment alignFor(const QString &spec) {
    const QString s = spec.trimmed();
    const bool l = s.startsWith(QLatin1Char(':'));
    const bool r = s.endsWith(QLatin1Char(':'));
    if (l && r) return Qt::AlignHCenter;
    if (r)      return Qt::AlignRight;
    return Qt::AlignLeft;  // ":---" and "---" both render left
}

// A GFM separator row: every cell is non-empty and consists only of ':' and
// '-', with at least one '-'.
bool isSeparatorRow(const QStringList &cells) {
    if (cells.isEmpty()) return false;
    for (const QString &c : cells) {
        const QString s = c.trimmed();
        if (s.isEmpty()) return false;
        bool dash = false;
        for (QChar ch : s) {
            if (ch != QLatin1Char(':') && ch != QLatin1Char('-')) return false;
            if (ch == QLatin1Char('-')) dash = true;
        }
        if (!dash) return false;
    }
    return true;
}

}  // namespace

ParsedTable parsePipeTable(const QByteArray &buffer) {
    ParsedTable t;
    const QString text = QString::fromUtf8(buffer);
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 2) return t;  // need header + separator

    const QStringList header = splitRow(lines.at(0));
    const QStringList sep    = splitRow(lines.at(1));
    if (header.isEmpty()) return t;
    if (!isSeparatorRow(sep)) return t;

    const int cols = header.size();
    t.header = header;
    for (int c = 0; c < cols; ++c)
        t.alignments.append(c < sep.size() ? alignFor(sep.at(c))
                                           : Qt::AlignLeft);

    for (int i = 2; i < lines.size(); ++i) {
        QStringList row = splitRow(lines.at(i));
        while (row.size() < cols) row.append(QString());
        if (row.size() > cols) row = row.mid(0, cols);
        t.body.append(row);
    }
    t.ok = true;
    return t;
}

int materializeTable(QTextCursor &at, const ParsedTable &t,
                     const QString &commentKey, qreal fontScale) {
    const int rows = 1 + static_cast<int>(t.body.size());
    const int cols = static_cast<int>(t.header.size());
    if (cols == 0) return 0;
    const int before = at.position();

    QTextTableFormat tf;
    tf.setBorder(1);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tf.setBorderCollapse(true);
    tf.setCellPadding(4.0 * fontScale);
    tf.setCellSpacing(0);
    tf.setProperty(Markoff::OpaqueBlockKeyProperty, commentKey);
    QTextTable *table = at.insertTable(rows, cols, tf);

    auto fill = [&](int r, int c, const QString &text, bool header) {
        QTextCursor cc = table->cellAt(r, c).firstCursorPosition();
        QTextBlockFormat bf = cc.blockFormat();
        if (c < t.alignments.size()) bf.setAlignment(t.alignments.at(c));
        cc.setBlockFormat(bf);
        QTextCharFormat cf;
        if (header) cf.setFontWeight(QFont::Bold);
        if (text.isEmpty()) cc.setBlockCharFormat(cf);
        else                cc.insertText(text, cf);
    };

    for (int c = 0; c < cols; ++c)
        fill(0, c, t.header.value(c), /*header=*/true);
    for (int r = 0; r < static_cast<int>(t.body.size()); ++r)
        for (int c = 0; c < cols; ++c)
            fill(r + 1, c, t.body.at(r).value(c), /*header=*/false);

    return at.position() - before;
}

}  // namespace Markoff::Styled
