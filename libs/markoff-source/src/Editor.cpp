// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/Editor.h>
#include "Detail/SourceFindAdapter.h"
#include "Gutter.h"
#include "InnerEditor.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/Detail/FlatBlockResolve.h>
#include <markoff/core/EditorContext.h>
#include <markoff/core/FormatOps.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/SourceTextDocumentBinding.h>

#include <QKeyEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QVBoxLayout>

#include <optional>

namespace Markoff::Source {

using Detail::Gutter;
using Detail::InnerEditor;

namespace {
KSyntaxHighlighting::Repository &repo() {
    static KSyntaxHighlighting::Repository r;
    return r;
}
} // anon

Editor::Editor(QWidget *parent)
    : Markoff::MarkdownView(parent),
      m_editor(new InnerEditor(this)),
      m_binding(new Markoff::SourceTextDocumentBinding(this)),
      m_highlighter(new KSyntaxHighlighting::SyntaxHighlighter(this)),
      m_theme(Markoff::Theme::defaultLight())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editor);

    // The binding talks to the QPlainTextEdit's underlying QTextDocument.
    // It also disables that QTextDocument's own undo stack — CRDT undo via
    // MarkoffDocument is canonical (see SourceTextDocumentBinding::rewireQtDocument).
    m_binding->setTextDocument(m_editor->document());
    connect(m_binding, &Markoff::SourceTextDocumentBinding::caretResolved,
            this, [this](int start, int active) {
                QTextCursor c(m_editor->document());
                c.setPosition(start);
                if (active != start)
                    c.setPosition(active, QTextCursor::KeepAnchor);
                m_editor->setTextCursor(c);
            });

    // Parent the highlighter to the Editor (not to the QTextDocument) so its
    // lifetime is tied to the Editor regardless of any later setDocument() on
    // the underlying QPlainTextEdit.
    m_highlighter->setDocument(m_editor->document());
    m_highlighter->setDefinition(repo().definitionForName(QStringLiteral("Markdown")));
    m_highlighter->setTheme(repo().defaultTheme(KSyntaxHighlighting::Repository::LightTheme));

    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    m_gutter = new Gutter(this);
    m_findAdapter = new Detail::SourceFindAdapter(this, this);
    connect(m_editor, &QPlainTextEdit::blockCountChanged,
            this, [this]() { recomputeGutterWidth(); });
    connect(m_editor, &QPlainTextEdit::updateRequest,
            this, [this](const QRect &rect, int dy) {
        if (dy) m_gutter->scroll(0, dy);
        else m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
        if (rect.contains(m_editor->viewport()->rect())) recomputeGutterWidth();
    });
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged,
            this, [this]() { if (m_gutter) m_gutter->update(); });
    recomputeGutterWidth();

    // Forward key events (undo/redo) from the inner editor
    m_editor->installEventFilter(this);
}

Editor::~Editor() = default;

