// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TEXTCONTROL_TESTUTIL_H
#define MARKOFF_TEXTCONTROL_TESTUTIL_H

#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextObjectInterface>
#include <QWidget>
#include <algorithm>
#include <memory>

#include "TextControl.h"

namespace Markoff::TestUtil {

/// Test-only inline text object. Sentinel TypeId well clear of
/// MathTextObject::TypeId (UserObject+1) and CheckboxTextObject::TypeId
/// (UserObject+2).
class TestPlaceholderObject : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    static constexpr int TypeId = QTextFormat::UserObject + 100;

    explicit TestPlaceholderObject(QObject *parent = nullptr)
        : QObject(parent) {}

    QSizeF intrinsicSize(QTextDocument *, int,
                         const QTextFormat &) override {
        return QSizeF(10.0, 16.0);
    }
    void drawObject(QPainter *, const QRectF &, QTextDocument *,
                    int, const QTextFormat &) override {
        // paint nothing — tests don't render
    }
};

/// RAII fixture. `control` targets `document`; `contextWidget` is needed
/// by processEvent() for IME + clipboard focus affordances. The
/// placeholder handler is pre-registered on the document.
struct TextControlFixture {
    std::unique_ptr<QTextDocument> document;
    std::unique_ptr<QWidget> contextWidget;
    std::unique_ptr<TestPlaceholderObject> placeholder;
    Markoff::TextControl control;

    TextControlFixture()
        : document(std::make_unique<QTextDocument>()),
          contextWidget(std::make_unique<QWidget>()),
          placeholder(std::make_unique<TestPlaceholderObject>())
    {
        document->documentLayout()->registerHandler(
            TestPlaceholderObject::TypeId, placeholder.get());
        control.setDocument(document.get());
    }
};

/// Build a fixture, set plain text, position cursor at `cursorPos`.
inline TextControlFixture makeFixture(const QString &text = {},
                                      int cursorPos = 0)
{
    TextControlFixture fx;
    fx.control.setPlainText(text);
    QTextCursor c(fx.document.get());
    c.setPosition(std::min<qsizetype>(cursorPos, text.size()));
    fx.control.setTextCursor(c);
    return fx;
}

/// Insert one U+FFFC run bound to TestPlaceholderObject::TypeId at the
/// cursor's current position. The cursor is advanced past the inserted
/// glyph.
inline void insertPlaceholder(QTextCursor &c)
{
    QTextCharFormat fmt;
    fmt.setObjectType(TestPlaceholderObject::TypeId);
    c.insertText(QString(QChar(QChar::ObjectReplacementCharacter)), fmt);
}

/// Synthesize and dispatch a QKeyEvent (KeyPress) through
/// TextControl::processEvent().
inline void sendKey(Markoff::TextControl &tc, int key,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                    const QString &text = {})
{
    QKeyEvent ev(QEvent::KeyPress, key, modifiers, text);
    tc.processEvent(&ev, QPointF(0, 0));
}

/// Synthesize a mouse press event at document-local position `pos`.
inline void sendMousePress(Markoff::TextControl &tc, const QPointF &pos,
                           Qt::MouseButton button = Qt::LeftButton,
                           Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                           QWidget *context = nullptr)
{
    QMouseEvent ev(QEvent::MouseButtonPress, pos, pos, button, button,
                   modifiers);
    tc.processEvent(&ev, QPointF(0, 0), context);
}

inline void sendMouseMove(Markoff::TextControl &tc, const QPointF &pos,
                          Qt::MouseButtons buttons = Qt::LeftButton,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                          QWidget *context = nullptr)
{
    QMouseEvent ev(QEvent::MouseMove, pos, pos, Qt::NoButton, buttons,
                   modifiers);
    tc.processEvent(&ev, QPointF(0, 0), context);
}

inline void sendMouseRelease(Markoff::TextControl &tc, const QPointF &pos,
                             Qt::MouseButton button = Qt::LeftButton,
                             Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                             QWidget *context = nullptr)
{
    QMouseEvent ev(QEvent::MouseButtonRelease, pos, pos, button,
                   Qt::NoButton, modifiers);
    tc.processEvent(&ev, QPointF(0, 0), context);
}

inline void sendMouseDoubleClick(Markoff::TextControl &tc, const QPointF &pos,
                                 Qt::MouseButton button = Qt::LeftButton,
                                 Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                                 QWidget *context = nullptr)
{
    QMouseEvent ev(QEvent::MouseButtonDblClick, pos, pos, button, button,
                   modifiers);
    tc.processEvent(&ev, QPointF(0, 0), context);
}

/// Synthesize an IME event. `commitString` is text to commit (replaces
/// preedit); `preeditString` is the still-composing preedit to show.
inline void sendInputMethod(Markoff::TextControl &tc,
                            const QString &commitString,
                            const QString &preeditString,
                            const QList<QInputMethodEvent::Attribute> &attrs = {})
{
    QInputMethodEvent ev(preeditString, attrs);
    if (!commitString.isEmpty())
        ev.setCommitString(commitString);
    tc.processEvent(&ev, QPointF(0, 0));
}

} // namespace Markoff::TestUtil

#endif // MARKOFF_TEXTCONTROL_TESTUTIL_H
