// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_MATHTEXTOBJECT_H
#define MARKOFF_MATHTEXTOBJECT_H

#include <QObject>
#include <QTextFormat>
#include <QTextObjectInterface>

namespace Markoff {

/// Custom text object that renders inline LaTeX math via JKQTMathText.
///
/// Usage: register one instance with each QTextDocument's documentLayout
/// under the type id MathTextObject::TypeId. To embed math, insert a
/// QChar::ObjectReplacementCharacter (U+FFFC) into the document and apply
/// a QTextCharFormat whose objectType == TypeId, with the math source
/// stored in the SourceProperty and the display flag in DisplayProperty.
///
/// The text engine will then call intrinsicSize/drawObject for that
/// character, sizing the line to fit the rendered glyph.
class MathTextObject : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    /// Object type id used in QTextCharFormat::setObjectType().
    static constexpr int TypeId = QTextFormat::UserObject + 1;

    /// Property holding the LaTeX source (without surrounding $/$$ delimiters).
    static constexpr int SourceProperty = QTextFormat::UserProperty + 1;

    /// Property holding the original delimiter form, which is what
    /// `$x^2$` or `$$x^2$$` looked like in the source markdown. We round-trip
    /// this through serialization so the original form is preserved.
    static constexpr int RawProperty = QTextFormat::UserProperty + 2;

    /// Property holding bool: true for display math ($$...$$), false for inline.
    static constexpr int DisplayProperty = QTextFormat::UserProperty + 3;

    explicit MathTextObject(QObject *parent = nullptr);

    QSizeF intrinsicSize(QTextDocument *doc, int posInDocument,
                          const QTextFormat &format) override;

    void drawObject(QPainter *painter, const QRectF &rect,
                     QTextDocument *doc, int posInDocument,
                     const QTextFormat &format) override;
};

} // namespace Markoff

#endif // MARKOFF_MATHTEXTOBJECT_H