void Editor::setDocument(Markoff::MarkoffDocument *doc) {
    if (auto *prev = Markoff::MarkdownView::document()) {
        if (prev == doc) return;
        // Tear down the previous session, if any.
        if (m_session) {
            m_binding->setSession(nullptr);
            prev->destroySession(m_session.data());
            m_session = nullptr;
        }
        // No direct prev→this connections exist; SourceTextDocumentBinding
        // owns all doc subscriptions and cleans them up via setMarkoffDocument(nullptr).
    }

    // Disconnect the stale d2DocumentChanged→applyParagraphMargins connection
    // from the previous document before wiring the new one.
    if (m_paragraphMarginsCon) {
        QObject::disconnect(m_paragraphMarginsCon);
        m_paragraphMarginsCon = {};
    }
    // Disconnect stale context connections.
    if (m_contextD2Con) {
        QObject::disconnect(m_contextD2Con);
        m_contextD2Con = {};
    }
    if (m_contextCursorCon) {
        QObject::disconnect(m_contextCursorCon);
        m_contextCursorCon = {};
    }

    Markoff::MarkdownView::setDocument(doc);

    if (doc) {
        m_session = doc->createSession();
        m_binding->setMarkoffDocument(doc);
        m_binding->setSession(m_session.data());
        m_paragraphMarginsCon = QObject::connect(
            doc, &Markoff::MarkoffDocument::d2DocumentChanged,
            this, &Editor::applyParagraphMargins);
        // Wire context-refresh. Reset the sentinel so the first cursor
        // movement after setDocument() always emits contextChanged.
        m_lastContext = Markoff::EditorContext{};
        m_lastContext.blockKind = QString{};  // sentinel: not a valid kind name
        // Connect context-refresh to cursor movement only. The source leaf
        // delegates kind inference to structural keys (Enter, Backspace) which
        // always move the cursor; connecting d2DocumentChanged is unnecessary
        // and causes false-fires from the syntax highlighter's format-only
        // contentsChange notifies (which reach d2DocumentChanged via the
        // binding's no-op edit path).
        m_contextCursorCon = QObject::connect(
            m_editor, &QPlainTextEdit::cursorPositionChanged,
            this, &Editor::recomputeContext);
        // Initial pass — the binding seeded qdoc from widgetFlatView in
        // setMarkoffDocument; margins for the initial blocks need to land too.
        applyParagraphMargins();
    } else {
        m_binding->setMarkoffDocument(nullptr);
    }
}

Markoff::CursorPos Editor::cursorPosition() const {
    auto cursor = m_editor->textCursor();
    return { cursor.blockNumber() + 1, cursor.positionInBlock() + 1 };
}

void Editor::setCursorPosition(Markoff::CursorPos pos) {
    auto block = m_editor->document()->findBlockByNumber(pos.line - 1);
    if (!block.isValid())
        block = m_editor->document()->lastBlock();
    QTextCursor cursor(block);
    // length() includes the block separator, so length()-1 is end-of-text.
    cursor.setPosition(block.position()
                       + qMin(qMax(0, pos.column - 1), block.length() - 1));
    m_editor->setTextCursor(cursor);
}

float Editor::scrollPositionVisualLine() const {
    auto *sb = m_editor->verticalScrollBar();
    if (!sb || sb->maximum() == 0) return 0.0f;
    return static_cast<float>(sb->value()) / static_cast<float>(sb->maximum());
}

void Editor::setScrollPositionVisualLine(float pos) {
    auto *sb = m_editor->verticalScrollBar();
    if (!sb || sb->maximum() == 0) return;
    sb->setValue(static_cast<int>(pos * static_cast<float>(sb->maximum())));
}

void Editor::setReadOnly(bool ro) {
    Markoff::MarkdownView::setReadOnly(ro);
    m_editor->setReadOnly(ro);
}

bool Editor::isReadOnly() const { return m_editor->isReadOnly(); }

void Editor::attachFindController(Markoff::FindController *fc)
{
    if (m_findAdapter) m_findAdapter->attach(fc);
}

void Editor::detachFindController()
{
    if (m_findAdapter) m_findAdapter->detach();
}

Markoff::Theme Editor::theme() const { return m_theme; }

void Editor::setTheme(const Markoff::Theme &t) {
    MarkdownView::setTheme(t);  // base stores + emits themeChanged
    m_theme = t;
    QPalette p = m_editor->palette();
    p.setColor(QPalette::Base,            t.color(Markoff::Theme::Slot::EditorBackground));
    p.setColor(QPalette::Text,            t.color(Markoff::Theme::Slot::TextDefault));
    p.setColor(QPalette::Highlight,       t.color(Markoff::Theme::Slot::SelectionBackground));
    p.setColor(QPalette::HighlightedText, t.color(Markoff::Theme::Slot::TextDefault));
    m_editor->setPalette(p);

    const bool darkUi = t.color(Markoff::Theme::Slot::EditorBackground).lightnessF() < 0.5;
    if (m_highlighter) {
        m_highlighter->setTheme(repo().defaultTheme(
            darkUi ? KSyntaxHighlighting::Repository::DarkTheme
                   : KSyntaxHighlighting::Repository::LightTheme));
        m_highlighter->rehighlight();
    }
    if (m_gutter) m_gutter->update();
}

