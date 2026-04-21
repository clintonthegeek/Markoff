// SPDX-License-Identifier: GPL-3.0-or-later
// Forked from Qt's QWidgetTextControl
// Original: Copyright (C) The Qt Company Ltd. (GPL-2.0-only OR GPL-3.0-only)

#include "TextControl.h"
#include "TextControl_p.h"

#include <qfont.h>
#include <qpainter.h>
#include <qevent.h>
#include <QGraphicsSceneEvent>
#include <qdebug.h>
#if QT_CONFIG(draganddrop)
#include <qdrag.h>
#endif
#include <qclipboard.h>
#include <qstyle.h>
#include <qapplication.h>
#include <qtextdocument.h>
#include <qtextlist.h>
#include <qpagedpaintdevice.h>
#include <qtextdocumentwriter.h>
#include <qstylehints.h>
#include <qtextformat.h>
#include <qdatetime.h>
#include <qbuffer.h>
#include <QLoggingCategory>
#include <algorithm>
#include <limits.h>
#include <qtexttable.h>
#include <qvariant.h>
#include <qurl.h>
#include <qdesktopservices.h>
#include <qinputmethod.h>
#if QT_CONFIG(tooltip)
#include <qtooltip.h>
#endif
#include <qstyleoption.h>
#include <QtGui/qaccessible.h>
#include <QtCore/qmetaobject.h>

#if QT_CONFIG(shortcut)
#include <qkeysequence.h>
#define ACCEL_KEY(k) (u'\t' + QKeySequence(k).toString(QKeySequence::NativeText))
#else
#define ACCEL_KEY(k) QString()
#endif

#include <algorithm>

namespace Markoff {

using namespace Qt::StringLiterals;

// Helper from Qt source
static QTextLine currentTextLine(const QTextCursor &cursor)
{
    const QTextBlock block = cursor.block();
    if (!block.isValid())
        return QTextLine();

    const QTextLayout *layout = block.layout();
    if (!layout)
        return QTextLine();

    const int relativePos = cursor.position() - block.position();
    return layout->lineForTextPosition(relativePos);
}

// ============================================================================
// TextControl
// ============================================================================

TextControl::TextControl(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<TextControlPrivate>())
{
    d->q = this;
    d->init();
}

TextControl::~TextControl()
{
}

void TextControl::setDocument(QTextDocument *document)
{
    if (d->doc == document)
        return;

    d->doc->disconnect(this);
    d->doc->documentLayout()->disconnect(this);
    d->doc->documentLayout()->setPaintDevice(nullptr);

    if (d->doc->parent() == this)
        delete d->doc;

    d->doc = nullptr;
    d->setContent(QString(), document);
}

QTextDocument *TextControl::document() const
{
    return d->doc;
}

void TextControl::setTextCursor(const QTextCursor &cursor, bool selectionClipboard)
{
    d->cursorIsFocusIndicator = false;
    const bool posChanged = cursor.position() != d->cursor.position();
    const QTextCursor oldSelection = d->cursor;
    d->cursor = cursor;
    d->cursorOn = d->hasFocus
            && (d->interactionFlags & (Qt::TextSelectableByKeyboard | Qt::TextEditable));
    d->_q_updateCurrentCharFormatAndSelection();
    ensureCursorVisible();
    d->repaintOldAndNewSelection(oldSelection);
    if (posChanged)
        emit cursorPositionChanged();

#ifndef QT_NO_CLIPBOARD
    if (selectionClipboard)
        d->setClipboardSelection();
#else
    Q_UNUSED(selectionClipboard);
#endif
}

QTextCursor TextControl::textCursor() const
{
    return d->cursor;
}

#ifndef QT_NO_CLIPBOARD
void TextControl::cut()
{
    if (!(d->interactionFlags & Qt::TextEditable) || !d->cursor.hasSelection())
        return;
    copy();
    d->cursor.removeSelectedText();
}

void TextControl::copy()
{
    if (!d->cursor.hasSelection())
        return;
    QMimeData *data = createMimeDataFromSelection();
    QGuiApplication::clipboard()->setMimeData(data);
}

void TextControl::paste(QClipboard::Mode mode)
{
    const QMimeData *md = QGuiApplication::clipboard()->mimeData(mode);
    if (md)
        insertFromMimeData(md);
}
#endif

void TextControl::clear()
{
    d->extraSelections.clear();
    d->setContent();
}

void TextControl::selectAll()
{
    const int selectionLength = qAbs(d->cursor.position() - d->cursor.anchor());
    const int oldCursorPos = d->cursor.position();
    d->cursor.select(QTextCursor::Document);
    d->selectionChanged(selectionLength != qAbs(d->cursor.position() - d->cursor.anchor()));
    d->cursorIsFocusIndicator = false;
    if (d->cursor.position() != oldCursorPos)
        emit cursorPositionChanged();
    emit updateRequest();
}

void TextControl::processEvent(QEvent *e, const QPointF &coordinateOffset, QWidget *contextWidget)
{
    QTransform t;
    t.translate(coordinateOffset.x(), coordinateOffset.y());
    processEvent(e, t, contextWidget);
}

void TextControl::processEvent(QEvent *e, const QTransform &transform, QWidget *contextWidget)
{
    if (d->interactionFlags == Qt::NoTextInteraction) {
        e->ignore();
        return;
    }

    d->contextWidget = contextWidget;

    switch (e->type()) {
        case QEvent::KeyPress:
            d->keyPressEvent(static_cast<QKeyEvent *>(e));
            break;
        case QEvent::MouseButtonPress: {
            QMouseEvent *ev = static_cast<QMouseEvent *>(e);
            d->mousePressEvent(ev, ev->button(), transform.map(ev->position()), ev->modifiers(),
                               ev->buttons(), ev->globalPosition());
            break; }
        case QEvent::Enter:
            d->updateHighlightedAnchor(transform.map(static_cast<QEnterEvent *>(e)->position()));
            break;
        case QEvent::Leave:
            d->resetHighlightedAnchor();
            break;
        case QEvent::MouseMove: {
            QMouseEvent *ev = static_cast<QMouseEvent *>(e);
            d->mouseMoveEvent(ev, ev->button(), transform.map(ev->position()), ev->modifiers(),
                              ev->buttons(), ev->globalPosition());
            break; }
        case QEvent::MouseButtonRelease: {
            QMouseEvent *ev = static_cast<QMouseEvent *>(e);
            d->mouseReleaseEvent(ev, ev->button(), transform.map(ev->position()), ev->modifiers(),
                                 ev->buttons(), ev->globalPosition());
            break; }
        case QEvent::MouseButtonDblClick: {
            QMouseEvent *ev = static_cast<QMouseEvent *>(e);
            d->mouseDoubleClickEvent(ev, ev->button(), transform.map(ev->position()), ev->modifiers(),
                                     ev->buttons(), ev->globalPosition());
            break; }
#if QT_CONFIG(graphicsview)
        case QEvent::GraphicsSceneMousePress: {
            QGraphicsSceneMouseEvent *ev = static_cast<QGraphicsSceneMouseEvent *>(e);
            d->mousePressEvent(ev, ev->button(), transform.map(ev->pos()), ev->modifiers(),
                               ev->buttons(), ev->screenPos());
            break; }
        case QEvent::GraphicsSceneMouseMove: {
            QGraphicsSceneMouseEvent *ev = static_cast<QGraphicsSceneMouseEvent *>(e);
            d->mouseMoveEvent(ev, ev->button(), transform.map(ev->pos()), ev->modifiers(),
                              ev->buttons(), ev->screenPos());
            break; }
        case QEvent::GraphicsSceneMouseRelease: {
            QGraphicsSceneMouseEvent *ev = static_cast<QGraphicsSceneMouseEvent *>(e);
            d->mouseReleaseEvent(ev, ev->button(), transform.map(ev->pos()), ev->modifiers(),
                                 ev->buttons(), ev->screenPos());
            break; }
        case QEvent::GraphicsSceneMouseDoubleClick: {
            QGraphicsSceneMouseEvent *ev = static_cast<QGraphicsSceneMouseEvent *>(e);
            d->mouseDoubleClickEvent(ev, ev->button(), transform.map(ev->pos()), ev->modifiers(),
                                     ev->buttons(), ev->screenPos());
            break; }
#endif
        case QEvent::InputMethod:
            d->inputMethodEvent(static_cast<QInputMethodEvent *>(e));
            break;
#ifndef QT_NO_CONTEXTMENU
        case QEvent::ContextMenu: {
            QContextMenuEvent *ev = static_cast<QContextMenuEvent *>(e);
            d->contextMenuEvent(ev->globalPos(), transform.map(ev->pos()), contextWidget);
            break; }
#endif
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            d->focusEvent(static_cast<QFocusEvent *>(e));
            break;

        case QEvent::EnabledChange:
            d->isEnabled = e->isAccepted();
            break;

#if QT_CONFIG(tooltip)
        case QEvent::ToolTip: {
            QHelpEvent *ev = static_cast<QHelpEvent *>(e);
            d->showToolTip(ev->globalPos(), transform.map(ev->pos()), contextWidget);
            break;
        }
#endif

#if QT_CONFIG(draganddrop)
        case QEvent::DragEnter: {
            QDragEnterEvent *ev = static_cast<QDragEnterEvent *>(e);
            if (d->dragEnterEvent(e, ev->mimeData()))
                ev->acceptProposedAction();
            break;
        }
        case QEvent::DragLeave:
            d->dragLeaveEvent();
            break;
        case QEvent::DragMove: {
            QDragMoveEvent *ev = static_cast<QDragMoveEvent *>(e);
            if (d->dragMoveEvent(e, ev->mimeData(), transform.map(ev->position())))
                ev->acceptProposedAction();
            break;
        }
        case QEvent::Drop: {
            QDropEvent *ev = static_cast<QDropEvent *>(e);
            if (d->dropEvent(ev->mimeData(), transform.map(ev->position()), ev->dropAction(), ev->source()))
                ev->acceptProposedAction();
            break;
        }
#endif

        case QEvent::ShortcutOverride:
            if (d->interactionFlags & Qt::TextEditable) {
                QKeyEvent* ke = static_cast<QKeyEvent *>(e);
                if (isCommonTextEditShortcut(ke))
                    ke->accept();
            }
            break;
        default:
            break;
    }
}

bool TextControl::event(QEvent *e)
{
    return QObject::event(e);
}

void TextControl::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == d->cursorBlinkTimer.timerId()) {
        d->cursorOn = !d->cursorOn;

        if (d->cursor.hasSelection())
            d->cursorOn &= (QApplication::style()->styleHint(QStyle::SH_BlinkCursorWhenTextSelected) != 0);

        d->repaintCursor();
    } else if (e->timerId() == d->trippleClickTimer.timerId()) {
        d->trippleClickTimer.stop();
    }
}

