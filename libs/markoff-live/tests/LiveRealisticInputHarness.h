// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCoreApplication>
#include <QQuickWindow>
#include <QTest>

namespace Markoff::Live::Test {

/// Realistic-keyboard-timing harness for async-UX tests.
///
/// QTest::keyClick alone is wrong: it delivers events synchronously
/// between event-loop spins, masking async races (the v0 holes' F2
/// character-scramble passed a QTest::keyClick-driven test while
/// scrambling on real keyboard input). This harness interposes
/// qWait + processEvents between every key event so async paths
/// have a chance to run between keystrokes.
class LiveRealisticInputHarness {
public:
    explicit LiveRealisticInputHarness(QQuickWindow *window,
                                       int defaultGapMs = 30)
        : m_window(window), m_defaultGapMs(defaultGapMs) {}

    void keyClick(Qt::Key key,
                  Qt::KeyboardModifiers mods = Qt::NoModifier) {
        keyClick(key, mods, m_defaultGapMs);
    }

    void keyClick(Qt::Key key,
                  Qt::KeyboardModifiers mods,
                  int gapMs) {
        QTest::keyClick(m_window, key, mods);
        QTest::qWait(gapMs);
        QCoreApplication::processEvents();
    }

    /// ASCII letters, digits, and spaces only.
    ///
    /// Uses QTest::keyClicks(window, char) — the char overload populates the
    /// key event's text() field from the char value (e.g. 'T' → text="T"),
    /// which is what Qt Quick TextEdit uses for insertion. The Key+Modifier
    /// overload may produce an empty text() on some platforms in headless-test
    /// mode, causing the Shift modifier to be silently discarded.
    void typeChar(QChar c) {
        if (c == QLatin1Char(' ')) {
            // Space has no char shortcut in QTest; use the key overload.
            keyClick(Qt::Key_Space);
            return;
        }
        Q_ASSERT(c.unicode() < 0x80 && c.isLetterOrNumber());
        // Deliver via the char overload so text() is populated correctly.
        QTest::keyClick(m_window, c.toLatin1());
        QTest::qWait(m_defaultGapMs);
        QCoreApplication::processEvents();
    }

    void typeString(const QString &text) {
        for (QChar c : text) typeChar(c);
    }

    /// Batches `chars.size()` keystrokes back-to-back with NO inter-
    /// keystroke wait, then a single `pauseMs` settle at the end. Use
    /// for testing batched-event paths and "fast typist" scenarios — NOT
    /// for exposing async races; for that, use `typeString`.
    void burst(const QString &chars, int pauseMs) {
        for (QChar c : chars)
            QTest::keyClick(m_window, static_cast<Qt::Key>(c.toUpper().unicode()));
        QTest::qWait(pauseMs);
        QCoreApplication::processEvents();
    }

    void idle(int durationMs) {
        QTest::qWait(durationMs);
        QCoreApplication::processEvents();
    }

    int defaultGapMs() const { return m_defaultGapMs; }
    void setDefaultGapMs(int ms) { m_defaultGapMs = ms; }

private:
    QQuickWindow *m_window;
    int m_defaultGapMs;
};

}  // namespace Markoff::Live::Test