bool Editor::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        auto *e = static_cast<QKeyEvent *>(event);
        const auto m = e->modifiers();
        auto *doc = Markoff::MarkdownView::document();
        if (doc && (m & Qt::ControlModifier)) {
            if (e->key() == Qt::Key_Z && !(m & Qt::ShiftModifier)) {
                doc->undoD2();
                return true;
            }
            if (e->key() == Qt::Key_Y || (e->key() == Qt::Key_Z && (m & Qt::ShiftModifier))) {
                doc->redoD2();
                return true;
            }
        }
    }
    return Markoff::MarkdownView::eventFilter(watched, event);
}

void Editor::resizeEvent(QResizeEvent *e) {
    Markoff::MarkdownView::resizeEvent(e);
    // Gutter is not in the layout; position it to match the inner editor.
    QRect r = m_editor->geometry();
    m_gutter->setGeometry(QRect(r.left(), r.top(), gutterWidth(), r.height()));
}

int Editor::gutterWidth() const {
    int digits = 1;
    int max = qMax(1, m_editor->blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    return 3 + m_editor->fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 6;
}

void Editor::recomputeGutterWidth() {
    static_cast<InnerEditor *>(m_editor)->setViewportMargins(gutterWidth(), 0, 0, 0);
}

// --- Markdown format operations -------------------------------------------
//
// The format-op logic lives in Markoff::FormatOps (markoff-core), hoisted
// from this file 2026-06-09 (MarkdownView contract v2 §5) so the styled
// leaf can share it. The verbs below are thin wrappers: extract the
// selection from the inner QPlainTextEdit, call FormatOps with
// toPlainText() (== widgetFlatView()), re-apply the returned
// caret/selection. FormatOps flushes the model itself, so the binding has
// already synced the QTextDocument by the time the range comes back.

namespace {

// Re-apply a FormatOps result to the inner editor's cursor. A nullopt
// result means no edit was performed; leave the cursor untouched (matching
// the pre-hoist early-return paths, which never called setTextCursor).
void applyFormatOpsResult(QPlainTextEdit *te,
                          const std::optional<Markoff::FormatOps::QtRange> &r) {
    if (!te || !r) return;
    QTextCursor c2 = te->textCursor();
    c2.setPosition(r->start);
    if (r->end != r->start)
        c2.setPosition(r->end, QTextCursor::KeepAnchor);
    te->setTextCursor(c2);
}

void wrapToggle(QPlainTextEdit *te,
                Markoff::SourceTextDocumentBinding *binding,
                const QByteArray &delim) {
    if (!te || !binding) return;
    Markoff::MarkoffDocument *doc = binding->markoffDocument();
    if (!doc) return;
    const QTextCursor c = te->textCursor();
    applyFormatOpsResult(
        te, Markoff::FormatOps::wrapToggle(
                doc, te->toPlainText(),
                {c.selectionStart(), c.selectionEnd()}, delim));
}

} // namespace

// Format verbs are blocked while read-only (MarkdownView contract §10
// check 2) — they mutate via d2ApplyBufferEdit, so the inner widget's
// readOnly flag alone would not stop them.
void Editor::toggleBold()          { if (isReadOnly()) return; wrapToggle(m_editor, m_binding, "**"); }
void Editor::toggleItalic()        { if (isReadOnly()) return; wrapToggle(m_editor, m_binding, "_");  }
void Editor::toggleStrikethrough() { if (isReadOnly()) return; wrapToggle(m_editor, m_binding, "~~"); }
void Editor::toggleInlineCode()    { if (isReadOnly()) return; wrapToggle(m_editor, m_binding, "`");  }

void Editor::insertLink() {
    if (isReadOnly()) return;
    if (!m_editor || !m_binding) return;
    Markoff::MarkoffDocument *doc = m_binding->markoffDocument();
    if (!doc) return;
    const QTextCursor c = m_editor->textCursor();
    applyFormatOpsResult(
        m_editor, Markoff::FormatOps::insertLink(
                      doc, m_editor->toPlainText(),
                      {c.selectionStart(), c.selectionEnd()}));
}

void Editor::setHeadingLevel(int level) {
    if (isReadOnly()) return;
    if (!m_editor || !m_binding) return;
    auto *doc = m_binding->markoffDocument();
    if (!doc) return;
    // FormatOps validates the level and resolves the caret's line via the
    // shared block-aware helpers (see FormatOps.cpp for the 2026-05-21
    // boundary-merge root-cause writeup).
    applyFormatOpsResult(
        m_editor, Markoff::FormatOps::setHeadingLevel(
                      doc, m_editor->toPlainText(),
                      m_editor->textCursor().position(), level));
}

namespace {

// Map BlockKind enum to the canonical BlockKindNames string.
const char *blockKindToName(Markoff::BlockKind kind) {
    using BK = Markoff::BlockKind;
    namespace BKN = Markoff::BlockKindNames;
    switch (kind) {
    case BK::Paragraph:      return BKN::Paragraph;
    case BK::Heading:        return BKN::Heading;
    case BK::CodeBlock:      return BKN::CodeBlock;
    case BK::ListItem:       return BKN::ListItem;
    case BK::BlockQuote:     return BKN::Blockquote;
    case BK::HorizontalRule: return BKN::HorizontalRule;
    case BK::Image:          return BKN::Image;
    case BK::Math:           return BKN::Math;
    case BK::Table:          return BKN::Table;
    default:                 return BKN::Paragraph;  // Mermaid, HtmlBlock → graceful fallback
    }
}

} // namespace

void Editor::recomputeContext()
{
    auto *doc = Markoff::MarkdownView::document();
    if (!doc || !m_editor) return;

    // Map the caret qt-position (UTF-16 char offset in the QTextDocument) to
    // a sep-view byte offset, then look up the containing block.
    const QTextCursor cursor = m_editor->textCursor();
    const int qtPos = cursor.position();
    // The QTextDocument is seeded from widgetFlatView() so its text is the
    // flat view. Convert using the same helper FormatOps uses.
    const QString flatText = m_editor->toPlainText();
    const quint32 sepByte =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(flatText, qtPos);

    const auto hit = Markoff::Detail::findBlockAtSepByte(doc, sepByte,
                                                          /*biasForward=*/false);
    if (!hit) return;

    const Markoff::BlockKind kind = doc->blockKind(hit->blockId);
    Markoff::EditorContext ctx;
    ctx.blockKind = blockKindToName(kind);
    ctx.inTable   = (kind == Markoff::BlockKind::Table);

    // Heading level from the "level" attr (int 1–6).
    if (kind == Markoff::BlockKind::Heading) {
        const auto attrs = doc->blockAttrs(hit->blockId);
        auto it = attrs.constFind(Markoff::AttrNames::Level);
        if (it != attrs.cend()) {
            if (const int *p = std::get_if<int>(&it.value()))
                ctx.headingLevel = *p;
        }
    }

    // Change-gate: only emit if something actually changed.
    if (ctx == m_lastContext) return;
    m_lastContext = ctx;
    emit contextChanged(m_lastContext);
}

void Editor::applyParagraphMargins()
{
    if (!m_editor) return;
    QTextDocument *qdoc = m_editor->document();
    if (!qdoc) return;
    QTextCursor c(qdoc);
    QSignalBlocker block(qdoc);   // do NOT loop back through the binding
    c.beginEditBlock();
    for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next()) {
        c.setPosition(b.position());
        QTextBlockFormat bf = b.blockFormat();
        // Same values as styled — symmetry maintains a consistent visual
        // gap between view leaves (spec 2026-05-28 §3.5).
        bf.setTopMargin(5);
        bf.setBottomMargin(5);
        c.setBlockFormat(bf);
    }
    c.endEditBlock();
}

} // namespace Markoff::Source
