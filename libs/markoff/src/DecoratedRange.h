// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_DECORATEDRANGE_H
#define MARKOFF_DECORATEDRANGE_H

#include <QString>
#include <QColor>

namespace Markoff {

/// A contiguous set of QTextBlocks that share visual decoration.
/// The text stays in the QTextDocument; decorations are painted
/// around it in paintEvent.
struct DecoratedRange {
    enum Type { CodeBlock, Callout, Blockquote, Table, HorizontalRule, Heading };

    Type type = CodeBlock;
    int firstBlock = -1;
    int lastBlock = -1;

    // Code block
    QString language;

    // Callout
    QString calloutType;
    QString calloutTitle;
    QColor calloutColor;

    // Blockquote (max depth in this range, for sizing)
    int blockquoteDepth = 0;

    // Heading
    QColor headingBackground;

    /// Get the accent color for a callout type
    static QColor colorForCalloutType(const QString &type);
};

} // namespace Markoff

#endif // MARKOFF_DECORATEDRANGE_H