void TextControl::setPlainText(const QString &text)
{
    d->setContent(text);
}

void TextControl::undo()
{
    d->repaintSelection();
    const int oldCursorPos = d->cursor.position();
    d->doc->undo(&d->cursor);
    if (d->cursor.position() != oldCursorPos)
        emit cursorPositionChanged();
    emit microFocusChanged();
    ensureCursorVisible();
}

void TextControl::redo()
{
    d->repaintSelection();
    const int oldCursorPos = d->cursor.position();
    d->doc->redo(&d->cursor);
    if (d->cursor.position() != oldCursorPos)
        emit cursorPositionChanged();
    emit microFocusChanged();
    ensureCursorVisible();
}

// ============================================================================
// TextControlPrivate
// ============================================================================

bool TextControlPrivate::cursorMoveKeyEvent(QKeyEvent *e)
{
#ifdef QT_NO_SHORTCUT
    Q_UNUSED(e);
#endif

    if (cursor.isNull())
        return false;

    const QTextCursor oldSelection = cursor;
    const int oldCursorPos = cursor.position();

    QTextCursor::MoveMode mode = QTextCursor::MoveAnchor;
    QTextCursor::MoveOperation op = QTextCursor::NoMove;

    if (false) {
    }
#ifndef QT_NO_SHORTCUT
    if (e == QKeySequence::MoveToNextChar) {
        op = QTextCursor::Right;
    }
    else if (e == QKeySequence::MoveToPreviousChar) {
        op = QTextCursor::Left;
    }
    else if (e == QKeySequence::SelectNextChar) {
        op = QTextCursor::Right;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectPreviousChar) {
        op = QTextCursor::Left;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectNextWord) {
        op = QTextCursor::WordRight;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectPreviousWord) {
        op = QTextCursor::WordLeft;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectStartOfLine) {
        op = QTextCursor::StartOfLine;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectEndOfLine) {
        op = QTextCursor::EndOfLine;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectStartOfBlock) {
        op = QTextCursor::StartOfBlock;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectEndOfBlock) {
        op = QTextCursor::EndOfBlock;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectStartOfDocument) {
        op = QTextCursor::Start;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectEndOfDocument) {
        op = QTextCursor::End;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectPreviousLine) {
        op = QTextCursor::Up;
        mode = QTextCursor::KeepAnchor;
        {
            QTextBlock block = cursor.block();
            QTextLine line = currentTextLine(cursor);
            if (!block.previous().isValid()
                && line.isValid()
                && line.lineNumber() == 0)
                op = QTextCursor::Start;
        }
    }
    else if (e == QKeySequence::SelectNextLine) {
        op = QTextCursor::Down;
        mode = QTextCursor::KeepAnchor;
        {
            QTextBlock block = cursor.block();
            QTextLine line = currentTextLine(cursor);
            if (!block.next().isValid()
                && line.isValid()
                && line.lineNumber() == block.layout()->lineCount() - 1)
                op = QTextCursor::End;
        }
    }
    else if (e == QKeySequence::MoveToNextWord) {
        op = QTextCursor::WordRight;
    }
    else if (e == QKeySequence::MoveToPreviousWord) {
        op = QTextCursor::WordLeft;
    }
    else if (e == QKeySequence::MoveToEndOfBlock) {
        op = QTextCursor::EndOfBlock;
    }
    else if (e == QKeySequence::MoveToStartOfBlock) {
        op = QTextCursor::StartOfBlock;
    }
    else if (e == QKeySequence::MoveToNextLine) {
        op = QTextCursor::Down;
    }
    else if (e == QKeySequence::MoveToPreviousLine) {
        op = QTextCursor::Up;
    }
    else if (e == QKeySequence::MoveToStartOfLine) {
        op = QTextCursor::StartOfLine;
    }
    else if (e == QKeySequence::MoveToEndOfLine) {
        op = QTextCursor::EndOfLine;
    }
    else if (e == QKeySequence::MoveToStartOfDocument) {
        op = QTextCursor::Start;
    }
    else if (e == QKeySequence::MoveToEndOfDocument) {
        op = QTextCursor::End;
    }
#endif // QT_NO_SHORTCUT
    else {
        return false;
    }

    bool visualNavigation = cursor.visualNavigation();
    cursor.setVisualNavigation(true);
    const bool moved = cursor.movePosition(op, mode);
    cursor.setVisualNavigation(visualNavigation);
    q->ensureCursorVisible();

    bool ignoreNavigationEvents = ignoreUnusedNavigationEvents;
    bool isNavigationEvent = e->key() == Qt::Key_Up || e->key() == Qt::Key_Down;
    isNavigationEvent = isNavigationEvent || e->key() == Qt::Key_Left || e->key() == Qt::Key_Right;

    if (moved) {
        if (cursor.position() != oldCursorPos)
            emit q->cursorPositionChanged();
        emit q->microFocusChanged();
    } else if (ignoreNavigationEvents && isNavigationEvent && oldSelection.anchor() == cursor.anchor()) {
        return false;
    }

    selectionChanged(/*forceEmitSelectionChanged =*/(mode == QTextCursor::KeepAnchor));
    repaintOldAndNewSelection(oldSelection);

    return true;
}

void TextControlPrivate::updateCurrentCharFormat()
{
    QTextCharFormat fmt = cursor.charFormat();
    if (fmt == lastCharFormat)
        return;
    lastCharFormat = fmt;
    emit q->microFocusChanged();
}

void TextControlPrivate::init(const QString &text, QTextDocument *document)
{
    setContent(text, document);
    doc->setUndoRedoEnabled(interactionFlags & Qt::TextEditable);
    q->setCursorWidth(-1);
}

void TextControlPrivate::setContent(const QString &text, QTextDocument *document)
{
    const QTextCharFormat charFormatForInsertion = cursor.charFormat();

    bool clearDocument = true;
    if (!doc) {
        if (document) {
            doc = document;
        } else {
            palette = QApplication::palette("QWidgetTextControl");
            doc = new QTextDocument(q);
        }
        clearDocument = false;
        _q_documentLayoutChanged();
        cursor = QTextCursor(doc);

        QObject::connect(doc, &QTextDocument::contentsChanged, q,
                         [this]() { _q_updateCurrentCharFormatAndSelection(); });
        QObject::connect(doc, &QTextDocument::cursorPositionChanged, q,
                         [this](const QTextCursor &c) { _q_emitCursorPosChanged(c); });
        QObject::connect(doc, &QTextDocument::documentLayoutChanged, q,
                         [this]() { _q_documentLayoutChanged(); });

        QObject::connect(doc, &QTextDocument::undoAvailable, q, &TextControl::undoAvailable);
        QObject::connect(doc, &QTextDocument::redoAvailable, q, &TextControl::redoAvailable);
        QObject::connect(doc, &QTextDocument::modificationChanged, q,
                         &TextControl::modificationChanged);
        QObject::connect(doc, &QTextDocument::blockCountChanged, q,
                         &TextControl::blockCountChanged);
    }

    bool previousUndoRedoState = doc->isUndoRedoEnabled();
    if (!document)
        doc->setUndoRedoEnabled(false);

    // Avoid multiple textChanged() signals
    static int contentsChangedIndex = QMetaMethod::fromSignal(&QTextDocument::contentsChanged).methodIndex();
    static int textChangedIndex = QMetaMethod::fromSignal(&TextControl::textChanged).methodIndex();
    QMetaObject::disconnect(doc, contentsChangedIndex, q, textChangedIndex);

    if (!text.isEmpty()) {
        cursor = QTextCursor();
        QTextCursor formatCursor(doc);
        formatCursor.beginEditBlock();
        doc->setPlainText(text);
        doc->setUndoRedoEnabled(false);
        formatCursor.select(QTextCursor::Document);
        formatCursor.setCharFormat(charFormatForInsertion);
        formatCursor.endEditBlock();
        cursor = QTextCursor(doc);
    } else if (clearDocument) {
        doc->clear();
    }
    cursor.setCharFormat(charFormatForInsertion);

    QMetaObject::connect(doc, contentsChangedIndex, q, textChangedIndex);
    emit q->textChanged();
    if (!document)
        doc->setUndoRedoEnabled(previousUndoRedoState);
    _q_updateCurrentCharFormatAndSelection();
    if (!document)
        doc->setModified(false);

    q->ensureCursorVisible();
    emit q->cursorPositionChanged();

    QObject::connect(doc, &QTextDocument::contentsChange, q,
                     [this](int from, int removed, int added) { _q_contentsChanged(from, removed, added); });
}

void TextControlPrivate::startDrag()
{
#if QT_CONFIG(draganddrop)
    mousePressed = false;
    if (!contextWidget)
        return;
    QMimeData *data = q->createMimeDataFromSelection();

    QDrag *drag = new QDrag(contextWidget);
    drag->setMimeData(data);

    Qt::DropActions actions = Qt::CopyAction;
    Qt::DropAction action;
    if (interactionFlags & Qt::TextEditable) {
        actions |= Qt::MoveAction;
        action = drag->exec(actions, Qt::MoveAction);
    } else {
        action = drag->exec(actions, Qt::CopyAction);
    }

    if (action == Qt::MoveAction && drag->target() != contextWidget)
        cursor.removeSelectedText();
#endif
}

void TextControlPrivate::setCursorPosition(const QPointF &pos)
{
    const int cursorPos = q->hitTest(pos, Qt::FuzzyHit);
    if (cursorPos == -1)
        return;
    cursor.setPosition(cursorPos);
}

void TextControlPrivate::setCursorPosition(int pos, QTextCursor::MoveMode mode)
{
    cursor.setPosition(pos, mode);
    if (mode != QTextCursor::KeepAnchor) {
        selectedWordOnDoubleClick = QTextCursor();
        selectedBlockOnTrippleClick = QTextCursor();
    }
}

void TextControlPrivate::repaintCursor()
{
    emit q->updateRequest(cursorRectPlusUnicodeDirectionMarkers(cursor));
}

void TextControlPrivate::repaintOldAndNewSelection(const QTextCursor &oldSelection)
{
    if (cursor.hasSelection()
        && oldSelection.hasSelection()
        && cursor.currentFrame() == oldSelection.currentFrame()
        && !cursor.hasComplexSelection()
        && !oldSelection.hasComplexSelection()
        && cursor.anchor() == oldSelection.anchor()
        ) {
        QTextCursor differenceSelection(doc);
        differenceSelection.setPosition(oldSelection.position());
        differenceSelection.setPosition(cursor.position(), QTextCursor::KeepAnchor);
        emit q->updateRequest(q->selectionRect(differenceSelection));
    } else {
        if (!oldSelection.isNull())
            emit q->updateRequest(q->selectionRect(oldSelection) | cursorRectPlusUnicodeDirectionMarkers(oldSelection));
        emit q->updateRequest(q->selectionRect() | cursorRectPlusUnicodeDirectionMarkers(cursor));
    }
}

