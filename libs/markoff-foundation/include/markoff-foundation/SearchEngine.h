// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFlags>
#include <QObject>
#include <QString>

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

class MarkoffDocument;
class Session;

class MARKOFF_FOUNDATION_EXPORT SearchEngine : public QObject {
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

    int  findAll(MarkoffDocument *, Session *, const QString &needle, FindFlags = NoFlags);
    bool findNext(MarkoffDocument *, Session *);
    bool findPrevious(MarkoffDocument *, Session *);
    void clearMatches(Session *);
};

}  // namespace Markoff

Q_DECLARE_OPERATORS_FOR_FLAGS(Markoff::SearchEngine::FindFlags)
