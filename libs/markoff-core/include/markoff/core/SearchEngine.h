// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFlags>
#include <QList>
#include <QObject>
#include <QString>

#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

class MarkoffDocument;
class Session;

/// A single search match located within a specific block.
struct MARKOFF_CORE_EXPORT SearchHit {
    BlockId  blockId;
    uint32_t matchStart = 0;  ///< byte offset within block
    uint32_t matchLen   = 0;  ///< byte length of match
};

class MARKOFF_CORE_EXPORT SearchEngine : public QObject {
    Q_OBJECT
public:
    enum FindFlag {
        NoFlags        = 0x00,
        CaseSensitive  = 0x01,
        WholeWords     = 0x02,
        Regex          = 0x04,
        Backwards      = 0x08,
    };
    Q_DECLARE_FLAGS(FindFlags, FindFlag)
    Q_FLAG(FindFlags)

    explicit SearchEngine(QObject *parent = nullptr);
    ~SearchEngine() override;

    /// LEGACY coordinate space: findAll searches `toMarkdown()` (the legacy
    /// flat buffer) and anchors Session selections in that buffer. On a
    /// D2-loaded document the legacy buffer is stale-or-empty, so these find
    /// nothing useful — D2-native consumers use `findByBlock` (FindController
    /// does). No production caller remains; kept for the legacy Session-
    /// selection path and its tests. Migrate or delete alongside the legacy
    /// buffer's retirement (tracked in docs/queue.md).
    int  findAll(MarkoffDocument *, Session *, const QString &needle, FindFlags = NoFlags);
    bool findNext(MarkoffDocument *, Session *);
    bool findPrevious(MarkoffDocument *, Session *);
    void clearMatches(Session *);

    /// D2: iterate blocks directly and return one SearchHit per match.
    /// Does not touch Sessions or Selections; pure query.
    static QList<SearchHit> findByBlock(const MarkoffDocument &doc,
                                        const QString &needle,
                                        FindFlags flags = {});
};

}  // namespace Markoff

Q_DECLARE_OPERATORS_FOR_FLAGS(Markoff::SearchEngine::FindFlags)
