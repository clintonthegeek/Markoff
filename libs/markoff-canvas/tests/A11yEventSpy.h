// SPDX-License-Identifier: GPL-3.0-or-later
//
// G1 a11y arc, A3.1 — test-only QAccessible event spy. Reusable by A3.2
// (caret/selection/focus events) and A3.3 (text/structure events).
//
// Scope-based: constructing installs QAccessible's (global, process-wide)
// update handler and forces QAccessible active; destroying restores BOTH the
// previous handler and the previous active state, so a later test case never
// sees stale events or a stale handler. Construct one per test case, on the
// stack, and let it go out of scope.
//
// QAccessible::UpdateHandler is a plain function pointer, so delivery goes
// through a static "current spy" pointer (nested spies chain and restore in
// LIFO order). Events are SNAPSHOTTED at delivery: the caller owns and
// destroys the QAccessibleEvent right after updateAccessibility() returns.

#pragma once

#include <QAccessible>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

namespace MarkoffTest {

struct A11yEventRecord {
    QAccessible::Event type = QAccessible::InvalidEvent;
    QPointer<QObject> object;  // event->object(); null if built from an interface
    int child = 0;             // event->child()
    // Payload, by event type (unused fields stay at defaults):
    //   TextCaretMoved   : a = cursorPosition
    //   TextInserted     : a = cursorPosition (insert start), text = inserted text
    //   TextRemoved      : a = cursorPosition (remove start), text = removed text
    //   TextSelectionChanged : a = selectionStart, b = selectionEnd
    //   StateChanged     : changedStates
    int a = -1;
    int b = -1;
    QString text;
    QAccessible::State changedStates;
};

class A11yEventSpy {
public:
    A11yEventSpy()
        : m_previousSpy(s_current)
        , m_wasActive(QAccessible::isActive())
    {
        m_previousHandler = QAccessible::installUpdateHandler(&A11yEventSpy::deliver);
        s_current = this;
        // updateAccessibility() only consults some state while active, and
        // View-side emit code may gate on isActive() — force it on. (A no-op
        // if the platform has no accessibility integration; see isForcedActive.)
        QAccessible::setActive(true);
    }

    ~A11yEventSpy()
    {
        QAccessible::setActive(m_wasActive);
        s_current = m_previousSpy;
        QAccessible::installUpdateHandler(m_previousHandler);
    }

    A11yEventSpy(const A11yEventSpy &) = delete;
    A11yEventSpy &operator=(const A11yEventSpy &) = delete;

    const QList<A11yEventRecord> &events() const { return m_events; }
    int count() const { return m_events.size(); }
    void clear() { m_events.clear(); }

    /// Events of one type, optionally restricted to one object.
    QList<A11yEventRecord> eventsOfType(QAccessible::Event type,
                                        const QObject *object = nullptr) const
    {
        QList<A11yEventRecord> out;
        for (const A11yEventRecord &r : m_events)
            if (r.type == type && (!object || r.object == object))
                out.append(r);
        return out;
    }
    int countOfType(QAccessible::Event type, const QObject *object = nullptr) const
    {
        return eventsOfType(type, object).size();
    }

    bool wasActiveBefore() const { return m_wasActive; }

private:
    static void deliver(QAccessibleEvent *event)
    {
        if (s_current)
            s_current->record(event);
    }

    void record(QAccessibleEvent *event)
    {
        A11yEventRecord r;
        r.type = event->type();
        r.object = event->object();
        r.child = event->child();
        switch (event->type()) {
        case QAccessible::TextCaretMoved: {
            auto *e = static_cast<QAccessibleTextCursorEvent *>(event);
            r.a = e->cursorPosition();
            break;
        }
        case QAccessible::TextInserted: {
            auto *e = static_cast<QAccessibleTextInsertEvent *>(event);
            r.a = e->changePosition();
            r.text = e->textInserted();
            break;
        }
        case QAccessible::TextRemoved: {
            auto *e = static_cast<QAccessibleTextRemoveEvent *>(event);
            r.a = e->changePosition();
            r.text = e->textRemoved();
            break;
        }
        case QAccessible::TextSelectionChanged: {
            auto *e = static_cast<QAccessibleTextSelectionEvent *>(event);
            r.a = e->selectionStart();
            r.b = e->selectionEnd();
            break;
        }
        case QAccessible::StateChanged: {
            auto *e = static_cast<QAccessibleStateChangeEvent *>(event);
            r.changedStates = e->changedStates();
            break;
        }
        default:
            break;
        }
        m_events.append(r);
    }

    inline static A11yEventSpy *s_current = nullptr;

    A11yEventSpy *m_previousSpy;
    QAccessible::UpdateHandler m_previousHandler = nullptr;
    bool m_wasActive;
    QList<A11yEventRecord> m_events;
};

}  // namespace MarkoffTest
