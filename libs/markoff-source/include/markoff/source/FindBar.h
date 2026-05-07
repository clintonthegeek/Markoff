// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QTextEdit>
#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;

namespace Markoff::Source::Widget {

class Editor;

class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(Editor *editor, QWidget *parent = nullptr);

public Q_SLOTS:
    void activate();   // show + focus + (re-search current needle)
    void deactivate(); // hide + clear highlights

Q_SIGNALS:
    void closed();

private Q_SLOTS:
    void onNeedleChanged(const QString &);
    void next();
    void prev();

private:
    void recomputeMatches();
    void highlightAll();
    void seekTo(int matchIndex);
    void updateCountLabel();

    Editor      *m_editor   = nullptr;
    QLineEdit   *m_input    = nullptr;
    QToolButton *m_prev     = nullptr;
    QToolButton *m_next     = nullptr;
    QToolButton *m_close    = nullptr;
    QLabel      *m_count    = nullptr;
    QList<QTextEdit::ExtraSelection> m_matches;
    int          m_currentIndex = -1;
};

} // namespace Markoff::Source::Widget
