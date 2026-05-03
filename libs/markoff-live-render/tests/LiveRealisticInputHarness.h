// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCoreApplication>
#include <QQuickWindow>
#include <QTest>

namespace Markoff::LiveRender::Test {

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

    void typeChar(QChar c) {
        Qt::Key k = static_cast<Qt::Key>(c.toUpper().unicode());
        Qt::KeyboardModifiers mods = c.isUpper() ? Qt::ShiftModifier
                                                 : Qt::NoModifier;
        keyClick(k, mods);
    }

    void typeString(const QString &text) {
        for (QChar c : text) typeChar(c);
    }

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

}  // namespace Markoff::LiveRender::Test
