// SPDX-License-Identifier: GPL-3.0-or-later
// Forked from Qt's QWidgetTextControl (QWidgetTextControlPrivate)
// Original: Copyright (C) The Qt Company Ltd. (GPL-2.0-only OR GPL-3.0-only)

#ifndef MARKOFF_TEXTCONTROL_P_H
#define MARKOFF_TEXTCONTROL_P_H

#include <QtGui/qtextdocumentfragment.h>
#include <QtGui/qtextcursor.h>
#include <QtGui/qtextformat.h>
#include <QtGui/qtextobject.h>
#include <QtGui/qabstracttextdocumentlayout.h>
#include <QtCore/qbasictimer.h>
#include <QtCore/qpointer.h>
#include <QtWidgets/qmenu.h>

class QMimeData;

namespace Markoff {

class TextControl;

// Plain struct replacing QWidgetTextControlPrivate : QObjectPrivate
struct TextControlPrivate {
    TextControl *q = nullptr;

    // --- Methods ---
    bool cursorMoveKeyEvent(QKeyEvent *e);
    void updateCurrentCharFormat();
    void init(const QString &text = QString(), QTextDocument *document = nullptr);
    void setContent(const QString &text = QString(), QTextDocument *document = nullptr);
    void startDrag();
    void paste(const QMimeData *source);
    void setCursorPosition(const QPointF &pos);
    void setCursorPosition(int pos, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);
    void repaintCursor();
    void repaintSelection() { repaintOldAndNewSelection(QTextCursor()); }
    void repaintOldAndNewSelection(const QTextCursor &oldSelection);
    void selectionChanged(bool forceEmitSelectionChanged = false);
    void _q_updateCurrentCharFormatAndSelection();
#ifndef QT_NO_CLIPBOARD
    void setClipboardSelection();
#endif
    void _q_emitCursorPosChanged(const QTextCursor &someCursor);
    void _q_contentsChanged(int from, int charsRemoved, int charsAdded);
    void setCursorVisible(bool visible);
    void setBlinkingCursorEnabled(bool enable);
    void updateCursorBlinking();
    void extendWordwiseSelection(int suggestedNewPosition, qreal mouseXPosition);
    void extendBlockwiseSelection(int suggestedNewPosition);
    void _q_deleteSelected();
    void _q_setCursorAfterUndoRedo(int undoPosition, int charsAdded, int charsRemoved);
    QRectF cursorRectPlusUnicodeDirectionMarkers(const QTextCursor &cursor) const;
    QRectF rectForPosition(int position) const;
    QRectF selectionRect(const QTextCursor &cursor) const;
    QRectF selectionRect() const { return selectionRect(this->cursor); }
    void keyPressEvent(QKeyEvent *e);
    void mousePressEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
                         Qt::KeyboardModifiers modifiers,
                         Qt::MouseButtons buttons,
                         const QPointF &globalPos);
    void mouseMoveEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
                        Qt::KeyboardModifiers modifiers,
                        Qt::MouseButtons buttons,
                        const QPointF &globalPos);
    void mouseReleaseEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
                           Qt::KeyboardModifiers modifiers,
                           Qt::MouseButtons buttons,
                           const QPointF &globalPos);
    void mouseDoubleClickEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
                               Qt::KeyboardModifiers modifiers,
                               Qt::MouseButtons buttons,
                               const QPointF &globalPos);
    bool sendMouseEventToInputContext(QEvent *e, QEvent::Type eventType, Qt::MouseButton button,
                                      const QPointF &pos,
                                      Qt::KeyboardModifiers modifiers,
                                      Qt::MouseButtons buttons,
                                      const QPointF &globalPos);
    void contextMenuEvent(const QPointF &screenPos, const QPointF &docPos, QWidget *contextWidget);
    void focusEvent(QFocusEvent *e);
    bool dragEnterEvent(QEvent *e, const QMimeData *mimeData);
    void dragLeaveEvent();
    bool dragMoveEvent(QEvent *e, const QMimeData *mimeData, const QPointF &pos);
    bool dropEvent(const QMimeData *mimeData, const QPointF &pos, Qt::DropAction dropAction, QObject *source);
    void inputMethodEvent(QInputMethodEvent *e);
    void activateLinkUnderCursor(QString href = QString());
#if QT_CONFIG(tooltip)
    void showToolTip(const QPoint &globalPos, const QPointF &pos, QWidget *contextWidget);
#endif
    bool isPreediting() const;
    void commitPreedit();
    void insertParagraphSeparator();
    void append(const QString &text, Qt::TextFormat format = Qt::AutoText);

    QString anchorForCursor(const QTextCursor &anchor) const;
    void updateHighlightedAnchor(QPointF mousePos);
    void resetHighlightedAnchor();
    void _q_updateBlock(const QTextBlock &);
    void _q_documentLayoutChanged();
    void _q_copyLink();

    // --- State ---
    QTextDocument *doc = nullptr;
    bool cursorOn = false;
    bool cursorVisible = false;
    QTextCursor cursor;
    bool cursorIsFocusIndicator = false;
    QTextCharFormat lastCharFormat;
    QTextCursor dndFeedbackCursor;
    Qt::TextInteractionFlags interactionFlags = Qt::TextEditorInteraction;
    QBasicTimer cursorBlinkTimer;
    QBasicTimer trippleClickTimer;
    QPointF trippleClickPoint;
    bool dragEnabled = true;
    bool mousePressed = false;
    bool mightStartDrag = false;
    QPointF mousePressPos;
    QPointer<QWidget> contextWidget;
    int lastSelectionPosition = 0;
    int lastSelectionAnchor = 0;
    bool ignoreAutomaticScrollbarAdjustement = false;
    QTextCursor selectedWordOnDoubleClick;
    QTextCursor selectedBlockOnTrippleClick;
    bool overwriteMode = false;
    bool acceptRichText = false; // plain text only for Markoff
    int preeditCursor = 0;
    bool hideCursor = false;
    QList<QAbstractTextDocumentLayout::Selection> extraSelections;
    QPalette palette;
    bool hasFocus = false;
    bool isEnabled = true;
    QString highlightedAnchor;
    QString anchorOnMousePress;
    QTextBlock blockWithMarkerUnderMouse;
    bool hadSelectionOnMousePress = false;
    bool ignoreUnusedNavigationEvents = false;
    bool openExternalLinks = false;
    bool wordSelectionEnabled = false;
    QString linkToCopy;
};

} // namespace Markoff

#endif // MARKOFF_TEXTCONTROL_P_H
