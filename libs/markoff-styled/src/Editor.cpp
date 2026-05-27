// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/styled/Editor.h>

#include <QHBoxLayout>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextEdit>

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
}

Editor::~Editor() = default;

// ---- MarkdownView contract ----------------------------------------------

void Editor::setDocument(Markoff::MarkoffDocument *doc) {
    // Binding/StyleApplier wiring lands in Task 3 + Task 4.
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
    auto *bar = m_editor->verticalScrollBar();
    return bar->maximum() == 0 ? 0.0f : float(bar->value());
}

void Editor::setScrollPositionVisualLine(float pos) {
    m_editor->verticalScrollBar()->setValue(int(pos));
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
    emit sessionChanged();
}

// ---- Theme --------------------------------------------------------------

Markoff::Theme Editor::theme() const { return m_theme; }

void Editor::setTheme(const Markoff::Theme &t) {
    m_theme = t;
    emit themeChanged();
}

// ---- LinkService --------------------------------------------------------

Markoff::LinkService *Editor::linkService() const {
    if (m_linkService) return m_linkService;
    // Lazy DefaultLinkService is created on first read in Task 8 wiring.
    return nullptr;
}

void Editor::setLinkService(Markoff::LinkService *svc) {
    if (m_linkService == svc) return;
    m_linkService = svc;
    emit linkServiceChanged();
}

QString Editor::fromContext() const { return m_fromContext; }

void Editor::setFromContext(const QString &c) {
    if (m_fromContext == c) return;
    m_fromContext = c;
    emit fromContextChanged();
}

// ---- Font scale ---------------------------------------------------------

qreal Editor::fontScale() const { return m_fontScale; }

void Editor::setFontScale(qreal s) {
    if (qFuzzyCompare(m_fontScale, s)) return;
    m_fontScale = s;
    emit fontScaleChanged();
}

}  // namespace Markoff::Styled