void TextControlPrivate::selectionChanged(bool forceEmitSelectionChanged)
{
    if (cursor.position() == lastSelectionPosition
        && cursor.anchor() == lastSelectionAnchor)
        return;

    bool selectionStateChange = (cursor.hasSelection()
                                 != (lastSelectionPosition != lastSelectionAnchor));
    if (selectionStateChange)
        emit q->copyAvailable(cursor.hasSelection());

    if (!forceEmitSelectionChanged
        && (selectionStateChange
            || (cursor.hasSelection()
                && (cursor.position() != lastSelectionPosition
                    || cursor.anchor() != lastSelectionAnchor)))) {
        emit q->selectionChanged();
#if QT_CONFIG(accessibility)
        if (q->parent() && q->parent()->isWidgetType()) {
            QAccessibleTextSelectionEvent ev(q->parent(), cursor.anchor(), cursor.position());
            QAccessible::updateAccessibility(&ev);
        }
#endif
    }
    emit q->microFocusChanged();
    lastSelectionPosition = cursor.position();
    lastSelectionAnchor = cursor.anchor();
}

void TextControlPrivate::_q_updateCurrentCharFormatAndSelection()
{
    updateCurrentCharFormat();
    selectionChanged();
}

#ifndef QT_NO_CLIPBOARD
void TextControlPrivate::setClipboardSelection()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!cursor.hasSelection() || !clipboard->supportsSelection())
        return;
    QMimeData *data = q->createMimeDataFromSelection();
    clipboard->setMimeData(data, QClipboard::Selection);
}
#endif

void TextControlPrivate::_q_emitCursorPosChanged(const QTextCursor &someCursor)
{
    if (someCursor.isCopyOf(cursor)) {
        emit q->cursorPositionChanged();
        emit q->microFocusChanged();
    }
}

void TextControlPrivate::_q_contentsChanged(int from, int charsRemoved, int charsAdded)
{
#if QT_CONFIG(accessibility)
    if (QAccessible::isActive() && q->parent() && q->parent()->isWidgetType()) {
        QTextCursor tmp(doc);
        tmp.setPosition(from);
        tmp.setPosition(qMin(doc->characterCount() - 1, from + charsAdded), QTextCursor::KeepAnchor);
        QString newText = tmp.selectedText();
        QString oldText = QString(charsRemoved, u' ');

        QAccessibleEvent *ev = nullptr;
        if (charsRemoved == 0) {
            ev = new QAccessibleTextInsertEvent(q->parent(), from, newText);
        } else if (charsAdded == 0) {
            ev = new QAccessibleTextRemoveEvent(q->parent(), from, oldText);
        } else {
            ev = new QAccessibleTextUpdateEvent(q->parent(), from, oldText, newText);
        }
        QAccessible::updateAccessibility(ev);
        delete ev;
    }
#else
    Q_UNUSED(from);
    Q_UNUSED(charsRemoved);
    Q_UNUSED(charsAdded);
#endif
}

void TextControlPrivate::_q_documentLayoutChanged()
{
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    QObject::connect(layout, &QAbstractTextDocumentLayout::update, q,
                     &TextControl::updateRequest);
    QObject::connect(layout, &QAbstractTextDocumentLayout::updateBlock, q,
                     [this](const QTextBlock &block) { _q_updateBlock(block); });
    QObject::connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged, q,
                     &TextControl::documentSizeChanged);
}

void TextControlPrivate::setCursorVisible(bool visible)
{
    if (cursorVisible == visible)
        return;

    cursorVisible = visible;
    updateCursorBlinking();

    if (cursorVisible)
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::cursorFlashTimeChanged,
                         q, [this]() { updateCursorBlinking(); });
    else
        QObject::disconnect(QGuiApplication::styleHints(), &QStyleHints::cursorFlashTimeChanged,
                            q, nullptr);
}

void TextControlPrivate::setBlinkingCursorEnabled(bool enable)
{
    // Alias for setCursorVisible in our simplified version
    setCursorVisible(enable);
}

void TextControlPrivate::updateCursorBlinking()
{
    cursorBlinkTimer.stop();
    if (cursorVisible) {
        int flashTime = QGuiApplication::styleHints()->cursorFlashTime();
        if (flashTime >= 2)
            cursorBlinkTimer.start(flashTime / 2, q);
    }

    cursorOn = cursorVisible;
    repaintCursor();
}

void TextControlPrivate::extendWordwiseSelection(int suggestedNewPosition, qreal mouseXPosition)
{
    // if inside the initial selected word keep that
    if (suggestedNewPosition >= selectedWordOnDoubleClick.selectionStart()
        && suggestedNewPosition <= selectedWordOnDoubleClick.selectionEnd()) {
        q->setTextCursor(selectedWordOnDoubleClick);
        return;
    }

    QTextCursor curs = selectedWordOnDoubleClick;
    curs.setPosition(suggestedNewPosition, QTextCursor::KeepAnchor);

    if (!curs.movePosition(QTextCursor::StartOfWord))
        return;
    const int wordStartPos = curs.position();

    const int blockPos = curs.block().position();
    const QPointF blockCoordinates = q->blockBoundingRect(curs.block()).topLeft();

    QTextLine line = currentTextLine(curs);
    if (!line.isValid())
        return;

    const qreal wordStartX = line.cursorToX(curs.position() - blockPos) + blockCoordinates.x();

    if (!curs.movePosition(QTextCursor::EndOfWord))
        return;
    const int wordEndPos = curs.position();

    const QTextLine otherLine = currentTextLine(curs);
    if (otherLine.textStart() != line.textStart()
        || wordEndPos == wordStartPos)
        return;

    const qreal wordEndX = line.cursorToX(curs.position() - blockPos) + blockCoordinates.x();

    if (!wordSelectionEnabled && (mouseXPosition < wordStartX || mouseXPosition > wordEndX))
        return;

    if (wordSelectionEnabled) {
        if (suggestedNewPosition < selectedWordOnDoubleClick.position()) {
            cursor.setPosition(selectedWordOnDoubleClick.selectionEnd());
            setCursorPosition(wordStartPos, QTextCursor::KeepAnchor);
        } else {
            cursor.setPosition(selectedWordOnDoubleClick.selectionStart());
            setCursorPosition(wordEndPos, QTextCursor::KeepAnchor);
        }
    } else {
        if (suggestedNewPosition < selectedWordOnDoubleClick.position())
            cursor.setPosition(selectedWordOnDoubleClick.selectionEnd());
        else
            cursor.setPosition(selectedWordOnDoubleClick.selectionStart());

        const qreal differenceToStart = mouseXPosition - wordStartX;
        const qreal differenceToEnd = wordEndX - mouseXPosition;

        if (differenceToStart < differenceToEnd)
            setCursorPosition(wordStartPos, QTextCursor::KeepAnchor);
        else
            setCursorPosition(wordEndPos, QTextCursor::KeepAnchor);
    }

    if (interactionFlags & Qt::TextSelectableByMouse) {
#ifndef QT_NO_CLIPBOARD
        setClipboardSelection();
#endif
        selectionChanged(true);
    }
}

void TextControlPrivate::extendBlockwiseSelection(int suggestedNewPosition)
{
    // if inside the initial selected line keep that
    if (suggestedNewPosition >= selectedBlockOnTrippleClick.selectionStart()
        && suggestedNewPosition <= selectedBlockOnTrippleClick.selectionEnd()) {
        q->setTextCursor(selectedBlockOnTrippleClick);
        return;
    }

    if (suggestedNewPosition < selectedBlockOnTrippleClick.position()) {
        cursor.setPosition(selectedBlockOnTrippleClick.selectionEnd());
        cursor.setPosition(suggestedNewPosition, QTextCursor::KeepAnchor);
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(selectedBlockOnTrippleClick.selectionStart());
        cursor.setPosition(suggestedNewPosition, QTextCursor::KeepAnchor);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    }

    if (interactionFlags & Qt::TextSelectableByMouse) {
#ifndef QT_NO_CLIPBOARD
        setClipboardSelection();
#endif
        selectionChanged(true);
    }
}

void TextControlPrivate::_q_deleteSelected()
{
    if (!(interactionFlags & Qt::TextEditable) || !cursor.hasSelection())
        return;
    cursor.removeSelectedText();
}

void TextControlPrivate::_q_setCursorAfterUndoRedo(int, int, int)
{
    // Stub - not needed for plain text
}

