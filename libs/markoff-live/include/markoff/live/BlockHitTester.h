// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>

#include <QObject>
#include <QVariantMap>
#include <qqmlintegration.h>

namespace Markoff::Live {

/// Lightweight C++ anchor for the live-view hit-test.
///
/// The hit-test coordinate math lives in LiveView.qml as a JS function
/// (where itemAt / positionAt are natural QML invocables with no
/// C++→QML pointer-return-type conversion issues). QML calls
/// `reportHit(blockIndex, qtPos)` after computing the result so that
/// C++ can react via the `hitReported` signal.
///
/// `BlockHitTester` is exposed as a QML property of `LiveListModelBinding`
/// so that `LiveView.qml` can bind to it in Component.onCompleted and
/// connect signals.
class MARKOFF_LIVE_EXPORT BlockHitTester : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("BlockHitTester is provided by LiveListModelBinding")

public:
    explicit BlockHitTester(QObject *parent = nullptr);

    int lastBlockIndex() const { return m_lastBlockIndex; }
    int lastQtPos()      const { return m_lastQtPos; }

    /// Called from QML after the JS hit() function resolves a block + offset.
    Q_INVOKABLE void reportHit(int blockIndex, int qtPos);

Q_SIGNALS:
    void hitReported(int blockIndex, int qtPos);

private:
    int m_lastBlockIndex = -1;
    int m_lastQtPos      = -1;
};

}  // namespace Markoff::Live
