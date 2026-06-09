// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/MarkdownView.h>

#include <algorithm>

#include <QDebug>

#include <markoff/core/MarkoffDocument.h>

namespace Markoff {

MarkdownView::MarkdownView(QWidget *parent) : QWidget(parent) {}
MarkdownView::~MarkdownView() = default;

void MarkdownView::setDocument(MarkoffDocument *doc)
{
    if (m_document == doc) return;
    m_document = doc;
    emit documentChanged(m_document);
}
MarkoffDocument *MarkdownView::document() const { return m_document; }

CursorPos MarkdownView::cursorPosition() const { return {}; }
void MarkdownView::setCursorPosition(CursorPos) {}

float MarkdownView::scrollPositionVisualLine() const { return 0.0f; }
void  MarkdownView::setScrollPositionVisualLine(float) {}

void MarkdownView::setReadOnly(bool ro) { m_readOnly = ro; }
bool MarkdownView::isReadOnly() const   { return m_readOnly; }

void MarkdownView::attachFindController(FindController *)
{
    qWarning() << metaObject()->className()
               << "does not implement attachFindController(); find is unavailable in this view";
}
void MarkdownView::detachFindController() {}

void MarkdownView::undo()
{
    if (auto *doc = document(); doc && !isReadOnly()) doc->undoD2();
}
void MarkdownView::redo()
{
    if (auto *doc = document(); doc && !isReadOnly()) doc->redoD2();
}

Theme MarkdownView::theme() const { return m_theme; }
void MarkdownView::setTheme(const Theme &t)
{
    m_theme = t;
    emit themeChanged();
}

qreal MarkdownView::fontScale() const { return m_fontScale; }
void MarkdownView::setFontScale(qreal s)
{
    s = std::clamp(s, 0.25, 4.0);
    if (qFuzzyCompare(s, m_fontScale)) return;
    m_fontScale = s;
    emit fontScaleChanged(s);
}

} // namespace Markoff
