// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <markoff/TextSpan.h>

namespace Markoff {

class MarkoffDocument;
class SearchAdapter;

class SearchController : public QObject {
    Q_OBJECT
public:
    struct Flags {
        bool caseSensitive = true;  // default true: tests define expected behavior
        bool wholeWord = false;
        bool regex = false;
        bool wrap = true;
    };

    SearchController(MarkoffDocument *doc, SearchAdapter *adapter,
                     QObject *parent = nullptr);
    ~SearchController() override;

    void setFlags(Flags);
    Flags flags() const;

    void setQuery(const QString &q);
    QString query() const;

    int matchCount() const;
    int currentIndex() const;
    const QVector<TextSpan> &matches() const;

    void next();
    void prev();

Q_SIGNALS:
    void matchesChanged();
    void currentMatchChanged(int index);

protected:
    void recomputeMatches();
    void notifyAdapterHighlight();
    void scrollToCurrent();

    MarkoffDocument *m_doc = nullptr;
    SearchAdapter *m_adapter = nullptr;
    Flags m_flags;
    QString m_query;
    QVector<TextSpan> m_matches;
    int m_current = -1;
};

}  // namespace Markoff
