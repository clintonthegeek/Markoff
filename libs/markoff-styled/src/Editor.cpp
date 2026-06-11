// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/styled/Editor.h>

#include "Detail/StyledFindAdapter.h"
#include "DocHighlighter.h"
#include "LinkInteraction.h"
#include "StyleApplier.h"
#include "StructuralTextEdit.h"
#include "StyledTableRenderer.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextFrame>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/DefaultLinkService.h>
#include <markoff/core/Detail/FlatBlockResolve.h>
#include <markoff/core/EditorContext.h>
#include <markoff/core/FormatOps.h>
#include <markoff/core/LinkService.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/SourceTextDocumentBinding.h>

namespace Markoff::Styled {

Editor::Editor(QWidget *parent)
    : Markoff::MarkdownView(parent),
      m_editor(new StructuralTextEdit(this)) {
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
    m_styleApplier->setTextEdit(m_editor);

    m_linkInteract = new LinkInteraction(m_editor, this);
    // Propagate the lazy default so LinkInteraction always has a non-null
    // service even before any setLinkService() call (spec §4).
    m_linkInteract->setLinkService(linkService());

    m_highlighter = new DocHighlighter(m_editor->document());

    // Contract §9: emit scrollPositionChanged on native scroll (user drag,
    // programmatic setValue) via a single valueChanged connection. Covers
    // both user-driven scrolling and the programmatic path in
    // setScrollPositionVisualLine (which calls setValue). The manual emit has
    // been removed from that setter so this is the single emit path.
    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) {
                Q_EMIT scrollPositionChanged(scrollPositionVisualLine());
            });
    // Contract v2: surface caret movement on the base signal so a host (e.g.
    // an Ln/Col statusbar) can follow without reaching into the inner editor.
    // QTextEdit::cursorPositionChanged is itself change-driven, so no extra
    // gating is needed. Fires even while read-only (caret still moves).
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, [this]() {
        const Markoff::CursorPos p = cursorPosition();
        Q_EMIT cursorPositionChanged(p.line, p.column);
    });
}

Editor::~Editor() = default;

// ---- MarkdownView contract ----------------------------------------------

