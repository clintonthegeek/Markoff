// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/live/MarkoffLiveExport.h>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <qqmlintegration.h>

namespace Markoff::Live {

/// Wraps JKQTMathText to render LaTeX source to a QPixmap.
/// Registered as a QML singleton so delegates can call render() from JS.
class MARKOFF_LIVE_EXPORT MathRenderer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit MathRenderer(QObject *parent = nullptr);

    /// Render `latex` to a QPixmap at the given point size.
    /// Returns a null pixmap if parsing fails.
    Q_INVOKABLE QPixmap render(const QString &latex, bool displayMode,
                               qreal pointSize = 14.0) const;

    /// Returns true if the last render() call succeeded.
    Q_INVOKABLE bool lastRenderOk() const { return m_lastOk; }

private:
    mutable bool m_lastOk = false;
};

}  // namespace Markoff::Live
