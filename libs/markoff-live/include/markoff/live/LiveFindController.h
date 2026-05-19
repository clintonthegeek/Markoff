// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>

#include <QList>
#include <QObject>
#include <QString>
#include <qqmlintegration.h>

namespace Markoff::Live {

class LiveBlockModel;
class LiveCursorState;

/// Drives the search loop for the live find UI (`Markoff/Live/FindBar.qml`).
///
/// Owns the current needle string, the list of matches across all blocks,
/// and the index of the currently-highlighted match. Search runs against
/// `LiveBlockModel`'s row texts; navigation calls into `LiveCursorState`
/// to scroll + place caret at each match.
///
/// Instantiated and wired by `LiveListModelBinding`; QML accesses via
/// `binding.findController`.
class MARKOFF_LIVE_EXPORT LiveFindController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveFindController is provided by LiveListModelBinding")

    Q_PROPERTY(QString needle READ needle WRITE setNeedle NOTIFY needleChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY matchesChanged)
    Q_PROPERTY(int currentMatchIndex READ currentMatchIndex NOTIFY currentMatchChanged)
    Q_PROPERTY(bool isActive READ isActive NOTIFY activeChanged)

public:
    explicit LiveFindController(QObject *parent = nullptr);
    ~LiveFindController() override;

    QString needle() const { return m_needle; }
    void    setNeedle(const QString &);

    int  matchCount()        const { return static_cast<int>(m_matches.size()); }
    int  currentMatchIndex() const { return m_currentIndex; }
    bool isActive()          const { return m_isActive; }

    Q_INVOKABLE void activate();
    Q_INVOKABLE void deactivate();
    Q_INVOKABLE void findNext();
    Q_INVOKABLE void findPrevious();

    /// Internal wiring (called by LiveListModelBinding during setup).
    void setBlockModel(LiveBlockModel *);
    void setCursorState(LiveCursorState *);

Q_SIGNALS:
    void needleChanged();
    void matchesChanged();
    void currentMatchChanged();
    void activeChanged();

private:
    struct Match { int row; int startQtPos; int length; };

    void recomputeMatches();
    void seekToCurrent();

    LiveBlockModel  *m_blockModel  = nullptr;
    LiveCursorState *m_cursorState = nullptr;

    QString      m_needle;
    QList<Match> m_matches;
    int          m_currentIndex = -1;
    bool         m_isActive     = false;
};

} // namespace Markoff::Live