void Editor::setDocument(Markoff::MarkoffDocument *doc) {
    if (document() == doc) {
        Markoff::MarkdownView::setDocument(doc);
        return;
    }

    // Disconnect any prior early-capture connection.
    if (m_d2ScrollCaptureCon) {
        QObject::disconnect(m_d2ScrollCaptureCon);
        m_d2ScrollCaptureCon = {};
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

    if (!m_binding) {
        m_binding = new Markoff::SourceTextDocumentBinding(this);
        m_binding->setTextDocument(m_editor->document());
        connect(m_binding, &Markoff::SourceTextDocumentBinding::caretResolved,
                this, [this](int start, int active) {
                    QTextCursor c(m_editor->document());
                    c.setPosition(start);
                    if (active != start)
                        c.setPosition(active, QTextCursor::KeepAnchor);
                    m_editor->setTextCursor(c);
                });
        m_editor->setBinding(m_binding);
    }

    // Table blocks render as opaque QTextTable frames (read-only). The renderer
    // is registered on the binding so its reverse path materializes + preserves
    // the frames; FormatPass skips Table blocks. nullptr doc → renderer points
    // at nullptr and reports nothing opaque (binding falls back to plain text).
    if (!m_tableRenderer) {
        m_tableRenderer = std::make_unique<StyledTableRenderer>();
        m_binding->setOpaqueRenderer(m_tableRenderer.get());
    }
    m_tableRenderer->setMarkoffDocument(doc);
    m_tableRenderer->setFontScale(fontScale());

    // Connect captureScrollBeforeEdit BEFORE the binding wires its own
    // onD2DocumentChanged. Connection order guarantees we fire first
    // and capture the scroll value before setPlainText resets it.
    if (doc && m_styleApplier) {
        m_d2ScrollCaptureCon = QObject::connect(
            doc, &Markoff::MarkoffDocument::d2DocumentChanged,
            m_styleApplier, &Markoff::Styled::StyleApplier::captureScrollBeforeEdit);
    }

    m_binding->setMarkoffDocument(doc);
    m_styleApplier->setMarkoffDocument(doc);
    if (m_linkInteract) m_linkInteract->setMarkoffDocument(doc);
    if (m_session) m_binding->setSession(m_session.data());

    // Wire context-refresh on cursor movement (spec §7).
    // d2DocumentChanged is intentionally NOT connected here: it fires during
    // the initial qWait cycle (from StyleApplier's deferred format pass) and
    // would pre-warm m_lastContext before the first real cursor move, defeating
    // the change-gate. Cursor-position-based triggering is sufficient for the
    // contract: context is re-read on every cursor move regardless of what
    // caused the kind change.
    if (doc) {
        // Reset the sentinel so the first cursor movement after setDocument()
        // always emits contextChanged.
        m_lastContext = Markoff::EditorContext{};
        m_lastContext.blockKind = QString{};  // sentinel: not a valid kind name
        m_contextCursorCon = QObject::connect(
            m_editor, &QTextEdit::cursorPositionChanged,
            this, &Editor::recomputeContext);
    }

    Markoff::MarkdownView::setDocument(doc);
}

Markoff::CursorPos Editor::cursorPosition() const {
    QTextCursor c = m_editor->textCursor();
    const QTextBlock blk = c.block();
    return { blk.blockNumber() + 1, c.positionInBlock() + 1 };
}

QRect Editor::caretRect() const {
    if (!document()) return {};
    QTextEdit *te = textEdit();
    if (!te) return {};
    const QRect r = te->cursorRect();
    return QRect(te->viewport()->mapTo(const_cast<Editor *>(this), r.topLeft()),
                 r.size());
}

void Editor::setCursorPosition(Markoff::CursorPos pos) {
    QTextCursor c = m_editor->textCursor();
    QTextBlock blk = m_editor->document()->findBlockByNumber(pos.line - 1);
    if (!blk.isValid())
        blk = m_editor->document()->lastBlock();
    // length() includes the block separator, so length()-1 is end-of-text.
    c.setPosition(blk.position()
                  + qMin(qMax(0, pos.column - 1), blk.length() - 1));
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
    if (!sb) return;
    if (sb->maximum() != 0) {
        // setValue fires valueChanged, which is connected (in the constructor)
        // to emit scrollPositionChanged — single emit path (spec §9).
        sb->setValue(static_cast<int>(pos * static_cast<float>(sb->maximum())));
    } else {
        // Document fits entirely in the viewport (maximum == 0); setValue
        // would be a no-op and valueChanged would not fire. Emit explicitly
        // so the caller always observes the set (spec §9 contract minimum).
        Q_EMIT scrollPositionChanged(0.0f);
    }
}

void Editor::setReadOnly(bool ro) {
    m_editor->setReadOnly(ro);
    Markoff::MarkdownView::setReadOnly(ro);
}

bool Editor::isReadOnly() const { return m_editor->isReadOnly(); }

// ---- Find ----------------------------------------------------------------

void Editor::attachFindController(Markoff::FindController *fc) {
    if (!m_findAdapter)
        m_findAdapter = new Detail::StyledFindAdapter(this, this);
    m_findAdapter->attach(fc);
}

void Editor::detachFindController() {
    if (m_findAdapter) m_findAdapter->detach();
}

// ---- Format verbs (MarkdownView contract v2 §5) ---------------------------
//
// Thin wrappers over Markoff::FormatOps, modeled on the source leaf's
// (libs/markoff-source/src/Editor.cpp). Styled-specific wrinkle: Table
// blocks render as opaque QTextTable frames, and a frame's character
// stream is NOT the table block's flat bytes — so once a frame is in the
// QTextDocument, toPlainText() and cursor qt-positions diverge from
// widgetFlatView() for everything at/after the frame. FormatOps documents
// widgetFlatView as its contract space, so the verbs (a) always pass
// widgetFlatView() as flatText, never toPlainText(), and (b) no-op unless
// the selection lies in the region where QTextEdit cursor positions agree
// with widgetFlatView positions.
//
// v1 frame-guard policy (conservative, documented in CLAUDE.md): no-op
// with a qWarning when the caret/selection is inside a table frame, or
// when the document contains ANY table frame and the selection reaches
// the first frame or beyond. Strictly before the first frame the two
// coordinate spaces agree, so verbs are allowed there. Spec §5 only
// promises verbs where the coordinate space is trustworthy; format a
// table region by dropping to Source mode.

namespace {

// True when te's caret/selection sits in the frame-free prefix where
// QTextEdit qt-positions == widgetFlatView qt-positions.
bool selectionInTrustworthyRegion(QTextEdit *te) {
    QTextDocument *qdoc = te->document();
    const QTextCursor c = te->textCursor();
    if (c.currentFrame() != qdoc->rootFrame()) {
        qWarning() << "Markoff::Styled::Editor: format verbs are unavailable"
                      " inside a table frame; edit the table in Source mode";
        return false;
    }
    const QList<QTextFrame *> frames = qdoc->rootFrame()->childFrames();
    if (frames.isEmpty()) return true;
    int firstFramePos = INT_MAX;
    for (QTextFrame *f : frames)
        firstFramePos = qMin(firstFramePos, f->firstPosition());
    // firstPosition() is the first position INSIDE the frame; the frame's
    // opening boundary char sits one before it. A selection is trustworthy
    // only if it ends strictly before that boundary.
    if (c.selectionEnd() >= firstFramePos - 1) {
        qWarning() << "Markoff::Styled::Editor: format verbs are unavailable"
                      " at/after a table frame (cursor positions diverge from"
                      " the flat view); edit there in Source mode";
        return false;
    }
    return true;
}

// Re-apply a FormatOps result to the editor's cursor. nullopt means no
// edit was performed; leave the cursor untouched (mirrors the source
// leaf's applyFormatOpsResult).
void applyFormatOpsResult(QTextEdit *te,
                          const std::optional<Markoff::FormatOps::QtRange> &r) {
    if (!te || !r) return;
    QTextCursor c = te->textCursor();
    c.setPosition(r->start);
    if (r->end != r->start)
        c.setPosition(r->end, QTextCursor::KeepAnchor);
    te->setTextCursor(c);
}

void wrapToggleVerb(QTextEdit *te,
                    Markoff::SourceTextDocumentBinding *binding,
                    const QByteArray &delim) {
    if (!te || !binding) return;
    Markoff::MarkoffDocument *doc = binding->markoffDocument();
    if (!doc) return;
    if (!selectionInTrustworthyRegion(te)) return;
    const QTextCursor c = te->textCursor();
    applyFormatOpsResult(
        te, Markoff::FormatOps::wrapToggle(
                doc, QString::fromUtf8(doc->widgetFlatView()),
                {c.selectionStart(), c.selectionEnd()}, delim));
}

} // namespace

// Format verbs are blocked while read-only (MarkdownView contract §10
// check 2) — they mutate via d2 primitives, so the inner widget's
// readOnly flag alone would not stop them.
void Editor::toggleBold()          { if (isReadOnly()) return; wrapToggleVerb(m_editor, m_binding, "**"); }
void Editor::toggleItalic()        { if (isReadOnly()) return; wrapToggleVerb(m_editor, m_binding, "_");  }
void Editor::toggleStrikethrough() { if (isReadOnly()) return; wrapToggleVerb(m_editor, m_binding, "~~"); }
void Editor::toggleInlineCode()    { if (isReadOnly()) return; wrapToggleVerb(m_editor, m_binding, "`");  }

void Editor::insertLink() {
    if (isReadOnly()) return;
    if (!m_editor || !m_binding) return;
    Markoff::MarkoffDocument *doc = m_binding->markoffDocument();
    if (!doc) return;
    if (!selectionInTrustworthyRegion(m_editor)) return;
    const QTextCursor c = m_editor->textCursor();
    applyFormatOpsResult(
        m_editor, Markoff::FormatOps::insertLink(
                      doc, QString::fromUtf8(doc->widgetFlatView()),
                      {c.selectionStart(), c.selectionEnd()}));
}

void Editor::setHeadingLevel(int level) {
    if (isReadOnly()) return;
    if (!m_editor || !m_binding) return;
    Markoff::MarkoffDocument *doc = m_binding->markoffDocument();
    if (!doc) return;
    if (!selectionInTrustworthyRegion(m_editor)) return;
    applyFormatOpsResult(
        m_editor, Markoff::FormatOps::setHeadingLevel(
                      doc, QString::fromUtf8(doc->widgetFlatView()),
                      m_editor->textCursor().position(), level));
}

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
    // No same-value guard: Markoff::Theme has no operator==. Idempotent
    // restyle on a same-theme set is cheap; consumers shouldn't see spurious
    // themeChanged emissions in practice since theme changes are rare.
    MarkdownView::setTheme(t);  // base stores + emits themeChanged
    m_theme = t;
    if (m_styleApplier) m_styleApplier->setTheme(&m_theme);
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

// ---- EditorContext feed (spec §7) ----------------------------------------

namespace {

// Map BlockKind enum to the canonical BlockKindNames string.
// (Mirrors the same helper in markoff-source/src/Editor.cpp; factored here
// because styled has its own anonymous namespace for the frame-guard helpers.)
const char *styledBlockKindToName(Markoff::BlockKind kind) {
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
    default:                 return BKN::Paragraph;  // Mermaid, HtmlBlock → fallback
    }
}

} // namespace

