// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/FindBar.h>
#include <markoff/source/Editor.h>

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QTextDocument>
#include <QToolButton>

namespace Markoff::Source {

FindBar::FindBar(Editor *editor, QWidget *parent)
    : QWidget(parent), m_editor(editor)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Find"));
    m_prev  = new QToolButton(this);
    m_prev->setIcon(QIcon::fromTheme(QStringLiteral("go-up")));
    m_prev->setToolTip(tr("Previous match"));
    m_next  = new QToolButton(this);
    m_next->setIcon(QIcon::fromTheme(QStringLiteral("go-down")));
    m_next->setToolTip(tr("Next match"));
    m_count = new QLabel(this);
    m_close = new QToolButton(this);
    m_close->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    m_close->setToolTip(tr("Close"));

    layout->addWidget(m_input, 1);
    layout->addWidget(m_count);
    layout->addWidget(m_prev);
    layout->addWidget(m_next);
    layout->addWidget(m_close);

    connect(m_input, &QLineEdit::textChanged, this, &FindBar::onNeedleChanged);
    connect(m_input, &QLineEdit::returnPressed, this, &FindBar::next);
    connect(m_prev,  &QToolButton::clicked, this, &FindBar::prev);
    connect(m_next,  &QToolButton::clicked, this, &FindBar::next);
    connect(m_close, &QToolButton::clicked, this, &FindBar::deactivate);

    hide();
}

void FindBar::activate() {
    show();
    m_input->setFocus();
    m_input->selectAll();
    recomputeMatches();
    m_currentIndex = m_matches.isEmpty() ? -1 : 0;
    highlightAll();
    if (m_currentIndex >= 0) seekTo(m_currentIndex);
    updateCountLabel();
}

void FindBar::deactivate() {
    hide();
    m_matches.clear();
    m_currentIndex = -1;
    if (m_editor) m_editor->plainTextEdit()->setExtraSelections({});
    emit closed();
}

void FindBar::onNeedleChanged(const QString &) {
    recomputeMatches();
    m_currentIndex = m_matches.isEmpty() ? -1 : 0;
    highlightAll();
    if (m_currentIndex >= 0) seekTo(m_currentIndex);
    updateCountLabel();
}

void FindBar::recomputeMatches() {
    m_matches.clear();
    if (!m_editor) return;
    const QString needle = m_input->text();
    if (needle.isEmpty()) return;
    QTextDocument *doc = m_editor->plainTextEdit()->document();
    if (!doc) return;
    QTextCursor c(doc);
    while (true) {
        c = doc->find(needle, c);
        if (c.isNull()) break;
        QTextEdit::ExtraSelection sel;
        sel.cursor = c;
        sel.format.setBackground(QColor("#ffe080"));
        m_matches << sel;
    }
}

void FindBar::highlightAll() {
    if (m_editor) m_editor->plainTextEdit()->setExtraSelections(m_matches);
}

void FindBar::seekTo(int matchIndex) {
    if (!m_editor) return;
    if (matchIndex < 0 || matchIndex >= m_matches.size()) return;
    m_editor->plainTextEdit()->setTextCursor(m_matches[matchIndex].cursor);
    m_editor->plainTextEdit()->ensureCursorVisible();
}

void FindBar::next() {
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex + 1) % m_matches.size();
    seekTo(m_currentIndex);
    updateCountLabel();
}

void FindBar::prev() {
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex - 1 + m_matches.size()) % m_matches.size();
    seekTo(m_currentIndex);
    updateCountLabel();
}

void FindBar::updateCountLabel() {
    if (m_matches.isEmpty()) {
        m_count->setText(m_input->text().isEmpty() ? QString() : tr("No matches"));
        return;
    }
    m_count->setText(tr("%1 of %2")
        .arg(m_currentIndex + 1).arg(m_matches.size()));
}

} // namespace Markoff::Source
