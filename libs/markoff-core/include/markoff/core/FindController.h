// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <markoff/core/BlockAnchor.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/SearchEngine.h>

namespace Markoff {

class MarkoffDocument;

/// Drives a find session against a MarkoffDocument. Holds the needle,
/// the match list, and the currently-selected match index. Operates on
/// the document directly (block-by-block via SearchEngine::findByBlock);
/// never touches focus, cursors, or scroll.
///
/// View leaves subscribe via their own attach hooks (e.g.
/// LiveListModelBinding::attachFindController) and render highlights /
/// respond to navigationRequested in their own idiom.
///
/// Lifetime: owned by the consumer. May later be assumed by a
/// Markoff::Session-scope owner; the API does not change.
class MARKOFF_CORE_EXPORT FindController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString needle             READ needle WRITE setNeedle NOTIFY needleChanged)
    Q_PROPERTY(int     matchCount         READ matchCount             NOTIFY matchesChanged)
    Q_PROPERTY(int     currentMatchIndex  READ currentMatchIndex      NOTIFY currentMatchChanged)
    Q_PROPERTY(bool    isActive           READ isActive               NOTIFY activeChanged)

public:
    /// View-agnostic match descriptor. Byte offsets within the block's
    /// current text — same units as SearchEngine::SearchHit.
    struct Match {
        Markoff::BlockAnchor block;
        quint32              byteOffset = 0;
        quint32              byteLength = 0;
    };

    explicit FindController(MarkoffDocument *doc, QObject *parent = nullptr);
    ~FindController() override;

    QString needle() const            { return m_needle; }
    void    setNeedle(const QString &);

    const QList<Match> &matches() const { return m_matches; }
    int  matchCount()         const   { return static_cast<int>(m_matches.size()); }
    int  currentMatchIndex()  const   { return m_currentIndex; }
    bool isActive()           const   { return m_isActive; }

    /// Optional flags. Default is case-insensitive (NoFlags). Whole-words /
    /// regex are accepted by the underlying SearchEngine but UI work is
    /// deferred (see spec § Out of scope).
    SearchEngine::FindFlags flags() const { return m_flags; }
    void setFlags(SearchEngine::FindFlags);

    Q_INVOKABLE void activate();
    Q_INVOKABLE void deactivate();
    Q_INVOKABLE void findNext();
    Q_INVOKABLE void findPrevious();

    /// Move the current-match selection to the first match at or after
    /// (block, offset) in document order, wrapping to index 0 if none.
    /// Mutation-free: touches only currentMatchIndex (emits currentMatchChanged).
    /// Never touches the document, focus, cursor, or scroll (invariant D3).
    void selectMatchAtOrAfter(Markoff::BlockAnchor block, quint32 offset);

Q_SIGNALS:
    void needleChanged();
    void matchesChanged();
    void currentMatchChanged();
    void activeChanged();

    /// Emitted by findNext / findPrevious only — never by setNeedle. The
    /// active view's adapter MAY scroll the match into view and place a
    /// non-focusing caret in response. The adapter MUST NOT take focus.
    void navigationRequested(Markoff::FindController::Match match);

private:
    void recomputeMatches();

    MarkoffDocument         *m_doc          = nullptr;
    QString                  m_needle;
    SearchEngine::FindFlags  m_flags        = SearchEngine::NoFlags;
    QList<Match>             m_matches;
    int                      m_currentIndex = -1;
    bool                     m_isActive     = false;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::FindController::Match)
