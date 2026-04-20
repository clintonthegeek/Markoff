// SPDX-License-Identifier: GPL-3.0-or-later
// Forked from Qt's QWidgetTextControl
// Original: Copyright (C) The Qt Company Ltd. (GPL-2.0-only OR GPL-3.0-only)

#ifndef MARKOFF_TEXTCONTROL_H
#define MARKOFF_TEXTCONTROL_H

#include <QtGui/qtextdocument.h>
#include <QtGui/qtextoption.h>
#include <QtGui/qtextcursor.h>
#include <QtGui/qtextformat.h>
#include <QtWidgets/qtextedit.h>
#include <QtWidgets/qmenu.h>
#include <QtCore/qrect.h>
#include <QtGui/qabstracttextdocumentlayout.h>
#include <QtGui/qtextdocumentfragment.h>
#include <QtGui/qclipboard.h>
#include <QtCore/qmimedata.h>
#include <QObject>

namespace Markoff {

struct TextControlPrivate;

class TextControl : public QObject {
    Q_OBJECT
public:
    explicit TextControl(QObject *parent = nullptr);
    ~TextControl() override;

    void setDocument(QTextDocument *document);
    QTextDocument *document() const;

    void setTextCursor(const QTextCursor &cursor, bool selectionClipboard = false);
    QTextCursor textCursor() const;

    void setTextInteractionFlags(Qt::TextInteractionFlags flags);
    Qt::TextInteractionFlags textInteractionFlags() const;

    void mergeCurrentCharFormat(const QTextCharFormat &modifier);
    void setCurrentCharFormat(const QTextCharFormat &format);
    QTextCharFormat currentCharFormat() const;

    bool find(const QString &exp, QTextDocument::FindFlags options = {});
#if QT_CONFIG(regularexpression)
    bool find(const QRegularExpression &exp, QTextDocument::FindFlags options = {});
#endif

    QString toPlainText() const;

    virtual void ensureCursorVisible();

    QMenu *createStandardContextMenu(const QPointF &pos, QWidget *parent);

    QTextCursor cursorForPosition(const QPointF &pos) const;
    QRectF cursorRect(const QTextCursor &cursor) const;
    QRectF cursorRect() const;
    QRectF selectionRect(const QTextCursor &cursor) const;
    QRectF selectionRect() const;

    virtual QString anchorAt(const QPointF &pos) const;

    bool overwriteMode() const;
    void setOverwriteMode(bool overwrite);

    int cursorWidth() const;
    void setCursorVisible(bool visible);
    void setCursorWidth(int width);

    void setAcceptRichText(bool accept);
    bool acceptRichText() const;

    void setExtraSelections(const QList<QTextEdit::ExtraSelection> &selections);
    QList<QTextEdit::ExtraSelection> extraSelections() const;

    void moveCursor(QTextCursor::MoveOperation op, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);

    bool canPaste() const;

    virtual int hitTest(const QPointF &point, Qt::HitTestAccuracy accuracy) const;
    virtual QRectF blockBoundingRect(const QTextBlock &block) const;
    QAbstractTextDocumentLayout::PaintContext getPaintContext(QWidget *widget) const;

    // --- Replacement for isAcceptableInput ---
    bool isAcceptableInput(const QKeyEvent *event) const;
    static bool isCommonTextEditShortcut(const QKeyEvent *ke);

public Q_SLOTS:
    void setPlainText(const QString &text);

#ifndef QT_NO_CLIPBOARD
    void cut();
    void copy();
    void paste(QClipboard::Mode mode = QClipboard::Clipboard);
#endif

    void undo();
    void redo();
    void clear();
    void selectAll();

    void insertPlainText(const QString &text);
    void append(const QString &text);
    void appendPlainText(const QString &text);

Q_SIGNALS:
    void textChanged();
    void undoAvailable(bool b);
    void redoAvailable(bool b);
    void copyAvailable(bool b);
    void selectionChanged();
    void cursorPositionChanged();

    void updateRequest(const QRectF &rect = QRectF());
    void documentSizeChanged(const QSizeF &);
    void blockCountChanged(int newBlockCount);
    void visibilityRequest(const QRectF &rect);
    void microFocusChanged();
    void linkActivated(const QString &link);
    void linkHovered(const QString &);
    void modificationChanged(bool m);

public:
    QPalette palette() const;
    void setPalette(const QPalette &pal);

    void processEvent(QEvent *e, const QTransform &transform, QWidget *contextWidget = nullptr);
    void processEvent(QEvent *e, const QPointF &coordinateOffset = QPointF(), QWidget *contextWidget = nullptr);

    void drawContents(QPainter *painter, const QRectF &rect = QRectF(), QWidget *widget = nullptr);

    void setFocus(bool focus, Qt::FocusReason = Qt::OtherFocusReason);

    virtual QVariant inputMethodQuery(Qt::InputMethodQuery property, QVariant argument) const;

    virtual QMimeData *createMimeDataFromSelection() const;
    virtual bool canInsertFromMimeData(const QMimeData *source) const;
    virtual void insertFromMimeData(const QMimeData *source);

protected:
    void timerEvent(QTimerEvent *e) override;
    bool event(QEvent *e) override;

private:
    Q_DISABLE_COPY_MOVE(TextControl)
    std::unique_ptr<TextControlPrivate> d;
    friend struct TextControlPrivate;
};

} // namespace Markoff

#endif // MARKOFF_TEXTCONTROL_H
