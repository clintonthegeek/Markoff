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

    disconnect(m_documentDestroyed);
    m_documentDestroyed = {};
    m_document = doc;

    if (doc) {
        // Retire-on-destroy: every leaf holds the document by pointer, and the
        // host may delete it while the view is still attached. Null the base
        // pointer so no accessor (cursorPosition/undo/redo/…) can dereference
        // freed memory, and tell listeners the view is now document-less.
        m_documentDestroyed = connect(doc, &QObject::destroyed, this, [this] {
            m_documentDestroyed = {};
            m_document = nullptr;
            emit documentChanged(nullptr);
        });
    }

    emit documentChanged(m_document.data());
}

MarkoffDocument *MarkdownView::document() const { return m_document.data(); }

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