void TextControlPrivate::keyPressEvent(QKeyEvent *e)
{
#ifndef QT_NO_SHORTCUT
    if (e == QKeySequence::SelectAll) {
        e->accept();
        q->selectAll();
#ifndef QT_NO_CLIPBOARD
        setClipboardSelection();
#endif
        return;
    }
#ifndef QT_NO_CLIPBOARD
    else if (e == QKeySequence::Copy) {
        e->accept();
        q->copy();
        return;
    }
#endif
#endif // QT_NO_SHORTCUT

    // Table cell navigation
    QTextTable *table = cursor.currentTable();
    if (table) {
        if (e->key() == Qt::Key_Tab && !(e->modifiers() & Qt::ShiftModifier)) {
            // Tab: move to next cell, insert row if at last cell
            QTextTableCell cell = table->cellAt(cursor);
            int row = cell.row();
            int col = cell.column() + cell.columnSpan();
            if (col >= table->columns()) {
                col = 0;
                ++row;
            }
            if (row >= table->rows()) {
                table->insertRows(table->rows(), 1);
                row = table->rows() - 1;
                col = 0;
            }
            QTextTableCell target = table->cellAt(row, col);
            cursor = target.firstCursorPosition();
            cursor.setPosition(target.lastCursorPosition().position(),
                               QTextCursor::KeepAnchor);
            q->ensureCursorVisible();
            e->accept();
            goto accept;
        }
        // Shift+Tab / Backtab: move to previous cell
        if ((e->key() == Qt::Key_Tab && (e->modifiers() & Qt::ShiftModifier))
            || e->key() == Qt::Key_Backtab) {
            QTextTableCell cell = table->cellAt(cursor);
            int row = cell.row();
            int col = cell.column() - 1;
            if (col < 0) {
                col = table->columns() - 1;
                --row;
            }
            if (row >= 0) {
                QTextTableCell target = table->cellAt(row, col);
                cursor = target.firstCursorPosition();
                cursor.setPosition(target.lastCursorPosition().position(),
                                   QTextCursor::KeepAnchor);
                q->ensureCursorVisible();
            }
            e->accept();
            goto accept;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            QTextTableCell cell = table->cellAt(cursor);
            int row = cell.row();
            int col = cell.column();
            if (row == table->rows() - 1) {
                // Last row — insert new row and move there
                table->insertRows(row + 1, 1);
                cursor = table->cellAt(row + 1, col).firstCursorPosition();
            } else {
                // Middle row — move to same column in next row, select content
                QTextTableCell target = table->cellAt(row + 1, col);
                cursor = target.firstCursorPosition();
                cursor.setPosition(target.lastCursorPosition().position(),
                                   QTextCursor::KeepAnchor);
            }
            q->ensureCursorVisible();
            e->accept();
            goto accept;
        }
        if (e->key() == Qt::Key_Escape) {
            // Escape: move cursor out of table (to block after table)
            QTextTableCell lastCell = table->cellAt(table->rows() - 1,
                                                     table->columns() - 1);
            cursor = lastCell.lastCursorPosition();
            cursor.movePosition(QTextCursor::NextBlock);
            // If still in table or at document end, insert a blank line
            if (cursor.currentTable() || cursor.atEnd()) {
                cursor.setPosition(table->lastPosition() + 1);
                cursor.insertBlock();
            }
            q->ensureCursorVisible();
            e->accept();
            goto accept;
        }
    }

    if (interactionFlags & Qt::TextSelectableByKeyboard
        && cursorMoveKeyEvent(e))
        goto accept;

    if (interactionFlags & Qt::LinksAccessibleByKeyboard) {
        if ((e->key() == Qt::Key_Return
             || e->key() == Qt::Key_Enter)
            && cursor.hasSelection()) {
            e->accept();
            activateLinkUnderCursor();
            return;
        }
    }

    if (!(interactionFlags & Qt::TextEditable)) {
        e->ignore();
        return;
    }

    if (e->key() == Qt::Key_Direction_L || e->key() == Qt::Key_Direction_R) {
        QTextBlockFormat fmt;
        fmt.setLayoutDirection((e->key() == Qt::Key_Direction_L) ? Qt::LeftToRight : Qt::RightToLeft);
        cursor.mergeBlockFormat(fmt);
        goto accept;
    }

    repaintSelection();

    // Table destruction: backspace at start of line after a table
    if (e->key() == Qt::Key_Backspace && !cursor.hasSelection()
        && !(e->modifiers() & ~(Qt::ShiftModifier | Qt::GroupSwitchModifier))) {
        if (cursor.atBlockStart()) {
            // Check if previous block is inside a table
            QTextBlock prevBlock = cursor.block().previous();
            if (prevBlock.isValid()) {
                QTextCursor probe(prevBlock);
                QTextTable *table = probe.currentTable();
                if (table) {
                    // Select the entire table frame
                    int frameStart = table->firstPosition() - 1;
                    int frameEnd = table->lastPosition() + 1;
                    cursor.setPosition(frameStart);
                    cursor.setPosition(frameEnd, QTextCursor::KeepAnchor);
                    repaintSelection();
                    e->accept();
                    goto accept;
                }
            }
        }
    }

    // Backspace with table selected: delete the table
    if (e->key() == Qt::Key_Backspace && cursor.hasSelection()
        && !(e->modifiers() & ~(Qt::ShiftModifier | Qt::GroupSwitchModifier))) {
        // Check if the selection spans a table frame
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        QTextBlock startBlock = cursor.document()->findBlock(start + 1);
        if (startBlock.isValid()) {
            QTextCursor startProbe(startBlock);
            QTextTable *table = startProbe.currentTable();
            if (table && table->lastPosition() < end) {
                // Selection spans the table — delete it
                cursor.removeSelectedText();
                repaintSelection();
                e->accept();
                goto accept;
            }
        }
    }

    if (e->key() == Qt::Key_Backspace && !(e->modifiers() & ~(Qt::ShiftModifier | Qt::GroupSwitchModifier))) {
        QTextBlockFormat blockFmt = cursor.blockFormat();
        QTextList *list = cursor.currentList();
        if (list && cursor.atBlockStart() && !cursor.hasSelection()) {
            list->remove(cursor.block());
        } else if (cursor.atBlockStart() && blockFmt.indent() > 0) {
            blockFmt.setIndent(blockFmt.indent() - 1);
            cursor.setBlockFormat(blockFmt);
        } else {
            QTextCursor localCursor = cursor;
            localCursor.deletePreviousChar();
            // cursor.d->setX() removed: private API, optimization only
        }
        goto accept;
    }
#ifndef QT_NO_SHORTCUT
    else if (e == QKeySequence::InsertParagraphSeparator) {
        insertParagraphSeparator();
        e->accept();
        goto accept;
    } else if (e == QKeySequence::InsertLineSeparator) {
        cursor.insertText(QString(QChar::LineSeparator));
        e->accept();
        goto accept;
    }
#endif
    if (false) {
    }
#ifndef QT_NO_SHORTCUT
    else if (e == QKeySequence::Undo) {
        q->undo();
    }
    else if (e == QKeySequence::Redo) {
        q->redo();
    }
#ifndef QT_NO_CLIPBOARD
    else if (e == QKeySequence::Cut) {
        q->cut();
    }
    else if (e == QKeySequence::Paste) {
        QClipboard::Mode mode = QClipboard::Clipboard;
        if (QGuiApplication::clipboard()->supportsSelection()) {
            if (e->modifiers() == (Qt::CTRL | Qt::SHIFT) && e->key() == Qt::Key_Insert)
                mode = QClipboard::Selection;
        }
        q->paste(mode);
    }
#endif
    else if (e == QKeySequence::Delete) {
        QTextCursor localCursor = cursor;
        localCursor.deleteChar();
        // cursor.d->setX() removed: private API, optimization only
    } else if (e == QKeySequence::Backspace) {
        QTextCursor localCursor = cursor;
        localCursor.deletePreviousChar();
        // cursor.d->setX() removed: private API, optimization only
    } else if (e == QKeySequence::DeleteEndOfWord) {
        if (!cursor.hasSelection())
            cursor.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    else if (e == QKeySequence::DeleteStartOfWord) {
        if (!cursor.hasSelection())
            cursor.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    else if (e == QKeySequence::DeleteEndOfLine) {
        QTextBlock block = cursor.block();
        if (cursor.position() == block.position() + block.length() - 2)
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
        else
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
#endif // QT_NO_SHORTCUT
    else {
        goto process;
    }
    goto accept;

process:
    {
        if (q->isAcceptableInput(e)) {
            if (overwriteMode
                && !cursor.hasSelection()
                && !cursor.atBlockEnd())
                cursor.deleteChar();

            cursor.insertText(e->text());
            selectionChanged();
        } else {
            e->ignore();
            return;
        }
    }

accept:

#ifndef QT_NO_CLIPBOARD
    setClipboardSelection();
#endif

    e->accept();
    cursorOn = true;

    q->ensureCursorVisible();
    updateCurrentCharFormat();
}

// ============================================================================
// Remaining TextControl public methods
// ============================================================================

void TextControl::setTextInteractionFlags(Qt::TextInteractionFlags flags)
{
    if (flags == d->interactionFlags)
        return;
    d->interactionFlags = flags;
    if (d->hasFocus)
        d->setCursorVisible(flags & Qt::TextEditable);
}

Qt::TextInteractionFlags TextControl::textInteractionFlags() const
{
    return d->interactionFlags;
}

void TextControl::mergeCurrentCharFormat(const QTextCharFormat &modifier)
{
    d->cursor.mergeCharFormat(modifier);
    d->updateCurrentCharFormat();
}

void TextControl::setCurrentCharFormat(const QTextCharFormat &format)
{
    d->cursor.setCharFormat(format);
    d->updateCurrentCharFormat();
}

QTextCharFormat TextControl::currentCharFormat() const
{
    return d->cursor.charFormat();
}

void TextControl::insertPlainText(const QString &text)
{
    d->cursor.insertText(text);
}

bool TextControl::find(const QString &exp, QTextDocument::FindFlags options)
{
    QTextCursor search = d->doc->find(exp, d->cursor, options);
    if (search.isNull())
        return false;
    setTextCursor(search);
    return true;
}

#if QT_CONFIG(regularexpression)
bool TextControl::find(const QRegularExpression &exp, QTextDocument::FindFlags options)
{
    QTextCursor search = d->doc->find(exp, d->cursor, options);
    if (search.isNull())
        return false;
    setTextCursor(search);
    return true;
}
#endif

QString TextControl::toPlainText() const
{
    return document()->toPlainText();
}

void TextControl::ensureCursorVisible()
{
    QRectF crect = d->rectForPosition(d->cursor.position()).adjusted(-5, 0, 5, 0);
    emit visibilityRequest(crect);
    emit microFocusChanged();
}

QMenu *TextControl::createStandardContextMenu(const QPointF &pos, QWidget *parent)
{
    const bool showTextSelectionActions = d->interactionFlags & (Qt::TextEditable | Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);

    d->linkToCopy = QString();
    if (!pos.isNull())
        d->linkToCopy = anchorAt(pos);

    if (d->linkToCopy.isEmpty() && !showTextSelectionActions)
        return nullptr;

    QMenu *menu = new QMenu(parent);
    QAction *a;

    if (d->interactionFlags & Qt::TextEditable) {
        a = menu->addAction(tr("&Undo") + ACCEL_KEY(QKeySequence::Undo), this, SLOT(undo()));
        a->setEnabled(d->doc->isUndoAvailable());
        a->setObjectName(QStringLiteral("edit-undo"));
        a = menu->addAction(tr("&Redo") + ACCEL_KEY(QKeySequence::Redo), this, SLOT(redo()));
        a->setEnabled(d->doc->isRedoAvailable());
        a->setObjectName(QStringLiteral("edit-redo"));
        menu->addSeparator();

#ifndef QT_NO_CLIPBOARD
        a = menu->addAction(tr("Cu&t") + ACCEL_KEY(QKeySequence::Cut), this, SLOT(cut()));
        a->setEnabled(d->cursor.hasSelection());
        a->setObjectName(QStringLiteral("edit-cut"));
#endif
    }

#ifndef QT_NO_CLIPBOARD
    if (showTextSelectionActions) {
        a = menu->addAction(tr("&Copy") + ACCEL_KEY(QKeySequence::Copy), this, SLOT(copy()));
        a->setEnabled(d->cursor.hasSelection());
        a->setObjectName(QStringLiteral("edit-copy"));
    }
#endif

    if (d->interactionFlags & Qt::TextEditable) {
#ifndef QT_NO_CLIPBOARD
        a = menu->addAction(tr("&Paste") + ACCEL_KEY(QKeySequence::Paste), this, SLOT(paste()));
        a->setEnabled(canPaste());
        a->setObjectName(QStringLiteral("edit-paste"));
#endif
        a = menu->addAction(tr("Delete"), this, [this]() { d->_q_deleteSelected(); });
        a->setEnabled(d->cursor.hasSelection());
        a->setObjectName(QStringLiteral("edit-delete"));
    }

    if (showTextSelectionActions) {
        menu->addSeparator();
        a = menu->addAction(tr("Select All") + ACCEL_KEY(QKeySequence::SelectAll), this, SLOT(selectAll()));
        a->setEnabled(!d->doc->isEmpty());
        a->setObjectName(QStringLiteral("select-all"));
    }

    return menu;
}

QTextCursor TextControl::cursorForPosition(const QPointF &pos) const
{
    int cursorPos = hitTest(pos, Qt::FuzzyHit);
    if (cursorPos == -1)
        cursorPos = 0;
    QTextCursor c(d->doc);
    c.setPosition(cursorPos);
    return c;
}

QRectF TextControl::cursorRect(const QTextCursor &cursor) const
{
    if (cursor.isNull())
        return QRectF();
    return d->rectForPosition(cursor.position());
}

QRectF TextControl::cursorRect() const
{
    return cursorRect(d->cursor);
}

QRectF TextControl::selectionRect(const QTextCursor &cursor) const
{
    QRectF r = d->rectForPosition(cursor.selectionStart());

    if (cursor.hasSelection()) {
        const int position = cursor.selectionStart();
        const int anchor = cursor.selectionEnd();
        const QTextBlock posBlock = d->doc->findBlock(position);
        const QTextBlock anchorBlock = d->doc->findBlock(anchor);
        if (posBlock == anchorBlock && posBlock.isValid() && posBlock.layout()->lineCount()) {
            const QTextLine posLine = posBlock.layout()->lineForTextPosition(position - posBlock.position());
            const QTextLine anchorLine = anchorBlock.layout()->lineForTextPosition(anchor - anchorBlock.position());

            const int firstLine = qMin(posLine.lineNumber(), anchorLine.lineNumber());
            const int lastLine = qMax(posLine.lineNumber(), anchorLine.lineNumber());
            const QTextLayout *layout = posBlock.layout();
            r = QRectF();
            for (int i = firstLine; i <= lastLine; ++i) {
                r |= layout->lineAt(i).rect();
                r |= layout->lineAt(i).naturalTextRect();
            }
            r.translate(blockBoundingRect(posBlock).topLeft());
        } else {
            QRectF anchorRect = d->rectForPosition(cursor.selectionEnd());
            r |= anchorRect;
            QRectF frameRect(d->doc->documentLayout()->frameBoundingRect(cursor.currentFrame()));
            r.setLeft(frameRect.left());
            r.setRight(frameRect.right());
        }
        if (r.isValid())
            r.adjust(-1, -1, 1, 1);
    }

    return r;
}

QRectF TextControl::selectionRect() const
{
    return selectionRect(d->cursor);
}

QString TextControl::anchorAt(const QPointF &pos) const
{
    return d->doc->documentLayout()->anchorAt(pos);
}

bool TextControl::overwriteMode() const
{
    return d->overwriteMode;
}

void TextControl::setOverwriteMode(bool overwrite)
{
    d->overwriteMode = overwrite;
}

int TextControl::cursorWidth() const
{
    return d->doc->documentLayout()->property("cursorWidth").toInt();
}

void TextControl::setCursorWidth(int width)
{
    if (width == -1)
        width = QApplication::style()->pixelMetric(QStyle::PM_TextCursorWidth, nullptr,
                    qobject_cast<QWidget *>(parent()));
    d->doc->documentLayout()->setProperty("cursorWidth", width);
    d->repaintCursor();
}

void TextControl::setCursorVisible(bool visible)
{
    d->setCursorVisible(visible);
}

bool TextControl::acceptRichText() const
{
    return d->acceptRichText;
}

void TextControl::setAcceptRichText(bool accept)
{
    d->acceptRichText = accept;
}

void TextControl::setExtraSelections(const QList<QTextEdit::ExtraSelection> &selections)
{
    QMultiHash<int, int> hash;
    for (int i = 0; i < d->extraSelections.size(); ++i) {
        const QAbstractTextDocumentLayout::Selection &esel = d->extraSelections.at(i);
        hash.insert(esel.cursor.anchor(), i);
    }

    for (int i = 0; i < selections.size(); ++i) {
        const QTextEdit::ExtraSelection &sel = selections.at(i);
        const auto it = hash.constFind(sel.cursor.anchor());
        if (it != hash.cend()) {
            const QAbstractTextDocumentLayout::Selection &esel = d->extraSelections.at(it.value());
            if (esel.cursor.position() == sel.cursor.position()
                && esel.format == sel.format) {
                hash.erase(it);
                continue;
            }
        }
        QRectF r = selectionRect(sel.cursor);
        if (sel.format.boolProperty(QTextFormat::FullWidthSelection)) {
            r.setLeft(0);
            r.setWidth(qreal(INT_MAX));
        }
        emit updateRequest(r);
    }

    for (auto it = hash.cbegin(); it != hash.cend(); ++it) {
        const QAbstractTextDocumentLayout::Selection &esel = d->extraSelections.at(it.value());
        QRectF r = selectionRect(esel.cursor);
        if (esel.format.boolProperty(QTextFormat::FullWidthSelection)) {
            r.setLeft(0);
            r.setWidth(qreal(INT_MAX));
        }
        emit updateRequest(r);
    }

    d->extraSelections.resize(selections.size());
    for (int i = 0; i < selections.size(); ++i) {
        d->extraSelections[i].cursor = selections.at(i).cursor;
        d->extraSelections[i].format = selections.at(i).format;
    }
}

QList<QTextEdit::ExtraSelection> TextControl::extraSelections() const
{
    QList<QTextEdit::ExtraSelection> selections;
    const int numExtraSelections = d->extraSelections.size();
    selections.reserve(numExtraSelections);
    for (int i = 0; i < numExtraSelections; ++i) {
        QTextEdit::ExtraSelection sel;
        const QAbstractTextDocumentLayout::Selection &sel2 = d->extraSelections.at(i);
        sel.cursor = sel2.cursor;
        sel.format = sel2.format;
        selections.append(sel);
    }
    return selections;
}

void TextControl::moveCursor(QTextCursor::MoveOperation op, QTextCursor::MoveMode mode)
{
    const QTextCursor oldSelection = d->cursor;
    const bool moved = d->cursor.movePosition(op, mode);
    d->_q_updateCurrentCharFormatAndSelection();
    ensureCursorVisible();
    d->repaintOldAndNewSelection(oldSelection);
    if (moved)
        emit cursorPositionChanged();
}

bool TextControl::canPaste() const
{
#ifndef QT_NO_CLIPBOARD
    if (d->interactionFlags & Qt::TextEditable) {
        const QMimeData *md = QGuiApplication::clipboard()->mimeData();
        return md && canInsertFromMimeData(md);
    }
#endif
    return false;
}

int TextControl::hitTest(const QPointF &point, Qt::HitTestAccuracy accuracy) const
{
    return d->doc->documentLayout()->hitTest(point, accuracy);
}

QRectF TextControl::blockBoundingRect(const QTextBlock &block) const
{
    return d->doc->documentLayout()->blockBoundingRect(block);
}

QAbstractTextDocumentLayout::PaintContext TextControl::getPaintContext(QWidget *widget) const
{
    QAbstractTextDocumentLayout::PaintContext ctx;

    ctx.selections = d->extraSelections;
    ctx.palette = d->palette;

    if (d->cursorOn && d->isEnabled) {
        if (d->hideCursor)
            ctx.cursorPosition = -1;
        else if (d->preeditCursor != 0)
            ctx.cursorPosition = - (d->preeditCursor + 2);
        else
            ctx.cursorPosition = d->cursor.position();
    }

    if (!d->dndFeedbackCursor.isNull())
        ctx.cursorPosition = d->dndFeedbackCursor.position();

    if (d->cursor.hasSelection()) {
        QAbstractTextDocumentLayout::Selection selection;
        selection.cursor = d->cursor;
        if (d->cursorIsFocusIndicator) {
            QStyleOption opt;
            opt.palette = ctx.palette;
            QStyleHintReturnVariant ret;
            QStyle *style = QApplication::style();
            if (widget)
                style = widget->style();
            style->styleHint(QStyle::SH_TextControl_FocusIndicatorTextCharFormat, &opt, widget, &ret);
            selection.format = qvariant_cast<QTextFormat>(ret.variant).toCharFormat();
        } else {
            QPalette::ColorGroup cg = d->hasFocus ? QPalette::Active : QPalette::Inactive;
            selection.format.setBackground(ctx.palette.brush(cg, QPalette::Highlight));
            selection.format.setForeground(ctx.palette.brush(cg, QPalette::HighlightedText));
            QStyleOption opt;
            QStyle *style = QApplication::style();
            if (widget) {
                opt.initFrom(widget);
                style = widget->style();
            }
            if (style->styleHint(QStyle::SH_RichText_FullWidthSelection, &opt, widget))
                selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        }
        ctx.selections.append(selection);
    }

    return ctx;
}

void TextControl::drawContents(QPainter *p, const QRectF &rect, QWidget *widget)
{
    p->save();
    QAbstractTextDocumentLayout::PaintContext ctx = getPaintContext(widget);
    if (rect.isValid())
        p->setClipRect(rect, Qt::IntersectClip);
    ctx.clip = rect;
    d->doc->documentLayout()->draw(p, ctx);
    p->restore();
}

QPalette TextControl::palette() const
{
    return d->palette;
}

void TextControl::setPalette(const QPalette &pal)
{
    d->palette = pal;
}

void TextControl::setFocus(bool focus, Qt::FocusReason reason)
{
    QFocusEvent ev(focus ? QEvent::FocusIn : QEvent::FocusOut, reason);
    processEvent(&ev);
}

QVariant TextControl::inputMethodQuery(Qt::InputMethodQuery property, QVariant argument) const
{
    QTextBlock block = d->cursor.block();
    switch(property) {
    case Qt::ImCursorRectangle:
        return cursorRect();
    case Qt::ImAnchorRectangle:
        return d->rectForPosition(d->cursor.anchor());
    case Qt::ImFont:
        return QVariant(d->cursor.charFormat().font());
    case Qt::ImCursorPosition: {
        const QPointF pt = argument.toPointF();
        if (!pt.isNull())
            return QVariant(cursorForPosition(pt).position() - block.position());
        return QVariant(d->cursor.position() - block.position()); }
    case Qt::ImSurroundingText:
        return QVariant(block.text());
    case Qt::ImCurrentSelection:
        return QVariant(d->cursor.selectedText());
    case Qt::ImMaximumTextLength:
        return QVariant();
    case Qt::ImAnchorPosition:
        return QVariant(d->cursor.anchor() - block.position());
    case Qt::ImAbsolutePosition: {
        const QPointF pt = argument.toPointF();
        if (!pt.isNull())
            return QVariant(cursorForPosition(pt).position());
        return QVariant(d->cursor.position()); }
    case Qt::ImTextAfterCursor:
    {
        int maxLength = argument.isValid() ? argument.toInt() : 1024;
        QTextCursor tmpCursor = d->cursor;
        int localPos = d->cursor.position() - block.position();
        QString result = block.text().mid(localPos);
        while (result.size() < maxLength) {
            int currentBlock = tmpCursor.blockNumber();
            tmpCursor.movePosition(QTextCursor::NextBlock);
            if (tmpCursor.blockNumber() == currentBlock)
                break;
            result += u'\n' + tmpCursor.block().text();
        }
        return QVariant(result);
    }
    case Qt::ImTextBeforeCursor:
    {
        int maxLength = argument.isValid() ? argument.toInt() : 1024;
        QTextCursor tmpCursor = d->cursor;
        int localPos = d->cursor.position() - block.position();
        int numBlocks = 0;
        int resultLen = localPos;
        while (resultLen < maxLength) {
            int currentBlock = tmpCursor.blockNumber();
            tmpCursor.movePosition(QTextCursor::PreviousBlock);
            if (tmpCursor.blockNumber() == currentBlock)
                break;
            numBlocks++;
            resultLen += tmpCursor.block().length();
        }
        QString result;
        while (numBlocks) {
            result += tmpCursor.block().text() + u'\n';
            tmpCursor.movePosition(QTextCursor::NextBlock);
            --numBlocks;
        }
        result += QStringView{block.text()}.mid(0, localPos);
        return QVariant(result);
    }
    default:
        return QVariant();
    }
}

QMimeData *TextControl::createMimeDataFromSelection() const
{
    const QTextDocumentFragment fragment(d->cursor);
    QMimeData *mimeData = new QMimeData;
    mimeData->setText(fragment.toPlainText());
    return mimeData;
}

bool TextControl::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasText() && !source->text().isEmpty();
}

void TextControl::insertFromMimeData(const QMimeData *source)
{
    if (!(d->interactionFlags & Qt::TextEditable) || !source)
        return;

    if (source->hasText()) {
        QTextDocumentFragment fragment = QTextDocumentFragment::fromPlainText(source->text());
        d->cursor.insertFragment(fragment);
    }
    ensureCursorVisible();
}

// ============================================================================
// Remaining TextControlPrivate methods (mouse events, etc.)
// ============================================================================

QRectF TextControlPrivate::rectForPosition(int position) const
{
    // v0.6.0-alpha.6: defensive clamp against cursor drift. Three soak-week
    // crashes have hit "QTextCursor::setPosition: Position 'N' out of range"
    // inside QTextEngine::justify via rectForPosition when the cursor's
    // cached position exceeds doc->characterCount() - 1. Root cause still
    // being tracked (see the qCWarning instrumentation just below); while
    // that investigation proceeds, preventing the crash here keeps typing
    // usable. Behavior when drift hits: position clamps to end-of-doc and
    // the cursor-rect is computed at that position — visible to the user
    // as the caret painted slightly wrong for one frame, far better than
    // a SEGV that loses their work.
    if (!doc) return QRectF();
    const int docEnd = doc->characterCount() - 1;  // characterCount includes trailing \0
    if (position > docEnd || position < 0) {
        static QLoggingCategory lcCursorDrift("markoff.live.text_control.cursor_drift");
        qCWarning(lcCursorDrift,
                  "rectForPosition: position %d out of range [0, %d] — clamping. "
                  "Indicates TextControlPrivate::cursor drifted relative to doc "
                  "length; caller stack should reveal which edit path desyncd it.",
                  position, docEnd);
        position = std::clamp(position, 0, docEnd);
    }
    const QTextBlock block = doc->findBlock(position);
    if (!block.isValid())
        return QRectF();
    const QAbstractTextDocumentLayout *docLayout = doc->documentLayout();
    const QTextLayout *layout = block.layout();
    const QPointF layoutPos = q->blockBoundingRect(block).topLeft();
    int relativePos = position - block.position();
    if (preeditCursor != 0) {
        int preeditPos = layout->preeditAreaPosition();
        if (relativePos == preeditPos)
            relativePos += preeditCursor;
        else if (relativePos > preeditPos)
            relativePos += layout->preeditAreaText().size();
    }
    QTextLine line = layout->lineForTextPosition(relativePos);

    int cursorWidth;
    {
        bool ok = false;
        cursorWidth = docLayout->property("cursorWidth").toInt(&ok);
        if (!ok)
            cursorWidth = 1;
    }

    QRectF r;
    if (line.isValid()) {
        qreal x = line.cursorToX(relativePos);
        qreal w = 0;
        if (overwriteMode) {
            if (relativePos < line.textLength() - line.textStart())
                w = line.cursorToX(relativePos + 1) - x;
            else
                w = QFontMetrics(block.layout()->font()).horizontalAdvance(u' ');
        }
        r = QRectF(layoutPos.x() + x, layoutPos.y() + line.y(),
                   cursorWidth + w, line.height());
    } else {
        r = QRectF(layoutPos.x(), layoutPos.y(), cursorWidth, 10);
    }

    return r;
}

QRectF TextControlPrivate::cursorRectPlusUnicodeDirectionMarkers(const QTextCursor &cursor) const
{
    if (cursor.isNull())
        return QRectF();
    return rectForPosition(cursor.position()).adjusted(-4, 0, 4, 0);
}

void TextControlPrivate::mousePressEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
                                          Qt::KeyboardModifiers modifiers,
                                          Qt::MouseButtons buttons, const QPointF &globalPos)
{
    mousePressPos = pos;

#if QT_CONFIG(draganddrop)
    mightStartDrag = false;
#endif

    if (sendMouseEventToInputContext(e, QEvent::MouseButtonPress, button, pos, modifiers, buttons, globalPos))
        return;

    if (interactionFlags & Qt::LinksAccessibleByMouse) {
        anchorOnMousePress = q->anchorAt(pos);
        if (cursorIsFocusIndicator) {
            cursorIsFocusIndicator = false;
            repaintSelection();
            cursor.clearSelection();
        }
    }
    if (!(button & Qt::LeftButton) ||
        !((interactionFlags & Qt::TextSelectableByMouse) || (interactionFlags & Qt::TextEditable))) {
        e->ignore();
        return;
    }

    cursorIsFocusIndicator = false;
    const QTextCursor oldSelection = cursor;
    const int oldCursorPos = cursor.position();

    mousePressed = (interactionFlags & Qt::TextSelectableByMouse);

    commitPreedit();

    if (trippleClickTimer.isActive()
        && ((pos - trippleClickPoint).manhattanLength() < QApplication::startDragDistance())) {

        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        selectedBlockOnTrippleClick = cursor;

        anchorOnMousePress = QString();

        trippleClickTimer.stop();
    } else {
        int cursorPos = q->hitTest(pos, Qt::FuzzyHit);
        if (cursorPos == -1) {
            e->ignore();
            return;
        }

        if (modifiers == Qt::ShiftModifier && (interactionFlags & Qt::TextSelectableByMouse)) {
            if (wordSelectionEnabled && !selectedWordOnDoubleClick.hasSelection()) {
                selectedWordOnDoubleClick = cursor;
                selectedWordOnDoubleClick.select(QTextCursor::WordUnderCursor);
            }

            if (selectedBlockOnTrippleClick.hasSelection())
                extendBlockwiseSelection(cursorPos);
            else if (selectedWordOnDoubleClick.hasSelection())
                extendWordwiseSelection(cursorPos, pos.x());
            else if (!wordSelectionEnabled)
                setCursorPosition(cursorPos, QTextCursor::KeepAnchor);
        } else {
            if (dragEnabled
                && cursor.hasSelection()
                && !cursorIsFocusIndicator
                && cursorPos >= cursor.selectionStart()
                && cursorPos <= cursor.selectionEnd()
                && q->hitTest(pos, Qt::ExactHit) != -1) {
#if QT_CONFIG(draganddrop)
                mightStartDrag = true;
#endif
                return;
            }

            setCursorPosition(cursorPos);
        }
    }

    if (interactionFlags & Qt::TextEditable) {
        q->ensureCursorVisible();
        if (cursor.position() != oldCursorPos)
            emit q->cursorPositionChanged();
        _q_updateCurrentCharFormatAndSelection();
    } else {
        if (cursor.position() != oldCursorPos) {
            emit q->cursorPositionChanged();
            emit q->microFocusChanged();
        }
        selectionChanged();
    }
    repaintOldAndNewSelection(oldSelection);
    hadSelectionOnMousePress = cursor.hasSelection();
}

void TextControlPrivate::mouseMoveEvent(QEvent *e, Qt::MouseButton button, const QPointF &mousePos,
                                         Qt::KeyboardModifiers modifiers,
                                         Qt::MouseButtons buttons, const QPointF &globalPos)
{
    if (interactionFlags & Qt::LinksAccessibleByMouse)
        updateHighlightedAnchor(mousePos);

    if (buttons & Qt::LeftButton) {
        const bool editable = interactionFlags & Qt::TextEditable;

        if (!(mousePressed
              || editable
              || mightStartDrag
              || selectedWordOnDoubleClick.hasSelection()
              || selectedBlockOnTrippleClick.hasSelection()))
            return;

        const QTextCursor oldSelection = cursor;
        const int oldCursorPos = cursor.position();

        if (mightStartDrag) {
            if ((mousePos - mousePressPos).manhattanLength() > QApplication::startDragDistance())
                startDrag();
            return;
        }

        const qreal mouseX = qreal(mousePos.x());
        int newCursorPos = q->hitTest(mousePos, Qt::FuzzyHit);

        if (isPreediting()) {
            int selectionStartPos = q->hitTest(mousePressPos, Qt::FuzzyHit);
            if (newCursorPos != selectionStartPos) {
                commitPreedit();
                newCursorPos = q->hitTest(mousePos, Qt::FuzzyHit);
                selectionStartPos = q->hitTest(mousePressPos, Qt::FuzzyHit);
                setCursorPosition(selectionStartPos);
            }
        }

        if (newCursorPos == -1)
            return;

        if (mousePressed && wordSelectionEnabled && !selectedWordOnDoubleClick.hasSelection()) {
            selectedWordOnDoubleClick = cursor;
            selectedWordOnDoubleClick.select(QTextCursor::WordUnderCursor);
        }

        if (selectedBlockOnTrippleClick.hasSelection())
            extendBlockwiseSelection(newCursorPos);
        else if (selectedWordOnDoubleClick.hasSelection())
            extendWordwiseSelection(newCursorPos, mouseX);
        else if (mousePressed && !isPreediting())
            setCursorPosition(newCursorPos, QTextCursor::KeepAnchor);

        if (interactionFlags & Qt::TextEditable) {
            if (cursor.position() != oldCursorPos)
                emit q->cursorPositionChanged();
            _q_updateCurrentCharFormatAndSelection();
#ifndef QT_NO_IM
            if (contextWidget)
                QGuiApplication::inputMethod()->update(Qt::ImQueryInput);
#endif
        } else {
            if (cursor.position() != oldCursorPos) {
                emit q->cursorPositionChanged();
                emit q->microFocusChanged();
            }
        }
        selectionChanged(true);
        repaintOldAndNewSelection(oldSelection);
    }

    sendMouseEventToInputContext(e, QEvent::MouseMove, button, mousePos, modifiers, buttons, globalPos);
}

void TextControlPrivate::mouseReleaseEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
                                            Qt::KeyboardModifiers modifiers,
                                            Qt::MouseButtons buttons, const QPointF &globalPos)
{
    const QTextCursor oldSelection = cursor;
    if (sendMouseEventToInputContext(e, QEvent::MouseButtonRelease, button, pos, modifiers, buttons, globalPos)) {
        repaintOldAndNewSelection(oldSelection);
        return;
    }

    const int oldCursorPos = cursor.position();

#if QT_CONFIG(draganddrop)
    if (mightStartDrag && (button & Qt::LeftButton)) {
        mousePressed = false;
        setCursorPosition(pos);
        cursor.clearSelection();
        selectionChanged();
    }
#endif
    if (mousePressed) {
        mousePressed = false;
#ifndef QT_NO_CLIPBOARD
        setClipboardSelection();
        selectionChanged(true);
    } else if (button == Qt::MiddleButton
               && (interactionFlags & Qt::TextEditable)
               && QGuiApplication::clipboard()->supportsSelection()) {
        setCursorPosition(pos);
        const QMimeData *md = QGuiApplication::clipboard()->mimeData(QClipboard::Selection);
        if (md)
            q->insertFromMimeData(md);
#endif
    }

    repaintOldAndNewSelection(oldSelection);

    if (cursor.position() != oldCursorPos) {
        emit q->cursorPositionChanged();
        emit q->microFocusChanged();
    }

    if (interactionFlags & Qt::LinksAccessibleByMouse) {
        if (!(button & Qt::LeftButton)) {
            e->ignore();
            return;
        }

        const QString anchor = q->anchorAt(pos);
        if (anchor.isEmpty()) {
            e->ignore();
            return;
        }

        if (!cursor.hasSelection()
            || (anchor == anchorOnMousePress && hadSelectionOnMousePress)) {
            const int anchorPos = q->hitTest(pos, Qt::ExactHit);
            if (anchorPos < 0) {
                e->ignore();
                return;
            }
            cursor.setPosition(anchorPos);
            activateLinkUnderCursor(std::exchange(anchorOnMousePress, QString()));
        }
    }
}

