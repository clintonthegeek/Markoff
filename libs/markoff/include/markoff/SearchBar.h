// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_SEARCHBAR_H
#define MARKOFF_SEARCHBAR_H

#include <QWidget>

class QLineEdit;
class QToolButton;
class QLabel;

namespace Markoff {

class SearchBar : public QWidget {
    Q_OBJECT

public:
    explicit SearchBar(QWidget *parent = nullptr);

    QString searchText() const;
    void setSearchText(const QString &text);
    bool matchCase() const;

    /// Show the bar in find-only mode.
    void showFind();
    /// Show the bar in find+replace mode.
    void showReplace();

    QString replaceText() const;

    void setMatchCount(int current, int total);

Q_SIGNALS:
    void searchTextChanged(const QString &text);
    void findNext();
    void findPrevious();
    void replaceRequested();
    void replaceAllRequested();
    void closed();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    void buildUi();

    QLineEdit *m_findEdit = nullptr;
    QLineEdit *m_replaceEdit = nullptr;
    QToolButton *m_prevButton = nullptr;
    QToolButton *m_nextButton = nullptr;
    QToolButton *m_caseButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QLabel *m_countLabel = nullptr;

    // Replace row widgets (shown/hidden together)
    QToolButton *m_replaceButton = nullptr;
    QToolButton *m_replaceAllButton = nullptr;
};

} // namespace Markoff

#endif // MARKOFF_SEARCHBAR_H
