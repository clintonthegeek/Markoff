// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.
//
// Pattern cross-reference: libs/markoff/src/MathTextObject.{h,cpp}.
// ReadingView cannot depend on Markoff (peer libraries), so the interface
// is duplicated here. The logic is also simpler: ReadingView math is
// static (no cursor-reveal), so we skip the on-demand-reveal machinery.

#ifndef CORBOMITE_READINGVIEW_READINGMATHOBJECT_H
#define CORBOMITE_READINGVIEW_READINGMATHOBJECT_H

#include <QObject>
#include <QTextFormat>
#include <QTextObjectInterface>

namespace Corbomite::ReadingView {

/// Custom QTextObject that renders inline LaTeX math via JKQTMathText.
///
/// Registered on each paragraph QTextDocument with
/// `document->documentLayout()->registerHandler(TypeId, ReadingMathObject*)`.
/// SpanRenderer inserts `QChar::ObjectReplacementCharacter` with a
/// QTextCharFormat whose `objectType == TypeId` and whose
/// `SourceProperty` holds the LaTeX source.
class ReadingMathObject : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    static constexpr int TypeId = QTextFormat::UserObject + 10;

    /// LaTeX source (without surrounding `$` / `$$`).
    static constexpr int SourceProperty = QTextFormat::UserProperty + 10;

    /// Display-mode flag (true for `$$...$$`, false for `$...$`).
    static constexpr int DisplayProperty = QTextFormat::UserProperty + 11;

    explicit ReadingMathObject(QObject *parent = nullptr);

    QSizeF intrinsicSize(QTextDocument *doc, int posInDocument,
                         const QTextFormat &format) override;

    void drawObject(QPainter *painter, const QRectF &rect,
                    QTextDocument *doc, int posInDocument,
                    const QTextFormat &format) override;
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_READINGMATHOBJECT_H