void TextControlPrivate::mouseDoubleClickEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
                                                Qt::KeyboardModifiers modifiers, Qt::MouseButtons buttons,
                                                const QPointF &globalPos)
{
    if (button == Qt::LeftButton && (interactionFlags & Qt::TextSelectableByMouse)) {
#if QT_CONFIG(draganddrop)
        mightStartDrag = false;
#endif
        commitPreedit();

        const QTextCursor oldSelection = cursor;
        setCursorPosition(pos);
        QTextLine line = currentTextLine(cursor);
        bool doEmit = false;
        if (line.isValid() && line.textLength()) {
            cursor.select(QTextCursor::WordUnderCursor);
            doEmit = true;
        }
        repaintOldAndNewSelection(oldSelection);

        cursorIsFocusIndicator = false;
        selectedWordOnDoubleClick = cursor;

        trippleClickPoint = pos;
        trippleClickTimer.start(QApplication::doubleClickInterval(), q);
        if (doEmit) {
            selectionChanged();
#ifndef QT_NO_CLIPBOARD
            setClipboardSelection();
#endif
            emit q->cursorPositionChanged();
        }
    } else if (!sendMouseEventToInputContext(e, QEvent::MouseButtonDblClick, button, pos,
                                             modifiers, buttons, globalPos)) {
        e->ignore();
    }
}

