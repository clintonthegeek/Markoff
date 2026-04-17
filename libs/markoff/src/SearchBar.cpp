// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/SearchBar.h"

#include <QGridLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QKeyEvent>
#include <QIcon>

namespace Markoff {

SearchBar::SearchBar(QWidget *parent)
    : QWidget(parent)
{
    // Opaque background + a top border so the bar reads as a distinct
    // docked strip rather than a transparent overlay on the document.
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Window);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "Markoff--SearchBar { "
        "  background: palette(window); "
        "  border-top: 1px solid palette(mid); "
        "}"));

    buildUi();
    hide();
}

void SearchBar::buildUi()
{
    // Grid layout so both rows share column widths:
    //   col 0 = edit (stretch)
    //   col 1 = prev / replace
    //   col 2 = next / replace-all
    //   col 3 = case / —
    //   col 4 = count / —
    //   col 5 = close / —
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(2);
    grid->setVerticalSpacing(2);

    // --- Row 0: find ---
    m_findEdit = new QLineEdit;
    m_findEdit->setPlaceholderText(tr("Find..."));
    m_findEdit->setClearButtonEnabled(true);

    m_prevButton = new QToolButton;
    m_prevButton->setIcon(QIcon::fromTheme(QStringLiteral("go-up")));
    m_prevButton->setToolTip(tr("Find Previous (Shift+Enter)"));
    m_prevButton->setAutoRaise(true);

    m_nextButton = new QToolButton;
    m_nextButton->setIcon(QIcon::fromTheme(QStringLiteral("go-down")));
    m_nextButton->setToolTip(tr("Find Next (Enter)"));
    m_nextButton->setAutoRaise(true);

    m_caseButton = new QToolButton;
    m_caseButton->setText(QStringLiteral("Aa"));
    m_caseButton->setToolTip(tr("Match Case"));
    m_caseButton->setCheckable(true);
    m_caseButton->setAutoRaise(true);

    m_countLabel = new QLabel;
    m_countLabel->setMinimumWidth(60);
    m_countLabel->setAlignment(Qt::AlignCenter);

    m_closeButton = new QToolButton;
    m_closeButton->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    m_closeButton->setToolTip(tr("Close (Escape)"));
    m_closeButton->setAutoRaise(true);

    grid->addWidget(m_findEdit,    0, 0);
    grid->addWidget(m_prevButton,  0, 1);
    grid->addWidget(m_nextButton,  0, 2);
    grid->addWidget(m_caseButton,  0, 3);
    grid->addWidget(m_countLabel,  0, 4);
    grid->addWidget(m_closeButton, 0, 5);

    // --- Row 1: replace (hidden by default) ---
    m_replaceEdit = new QLineEdit;
    m_replaceEdit->setPlaceholderText(tr("Replace..."));
    m_replaceEdit->setClearButtonEnabled(true);

    m_replaceButton = new QToolButton;
    m_replaceButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-find-replace")));
    m_replaceButton->setToolTip(tr("Replace (Enter in replace field)"));
    m_replaceButton->setAutoRaise(true);

    m_replaceAllButton = new QToolButton;
    m_replaceAllButton->setText(tr("All"));
    m_replaceAllButton->setToolTip(tr("Replace All"));
    m_replaceAllButton->setAutoRaise(true);

    grid->addWidget(m_replaceEdit,      1, 0);
    grid->addWidget(m_replaceButton,    1, 1);
    grid->addWidget(m_replaceAllButton, 1, 2);

    grid->setColumnStretch(0, 1);

    m_replaceEdit->hide();
    m_replaceButton->hide();
    m_replaceAllButton->hide();

    // Connections
    connect(m_findEdit, &QLineEdit::textChanged,
            this, &SearchBar::searchTextChanged);
    connect(m_nextButton, &QToolButton::clicked,
            this, &SearchBar::findNext);
    connect(m_prevButton, &QToolButton::clicked,
            this, &SearchBar::findPrevious);
    connect(m_closeButton, &QToolButton::clicked,
            this, &SearchBar::closed);
    connect(m_caseButton, &QToolButton::toggled,
            this, [this]() { emit searchTextChanged(m_findEdit->text()); });
    connect(m_replaceButton, &QToolButton::clicked,
            this, &SearchBar::replaceRequested);
    connect(m_replaceAllButton, &QToolButton::clicked,
            this, &SearchBar::replaceAllRequested);
}

QString SearchBar::searchText() const
{
    return m_findEdit->text();
}

void SearchBar::setSearchText(const QString &text)
{
    m_findEdit->setText(text);
}

bool SearchBar::matchCase() const
{
    return m_caseButton->isChecked();
}

QString SearchBar::replaceText() const
{
    return m_replaceEdit->text();
}

void SearchBar::setMatchCount(int current, int total)
{
    if (total == 0) {
        m_countLabel->setText(tr("No results"));
    } else if (total > 65536) {
        m_countLabel->setText(tr("65536+ matches"));
    } else {
        m_countLabel->setText(tr("%1 of %2").arg(current).arg(total));
    }
}

void SearchBar::showFind()
{
    m_replaceEdit->hide();
    m_replaceButton->hide();
    m_replaceAllButton->hide();
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void SearchBar::showReplace()
{
    m_replaceEdit->show();
    m_replaceButton->show();
    m_replaceAllButton->show();
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void SearchBar::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        emit closed();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (m_replaceEdit->hasFocus()) {
            emit replaceRequested();
        } else if (e->modifiers() & Qt::ShiftModifier) {
            emit findPrevious();
        } else {
            emit findNext();
        }
        return;
    }
    QWidget::keyPressEvent(e);
}

} // namespace Markoff
