// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_CHECKBOXTEXTOBJECT_H
#define MARKOFF_CHECKBOXTEXTOBJECT_H

#include <QColor>
#include <QObject>
#include <QTextFormat>
#include <QTextObjectInterface>

namespace Markoff {

struct Theme;

/// Custom text object that renders task list checkboxes as icons.
///
/// Register with each QTextDocument's layout under CheckboxTextObject::TypeId.
/// Insert U+FFFC with objectType == TypeId and CheckedProperty set.
/// Click-to-toggle is handled by MarkdownTextItem::mousePressEvent().
class CheckboxTextObject : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    static constexpr int TypeId = QTextFormat::UserObject + 2;

    /// Whether the checkbox is checked. Stored as bool on the format.
    static constexpr int CheckedProperty = QTextFormat::UserProperty + 4;

    explicit CheckboxTextObject(QObject *parent = nullptr);

    /// Update the paint colors used for the checked fill, check-mark
    /// stroke, and unchecked outline. Reads `theme.paint`.
    void setTheme(const Theme &theme);

    QSizeF intrinsicSize(QTextDocument *doc, int posInDocument,
                          const QTextFormat &format) override;

    void drawObject(QPainter *painter, const QRectF &rect,
                     QTextDocument *doc, int posInDocument,
                     const QTextFormat &format) override;

private:
    QColor m_checkedFill;
    QColor m_checkMark;
    QColor m_uncheckedOutline;
};

} // namespace Markoff

#endif // MARKOFF_CHECKBOXTEXTOBJECT_H