bool TextControlPrivate::sendMouseEventToInputContext(
        QEvent *e, QEvent::Type eventType, Qt::MouseButton button, const QPointF &pos,
        Qt::KeyboardModifiers modifiers, Qt::MouseButtons buttons, const QPointF &globalPos)
{
    Q_UNUSED(eventType);
    Q_UNUSED(button);
    Q_UNUSED(pos);
    Q_UNUSED(modifiers);
    Q_UNUSED(buttons);
    Q_UNUSED(globalPos);
#if !defined(QT_NO_IM)
    if (isPreediting()) {
        QTextLayout *layout = cursor.block().layout();
        int cursorPos = q->hitTest(pos, Qt::FuzzyHit) - cursor.position();

        if (cursorPos < 0 || cursorPos > layout->preeditAreaText().size())
            cursorPos = -1;

        if (cursorPos >= 0) {
            if (eventType == QEvent::MouseButtonRelease)
                QGuiApplication::inputMethod()->invokeAction(QInputMethod::Click, cursorPos);
            e->setAccepted(true);
            return true;
        }
    }
#else
    Q_UNUSED(e);
#endif
    return false;
}

void TextControlPrivate::contextMenuEvent(const QPointF &screenPos, const QPointF &docPos, QWidget *ctxWidget)
{
#ifdef QT_NO_CONTEXTMENU
    Q_UNUSED(screenPos);
    Q_UNUSED(docPos);
    Q_UNUSED(ctxWidget);
#else
    QMenu *menu = q->createStandardContextMenu(docPos, ctxWidget);
    if (!menu)
        return;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(screenPos.toPoint());
#endif
}

