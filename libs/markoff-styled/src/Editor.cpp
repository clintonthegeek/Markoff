// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/styled/Editor.h>

#include "LinkInteraction.h"
#include "StyleApplier.h"

#include <QHBoxLayout>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextEdit>

#include <markoff/core/DefaultLinkService.h>
#include <markoff/core/LinkService.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/SourceTextDocumentBinding.h>

namespace Markoff::Styled {

Editor::Editor(QWidget *parent)
    : Markoff::MarkdownView(parent),
      m_editor(new QTextEdit(this)) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editor);
    setLayout(layout);

    // Anchors (links) are styled but Qt must not attempt to open them
    // (QTextEdit has no setOpenLinks; link activation is handled by
    // LinkInteraction in Task 10 instead).
    m_editor->setTextInteractionFlags(Qt::TextEditorInteraction
                                      | Qt::LinksAccessibleByMouse);
    m_editor->viewport()->setMouseTracking(true);

    m_styleApplier = new StyleApplier(this);
    m_styleApplier->setTextDocument(m_editor->document());
    m_styleApplier->setTheme(&m_theme);

    m_linkInteract = new LinkInteraction(m_editor, this);
    // Propagate the lazy default so LinkInteraction always has a non-null
    // service even before any setLinkService() call (spec §4).
    m_linkInteract->setLinkService(linkService());
}

Editor::~Editor() = default;

// ---- MarkdownView contract ----------------------------------------------

void Editor::setDocument(Markoff::MarkoffDocument *doc) {
    if (document() == doc) {
        Markoff::MarkdownView::setDocument(doc);
        return;
    }

    if (!m_binding) {
        m_binding = new Markoff::SourceTextDocumentBinding(this);
        m_binding->setTextDocument(m_editor->document());
    }

    m_binding->setMarkoffDocument(doc);
    m_styleApplier->setMarkoffDocument(doc);
    if (m_linkInteract) m_linkInteract->setMarkoffDocument(doc);
    if (m_session) m_binding->setSession(m_session.data());

    Markoff::MarkdownView::setDocument(doc);
}

Markoff::CursorPos Editor::cursorPosition() const {
    QTextCursor c = m_editor->textCursor();
    const QTextBlock blk = c.block();
    return { blk.blockNumber() + 1, c.positionInBlock() + 1 };
}

void Editor::setCursorPosition(Markoff::CursorPos pos) {
    QTextCursor c = m_editor->textCursor();
    QTextBlock blk = m_editor->document()->findBlockByNumber(pos.line - 1);
    if (!blk.isValid()) return;
    c.setPosition(blk.position() + qMax(0, pos.column - 1));
    m_editor->setTextCursor(c);
}

float Editor::scrollPositionVisualLine() const {
    auto *sb = m_editor->verticalScrollBar();
    if (!sb || sb->maximum() == 0) return 0.0f;
    return static_cast<float>(sb->value())
         / static_cast<float>(sb->maximum());
}

void Editor::setScrollPositionVisualLine(float pos) {
    auto *sb = m_editor->verticalScrollBar();
    if (!sb || sb->maximum() == 0) return;
    sb->setValue(static_cast<int>(pos
                                  * static_cast<float>(sb->maximum())));
}

void Editor::setReadOnly(bool ro) {
    m_editor->setReadOnly(ro);
    Markoff::MarkdownView::setReadOnly(ro);
}

bool Editor::isReadOnly() const { return m_editor->isReadOnly(); }

// ---- Session ------------------------------------------------------------

Markoff::Session *Editor::session() const { return m_session.data(); }

void Editor::setSession(Markoff::Session *s) {
    if (m_session.data() == s) return;
    m_session = s;
    if (m_binding) m_binding->setSession(s);
    emit sessionChanged();
}

// ---- Theme --------------------------------------------------------------

Markoff::Theme Editor::theme() const { return m_theme; }

void Editor::setTheme(const Markoff::Theme &t) {
    m_theme = t;
    if (m_styleApplier) m_styleApplier->setTheme(&m_theme);
    emit themeChanged();
}

// ---- LinkService --------------------------------------------------------

Markoff::LinkService *Editor::linkService() const {
    if (m_linkService) return m_linkService;
    // Lazily create a DefaultLinkService so the Editor is functional
    // standalone (spec §4). Cached in m_defaultLink; ownership = this.
    if (!m_defaultLink) {
        m_defaultLink = new Markoff::DefaultLinkService(
            const_cast<Editor *>(this));
    }
    return m_defaultLink;
}

void Editor::setLinkService(Markoff::LinkService *svc) {
    if (m_linkService == svc) return;
    m_linkService = svc;
    // Pass linkService() (not svc) so LinkInteraction gets the lazy
    // default when svc is nullptr, keeping it non-null at all times.
    if (m_linkInteract) m_linkInteract->setLinkService(linkService());
    emit linkServiceChanged();
}

QString Editor::fromContext() const { return m_fromContext; }

void Editor::setFromContext(const QString &c) {
    if (m_fromContext == c) return;
    m_fromContext = c;
    if (m_linkInteract) m_linkInteract->setFromContext(c);
    emit fromContextChanged();
}

// ---- Font scale ---------------------------------------------------------

qreal Editor::fontScale() const { return m_fontScale; }

void Editor::setFontScale(qreal s) {
    if (qFuzzyCompare(m_fontScale, s)) return;
    m_fontScale = s;
    if (m_styleApplier) m_styleApplier->setFontScale(s);
    emit fontScaleChanged();
}

}  // namespace Markoff::Styled
