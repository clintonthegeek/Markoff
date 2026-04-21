// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/SourceEditor.h>

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QScrollBar>

#include <qutepart/qutepart.h>

#include <markoff/CursorPos.h>
#include <markoff/FoldSpec.h>
#include <markoff/MarkoffDocument.h>

#include "SourceSearchAdapter.h"

namespace Markoff::Source {

SourceEditor::SourceEditor(QWidget *parent)
    : Markoff::MarkdownView(parent)
{
    m_qutepart = new Qutepart::Qutepart(this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_qutepart);
    m_baseFontPt = m_qutepart->font().pointSize();
    m_searchAdapter = std::make_unique<SourceSearchAdapter>(this);
    setFocusProxy(m_qutepart);
}

SourceEditor::~SourceEditor() = default;

void SourceEditor::setDocument(Markoff::MarkoffDocument *doc)
{
    if (m_markoffDoc == doc) return;

    // Disconnect from the old document before replacing the pointer.
    if (m_markoffDoc) {
        disconnect(m_markoffDoc, nullptr, this, nullptr);
    }
    m_markoffDoc = doc;

    if (!doc) {
        // Disconnected — leave buffer content in place (frozen display).
        return;
    }

    // Disable Qutepart's own QTextDocument undo; canonical's QUndoStack is
    // the authoritative stack.
    m_qutepart->document()->setUndoRedoEnabled(false);

    // Load canonical text into Qutepart's buffer.
    m_applyingCanonicalDelta = true;
    m_qutepart->document()->setPlainText(doc->toMarkdown());
    m_applyingCanonicalDelta = false;

    // Subscribe to canonical events.
    connect(doc, &Markoff::MarkoffDocument::contentsChanged,
            this, &SourceEditor::onCanonicalContentsChanged);
    connect(doc, &Markoff::MarkoffDocument::documentReloaded,
            this, &SourceEditor::onCanonicalReloaded);
    // parseUpdated is not subscribed — Source renders text verbatim, not AST.
}

Markoff::MarkoffDocument *SourceEditor::document() const { return m_markoffDoc; }

QString SourceEditor::toPlainText() const
{
    return m_qutepart ? m_qutepart->toPlainText() : QString();
}

void SourceEditor::onCanonicalContentsChanged(qsizetype offset, qsizetype removed, qsizetype inserted)
{
    if (m_applyingCanonicalDelta) return;
    if (!m_markoffDoc) return;

    m_applyingCanonicalDelta = true;
    QTextCursor c(m_qutepart->document());
    c.setPosition(int(offset));
    c.setPosition(int(offset + removed), QTextCursor::KeepAnchor);
    const QString insertedText = m_markoffDoc->substring(offset, inserted);
    c.insertText(insertedText);
    m_applyingCanonicalDelta = false;
}

void SourceEditor::onCanonicalReloaded()
{
    if (!m_markoffDoc) return;
    m_applyingCanonicalDelta = true;
    m_qutepart->document()->setPlainText(m_markoffDoc->toMarkdown());
    m_applyingCanonicalDelta = false;
}

void SourceEditor::setViewTheme(const Markoff::Theme &) {}
void SourceEditor::setViewResourceProvider(Markoff::ResourceProvider *) {}
void SourceEditor::setViewLinkResolver(Markoff::LinkResolver *) {}

float SourceEditor::scrollPosition() const
{
    if (!m_qutepart) return 0.f;
    return static_cast<float>(m_qutepart->verticalScrollBar()->value());
}

void SourceEditor::setScrollPosition(float v)
{
    if (!m_qutepart) return;
    m_qutepart->verticalScrollBar()->setValue(static_cast<int>(v));
}

void SourceEditor::zoomIn()
{
    QFont f = m_qutepart->font();
    f.setPointSize(f.pointSize() + 1);
    m_qutepart->setFont(f);
}
void SourceEditor::zoomOut()
{
    QFont f = m_qutepart->font();
    if (f.pointSize() > 1) f.setPointSize(f.pointSize() - 1);
    m_qutepart->setFont(f);
}
void SourceEditor::resetZoom()
{
    QFont f = m_qutepart->font();
    if (m_baseFontPt > 0) f.setPointSize(m_baseFontPt);
    m_qutepart->setFont(f);
}

QJsonObject SourceEditor::ephemeralState() const
{
    QJsonObject j;
    j.insert(QStringLiteral("scroll"), scrollPosition());
    return j;
}
void SourceEditor::setEphemeralState(const QJsonObject &j)
{
    setScrollPosition(static_cast<float>(j.value(QStringLiteral("scroll")).toDouble(0.0)));
}

Markoff::SearchAdapter *SourceEditor::searchAdapter() { return m_searchAdapter.get(); }

Markoff::CursorPos SourceEditor::cursorPosition() const
{
    if (!m_qutepart) return {};
    const QTextCursor c = m_qutepart->textCursor();
    return { c.blockNumber() + 1, c.columnNumber() };
}
bool SourceEditor::setCursorPosition(Markoff::CursorPos p)
{
    if (!m_qutepart) return false;
    QTextCursor c(m_qutepart->document());
    const QTextBlock blk = m_qutepart->document()->findBlockByNumber(p.line - 1);
    if (!blk.isValid()) return false;
    c.setPosition(blk.position() + std::min(p.column, blk.length() - 1));
    m_qutepart->setTextCursor(c);
    return true;
}

bool SourceEditor::setReadOnly(bool ro) { m_qutepart->setReadOnly(ro); return true; }
bool SourceEditor::isReadOnly() const   { return m_qutepart->isReadOnly(); }

QVector<Markoff::FoldSpec> SourceEditor::foldedHeadings() const { return {}; }  // Phase A stub
void SourceEditor::setFoldedHeadings(const QVector<Markoff::FoldSpec> &) {}      // Phase A stub

}  // namespace Markoff::Source