void TextControlPrivate::focusEvent(QFocusEvent *e)
{
    emit q->updateRequest(q->selectionRect());
    if (e->gotFocus()) {
        cursorOn = (interactionFlags & (Qt::TextSelectableByKeyboard | Qt::TextEditable));
        if (interactionFlags & Qt::TextEditable) {
            setCursorVisible(true);
        }
    } else {
        setCursorVisible(false);
        cursorOn = false;

        if (cursorIsFocusIndicator
            && e->reason() != Qt::ActiveWindowFocusReason
            && e->reason() != Qt::PopupFocusReason
            && cursor.hasSelection()) {
            cursor.clearSelection();
        }
    }
    hasFocus = e->gotFocus();
}

bool TextControlPrivate::dragEnterEvent(QEvent *e, const QMimeData *mimeData)
{
    if (!(interactionFlags & Qt::TextEditable) || !q->canInsertFromMimeData(mimeData)) {
        e->ignore();
        return false;
    }
    dndFeedbackCursor = QTextCursor();
    return true;
}

void TextControlPrivate::dragLeaveEvent()
{
    const QRectF crect = q->cursorRect(dndFeedbackCursor);
    dndFeedbackCursor = QTextCursor();
    if (crect.isValid())
        emit q->updateRequest(crect);
}

bool TextControlPrivate::dragMoveEvent(QEvent *e, const QMimeData *mimeData, const QPointF &pos)
{
    if (!(interactionFlags & Qt::TextEditable) || !q->canInsertFromMimeData(mimeData)) {
        e->ignore();
        return false;
    }

    const int cursorPos = q->hitTest(pos, Qt::FuzzyHit);
    if (cursorPos != -1) {
        QRectF crect = q->cursorRect(dndFeedbackCursor);
        if (crect.isValid())
            emit q->updateRequest(crect);

        dndFeedbackCursor = cursor;
        dndFeedbackCursor.setPosition(cursorPos);

        crect = q->cursorRect(dndFeedbackCursor);
        emit q->updateRequest(crect);
    }

    return true;
}

bool TextControlPrivate::dropEvent(const QMimeData *mimeData, const QPointF &pos,
                                    Qt::DropAction dropAction, QObject *source)
{
    dndFeedbackCursor = QTextCursor();

    if (!(interactionFlags & Qt::TextEditable) || !q->canInsertFromMimeData(mimeData))
        return false;

    repaintSelection();

    QTextCursor insertionCursor = q->cursorForPosition(pos);
    insertionCursor.beginEditBlock();

    if (dropAction == Qt::MoveAction && source == contextWidget)
        cursor.removeSelectedText();

    cursor = insertionCursor;
    q->insertFromMimeData(mimeData);
    insertionCursor.endEditBlock();
    q->ensureCursorVisible();
    return true;
}

void TextControlPrivate::inputMethodEvent(QInputMethodEvent *e)
{
    if (!(interactionFlags & (Qt::TextEditable | Qt::TextSelectableByMouse)) || cursor.isNull()) {
        e->ignore();
        return;
    }
    bool isGettingInput = !e->commitString().isEmpty()
            || e->preeditString() != cursor.block().layout()->preeditAreaText()
            || e->replacementLength() > 0;

    if (!isGettingInput && e->attributes().isEmpty()) {
        e->ignore();
        return;
    }

    int oldCursorPos = cursor.position();

    cursor.beginEditBlock();
    if (isGettingInput) {
        cursor.removeSelectedText();
    }

    QTextBlock block;

    if (!e->commitString().isEmpty() || e->replacementLength()) {
        if (e->commitString().endsWith(QChar::LineFeed))
            block = cursor.block();
        QTextCursor c = cursor;
        c.setPosition(c.position() + e->replacementStart());
        c.setPosition(c.position() + e->replacementLength(), QTextCursor::KeepAnchor);
        c.insertText(e->commitString());
    }

    for (int i = 0; i < e->attributes().size(); ++i) {
        const QInputMethodEvent::Attribute &a = e->attributes().at(i);
        if (a.type == QInputMethodEvent::Selection) {
            QTextCursor oldCursor = cursor;
            int blockStart = a.start + cursor.block().position();
            cursor.setPosition(blockStart, QTextCursor::MoveAnchor);
            cursor.setPosition(blockStart + a.length, QTextCursor::KeepAnchor);
            q->ensureCursorVisible();
            repaintOldAndNewSelection(oldCursor);
        }
    }

    if (!block.isValid())
        block = cursor.block();
    QTextLayout *layout = block.layout();
    if (isGettingInput)
        layout->setPreeditArea(cursor.position() - block.position(), e->preeditString());
    QList<QTextLayout::FormatRange> overrides;
    overrides.reserve(e->attributes().size());
    const int oldPreeditCursor = preeditCursor;
    preeditCursor = e->preeditString().size();
    hideCursor = false;
    for (int i = 0; i < e->attributes().size(); ++i) {
        const QInputMethodEvent::Attribute &a = e->attributes().at(i);
        if (a.type == QInputMethodEvent::Cursor) {
            preeditCursor = a.start;
            hideCursor = !a.length;
        } else if (a.type == QInputMethodEvent::TextFormat) {
            QTextCharFormat f = cursor.charFormat();
            f.merge(qvariant_cast<QTextFormat>(a.value).toCharFormat());
            if (f.isValid()) {
                QTextLayout::FormatRange o;
                o.start = a.start + cursor.position() - block.position();
                o.length = a.length;
                o.format = f;

                QList<QTextLayout::FormatRange>::iterator it = overrides.end();
                while (it != overrides.begin()) {
                    QList<QTextLayout::FormatRange>::iterator previous = it - 1;
                    if (o.start >= previous->start) {
                        overrides.insert(it, o);
                        break;
                    }
                    it = previous;
                }

                if (it == overrides.begin())
                    overrides.prepend(o);
            }
        }
    }

    if (cursor.charFormat().isValid()) {
        int start = cursor.position() - block.position();
        int end = start + e->preeditString().size();

        QList<QTextLayout::FormatRange>::iterator it = overrides.begin();
        while (it != overrides.end()) {
            QTextLayout::FormatRange range = *it;
            int rangeStart = range.start;
            if (rangeStart > start) {
                QTextLayout::FormatRange o;
                o.start = start;
                o.length = rangeStart - start;
                o.format = cursor.charFormat();
                it = overrides.insert(it, o) + 1;
            }

            ++it;
            start = range.start + range.length;
        }

        if (start < end) {
            QTextLayout::FormatRange o;
            o.start = start;
            o.length = end - start;
            o.format = cursor.charFormat();
            overrides.append(o);
        }
    }
    layout->setFormats(overrides);

    cursor.endEditBlock();

    // cursor.d->setX() removed: private API, optimization only
    if (oldCursorPos != cursor.position())
        emit q->cursorPositionChanged();
    if (oldPreeditCursor != preeditCursor)
        emit q->microFocusChanged();
}

