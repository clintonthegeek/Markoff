// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/MarkdownView.h>

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

void MarkdownView::showFindBar()    {}
void MarkdownView::showReplaceBar() {}
void MarkdownView::hideFindBar()    {}

} // namespace Markoff