void Editor::recomputeContext()
{
    auto *doc = Markoff::MarkdownView::document();
    if (!doc || !m_editor || !m_binding) return;

    // Map the caret qt-position to a sep-view byte offset using the same
    // helper FormatOps uses, then look up the containing block.
    // Note: for styled, widgetFlatView() is the authoritative flat text
    // (toPlainText() diverges once QTextTable frames are present).
    const QTextCursor cursor = m_editor->textCursor();
    const int qtPos = cursor.position();
    const QString flatText = QString::fromUtf8(doc->widgetFlatView());
    const quint32 sepByte =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(flatText, qtPos);

    const auto hit = Markoff::Detail::findBlockAtSepByte(doc, sepByte,
                                                          /*biasForward=*/false);
    if (!hit) return;

    const Markoff::BlockKind kind = doc->blockKind(hit->blockId);
    Markoff::EditorContext ctx;
    ctx.blockKind = styledBlockKindToName(kind);
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

// ---- Test helpers -------------------------------------------------------

QTextEdit *Editor::textEdit() const { return m_editor; }

quint64 Editor::styleApplierHashSkips() const {
    return m_styleApplier ? m_styleApplier->hashSkips() : 0;
}

// ---- Font scale ---------------------------------------------------------
//
// The base MarkdownView stores the scale and emits fontScaleChanged.
// This override forwards the settled value to StyleApplier and
// StyledTableRenderer (the two styled-leaf consumers).
// No local m_fontScale copy: base fontScale() is the single authority
// (eliminates the dual-store pattern flagged in the review note;
// INVARIANTS §3). StyleApplier has its own internal m_fontScale but that
// is an implementation detail of StyleApplier, not a competing authority —
// it is kept in sync here via setFontScale().

void Editor::setFontScale(qreal s) {
    const qreal prev = fontScale();            // read base before update
    MarkdownView::setFontScale(s);             // clamps + stores + emits (no-op if same)
    if (qFuzzyCompare(prev, fontScale())) return;  // base was a no-op
    if (m_styleApplier) m_styleApplier->setFontScale(fontScale());
    if (m_tableRenderer) m_tableRenderer->setFontScale(fontScale());
}

}  // namespace Markoff::Styled