void TextControlPrivate::activateLinkUnderCursor(QString href)
{
    QTextCursor oldCursor = cursor;

    if (href.isEmpty()) {
        QTextCursor tmp = cursor;
        if (tmp.selectionStart() != tmp.position())
            tmp.setPosition(tmp.selectionStart());
        tmp.movePosition(QTextCursor::NextCharacter);
        href = tmp.charFormat().anchorHref();
    }
    if (href.isEmpty())
        return;

    if (!cursor.hasSelection()) {
        QTextBlock block = cursor.block();
        const int cursorPos = cursor.position();

        QTextBlock::Iterator it = block.begin();
        QTextBlock::Iterator linkFragment;

        for (; !it.atEnd(); ++it) {
            QTextFragment fragment = it.fragment();
            const int fragmentPos = fragment.position();
            if (fragmentPos <= cursorPos &&
                fragmentPos + fragment.length() > cursorPos) {
                linkFragment = it;
                break;
            }
        }

        if (!linkFragment.atEnd()) {
            it = linkFragment;
            cursor.setPosition(it.fragment().position());
            if (it != block.begin()) {
                do {
                    --it;
                    QTextFragment fragment = it.fragment();
                    if (fragment.charFormat().anchorHref() != href)
                        break;
                    cursor.setPosition(fragment.position());
                } while (it != block.begin());
            }

            for (it = linkFragment; !it.atEnd(); ++it) {
                QTextFragment fragment = it.fragment();
                if (fragment.charFormat().anchorHref() != href)
                    break;
                cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            }
        }
    }

    if (hasFocus) {
        cursorIsFocusIndicator = true;
    } else {
        cursorIsFocusIndicator = false;
        cursor.clearSelection();
    }
    repaintOldAndNewSelection(oldCursor);

#ifndef QT_NO_DESKTOPSERVICES
    if (openExternalLinks)
        QDesktopServices::openUrl(QUrl{href});
    else
#endif
        emit q->linkActivated(href);
}

void TextControlPrivate::updateHighlightedAnchor(QPointF mousePos)
{
    const QString anchor = q->anchorAt(mousePos);
    if (anchor != highlightedAnchor) {
        highlightedAnchor = anchor;
        emit q->linkHovered(anchor);
    }
}

void TextControlPrivate::resetHighlightedAnchor()
{
    if (!highlightedAnchor.isEmpty()) {
        highlightedAnchor.clear();
        emit q->linkHovered(QString());
    }
}

#if QT_CONFIG(tooltip)
void TextControlPrivate::showToolTip(const QPoint &globalPos, const QPointF &pos, QWidget *ctxWidget)
{
    const QString toolTip = q->cursorForPosition(pos).charFormat().toolTip();
    if (toolTip.isEmpty())
        return;
    QToolTip::showText(globalPos, toolTip, ctxWidget);
}
#endif

bool TextControlPrivate::isPreediting() const
{
    QTextLayout *layout = cursor.block().layout();
    if (layout && !layout->preeditAreaText().isEmpty())
        return true;
    return false;
}

void TextControlPrivate::commitPreedit()
{
    if (!isPreediting())
        return;

    QGuiApplication::inputMethod()->commit();

    if (!isPreediting())
        return;

    cursor.beginEditBlock();
    preeditCursor = 0;
    QTextBlock block = cursor.block();
    QTextLayout *layout = block.layout();
    layout->setPreeditArea(-1, QString());
    layout->clearFormats();
    cursor.endEditBlock();
}

void TextControlPrivate::insertParagraphSeparator()
{
    auto blockFmt = cursor.blockFormat();
    auto charFmt = cursor.charFormat();
    blockFmt.clearProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
    if (blockFmt.hasProperty(QTextFormat::HeadingLevel)) {
        blockFmt.clearProperty(QTextFormat::HeadingLevel);
        charFmt = QTextCharFormat();
    }
    if (cursor.currentList()) {
        auto existingFmt = cursor.blockFormat();
        existingFmt.clearProperty(QTextBlockFormat::BlockBottomMargin);
        cursor.setBlockFormat(existingFmt);
        if (blockFmt.marker() == QTextBlockFormat::MarkerType::Checked)
            blockFmt.setMarker(QTextBlockFormat::MarkerType::Unchecked);
    }

    if (cursor.block().text().isEmpty() &&
            !cursor.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth) &&
            !cursor.blockFormat().hasProperty(QTextFormat::BlockCodeLanguage)) {
        blockFmt = QTextBlockFormat();
        const bool blockFmtChanged = (cursor.blockFormat() != blockFmt);
        charFmt = QTextCharFormat();
        cursor.setBlockFormat(blockFmt);
        cursor.setCharFormat(charFmt);
        if (blockFmtChanged)
            return;
    }

    cursor.insertBlock(blockFmt, charFmt);
}

void TextControlPrivate::append(const QString &text, Qt::TextFormat format)
{
    Q_UNUSED(format);
    QTextCursor tmp(doc);
    tmp.beginEditBlock();
    tmp.movePosition(QTextCursor::End);

    if (!doc->isEmpty())
        tmp.insertBlock(cursor.blockFormat(), cursor.charFormat());
    else
        tmp.setCharFormat(cursor.charFormat());

    QTextCharFormat oldCharFormat = cursor.charFormat();
    tmp.insertText(text);

    if (!cursor.hasSelection())
        cursor.setCharFormat(oldCharFormat);

    tmp.endEditBlock();
}

void TextControlPrivate::_q_updateBlock(const QTextBlock &block)
{
    QRectF br = q->blockBoundingRect(block);
    br.setRight(qreal(INT_MAX));
    emit q->updateRequest(br);
}

QString TextControlPrivate::anchorForCursor(const QTextCursor &anchorCursor) const
{
    if (anchorCursor.hasSelection()) {
        QTextCursor curs = anchorCursor;
        if (curs.selectionStart() != curs.position())
            curs.setPosition(curs.selectionStart());
        curs.movePosition(QTextCursor::NextCharacter);
        QTextCharFormat fmt = curs.charFormat();
        if (fmt.isAnchor() && fmt.hasProperty(QTextFormat::AnchorHref))
            return fmt.stringProperty(QTextFormat::AnchorHref);
    }
    return QString();
}

// ============================================================================
// isAcceptableInput / isCommonTextEditShortcut
// Replacement for QInputControl methods
// ============================================================================

bool TextControl::isAcceptableInput(const QKeyEvent *event) const
{
    // Reimplemented from QInputControl::isAcceptableInput for TextEdit type
    const QString text = event->text();
    if (text.isEmpty())
        return false;

    const QChar c = text.at(0);

    // Shortcut overrides are always acceptable
    if (event->type() == QEvent::ShortcutOverride)
        return false;

    // Check for control characters
    if (c.category() == QChar::Other_Control) {
        // Allow tab
        if (c == u'\t')
            return true;
        return false;
    }

    // For text edits, most printable characters are acceptable
    if (c.isPrint())
        return true;

    if (c.category() == QChar::Other_PrivateUse)
        return true;

    // Check for special Unicode categories that should be accepted
    if (c == QChar::LineSeparator
        || c == QChar::ParagraphSeparator
        || c == QChar::ObjectReplacementCharacter)
        return true;

    return false;
}

bool TextControl::isCommonTextEditShortcut(const QKeyEvent *ke)
{
    if (ke->modifiers() == Qt::NoModifier
        || ke->modifiers() == Qt::ShiftModifier
        || ke->modifiers() == Qt::KeypadModifier) {
        if (ke->key() < Qt::Key_Escape) {
            return true;
        } else {
            switch (ke->key()) {
                case Qt::Key_Return:
                case Qt::Key_Enter:
                case Qt::Key_Delete:
                case Qt::Key_Home:
                case Qt::Key_End:
                case Qt::Key_Backspace:
                case Qt::Key_Left:
                case Qt::Key_Right:
                case Qt::Key_Up:
                case Qt::Key_Down:
                case Qt::Key_Tab:
                    return true;
                default:
                    break;
            }
        }
    } else if (ke->matches(QKeySequence::Copy)
               || ke->matches(QKeySequence::Paste)
               || ke->matches(QKeySequence::Cut)
               || ke->matches(QKeySequence::Redo)
               || ke->matches(QKeySequence::Undo)
               || ke->matches(QKeySequence::MoveToNextWord)
               || ke->matches(QKeySequence::MoveToPreviousWord)
               || ke->matches(QKeySequence::MoveToStartOfDocument)
               || ke->matches(QKeySequence::MoveToEndOfDocument)
               || ke->matches(QKeySequence::SelectNextWord)
               || ke->matches(QKeySequence::SelectPreviousWord)
               || ke->matches(QKeySequence::SelectStartOfLine)
               || ke->matches(QKeySequence::SelectEndOfLine)
               || ke->matches(QKeySequence::SelectStartOfBlock)
               || ke->matches(QKeySequence::SelectEndOfBlock)
               || ke->matches(QKeySequence::SelectStartOfDocument)
               || ke->matches(QKeySequence::SelectEndOfDocument)
               || ke->matches(QKeySequence::SelectAll)
              ) {
        return true;
    }
    return false;
}

void TextControl::append(const QString &text)
{
    QTextCursor cursor(d->doc);
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    cursor.insertBlock();
    cursor.insertText(text);
    cursor.endEditBlock();
}

void TextControl::appendPlainText(const QString &text)
{
    append(text);
}

} // namespace Markoff

#include "moc_TextControl.cpp"
