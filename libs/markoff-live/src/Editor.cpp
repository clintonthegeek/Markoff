// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/Editor.h"
#include "markoff/LinkRenderer.h"
#include "markoff/SearchBar.h"
#include "LiveSearchAdapter.h"
#include "SelectionScene.h"
#include "SelectionManager.h"
#include "SceneCoordinator.h"
#include "SelectableItem.h"
#include "MarkdownTextItem.h"
#include "TextControl.h"
#include "FoldingModel.h"
#include "FoldGutter.h"
#include "GutterColumn.h"
#include "CheckboxTextObject.h"
#include "MathTextObject.h"

#include <markoff/MarkoffDocument.h>

#include <QCursor>

#include <QGraphicsView>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QScrollBar>
#include <cmath>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextTable>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QClipboard>
#include <QGraphicsSceneMouseEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QWheelEvent>
#include <limits>
#include <memory>

namespace Markoff {

// Subclass of QGraphicsView used as Editor's composed child, exposing the
// normally-protected setViewportMargins() so the Editor can reserve space
// at the bottom for its SearchBar.
class EditorGraphicsView : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;
    using QGraphicsView::setViewportMargins;
};

Editor::Editor(QWidget *parent)
    : MarkdownView(parent)
    , m_view(new EditorGraphicsView(this))
    , m_scene(new SelectionScene(this))
    , m_coordinator(new SceneCoordinator(m_scene, this))
{
    // Compose the QGraphicsView child and fill the widget with it.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_view);

    m_view->setScene(m_scene);
    m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_view->setDragMode(QGraphicsView::NoDrag);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setFrameShape(QFrame::NoFrame);

    m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    m_view->setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    m_view->setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    m_view->setCacheMode(QGraphicsView::CacheNone);

    m_view->viewport()->setBackgroundRole(QPalette::Base);
    m_view->viewport()->setCursor(Qt::IBeamCursor);

    m_view->verticalScrollBar()->setSingleStep(20);

    // Route key / context-menu events from the graphics view back to us,
    // and capture mouse / wheel on the viewport for auto-scroll + zoom.
    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);
    m_view->setFocusPolicy(Qt::StrongFocus);
    setFocusProxy(m_view);

    m_searchAdapter = std::make_unique<LiveSearchAdapter>(this);

    m_autoScrollTimer = new QTimer(this);
    m_autoScrollTimer->setInterval(50);
    connect(m_autoScrollTimer, &QTimer::timeout, this, &Editor::doAutoScroll);

    connect(m_coordinator, &SceneCoordinator::textChanged,
            this, &Editor::textChanged);
    connect(m_coordinator, &SceneCoordinator::textChanged,
            this, &Editor::ensureFocusedCursorVisible);
    connect(m_coordinator, &SceneCoordinator::reparsed,
            this, &Editor::onDocumentReparsed);

    // Cluster J phase 3 — typed link emission surface + TextControl bridge.
    // After each reparse (which may recreate MarkdownTextItems when block
    // structure changes), we re-subscribe to each item's linkActivated /
    // linkHovered signals; `Qt::UniqueConnection` makes re-subscription
    // idempotent when items are preserved across the reparse.
    m_linkRenderer = new LinkRenderer(this);
    connect(m_coordinator, &SceneCoordinator::reparsed,
            this, &Editor::subscribeLinkSignalsForItems);

    // SearchBar is a child of the Editor (not the viewport), so it
    // stays pinned to the bottom of the visible area rather than
    // scrolling with the scene contents.
    m_searchBar = new SearchBar(this);
    m_searchBar->hide();

    connect(m_searchBar, &SearchBar::searchTextChanged,
            this, &Editor::highlightAllMatches);
    connect(m_searchBar, &SearchBar::findNext, this, [this]() {
        findText(m_searchBar->searchText(), searchFlags());
        updateMatchCount();
    });
    connect(m_searchBar, &SearchBar::findPrevious, this, [this]() {
        findText(m_searchBar->searchText(),
                 searchFlags() | QTextDocument::FindBackward);
        updateMatchCount();
    });
    connect(m_searchBar, &SearchBar::replaceRequested, this, [this]() {
        replaceText(m_searchBar->searchText(), m_searchBar->replaceText(),
                    searchFlags());
        highlightAllMatches(m_searchBar->searchText());
    });
    connect(m_searchBar, &SearchBar::replaceAllRequested, this, [this]() {
        int count = replaceAll(m_searchBar->searchText(),
                               m_searchBar->replaceText(), searchFlags());
        highlightAllMatches(m_searchBar->searchText());
        Q_UNUSED(count);
    });
    connect(m_searchBar, &SearchBar::closed, this, &Editor::hideSearchBar);

    m_foldingModel = new FoldingModel(this);
    connect(this, &Editor::headingsChanged,
            m_foldingModel, &FoldingModel::reconcile);
    connect(m_foldingModel, &FoldingModel::foldStateChanged,
            this, &Editor::foldStateChanged);
    m_coordinator->setFoldingModel(m_foldingModel);

    m_foldGutter = new FoldGutter(m_foldingModel);
    m_foldGutter->setCoordinator(m_coordinator);
    m_foldGutter->setColumns({ new FoldArrowColumn(m_foldingModel) });
    m_scene->addItem(m_foldGutter);
    m_foldGutter->setZValue(1.0);  // render above text items

    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, &Editor::repositionFoldGutter);
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &Editor::repositionFoldGutter);

    // Cluster E Phase 2 — bridge the pixel-granular scrollbar signal to the
    // visual-line float contract. Reading scrollPositionVisualLine() at the
    // moment of emission means consumers always see a consistent value.
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) {
        Q_EMIT scrollPositionVisualLineChanged(scrollPositionVisualLine());
    });

    createActions();
}

Editor::~Editor() = default;

// =========================================================================
// QAction registry
// =========================================================================

static QAction *makeAction(QObject *parent, const QString &name,
                           const QString &text,
                           const QString &iconName = {},
                           const QKeySequence &shortcut = {})
{
    auto *a = new QAction(text, parent);
    a->setObjectName(name);
    if (!iconName.isEmpty())
        a->setIcon(QIcon::fromTheme(iconName));
    if (!shortcut.isEmpty())
        a->setShortcut(shortcut);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    return a;
}

void Editor::createActions()
{
    auto reg = [this](ActionId id, QAction *a) {
        m_actions.insert(id, a);
        addAction(a);   // QWidget::addAction — required for shortcut dispatch
    };

    // --- Editing ---
    auto *a = makeAction(this, QStringLiteral("markoff_undo"), tr("Undo"),
                         QStringLiteral("edit-undo"), QKeySequence::Undo);
    connect(a, &QAction::triggered, this, &Editor::undo);
    reg(ActionId::Undo, a);

    a = makeAction(this, QStringLiteral("markoff_redo"), tr("Redo"),
                   QStringLiteral("edit-redo"), QKeySequence::Redo);
    connect(a, &QAction::triggered, this, &Editor::redo);
    reg(ActionId::Redo, a);

    a = makeAction(this, QStringLiteral("markoff_cut"), tr("Cut"),
                   QStringLiteral("edit-cut"), QKeySequence::Cut);
    connect(a, &QAction::triggered, this, &Editor::cut);
    reg(ActionId::Cut, a);

    a = makeAction(this, QStringLiteral("markoff_copy"), tr("Copy"),
                   QStringLiteral("edit-copy"), QKeySequence::Copy);
    connect(a, &QAction::triggered, this, &Editor::copy);
    reg(ActionId::Copy, a);

    a = makeAction(this, QStringLiteral("markoff_paste"), tr("Paste"),
                   QStringLiteral("edit-paste"), QKeySequence::Paste);
    connect(a, &QAction::triggered, this, &Editor::paste);
    reg(ActionId::Paste, a);

    a = makeAction(this, QStringLiteral("markoff_select_all"), tr("Select All"),
                   QStringLiteral("edit-select-all"), QKeySequence::SelectAll);
    connect(a, &QAction::triggered, this, &Editor::selectAll);
    reg(ActionId::SelectAll, a);

    // --- Search ---
    a = makeAction(this, QStringLiteral("markoff_find"), tr("Find"),
                   QStringLiteral("edit-find"), QKeySequence::Find);
    connect(a, &QAction::triggered, this, &Editor::showSearchBar);
    reg(ActionId::Find, a);

    a = makeAction(this, QStringLiteral("markoff_find_next"), tr("Find Next"),
                   QStringLiteral("go-down-search"), QKeySequence::FindNext);
    connect(a, &QAction::triggered, this, [this]() {
        if (m_searchBar && !m_searchBar->searchText().isEmpty()) {
            findText(m_searchBar->searchText(), searchFlags());
            updateMatchCount();
        }
    });
    reg(ActionId::FindNext, a);

    a = makeAction(this, QStringLiteral("markoff_find_previous"), tr("Find Previous"),
                   QStringLiteral("go-up-search"), QKeySequence::FindPrevious);
    connect(a, &QAction::triggered, this, [this]() {
        if (m_searchBar && !m_searchBar->searchText().isEmpty()) {
            findText(m_searchBar->searchText(),
                     searchFlags() | QTextDocument::FindBackward);
            updateMatchCount();
        }
    });
    reg(ActionId::FindPrevious, a);

    a = makeAction(this, QStringLiteral("markoff_replace"), tr("Replace"),
                   QStringLiteral("edit-find-replace"), QKeySequence::Replace);
    connect(a, &QAction::triggered, this, &Editor::showReplaceBar);
    reg(ActionId::Replace, a);

    // --- Zoom ---
    a = makeAction(this, QStringLiteral("markoff_zoom_in"), tr("Zoom In"),
                   QStringLiteral("zoom-in"), QKeySequence::ZoomIn);
    connect(a, &QAction::triggered, this, [this]() {
        if (m_fontSize < 48) setFontSize(m_fontSize + 1);
    });
    reg(ActionId::ZoomIn, a);

    a = makeAction(this, QStringLiteral("markoff_zoom_out"), tr("Zoom Out"),
                   QStringLiteral("zoom-out"), QKeySequence::ZoomOut);
    connect(a, &QAction::triggered, this, [this]() {
        if (m_fontSize > 6) setFontSize(m_fontSize - 1);
    });
    reg(ActionId::ZoomOut, a);

    // --- Formatting ---
    a = makeAction(this, QStringLiteral("markoff_toggle_bold"), tr("Bold"),
                   QStringLiteral("format-text-bold"), QKeySequence::Bold);
    connect(a, &QAction::triggered, this, &Editor::toggleBold);
    reg(ActionId::ToggleBold, a);

    a = makeAction(this, QStringLiteral("markoff_toggle_italic"), tr("Italic"),
                   QStringLiteral("format-text-italic"), QKeySequence::Italic);
    connect(a, &QAction::triggered, this, &Editor::toggleItalic);
    reg(ActionId::ToggleItalic, a);

    a = makeAction(this, QStringLiteral("markoff_toggle_strikethrough"),
                   tr("Strikethrough"), QStringLiteral("format-text-strikethrough"),
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X));
    connect(a, &QAction::triggered, this, &Editor::toggleStrikethrough);
    reg(ActionId::ToggleStrikethrough, a);

    a = makeAction(this, QStringLiteral("markoff_toggle_inline_code"),
                   tr("Inline Code"), QStringLiteral("code-context"),
                   QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft));
    connect(a, &QAction::triggered, this, &Editor::toggleInlineCode);
    reg(ActionId::ToggleInlineCode, a);

    a = makeAction(this, QStringLiteral("markoff_insert_link"), tr("Insert Link"),
                   QStringLiteral("insert-link"),
                   QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(a, &QAction::triggered, this, &Editor::insertLink);
    reg(ActionId::InsertLink, a);

    a = makeAction(this, QStringLiteral("markoff_insert_wiki_link"),
                   tr("Insert Wiki Link"), QStringLiteral("insert-link"),
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    connect(a, &QAction::triggered, this, &Editor::insertWikiLink);
    reg(ActionId::InsertWikiLink, a);

    // Actions without default shortcuts — still available for menus/command palette
    a = makeAction(this, QStringLiteral("markoff_insert_image"), tr("Insert Image"),
                   QStringLiteral("insert-image"));
    connect(a, &QAction::triggered, this, &Editor::insertImage);
    reg(ActionId::InsertImage, a);

    a = makeAction(this, QStringLiteral("markoff_insert_code_block"),
                   tr("Insert Code Block"), QStringLiteral("code-block"));
    connect(a, &QAction::triggered, this, &Editor::insertCodeBlock);
    reg(ActionId::InsertCodeBlock, a);

    a = makeAction(this, QStringLiteral("markoff_insert_block_quote"),
                   tr("Insert Block Quote"), QStringLiteral("format-text-blockquote"));
    connect(a, &QAction::triggered, this, &Editor::insertBlockQuote);
    reg(ActionId::InsertBlockQuote, a);

    a = makeAction(this, QStringLiteral("markoff_insert_horizontal_rule"),
                   tr("Insert Horizontal Rule"), QStringLiteral("distribute-horizontal-center"));
    connect(a, &QAction::triggered, this, &Editor::insertHorizontalRule);
    reg(ActionId::InsertHorizontalRule, a);

    a = makeAction(this, QStringLiteral("markoff_insert_table"),
                   tr("Insert Table"), QStringLiteral("insert-table"));
    connect(a, &QAction::triggered, this, [this]() { insertTable(3, 3); });
    reg(ActionId::InsertTable, a);

    a = makeAction(this, QStringLiteral("markoff_increase_heading"),
                   tr("Increase Heading Level"), QStringLiteral("format-header-more"));
    connect(a, &QAction::triggered, this, &Editor::increaseHeadingLevel);
    reg(ActionId::IncreaseHeading, a);

    a = makeAction(this, QStringLiteral("markoff_decrease_heading"),
                   tr("Decrease Heading Level"), QStringLiteral("format-header-less"));
    connect(a, &QAction::triggered, this, &Editor::decreaseHeadingLevel);
    reg(ActionId::DecreaseHeading, a);

    // Direct-select H1..H6 (Ctrl+1..Ctrl+6).
    static const ActionId setHeadingIds[6] = {
        ActionId::SetHeading1, ActionId::SetHeading2, ActionId::SetHeading3,
        ActionId::SetHeading4, ActionId::SetHeading5, ActionId::SetHeading6,
    };
    for (int level = 1; level <= 6; ++level) {
        a = makeAction(
            this,
            QStringLiteral("markoff_set_heading_%1").arg(level),
            tr("Heading %1").arg(level),
            QStringLiteral("format-text-heading"),
            QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_0 + level)));
        connect(a, &QAction::triggered, this, [this, level]() {
            setHeadingLevel(level);
        });
        reg(setHeadingIds[level - 1], a);
    }

    a = makeAction(this, QStringLiteral("markoff_toggle_checkbox"),
                   tr("Toggle Checkbox"), QStringLiteral("checkbox"));
    connect(a, &QAction::triggered, this, &Editor::toggleCheckbox);
    reg(ActionId::ToggleCheckbox, a);

    // --- Folding ---
    a = makeAction(this, QStringLiteral("markoff_toggle_fold_at_cursor"),
                   tr("Toggle Fold"), QStringLiteral("code-function"));
    connect(a, &QAction::triggered, this, &Editor::toggleFoldAtCursor);
    reg(ActionId::ToggleFoldAtCursor, a);

    a = makeAction(this, QStringLiteral("markoff_fold_all"), tr("Fold All"),
                   QStringLiteral("collapse-all"));
    connect(a, &QAction::triggered, this, &Editor::foldAll);
    reg(ActionId::FoldAll, a);

    a = makeAction(this, QStringLiteral("markoff_unfold_all"), tr("Unfold All"),
                   QStringLiteral("expand-all"));
    connect(a, &QAction::triggered, this, &Editor::unfoldAll);
    reg(ActionId::UnfoldAll, a);
}

QAction *Editor::action(ActionId id) const
{
    return m_actions.value(id, nullptr);
}

QList<QAction*> Editor::actions() const
{
    return m_actions.values();
}

// =========================================================================
// Event override — intercept Tab/Shift+Tab before Qt focus chain
// =========================================================================

bool Editor::event(QEvent *e)
{
    if (e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            keyPressEvent(ke);
            return true;
        }
    }
    return MarkdownView::event(e);
}

bool Editor::eventFilter(QObject *watched, QEvent *event)
{
    // Events flowing through the composed QGraphicsView child and its
    // viewport — intercept key/mouse/wheel/context-menu so the Editor's
    // behavior survives the composition change.
    if (watched == m_view) {
        switch (event->type()) {
        case QEvent::ContextMenu: {
            auto *ce = static_cast<QContextMenuEvent *>(event);
            contextMenuEvent(ce);
            if (ce->isAccepted())
                return true;
            break;
        }
        default:
            break;
        }
    } else if (watched == m_view->viewport()) {
        switch (event->type()) {
        case QEvent::MouseMove:
            handleMouseMoveOnViewport(static_cast<QMouseEvent *>(event));
            break;
        case QEvent::MouseButtonRelease:
            handleMouseReleaseOnViewport(static_cast<QMouseEvent *>(event));
            break;
        case QEvent::Wheel: {
            auto *we = static_cast<QWheelEvent *>(event);
            handleWheelOnViewport(we);
            if (we->isAccepted())
                return true;
            break;
        }
        default:
            break;
        }
    }
    return MarkdownView::eventFilter(watched, event);
}

// =========================================================================
// Tab smart-indent
// =========================================================================

bool Editor::handleTabKey(QKeyEvent *e)
{
    if (e->key() != Qt::Key_Tab && e->key() != Qt::Key_Backtab)
        return false;
    if (m_readOnly)
        return true;  // swallow but do nothing

    auto *textItem = focusedTextItem();
    if (!textItem)
        return false;

    auto *control = textItem->textControl();
    QTextCursor cursor = control->textCursor();

    // If inside a table, let TextControl handle cell navigation.
    // Forward the event to TextControl directly rather than returning
    // false (which would route through QGraphicsView → QGraphicsScene,
    // where the scene's default Tab handling does focus-chain navigation
    // between items instead of delivering to the focused item).
    if (cursor.currentTable()) {
        control->processEvent(e);
        e->accept();
        return true;
    }

    bool shiftTab = (e->key() == Qt::Key_Backtab)
                    || (e->modifiers() & Qt::ShiftModifier);

    QTextBlock block = cursor.block();
    QString lineText = block.text();
    int col = cursor.positionInBlock();

    // Detect whether this line is a list item (unordered or ordered).
    static const QRegularExpression listRx(
        QStringLiteral("^\\s*([-*+]|\\d+\\.)\\s"));
    bool isList = listRx.match(lineText).hasMatch();

    int firstNonSpace = 0;
    while (firstNonSpace < lineText.size() && lineText.at(firstNonSpace).isSpace())
        ++firstNonSpace;

    // Indent/dedent the whole line when:
    //   - cursor is at or before the first non-space character, OR
    //   - line is blank, OR
    //   - line is a list item (indent regardless of cursor position)
    bool shouldIndentLine = (col <= firstNonSpace)
                            || lineText.trimmed().isEmpty()
                            || isList;

    if (shouldIndentLine) {
        // Work at the source level to avoid a visible intermediate state
        // between the cursor edit and the deferred reparse (150ms timer).
        int globalLine = cursorLine();  // 1-based
        QString source = toPlainText();
        QStringList lines = source.split(QLatin1Char('\n'));
        int lineIdx = globalLine - 1;
        if (lineIdx < 0 || lineIdx >= lines.size()) {
            e->accept();
            return true;
        }

        int newCol = col;
        if (shiftTab) {
            QString &line = lines[lineIdx];
            int toRemove = 0;
            if (line.startsWith(QLatin1String("  ")))
                toRemove = 2;
            else if (line.startsWith(QLatin1Char('\t')))
                toRemove = 1;
            else if (line.startsWith(QLatin1Char(' ')))
                toRemove = 1;
            if (toRemove > 0) {
                line.remove(0, toRemove);
                newCol = qMax(0, col - toRemove);
            }
        } else {
            // For list items, enforce nesting rules: can only indent one
            // level (2 spaces) deeper than the closest preceding list item.
            if (isList) {
                int curIndent = firstNonSpace;
                int maxIndent = 0;  // no predecessor → top level only
                for (int i = lineIdx - 1; i >= 0; --i) {
                    QRegularExpressionMatch m = listRx.match(lines[i]);
                    if (m.hasMatch()) {
                        // Count leading whitespace of the predecessor
                        int predIndent = 0;
                        for (QChar ch : lines[i]) {
                            if (ch == QLatin1Char(' ')) ++predIndent;
                            else if (ch == QLatin1Char('\t')) predIndent += 2;
                            else break;
                        }
                        maxIndent = predIndent + 2;
                        break;
                    }
                    // Skip blank lines (continuation of a list)
                    if (!lines[i].trimmed().isEmpty())
                        break;  // non-blank, non-list → stop searching
                }
                if (curIndent >= maxIndent) {
                    e->accept();
                    return true;  // already at max nesting — no-op
                }
            }
            lines[lineIdx].prepend(QStringLiteral("  "));
            newCol = col + 2;
        }

        setPlainText(lines.join(QLatin1Char('\n')));
        goToLine(globalLine);

        // Restore cursor column within the line
        auto *ti = focusedTextItem();
        if (ti) {
            QTextCursor c = ti->textControl()->textCursor();
            c.movePosition(QTextCursor::StartOfBlock);
            c.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                           qMin(newCol, c.block().text().length()));
            ti->textControl()->setTextCursor(c);
        }

        e->accept();
        return true;
    }

    // Non-list, mid-line: Tab inserts spaces, Shift+Tab is a no-op
    if (!shiftTab) {
        cursor.insertText(QStringLiteral("  "));
        control->setTextCursor(cursor);
    }
    e->accept();
    return true;
}

void Editor::setPlainText(const QString &text)
{
    m_sourceText = text;
    rebuildScene();
}

void Editor::clear()
{
    setPlainText({});
}

QString Editor::toPlainText() const
{
    return m_coordinator->toMarkdown();
}

void Editor::setFontSize(int pointSize)
{
    m_fontSize = pointSize;
    applyEffectiveFont();
}

void Editor::applyEffectiveFont()
{
    if (!m_coordinator)
        return;
    QFont font = m_theme.textFont;
    if (m_fontSize > 0)
        font.setPointSize(m_fontSize);
    m_coordinator->setFont(font);
}

void Editor::resetZoom()
{
    setFontSize(m_defaultFontSize);
}

void Editor::resizeEvent(QResizeEvent *e)
{
    MarkdownView::resizeEvent(e);
    qreal width = m_view->viewport()->width() - 32;
    if (width > 100)
        m_coordinator->setItemWidth(width);
    repositionSearchBar();
    repositionFoldGutter();
}

// =========================================================================
// Auto-scroll during drag selection
// =========================================================================

void Editor::handleMouseMoveOnViewport(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton) {
        int y = e->pos().y();
        if (y < 0)
            startAutoScroll(y);
        else if (y > m_view->viewport()->height())
            startAutoScroll(y - m_view->viewport()->height());
        else
            stopAutoScroll();
    }
}

void Editor::handleMouseReleaseOnViewport(QMouseEvent *)
{
    stopAutoScroll();
}

void Editor::startAutoScroll(int delta)
{
    m_autoScrollDelta = qBound(-60, delta, 60);
    m_autoScrollActive = true;
    if (!m_autoScrollTimer->isActive())
        m_autoScrollTimer->start();
}

void Editor::stopAutoScroll()
{
    m_autoScrollTimer->stop();
    m_autoScrollDelta = 0;
    m_autoScrollActive = false;
}

void Editor::doAutoScroll()
{
    QScrollBar *vbar = m_view->verticalScrollBar();
    int oldVal = vbar->value();
    vbar->setValue(oldVal + m_autoScrollDelta);
    if (vbar->value() == oldVal)
        return;

    QPoint viewportEdge;
    if (m_autoScrollDelta < 0)
        viewportEdge = QPoint(m_view->viewport()->width() / 2, 0);
    else
        viewportEdge = QPoint(m_view->viewport()->width() / 2, m_view->viewport()->height() - 1);

    QPointF scenePos = m_view->mapToScene(viewportEdge);
    QGraphicsSceneMouseEvent moveEvent(QEvent::GraphicsSceneMouseMove);
    moveEvent.setScenePos(scenePos);
    moveEvent.setScreenPos(mapToGlobal(viewportEdge));
    moveEvent.setButtons(Qt::LeftButton);
    moveEvent.setButton(Qt::NoButton);
    moveEvent.setModifiers(QApplication::keyboardModifiers());
    QApplication::sendEvent(m_scene, &moveEvent);
}

// =========================================================================
// Context menu
// =========================================================================

void Editor::contextMenuEvent(QContextMenuEvent *e)
{
    if (m_readOnly) {
        e->ignore();
        return;
    }

    // Check if click is inside a table
    auto *item = focusedTextItem();
    if (item) {
        QTextCursor cursor = item->textControl()->textCursor();
        QTextTable *table = cursor.currentTable();
        if (table) {
            QMenu menu(this);
            menu.addAction(tr("Insert Row Above"), this, &Editor::tableInsertRowAbove);
            menu.addAction(tr("Insert Row Below"), this, &Editor::tableInsertRowBelow);
            menu.addSeparator();
            menu.addAction(tr("Insert Column Left"), this, &Editor::tableInsertColumnLeft);
            menu.addAction(tr("Insert Column Right"), this, &Editor::tableInsertColumnRight);
            menu.addSeparator();
            auto *delRow = menu.addAction(tr("Delete Row"), this, &Editor::tableDeleteRow);
            delRow->setEnabled(table->rows() > 1);
            auto *delCol = menu.addAction(tr("Delete Column"), this, &Editor::tableDeleteColumn);
            delCol->setEnabled(table->columns() > 1);
            menu.addSeparator();
            menu.addAction(tr("Select Row"), this, &Editor::tableSelectRow);
            menu.addAction(tr("Select Column"), this, &Editor::tableSelectColumn);
            menu.exec(e->globalPos());
            e->accept();
            return;
        }
    }

    QMenu menu(this);
    auto *mgr = m_scene->selectionManager();

    auto *undoAction = menu.addAction(tr("Undo"), this, [this]() {
        if (auto *ti = focusedTextItem()) ti->textControl()->undo();
    });
    auto *redoAction = menu.addAction(tr("Redo"), this, [this]() {
        if (auto *ti = focusedTextItem()) ti->textControl()->redo();
    });
    undoAction->setShortcut(QKeySequence::Undo);
    redoAction->setShortcut(QKeySequence::Redo);

    menu.addSeparator();

    auto *cutAction = menu.addAction(tr("Cut"), this, [this]() { cut(); });
    auto *copyAction = menu.addAction(tr("Copy"), this, [this]() { copy(); });
    auto *pasteAction = menu.addAction(tr("Paste"), this, [this]() { paste(); });

    menu.addSeparator();

    menu.addAction(tr("Select All"), this, [mgr]() {
        QKeyEvent e(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
        mgr->handleKeyPress(&e);
    });

    // Enable/disable
    bool hasSelection = mgr->hasSelection();
    if (!hasSelection) {
        if (auto *ti = focusedTextItem())
            hasSelection = ti->textControl()->textCursor().hasSelection();
    }
    cutAction->setEnabled(hasSelection);
    copyAction->setEnabled(hasSelection);
    pasteAction->setEnabled(QApplication::clipboard()->mimeData()
                            && QApplication::clipboard()->mimeData()->hasText());

    menu.exec(e->globalPos());
}

// =========================================================================
// Keyboard
// =========================================================================

void Editor::keyPressEvent(QKeyEvent *e)
{
    // Tab smart-indent (handled here because it's context-dependent:
    // inside a table TextControl handles cell navigation; outside,
    // Editor handles indent/dedent).
    if (handleTabKey(e)) {
        e->accept();
        return;
    }

    bool shift = e->modifiers() & Qt::ShiftModifier;
    bool ctrl  = e->modifiers() & Qt::ControlModifier;

    // Scene-level navigation that depends on modifier state (shift for
    // selection extension) — can't be modeled as static QActions.
    if (e->key() == Qt::Key_Home && ctrl) {
        jumpToDocumentEdge(true, shift);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_End && ctrl) {
        jumpToDocumentEdge(false, shift);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown) {
        pageUpDown(e->key() == Qt::Key_PageUp, shift);
        e->accept();
        return;
    }

    // Forward to the composed QGraphicsView so its default key handling
    // (scene -> focus item) runs unchanged. Using sendEvent (not
    // postEvent) so it runs synchronously before we check e->text() etc.
    QApplication::sendEvent(m_view, e);

    // Ensure cursor visible after cursor-moving keys
    switch (e->key()) {
    case Qt::Key_Up: case Qt::Key_Down:
    case Qt::Key_Return: case Qt::Key_Enter:
    case Qt::Key_Backspace: case Qt::Key_Delete:
        ensureFocusedCursorVisible();
        break;
    default:
        if (!e->text().isEmpty())
            ensureFocusedCursorVisible();
        break;
    }

    detectCompletionTriggers(e->text());
}

void Editor::jumpToDocumentEdge(bool toStart, bool select)
{
    const auto &items = m_coordinator->items();
    if (items.isEmpty()) return;

    // If selecting across entire document, use SelectionManager
    if (select) {
        auto *mgr = m_scene->selectionManager();
        QKeyEvent e(QEvent::KeyPress,
                    toStart ? Qt::Key_Home : Qt::Key_End,
                    Qt::ControlModifier | Qt::ShiftModifier);
        // Ctrl+A handles this — select everything
        QKeyEvent selectAll(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
        mgr->handleKeyPress(&selectAll);
        ensureFocusedCursorVisible();
        return;
    }

    // Jump without selection
    if (toStart) {
        for (auto *item : items) {
            if (item->isTextItem()) {
                auto *ti = static_cast<MarkdownTextItem *>(item);
                ti->setFocus();
                QTextCursor c(ti->document());
                c.movePosition(QTextCursor::Start);
                ti->textControl()->setTextCursor(c);
                break;
            }
        }
    } else {
        for (int i = items.size() - 1; i >= 0; --i) {
            if (items[i]->isTextItem()) {
                auto *ti = static_cast<MarkdownTextItem *>(items[i]);
                ti->setFocus();
                QTextCursor c(ti->document());
                c.movePosition(QTextCursor::End);
                ti->textControl()->setTextCursor(c);
                break;
            }
        }
    }
    ensureFocusedCursorVisible();
}

void Editor::pageUpDown(bool up, bool select)
{
    auto *sourceItem = focusedTextItem();
    if (!sourceItem) return;

    // Remember anchor for selection before scrolling
    int anchorPos = sourceItem->textControl()->textCursor().anchor();

    // Scroll by viewport height
    QScrollBar *vbar = m_view->verticalScrollBar();
    int pageStep = m_view->viewport()->height();
    vbar->setValue(vbar->value() + (up ? -pageStep : pageStep));

    // Find the text item nearest to viewport center (same nearest-by-Y
    // logic as SelectionManager::itemAt).
    QPointF sceneTarget = m_view->mapToScene(m_view->viewport()->width() / 2,
                                             m_view->viewport()->height() / 2);
    MarkdownTextItem *targetItem = nullptr;
    int targetPos = -1;
    qreal bestDist = std::numeric_limits<qreal>::max();

    for (auto *item : m_coordinator->items()) {
        if (!item->isTextItem()) continue;
        QRectF r = item->asGraphicsItem()->sceneBoundingRect();
        qreal dist = 0;
        if (sceneTarget.y() < r.top())
            dist = r.top() - sceneTarget.y();
        else if (sceneTarget.y() > r.bottom())
            dist = sceneTarget.y() - r.bottom();
        if (dist < bestDist) {
            bestDist = dist;
            targetItem = static_cast<MarkdownTextItem *>(item);
        }
    }
    if (!targetItem) return;

    QPointF localPos = targetItem->mapFromScene(sceneTarget);
    targetPos = targetItem->document()->documentLayout()->hitTest(localPos, Qt::FuzzyHit);
    if (targetPos < 0) targetPos = 0;

    if (!select) {
        // Simple navigation — clear any selection, move cursor
        m_scene->selectionManager()->clearSelection();
        targetItem->setFocus();
        QTextCursor cursor(targetItem->document());
        cursor.setPosition(targetPos);
        targetItem->textControl()->setTextCursor(cursor);
    } else if (targetItem == sourceItem) {
        // Same item — extend within-item selection
        QTextCursor cursor = sourceItem->textControl()->textCursor();
        cursor.setPosition(targetPos, QTextCursor::KeepAnchor);
        sourceItem->textControl()->setTextCursor(cursor);
    } else {
        // Cross-item — use SelectionManager
        auto *mgr = m_scene->selectionManager();

        // Set anchor item's selection from anchor to its edge
        int edgePos = up ? 0 : sourceItem->documentLength();
        sourceItem->setSelection(anchorPos, edgePos);

        // Fully select all items between source and target
        const auto &items = m_coordinator->items();
        int srcIdx = items.indexOf(static_cast<SelectableItem *>(sourceItem));
        int tgtIdx = items.indexOf(static_cast<SelectableItem *>(targetItem));
        int lo = qMin(srcIdx, tgtIdx), hi = qMax(srcIdx, tgtIdx);
        for (int i = lo + 1; i < hi; ++i) {
            if (items[i]->isTextItem())
                items[i]->setSelection(0, items[i]->documentLength());
            else
                items[i]->setFullySelected(true);
        }

        // Move focus and caret to target
        targetItem->setFocus();
        QTextCursor cursor(targetItem->document());
        int entryPos = up ? targetItem->documentLength() : 0;
        cursor.setPosition(entryPos);
        cursor.setPosition(targetPos, QTextCursor::KeepAnchor);
        targetItem->textControl()->setTextCursor(cursor);

        mgr->beginOrExtendKeyboardSelection(
            sourceItem, anchorPos, targetItem, targetPos);
    }
}

// =========================================================================
// Zoom
// =========================================================================

void Editor::handleWheelOnViewport(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        int delta = e->angleDelta().y();
        if (delta > 0 && m_fontSize < 48)
            setFontSize(m_fontSize + 1);
        else if (delta < 0 && m_fontSize > 6)
            setFontSize(m_fontSize - 1);
        e->accept();
        return;
    }
    // Leave the event unaccepted so QGraphicsView's default wheel
    // handling (vertical scroll) runs.
    e->ignore();
}

// =========================================================================
// Ensure cursor visible
// =========================================================================

void Editor::ensureFocusedCursorVisible()
{
    if (m_autoScrollActive)
        return;

    auto *ti = focusedTextItem();
    if (!ti) return;

    QRectF cursorRect = ti->textControl()->cursorRect();
    QRectF sceneRect = ti->mapToScene(cursorRect).boundingRect();
    m_view->ensureVisible(sceneRect, 0, 50);
}

MarkdownTextItem *Editor::focusedTextItem() const
{
    auto *item = m_scene->focusItem();
    if (!item) return nullptr;
    return dynamic_cast<MarkdownTextItem *>(item);
}

// =========================================================================
// Scene rebuild
// =========================================================================

void Editor::rebuildScene()
{
    m_coordinator->loadMarkdown(m_sourceText);

    qreal width = m_view->viewport()->width() - 32;
    if (width > 100)
        m_coordinator->setItemWidth(width);
    if (m_fontSize > 0) {
        QFont font = this->font();
        font.setPointSize(m_fontSize);
        m_coordinator->setFont(font);
    }

    for (auto *item : m_coordinator->items()) {
        if (item->isTextItem()) {
            item->asGraphicsItem()->setFocus();
            break;
        }
    }

    // Cluster J phase 3 — wire link signals on the freshly-created items.
    subscribeLinkSignalsForItems();
}

// =========================================================================
// Configuration
// =========================================================================

void Editor::setTheme(const Theme &theme)
{
    m_theme = theme;
    m_fontSize = theme.textFont.pointSize() > 0 ? theme.textFont.pointSize() : 14;
    m_defaultFontSize = m_fontSize;
    if (m_coordinator) {
        m_coordinator->setTheme(theme);
        // Apply the document default font AFTER setTheme so body text
        // renders at the theme's font size alongside the highlighter colors.
        applyEffectiveFont();
    }
}

Theme Editor::theme() const { return m_theme; }

void Editor::setResourceProvider(ResourceProvider *provider)
{
    m_resourceProvider = provider;
    if (m_coordinator)
        m_coordinator->setResourceProvider(provider);
}

// =========================================================================
// Read-only mode
// =========================================================================

bool Editor::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    // TextBrowserInteraction allows link clicking and selection but not editing
    auto flags = readOnly ? Qt::TextBrowserInteraction : Qt::TextEditorInteraction;
    for (auto *item : m_coordinator->items()) {
        if (item->isTextItem()) {
            auto *textItem = static_cast<MarkdownTextItem *>(item->asGraphicsItem());
            textItem->textControl()->setTextInteractionFlags(flags);
        }
    }

    // Disable editing actions when read-only; keep navigation/read actions
    static const ActionId editingActions[] = {
        ActionId::Undo, ActionId::Redo,
        ActionId::Cut, ActionId::Paste,
        ActionId::ToggleBold, ActionId::ToggleItalic,
        ActionId::ToggleStrikethrough, ActionId::ToggleInlineCode,
        ActionId::InsertLink, ActionId::InsertWikiLink, ActionId::InsertImage,
        ActionId::InsertCodeBlock, ActionId::InsertBlockQuote,
        ActionId::InsertHorizontalRule, ActionId::InsertTable,
        ActionId::IncreaseHeading, ActionId::DecreaseHeading,
        ActionId::ToggleCheckbox,
    };
    for (auto id : editingActions) {
        if (auto *a = m_actions.value(id))
            a->setEnabled(!readOnly);
    }
    return true;
}

bool Editor::isReadOnly() const
{
    return m_readOnly;
}

// =========================================================================
// Document accessor
// =========================================================================

const Document *Editor::parsedDocument() const { return m_document.get(); }

// =========================================================================
// Document reparsed
// =========================================================================

void Editor::onDocumentReparsed()
{
    m_document = Document::fromMarkdown(toPlainText());
    Q_EMIT headingsChanged(m_document->headings());
    Q_EMIT linksChanged(m_document->links());
    Q_EMIT tagsChanged(m_document->tags());
    Q_EMIT wordCountChanged(m_document->wordCount());
}

// =========================================================================
// Editing actions
// =========================================================================

void Editor::undo()      { if (auto *ti = focusedTextItem()) ti->textControl()->undo(); }
void Editor::redo()      { if (auto *ti = focusedTextItem()) ti->textControl()->redo(); }
void Editor::copy()
{
    // Try SelectionManager first — but only if it has a genuine cross-
    // boundary selection (m_anchorItem set). hasSelection() alone is not
    // enough because it also returns true for within-item cursor
    // selections, and serializeAsMarkdown() returns empty in that case.
    auto *mgr = m_scene->selectionManager();
    if (mgr) {
        QMimeData *data = mgr->createMimeData();
        if (data && !data->text().isEmpty()) {
            QApplication::clipboard()->setMimeData(data);
            return;
        }
        delete data;
    }
    // Fall back to the focused item's cursor selection, expanding any
    // math glyphs to raw $...$ source.
    auto *ti = focusedTextItem();
    if (!ti) return;
    const QString text = ti->selectedMarkdown();
    if (text.isEmpty()) return;
    QApplication::clipboard()->setText(text);
}

void Editor::cut()
{
    copy();

    // Remove selected text from text items.
    for (auto *item : m_coordinator->items()) {
        if (item->isTextItem()) {
            auto *ti = static_cast<MarkdownTextItem *>(item);
            QTextCursor c = ti->textControl()->textCursor();
            if (c.hasSelection())
                c.removeSelectedText();
        }
    }

    // Collect indices of fully-selected block items before clearing
    // selection (clearSelection resets their isFullySelected state).
    QList<int> toRemove;
    const auto &items = m_coordinator->items();
    for (int i = 0; i < items.size(); ++i) {
        if (!items[i]->isTextItem() && items[i]->isFullySelected())
            toRemove.prepend(i);  // reverse order for safe removal
    }

    // Clear selection first — this iterates all items, so they must
    // still be alive.
    m_scene->selectionManager()->clearSelection();

    // Now remove the block items (indices are in descending order).
    for (int idx : toRemove)
        m_coordinator->removeBlockItem(idx);

    if (!toRemove.isEmpty())
        m_scene->setSelectableItems(m_coordinator->items());
}

void Editor::paste()     { if (auto *ti = focusedTextItem()) ti->textControl()->paste(); }
void Editor::selectAll()
{
    m_scene->selectionManager()->selectAll();
}

// =========================================================================
// Formatting actions
// =========================================================================

void Editor::wrapSelection(const QString &before, const QString &after)
{
    auto *ti = focusedTextItem();
    if (!ti) return;
    auto *tc = ti->textControl();
    QTextCursor cursor = tc->textCursor();

    if (cursor.hasSelection()) {
        QString selected = cursor.selectedText();

        // Toggle: if the selection is already wrapped in the same delimiters,
        // strip them. e.g. **foo** + toggleBold → foo
        if (selected.size() >= before.size() + after.size()
            && selected.startsWith(before)
            && selected.endsWith(after)) {
            const QString inner = selected.mid(before.size(),
                                                selected.size() - before.size() - after.size());
            cursor.insertText(inner);
            // Reselect the now-unwrapped inner text so the user can keep
            // operating on it.
            const int newEnd = cursor.position();
            cursor.setPosition(newEnd - inner.size());
            cursor.setPosition(newEnd, QTextCursor::KeepAnchor);
            tc->setTextCursor(cursor);
            return;
        }

        // Toggle: if the chars OUTSIDE the selection (just before / just
        // after) are the delimiters, strip those instead. Lets the user
        // double-click "foo" inside `**foo**` and toggle off without
        // having to reselect the asterisks.
        const int selStart = cursor.selectionStart();
        const int selEnd = cursor.selectionEnd();
        QTextDocument *doc = tc->document();
        const QString docText = doc->toPlainText();
        if (selStart >= before.size() && selEnd + after.size() <= docText.size()
            && docText.mid(selStart - before.size(), before.size()) == before
            && docText.mid(selEnd, after.size()) == after) {
            QTextCursor outer(doc);
            outer.setPosition(selStart - before.size());
            outer.setPosition(selEnd + after.size(), QTextCursor::KeepAnchor);
            outer.insertText(selected);
            const int newEnd = outer.position();
            outer.setPosition(newEnd - selected.size());
            outer.setPosition(newEnd, QTextCursor::KeepAnchor);
            tc->setTextCursor(outer);
            return;
        }

        cursor.insertText(before + selected + after);
        return;
    }

    // No selection: insert the empty pair and place the cursor between.
    int pos = cursor.position();
    cursor.insertText(before + after);
    cursor.setPosition(pos + before.length());
    tc->setTextCursor(cursor);
}

void Editor::insertAtCursor(const QString &text)
{
    auto *ti = focusedTextItem();
    if (!ti) return;
    ti->textControl()->insertPlainText(text);
}

void Editor::toggleBold()          { wrapSelection(QStringLiteral("**"), QStringLiteral("**")); }
void Editor::toggleItalic()        { wrapSelection(QStringLiteral("*"),  QStringLiteral("*")); }
void Editor::toggleStrikethrough() { wrapSelection(QStringLiteral("~~"), QStringLiteral("~~")); }
void Editor::toggleInlineCode()    { wrapSelection(QStringLiteral("`"),  QStringLiteral("`")); }
void Editor::insertLink()          { insertAtCursor(QStringLiteral("[]()")); }
void Editor::insertWikiLink()      { insertAtCursor(QStringLiteral("[[]]")); }
void Editor::insertImage()         { insertAtCursor(QStringLiteral("![]()")); }
void Editor::insertCodeBlock()     { insertAtCursor(QStringLiteral("```\n\n```")); }
void Editor::insertBlockQuote()    { insertAtCursor(QStringLiteral("> ")); }
void Editor::insertHorizontalRule(){ insertAtCursor(QStringLiteral("\n---\n")); }
void Editor::insertCallout(const QString &type) {
    insertCallout(type, QString());
}

void Editor::insertCallout(const QString &type, const QString &title) {
    QString head = QStringLiteral("> [!%1]").arg(type);
    if (!title.isEmpty())
        head += QLatin1Char(' ') + title;
    head += QStringLiteral("\n> ");
    insertAtCursor(head);
}

void Editor::insertTable(int rows, int cols)
{
    insertTable(rows, cols, true);
}

void Editor::insertTable(int rows, int cols, bool hasHeader)
{
    auto *ti = focusedTextItem();
    if (!ti || m_readOnly) return;

    QTextCursor cursor = ti->textControl()->textCursor();
    cursor.beginEditBlock();

    // Ensure blank line before table if not at block start
    if (!cursor.atBlockStart())
        cursor.insertBlock();

    QTextTableFormat fmt;
    fmt.setBorderCollapse(true);
    fmt.setCellPadding(8);
    fmt.setCellSpacing(0);
    fmt.setBorder(1);

    const int totalRows = hasHeader ? rows + 1 : rows;
    auto *table = cursor.insertTable(totalRows, cols, fmt);

    // Position cursor in first cell so the user can start typing immediately.
    QTextCursor firstCell = table->cellAt(0, 0).firstCursorPosition();
    ti->textControl()->setTextCursor(firstCell);

    cursor.endEditBlock();
}

void Editor::increaseHeadingLevel()
{
    auto *ti = focusedTextItem();
    if (!ti) return;
    auto *tc = ti->textControl();
    QTextCursor cursor = tc->textCursor();
    int startBlock = cursor.document()->findBlock(cursor.selectionStart()).blockNumber();
    int endBlock = cursor.document()->findBlock(cursor.selectionEnd()).blockNumber();

    cursor.beginEditBlock();
    for (int b = startBlock; b <= endBlock; ++b) {
        QTextBlock block = cursor.document()->findBlockByNumber(b);
        QTextCursor bc(block);
        bc.movePosition(QTextCursor::StartOfBlock);
        bc.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QString line = bc.selectedText();
        int level = 0;
        while (level < line.size() && line.at(level) == QLatin1Char('#'))
            ++level;
        if (level < 6)
            bc.insertText(QStringLiteral("#") + (level == 0 ? QStringLiteral(" ") : QString()) + line);
    }
    cursor.endEditBlock();
}

void Editor::decreaseHeadingLevel()
{
    auto *ti = focusedTextItem();
    if (!ti) return;
    auto *tc = ti->textControl();
    QTextCursor cursor = tc->textCursor();
    int startBlock = cursor.document()->findBlock(cursor.selectionStart()).blockNumber();
    int endBlock = cursor.document()->findBlock(cursor.selectionEnd()).blockNumber();

    cursor.beginEditBlock();
    for (int b = startBlock; b <= endBlock; ++b) {
        QTextBlock block = cursor.document()->findBlockByNumber(b);
        QTextCursor bc(block);
        bc.movePosition(QTextCursor::StartOfBlock);
        bc.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QString line = bc.selectedText();
        if (line.startsWith(QLatin1Char('#')))
            bc.insertText(line.mid(1));
    }
    cursor.endEditBlock();
}

void Editor::setHeadingLevel(int level)
{
    auto *ti = focusedTextItem();
    if (!ti) return;
    auto *tc = ti->textControl();
    QTextCursor cursor = tc->textCursor();
    int startBlock = cursor.document()->findBlock(cursor.selectionStart()).blockNumber();
    int endBlock = cursor.document()->findBlock(cursor.selectionEnd()).blockNumber();

    cursor.beginEditBlock();
    for (int b = startBlock; b <= endBlock; ++b) {
        QTextBlock block = cursor.document()->findBlockByNumber(b);
        QTextCursor bc(block);
        bc.movePosition(QTextCursor::StartOfBlock);
        bc.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QString line = bc.selectedText();
        int existing = 0;
        while (existing < line.size() && line.at(existing) == QLatin1Char('#'))
            ++existing;
        // Skip the single space after the existing marker, if present.
        int stripTo = existing;
        if (stripTo < line.size() && line.at(stripTo) == QLatin1Char(' '))
            ++stripTo;
        QString body = line.mid(stripTo);
        QString replacement;
        if (level >= 1 && level <= 6)
            replacement = QString(level, QLatin1Char('#')) + QLatin1Char(' ') + body;
        else
            replacement = body;
        bc.insertText(replacement);
    }
    cursor.endEditBlock();
}

int Editor::currentHeadingLevel() const
{
    auto *ti = focusedTextItem();
    if (!ti) return 0;
    QTextCursor cursor = ti->textControl()->textCursor();
    QString line = cursor.block().text();
    int level = 0;
    while (level < line.size() && line.at(level) == QLatin1Char('#'))
        ++level;
    if (level == 0 || level > 6) return 0;
    // Require the canonical "# " form: leading '#'s followed by a space.
    if (level >= line.size() || line.at(level) != QLatin1Char(' '))
        return 0;
    return level;
}

void Editor::toggleCheckbox()
{
    // Three-state cycle per block in the selection:
    //   plain line  →  "- [ ] <line>"            (prepend unchecked)
    //   unchecked   →  "- [x] <line>"            (flip CheckedProperty)
    //   checked     →  "<line>"                  (strip "- " and glyph)
    //
    // The document is in substituted form — `[ ]` and `[x]` have already
    // been replaced with U+FFFC glyphs carrying CheckboxTextObject format.
    // We must inspect the glyph's format, not the block's text, because
    // the literal "- [ ]" / "- [x]" strings no longer appear in the
    // block's character stream after substitution.
    auto *ti = focusedTextItem();
    if (!ti) return;
    auto *tc = ti->textControl();
    QTextCursor cursor = tc->textCursor();
    QTextDocument *doc = cursor.document();
    const int startBlock = doc->findBlock(cursor.selectionStart()).blockNumber();
    const int endBlock = doc->findBlock(cursor.selectionEnd()).blockNumber();

    cursor.beginEditBlock();
    for (int b = startBlock; b <= endBlock; ++b) {
        QTextBlock block = doc->findBlockByNumber(b);
        if (!block.isValid()) continue;

        // Look for a substituted checkbox glyph first, then fall back
        // to literal source markdown. Both paths can appear in the
        // document depending on whether a reparse + substitution has
        // run since the text was last edited (the reparse is debounced
        // 150 ms, so a user rapidly re-toggling may still be looking
        // at the pre-substitution form).
        int glyphPos = -1;
        bool wasChecked = false;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            const QTextCharFormat fmt = frag.charFormat();
            if (fmt.objectType() != CheckboxTextObject::TypeId) continue;
            const QString t = frag.text();
            for (int i = 0; i < t.size(); ++i) {
                if (t.at(i) == QChar::ObjectReplacementCharacter) {
                    glyphPos = frag.position() + i;
                    wasChecked = fmt.property(
                        CheckboxTextObject::CheckedProperty).toBool();
                    break;
                }
            }
            if (glyphPos >= 0) break;
        }

        const int blockStart = block.position();
        const int blockEnd = blockStart + block.length() - 1;

        if (glyphPos >= 0 && !wasChecked) {
            // Unchecked → checked: flip glyph format in place.
            QTextCursor c(doc);
            c.setPosition(glyphPos);
            c.setPosition(glyphPos + 1, QTextCursor::KeepAnchor);
            QTextCharFormat newFmt;
            newFmt.setObjectType(CheckboxTextObject::TypeId);
            newFmt.setProperty(CheckboxTextObject::CheckedProperty, true);
            newFmt.setProperty(MathTextObject::RawProperty,
                               QStringLiteral("[x]"));
            c.setCharFormat(newFmt);
            continue;
        }

        if (glyphPos >= 0 && wasChecked) {
            // Checked → plain: strip the list-marker + glyph + trailing
            // space from the block's start. Returns the line to plain
            // text; re-adding a list marker needs an explicit toggle.
            QTextCursor c(doc);
            c.setPosition(blockStart);
            int endStrip = glyphPos + 1;
            if (endStrip < blockEnd
                && doc->characterAt(endStrip) == QLatin1Char(' ')) {
                ++endStrip;
            }
            c.setPosition(endStrip, QTextCursor::KeepAnchor);
            c.removeSelectedText();
            continue;
        }

        // No glyph — look at the literal source text. Substitution
        // may not have run yet (reparse is debounced 150 ms).
        const QString lineText = block.text();
        const int unchecked = lineText.indexOf(QStringLiteral("- [ ]"));

        int checkedIdx = -1;
        {
            const int lower = lineText.indexOf(QStringLiteral("- [x]"));
            const int upper = lineText.indexOf(QStringLiteral("- [X]"));
            if (lower >= 0 && upper >= 0) checkedIdx = std::min(lower, upper);
            else if (lower >= 0)          checkedIdx = lower;
            else if (upper >= 0)          checkedIdx = upper;
        }

        if (unchecked >= 0) {
            // Literal "- [ ]" → replace the middle char with 'x'.
            QTextCursor c(doc);
            c.setPosition(blockStart + unchecked + 3); // after "- ["
            c.setPosition(blockStart + unchecked + 4, QTextCursor::KeepAnchor);
            c.insertText(QStringLiteral("x"));
        } else if (checkedIdx >= 0) {
            // Literal "- [x]" → strip "- [x] " prefix (back to plain).
            QTextCursor c(doc);
            c.setPosition(blockStart + checkedIdx);
            int endStrip = blockStart + checkedIdx + 5; // past "- [x]"
            if (endStrip < blockEnd
                && doc->characterAt(endStrip) == QLatin1Char(' ')) {
                ++endStrip;
            }
            c.setPosition(endStrip, QTextCursor::KeepAnchor);
            c.removeSelectedText();
        } else {
            // Plain line → prepend "- [ ] ". Reparse will eventually
            // substitute the bracket text to a glyph.
            QTextCursor c(doc);
            c.setPosition(blockStart);
            c.insertText(QStringLiteral("- [ ] "));
        }
    }
    cursor.endEditBlock();
}

// =========================================================================
// Cursor info & navigation
// =========================================================================

int Editor::cursorLine() const
{
    auto *ti = focusedTextItem();
    if (!ti || !m_coordinator) return 1;

    auto gp = m_coordinator->globalPositionOf(
        ti,
        ti->textControl()->textCursor().blockNumber(),
        ti->textControl()->textCursor().positionInBlock());
    return gp.line;
}

int Editor::cursorColumn() const
{
    auto *ti = focusedTextItem();
    if (!ti) return 1;
    return ti->textControl()->textCursor().columnNumber() + 1;
}

QRect Editor::cursorScreenRect() const
{
    auto *ti = focusedTextItem();
    if (!ti) return {};
    QRectF itemRect = ti->textControl()->cursorRect();
    QRectF sceneRect = ti->mapToScene(itemRect).boundingRect();
    QPoint viewTL = m_view->mapFromScene(sceneRect.topLeft());
    QPoint viewBR = m_view->mapFromScene(sceneRect.bottomRight());
    return QRect(m_view->viewport()->mapToGlobal(viewTL),
                 m_view->viewport()->mapToGlobal(viewBR));
}

void Editor::goToLine(int line)
{
    if (!m_coordinator) return;

    auto pos = m_coordinator->itemAtGlobalLine(line);
    if (!pos.item) {
        ensureFocusedCursorVisible();
        return;
    }

    pos.item->setFocus();
    QTextBlock block = pos.item->document()->findBlockByNumber(pos.localBlockNumber);
    QTextCursor cursor(pos.item->document());
    if (block.isValid())
        cursor.setPosition(block.position());
    else
        cursor.movePosition(QTextCursor::End);
    pos.item->textControl()->setTextCursor(cursor);
    ensureFocusedCursorVisible();
}

void Editor::scrollToHeading(const HeadingInfo &heading)
{
    // Auto-unfold: find the heading's path and unfold any folded ancestors
    // so the target is visible before we scroll to it.
    if (m_foldingModel) {
        const auto &hs = m_foldingModel->headings();
        for (const auto &entry : hs) {
            if (entry.info.level == heading.level
                && entry.info.text == heading.text
                && entry.info.sourceOffset == heading.sourceOffset) {
                const auto unfolded = m_foldingModel->unfoldAncestors(entry.path);
                if (!unfolded.isEmpty())
                    Q_EMIT foldsAutoExpanded(unfolded);
                break;
            }
        }
    }

    // HeadingInfo::sourceOffset is a UTF-8 byte offset into the document's
    // serialized source. Convert it to a 1-based line number by counting
    // newlines up to that offset, then defer to goToLine().
    const QByteArray utf8 = toPlainText().toUtf8();
    if (heading.sourceOffset < 0 || heading.sourceOffset >= utf8.size()) {
        goToLine(1);
        return;
    }
    int line = 1;
    for (int i = 0; i < heading.sourceOffset; ++i) {
        if (utf8[i] == '\n') ++line;
    }
    goToLine(line);
}

// =========================================================================
// Completion trigger detection
// =========================================================================

void Editor::detectCompletionTriggers(const QString &insertedText)
{
    if (insertedText.isEmpty()) return;
    auto *ti = focusedTextItem();
    if (!ti) return;

    QTextCursor cursor = ti->textControl()->textCursor();
    int pos = cursor.positionInBlock();
    QString blockText = cursor.block().text();

    QChar ch = insertedText.at(0);
    if (ch == QLatin1Char('[') && pos >= 2 &&
        blockText.mid(pos - 2, 2) == QStringLiteral("[[")) {
        Q_EMIT wikiLinkTrigger(cursor.position());
    }
    if (ch == QLatin1Char('#') && pos > 1) {
        Q_EMIT tagTrigger(cursor.position());
    }
}

// =========================================================================
// Search
// =========================================================================

// Walk every MarkdownTextItem in the scene in order, starting from the
// focused item (if any), so find/replace can cross item boundaries.
// Wraps around once.
static QList<MarkdownTextItem *> textItemsInSearchOrder(SceneCoordinator *coord,
                                                         MarkdownTextItem *startAfter,
                                                         bool backward)
{
    QList<MarkdownTextItem *> result;
    if (!coord) return result;
    QList<MarkdownTextItem *> all;
    for (auto *item : coord->items()) {
        if (item->isTextItem())
            all.append(static_cast<MarkdownTextItem *>(item));
    }
    if (all.isEmpty()) return result;

    int startIdx = 0;
    if (startAfter) {
        for (int i = 0; i < all.size(); ++i) {
            if (all[i] == startAfter) { startIdx = i; break; }
        }
    }
    if (backward) {
        for (int i = startIdx; i >= 0; --i) result.append(all[i]);
        for (int i = all.size() - 1; i > startIdx; --i) result.append(all[i]);
    } else {
        for (int i = startIdx; i < all.size(); ++i) result.append(all[i]);
        for (int i = 0; i < startIdx; ++i) result.append(all[i]);
    }
    return result;
}

bool Editor::findText(const QString &text, QTextDocument::FindFlags flags)
{
    if (text.isEmpty()) return false;
    const bool backward = flags & QTextDocument::FindBackward;

    auto *focused = focusedTextItem();
    auto items = textItemsInSearchOrder(m_coordinator, focused, backward);
    if (items.isEmpty()) return false;

    // Helper: search a single document, picking the right starting cursor.
    auto searchOne = [&](MarkdownTextItem *ti, bool fromCursor) -> QTextCursor {
        auto *doc = ti->document();
        QTextCursor start;
        if (fromCursor) {
            start = ti->textControl()->textCursor();
        } else {
            start = QTextCursor(doc);
            if (backward)
                start.movePosition(QTextCursor::End);
        }
        return doc->find(text, start, flags);
    };

    // Helper: given a matched item and cursor, auto-unfold ancestors if needed
    // and position the cursor on the match.
    auto commitMatch = [&](MarkdownTextItem *ti, const QTextCursor &found) {
        ti->textControl()->setTextCursor(found);
        ti->setFocus();

        // Auto-unfold: determine the enclosing heading path for the match block
        // and unfold any folded ancestors so the match is visible.
        if (m_foldingModel && m_coordinator) {
            const auto &allItems = m_coordinator->items();
            int itemIdx = -1;
            for (int i = 0; i < allItems.size(); ++i) {
                if (allItems[i] == static_cast<SelectableItem *>(ti)) {
                    itemIdx = i;
                    break;
                }
            }
            if (itemIdx >= 0) {
                const int blockNum = found.block().blockNumber();
                const QStringList path =
                    m_coordinator->enclosingHeadingPathAtBlock(itemIdx, blockNum);
                if (!path.isEmpty()) {
                    const auto unfolded = m_foldingModel->unfoldAncestors(path);
                    if (!unfolded.isEmpty())
                        Q_EMIT foldsAutoExpanded(unfolded);
                }
            }
        }
    };

    // First pass: search the focused item from the current cursor (so
    // repeated finds advance), then any other items from their edges.
    bool isFirst = true;
    for (auto *ti : items) {
        QTextCursor found = searchOne(ti, isFirst && ti == focused);
        if (!found.isNull()) {
            commitMatch(ti, found);
            return true;
        }
        isFirst = false;
    }

    // Second pass: if nothing matched and we started mid-document on the
    // focused item, wrap within that item by retrying from its edge. This
    // is what users expect from "find next" — it should wrap around even
    // for a single-item document.
    if (focused) {
        QTextCursor found = searchOne(focused, /*fromCursor=*/false);
        if (!found.isNull()) {
            commitMatch(focused, found);
            return true;
        }
    }
    return false;
}

bool Editor::replaceText(const QString &find, const QString &replace,
                         QTextDocument::FindFlags flags)
{
    auto *ti = focusedTextItem();
    if (!ti) return findText(find, flags);
    QTextCursor cursor = ti->textControl()->textCursor();
    // Replace the current selection only if it actually matches `find`.
    const bool caseSensitive = flags & QTextDocument::FindCaseSensitively;
    if (cursor.hasSelection()
        && cursor.selectedText().compare(find,
                                          caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0) {
        cursor.insertText(replace);
        ti->textControl()->setTextCursor(cursor);
    }
    // Then advance to the next match (cross-item, wrapping).
    return findText(find, flags);
}

int Editor::replaceAll(const QString &find, const QString &replace,
                       QTextDocument::FindFlags flags)
{
    if (find.isEmpty() || !m_coordinator) return 0;
    int count = 0;
    for (auto *item : m_coordinator->items()) {
        if (!item->isTextItem()) continue;
        auto *ti = static_cast<MarkdownTextItem *>(item);
        auto *doc = ti->document();
        QTextCursor cursor(doc);
        QTextCursor first = doc->find(find, cursor, flags);
        if (first.isNull()) continue;

        cursor = first;
        cursor.beginEditBlock();
        do {
            cursor.insertText(replace);
            ++count;
            cursor = doc->find(find, cursor, flags);
        } while (!cursor.isNull());
        first.endEditBlock();
    }
    return count;
}

QTextDocument::FindFlags Editor::searchFlags() const
{
    QTextDocument::FindFlags flags;
    if (m_searchBar && m_searchBar->matchCase())
        flags |= QTextDocument::FindCaseSensitively;
    return flags;
}

void Editor::showSearchBar()
{
    if (auto *ti = focusedTextItem()) {
        QString sel = ti->textControl()->textCursor().selectedText();
        if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator))
            m_searchBar->setSearchText(sel);
    }
    m_searchBar->showFind();
    repositionSearchBar();
}

void Editor::showReplaceBar()
{
    if (auto *ti = focusedTextItem()) {
        QString sel = ti->textControl()->textCursor().selectedText();
        if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator))
            m_searchBar->setSearchText(sel);
    }
    m_searchBar->showReplace();
    repositionSearchBar();
}

void Editor::hideSearchBar()
{
    m_searchBar->hide();
    static_cast<EditorGraphicsView *>(m_view)->setViewportMargins(0, 0, 0, 0);
    clearSearchHighlights();
    setFocus();
}

void Editor::repositionSearchBar()
{
    if (!m_searchBar->isVisible()) {
        static_cast<EditorGraphicsView *>(m_view)->setViewportMargins(0, 0, 0, 0);
        return;
    }
    int barHeight = m_searchBar->sizeHint().height();
    // Reserve space at the bottom of the viewport so scene content
    // cannot scroll under the bar.
    static_cast<EditorGraphicsView *>(m_view)->setViewportMargins(0, 0, 0, barHeight);
    // Position the bar in the reserved strip, spanning the viewport's
    // horizontal extent (so it doesn't cover the vertical scrollbar).
    QPoint vpTopLeft = m_view->viewport()->mapTo(this, QPoint(0, 0));
    int vw = m_view->viewport()->width();
    int vh = m_view->viewport()->height();
    m_searchBar->setGeometry(vpTopLeft.x(), vpTopLeft.y() + vh,
                             vw, barHeight);
    m_searchBar->raise();
}

void Editor::highlightAllMatches(const QString &text)
{
    clearSearchHighlights();
    m_lastSearchText = text;
    m_totalMatchCount = 0;
    m_currentMatchIndex = -1;

    if (text.isEmpty() || !m_coordinator) {
        m_searchBar->setMatchCount(0, 0);
        return;
    }

    QTextDocument::FindFlags flags = searchFlags();

    QTextCharFormat matchFmt;
    matchFmt.setBackground(m_theme.paint.searchMatchBg);

    QTextCharFormat currentFmt;
    currentFmt.setBackground(m_theme.paint.searchCurrentMatchBg);

    // Find the focused item and cursor position for current-match tracking
    auto *focusedItem = focusedTextItem();
    int focusedCursorPos = -1;
    if (focusedItem)
        focusedCursorPos = focusedItem->textControl()->textCursor().position();

    int globalIndex = 0;
    int closestIndex = -1;
    int closestDistance = std::numeric_limits<int>::max();
    MarkdownTextItem *closestItem = nullptr;
    QTextCursor closestCursor;

    for (auto *item : m_coordinator->items()) {
        if (!item->isTextItem()) continue;
        auto *ti = static_cast<MarkdownTextItem *>(item);
        auto *doc = ti->document();
        QList<QTextEdit::ExtraSelection> selections;

        QTextCursor search(doc);
        while (!(search = doc->find(text, search, flags)).isNull()) {
            if (m_totalMatchCount >= 65536) break;

            QTextEdit::ExtraSelection sel;
            sel.cursor = search;
            sel.format = matchFmt;
            selections.append(sel);

            // Track closest match to cursor for current-match index
            if (ti == focusedItem && focusedCursorPos >= 0) {
                int dist = search.selectionStart() - focusedCursorPos;
                if (dist >= 0 && dist < closestDistance) {
                    closestDistance = dist;
                    closestIndex = globalIndex;
                    closestItem = ti;
                    closestCursor = search;
                }
            }

            ++globalIndex;
            ++m_totalMatchCount;
        }

        ti->textControl()->setExtraSelections(selections);
    }

    // If no match found at/after cursor, use the first match
    if (closestIndex < 0 && m_totalMatchCount > 0)
        closestIndex = 0;

    m_currentMatchIndex = closestIndex;

    // Highlight current match in orange
    if (closestItem && closestCursor.hasSelection()) {
        auto rawSelections = closestItem->textControl()->extraSelections();
        for (int i = 0; i < rawSelections.size(); ++i) {
            if (rawSelections[i].cursor.selectionStart() == closestCursor.selectionStart()
                && rawSelections[i].cursor.selectionEnd() == closestCursor.selectionEnd()) {
                rawSelections[i].format = currentFmt;
            }
        }
        closestItem->textControl()->setExtraSelections(rawSelections);
    }

    m_searchBar->setMatchCount(
        m_totalMatchCount > 0 ? m_currentMatchIndex + 1 : 0,
        m_totalMatchCount);
}

void Editor::clearSearchHighlights()
{
    if (!m_coordinator) return;
    const QList<QTextEdit::ExtraSelection> empty;
    for (auto *item : m_coordinator->items()) {
        if (!item->isTextItem()) continue;
        auto *ti = static_cast<MarkdownTextItem *>(item);
        ti->textControl()->setExtraSelections(empty);
    }
    m_totalMatchCount = 0;
    m_currentMatchIndex = -1;
}

void Editor::updateMatchCount()
{
    if (m_lastSearchText.isEmpty() || m_totalMatchCount == 0) return;

    // Recompute current index based on cursor position
    auto *focusedItem = focusedTextItem();
    if (!focusedItem) return;
    int cursorPos = focusedItem->textControl()->textCursor().selectionStart();

    int index = 0;
    QTextDocument::FindFlags flags = searchFlags();
    for (auto *item : m_coordinator->items()) {
        if (!item->isTextItem()) continue;
        auto *ti = static_cast<MarkdownTextItem *>(item);
        auto *doc = ti->document();
        QTextCursor search(doc);
        while (!(search = doc->find(m_lastSearchText, search, flags)).isNull()) {
            if (ti == focusedItem && search.selectionStart() == cursorPos) {
                m_currentMatchIndex = index;
                m_searchBar->setMatchCount(index + 1, m_totalMatchCount);

                // Update orange highlight
                highlightAllMatches(m_lastSearchText);
                return;
            }
            ++index;
        }
    }
}

// =========================================================================
// Folding public API
// =========================================================================

QList<QStringList> Editor::headingPaths() const
{
    return m_foldingModel->allPaths();
}

bool Editor::isFolded(const QStringList &path) const
{
    return m_foldingModel->isFolded(path);
}

QList<QStringList> Editor::foldedPaths() const
{
    return m_foldingModel->foldedPaths();
}

void Editor::fold(const QStringList &path)
{
    m_foldingModel->fold(path);
}

void Editor::unfold(const QStringList &path)
{
    m_foldingModel->unfold(path);
}

void Editor::toggleFold(const QStringList &path)
{
    m_foldingModel->toggle(path);
}

void Editor::toggleFoldAtCursor()
{
    // HeadingInfo::sourceOffset is a UTF-8 byte offset (same unit as used in
    // scrollToHeading). Convert cursor line to a byte offset range check by
    // finding the byte offset of the start of cursorLine(), then pick the
    // last heading whose sourceOffset is <= that byte offset.
    const int line = cursorLine(); // 1-based
    const QByteArray utf8 = toPlainText().toUtf8();

    // Compute the byte offset of the start of `line` (1-based).
    int lineStart = 0;
    int currentLine = 1;
    for (int i = 0; i < utf8.size() && currentLine < line; ++i) {
        if (utf8[i] == '\n') {
            ++currentLine;
            if (currentLine == line) {
                lineStart = i + 1;
                break;
            }
        }
    }

    const auto &hs = m_foldingModel->headings();
    const FoldingModel::HeadingEntry *best = nullptr;
    for (const auto &h : hs) {
        if (h.info.sourceOffset <= lineStart)
            best = &h;
        else
            break;
    }
    if (best)
        m_foldingModel->toggle(best->path);
}

void Editor::foldAll()          { m_foldingModel->foldAll(); }
void Editor::unfoldAll()        { m_foldingModel->unfoldAll(); }
void Editor::foldAllAtLevel(int level)   { m_foldingModel->foldAllAtLevel(level); }
void Editor::unfoldAllAtLevel(int level) { m_foldingModel->unfoldAllAtLevel(level); }
void Editor::foldLevel(int n)   { m_foldingModel->foldLevel(n); }
void Editor::unfoldLevel(int n) { m_foldingModel->unfoldLevel(n); }

QJsonObject Editor::serializeFoldState() const
{
    return m_foldingModel->serialize();
}

void Editor::restoreFoldState(const QJsonObject &state)
{
    m_foldingModel->restore(state);
}

void Editor::repositionFoldGutter()
{
    if (m_foldGutter)
        m_foldGutter->setPos(m_view->mapToScene(m_view->viewport()->rect().topLeft()));
}

void Editor::setGutterVisible(bool visible)
{
    m_gutterVisible = visible;
    if (m_foldGutter)
        m_foldGutter->setVisible(visible);
}

bool Editor::isGutterVisible() const
{
    return m_gutterVisible;
}

// ============================================================================
// Visual-line float scroll (Cluster E Phase 2)
// ============================================================================
//
// Markoff is a QGraphicsView-backed editor where each block (paragraph, table,
// image, code block, ...) is one QGraphicsItem in a vertically-stacked scene.
// A "visual line" here is one wrapped display line's worth of vertical space.
// For a block item we approximate its visual-line span as
// `ceil(boundingRect().height() / lineHeight)`, where `lineHeight` is the
// theme's base textFont line spacing. This is uniform across all block types
// (no per-class virtual method) because every block already publishes its
// height through `boundingRect()` — approximation is intentional: the
// ±0.5-visual-line contract tolerates the difference between a 1.5-line-high
// heading and a 2-line slot, and avoiding a virtual-method plumbing pass
// keeps the Phase 2 change surgical per the plan.
//
// The scene-Y ⇄ visual-line mapping is computed on-call by walking the
// coordinator's items list in display order. Input sizes are modest
// (hundreds of blocks), so a linear walk is cheaper than maintaining a
// persistent cumsum across every reparse.

namespace {

qreal editorLineHeight(const Markoff::Theme &theme)
{
    QFontMetricsF fm(theme.textFont);
    const qreal h = fm.lineSpacing();
    return h > 1.0 ? h : 16.0;
}

} // namespace

float Editor::scrollPositionVisualLine() const
{
    if (!m_coordinator)
        return 0.0f;
    const auto &items = m_coordinator->items();
    if (items.isEmpty())
        return 0.0f;

    const qreal lineH = editorLineHeight(m_theme);
    const qreal y = m_view->verticalScrollBar()->value();

    qreal linesSoFar = 0.0;
    for (auto *item : items) {
        QGraphicsItem *gi = item->asGraphicsItem();
        if (!gi || !gi->isVisible())
            continue;
        const QRectF r = gi->sceneBoundingRect();
        const qreal blockLines = std::max<qreal>(1.0, std::ceil(r.height() / lineH));
        if (y < r.bottom()) {
            // `y` is inside or above this block; compute fractional position.
            const qreal offset = std::max<qreal>(0.0, y - r.top());
            const qreal frac = std::min<qreal>(blockLines, offset / lineH);
            return static_cast<float>(linesSoFar + frac);
        }
        linesSoFar += blockLines;
    }
    return static_cast<float>(linesSoFar);
}

void Editor::setScrollPositionVisualLine(float visualLine)
{
    if (!m_coordinator)
        return;
    const auto &items = m_coordinator->items();
    if (items.isEmpty())
        return;

    const qreal lineH = editorLineHeight(m_theme);
    const qreal target = std::max<qreal>(0.0, visualLine);

    qreal linesSoFar = 0.0;
    qreal pixelY = 0.0;
    bool placed = false;
    for (auto *item : items) {
        QGraphicsItem *gi = item->asGraphicsItem();
        if (!gi || !gi->isVisible())
            continue;
        const QRectF r = gi->sceneBoundingRect();
        const qreal blockLines = std::max<qreal>(1.0, std::ceil(r.height() / lineH));
        if (target <= linesSoFar + blockLines) {
            const qreal insideLines = target - linesSoFar;
            pixelY = r.top() + insideLines * lineH;
            placed = true;
            break;
        }
        linesSoFar += blockLines;
        pixelY = r.bottom();
    }
    if (!placed) {
        // Past the end: clamp to the last block's bottom.
        pixelY = std::max<qreal>(0.0, pixelY);
    }

    QScrollBar *vbar = m_view->verticalScrollBar();
    const int clamped = std::clamp<int>(static_cast<int>(std::round(pixelY)),
                                        vbar->minimum(), vbar->maximum());
    vbar->setValue(clamped);
}

// =========================================================================
// Link emission bridge (Cluster J phase 3)
// =========================================================================

LinkRenderer *Editor::linkRenderer() const
{
    return m_linkRenderer;
}

void Editor::setCurrentNotePath(const QString &path)
{
    m_currentNotePath = path;
}

QString Editor::currentNotePath() const
{
    return m_currentNotePath;
}

void Editor::subscribeLinkSignalsForItems()
{
    if (!m_linkRenderer) return;
    for (auto *item : m_coordinator->items()) {
        if (!item->isTextItem()) continue;
        auto *textItem = static_cast<MarkdownTextItem *>(item->asGraphicsItem());
        auto *tc = textItem->textControl();
        if (!tc) continue;
        // Qt::UniqueConnection is permitted only with pointer-to-member
        // slots, not with lambdas. Connect to private member functions on
        // Editor so re-subscription across reparses stays idempotent.
        connect(tc, &TextControl::linkActivated,
                this, &Editor::handleLinkActivated,
                Qt::UniqueConnection);
        connect(tc, &TextControl::linkHovered,
                this, &Editor::handleLinkHovered,
                Qt::UniqueConnection);
        connect(tc, &TextControl::cursorPositionChanged,
                this, &Editor::onCursorMoved,
                Qt::UniqueConnection);
    }
}

void Editor::onCursorMoved()
{
    auto *item = focusedTextItem();
    if (item) {
        QTextCursor cursor = item->textControl()->textCursor();
        QTextTable *table = cursor.currentTable();
        if (table) {
            QTextTableCell cell = table->cellAt(cursor);
            if (!m_inTable) {
                m_inTable = true;
                Q_EMIT tableEntered(table->rows(), table->columns());
            }
            Q_EMIT tableCursorMoved(cell.row(), cell.column());
        } else if (m_inTable) {
            m_inTable = false;
            Q_EMIT tableExited();
        }
    } else if (m_inTable) {
        m_inTable = false;
        Q_EMIT tableExited();
    }

    Q_EMIT cursorPositionChanged(cursorLine(), cursorColumn());
}

void Editor::handleLinkActivated(const QString &href)
{
    if (!m_linkRenderer) return;
    static const QString kWikilinkPrefix = QStringLiteral("wikilink://");
    if (href.startsWith(kWikilinkPrefix)) {
        const QString target = href.mid(kWikilinkPrefix.length());
        LinkRenderer::FileLinkRequest req;
        req.linkText = target;
        req.fromPath = m_currentNotePath;
        req.sourceId = QStringLiteral("markoff:editor");
        req.anchorHint = QCursor::pos();
        m_linkRenderer->emitFileLinkActivated(req);
        // Legacy public signal (was declared in Editor's public API but
        // never emitted before Cluster J phase 3; now fires through the
        // new chokepoint for backward compat with MainWindow consumers).
        Q_EMIT linkClicked(target);
    } else {
        m_linkRenderer->emitExternalLinkActivated(QUrl(href),
                                                   QStringLiteral("markoff:editor"));
    }
}

void Editor::handleLinkHovered(const QString &href)
{
    if (!m_linkRenderer) return;
    static const QString kWikilinkPrefix = QStringLiteral("wikilink://");
    const QPoint globalPos = QCursor::pos();
    if (href.isEmpty()) {
        // "leave" event — still emit so subscribers can clear popovers.
        LinkRenderer::FileLinkRequest req;
        req.sourceId = QStringLiteral("markoff:editor");
        m_linkRenderer->emitFileLinkHovered(req);
        Q_EMIT linkHovered(QString(), QPoint());
        return;
    }
    if (href.startsWith(kWikilinkPrefix)) {
        const QString target = href.mid(kWikilinkPrefix.length());
        LinkRenderer::FileLinkRequest req;
        req.linkText = target;
        req.fromPath = m_currentNotePath;
        req.sourceId = QStringLiteral("markoff:editor");
        req.anchorHint = globalPos;
        m_linkRenderer->emitFileLinkHovered(req);
        Q_EMIT linkHovered(target, globalPos);
    } else {
        m_linkRenderer->emitExternalLinkHovered(QUrl(href),
                                                 QStringLiteral("markoff:editor"));
        Q_EMIT linkHovered(href, globalPos);
    }
}

void Editor::testActivateLink(const QString &href)
{
    handleLinkActivated(href);
}

void Editor::testHoverLink(const QString &href)
{
    handleLinkHovered(href);
}

// =========================================================================
// Table operations
// =========================================================================

/// Return the QTextTable under the focused text item's cursor, or nullptr.
static QTextTable *currentTableInEditor(Editor *editor)
{
    // focusedTextItem() is private; use the scene's focusItem() directly.
    auto *item = editor->scene()->focusItem();
    if (!item) return nullptr;
    auto *ti = dynamic_cast<MarkdownTextItem *>(item);
    if (!ti) return nullptr;
    QTextCursor cursor = ti->textControl()->textCursor();
    return cursor.currentTable();
}

bool Editor::cursorInTable() const
{
    auto *ti = focusedTextItem();
    if (!ti) return false;
    return ti->textControl()->textCursor().currentTable() != nullptr;
}

void Editor::tableInsertRowAbove()
{
    if (m_readOnly) return;
    QTextTable *table = currentTableInEditor(this);
    if (!table) return;
    auto *ti = focusedTextItem();
    QTextCursor cursor = ti->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->insertRows(cell.row(), 1);
    cursor.endEditBlock();
}

void Editor::tableInsertRowBelow()
{
    if (m_readOnly) return;
    QTextTable *table = currentTableInEditor(this);
    if (!table) return;
    auto *ti = focusedTextItem();
    QTextCursor cursor = ti->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->insertRows(cell.row() + 1, 1);
    cursor.endEditBlock();
}

void Editor::tableInsertColumnLeft()
{
    if (m_readOnly) return;
    QTextTable *table = currentTableInEditor(this);
    if (!table) return;
    auto *ti = focusedTextItem();
    QTextCursor cursor = ti->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->insertColumns(cell.column(), 1);
    cursor.endEditBlock();
}

void Editor::tableInsertColumnRight()
{
    if (m_readOnly) return;
    QTextTable *table = currentTableInEditor(this);
    if (!table) return;
    auto *ti = focusedTextItem();
    QTextCursor cursor = ti->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->insertColumns(cell.column() + 1, 1);
    cursor.endEditBlock();
}

void Editor::tableDeleteRow()
{
    if (m_readOnly) return;
    QTextTable *table = currentTableInEditor(this);
    if (!table) return;
    if (table->rows() <= 1) return;
    auto *ti = focusedTextItem();
    QTextCursor cursor = ti->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->removeRows(cell.row(), 1);
    cursor.endEditBlock();
}

void Editor::tableDeleteColumn()
{
    if (m_readOnly) return;
    QTextTable *table = currentTableInEditor(this);
    if (!table) return;
    if (table->columns() <= 1) return;
    auto *ti = focusedTextItem();
    QTextCursor cursor = ti->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->removeColumns(cell.column(), 1);
    cursor.endEditBlock();
}

void Editor::tableSelectRow()
{
    QTextTable *table = currentTableInEditor(this);
    if (!table) return;
    auto *ti = focusedTextItem();
    auto *tc = ti->textControl();
    QTextCursor cursor = tc->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    int row = cell.row();
    int cols = table->columns();

    QTextCursor first = table->cellAt(row, 0).firstCursorPosition();
    QTextCursor last = table->cellAt(row, cols - 1).lastCursorPosition();
    first.setPosition(last.position(), QTextCursor::KeepAnchor);
    tc->setTextCursor(first);
}

void Editor::tableSelectColumn()
{
    QTextTable *table = currentTableInEditor(this);
    if (!table) return;
    auto *ti = focusedTextItem();
    auto *tc = ti->textControl();
    QTextCursor cursor = tc->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    int col = cell.column();
    int rows = table->rows();

    QTextCursor first = table->cellAt(0, col).firstCursorPosition();
    QTextCursor last = table->cellAt(rows - 1, col).lastCursorPosition();
    first.setPosition(last.position(), QTextCursor::KeepAnchor);
    tc->setTextCursor(first);
}

// =========================================================================
// QGraphicsView passthrough (tests + internal helpers)
// =========================================================================

QGraphicsScene *Editor::scene() const { return m_view->scene(); }
QWidget *Editor::viewport() const { return m_view->viewport(); }
QScrollBar *Editor::verticalScrollBar() const { return m_view->verticalScrollBar(); }
QScrollBar *Editor::horizontalScrollBar() const { return m_view->horizontalScrollBar(); }
QPointF Editor::mapToScene(const QPoint &p) const { return m_view->mapToScene(p); }
QPointF Editor::mapToScene(int x, int y) const { return m_view->mapToScene(x, y); }
QPoint Editor::mapFromScene(const QPointF &p) const { return m_view->mapFromScene(p); }

// =========================================================================
// MarkdownView overrides (Phase A forwarding)
// =========================================================================

void Editor::setDocument(MarkoffDocument *doc) { m_markoffDoc = doc; }
MarkoffDocument *Editor::document() const { return m_markoffDoc; }

void Editor::setViewTheme(const Theme &) {}
void Editor::setViewResourceProvider(ResourceProvider *) {}
void Editor::setViewLinkResolver(LinkResolver *) {}

float Editor::scrollPosition() const
{
    return scrollPositionVisualLine();
}

void Editor::setScrollPosition(float visualLine)
{
    setScrollPositionVisualLine(visualLine);
}

void Editor::zoomIn()  { setFontSize(m_fontSize + 1); }
void Editor::zoomOut() { setFontSize(m_fontSize - 1); }

QJsonObject Editor::ephemeralState() const
{
    QJsonObject j;
    j.insert(QStringLiteral("scroll"), scrollPosition());
    return j;
}

void Editor::setEphemeralState(const QJsonObject &j)
{
    setScrollPosition(static_cast<float>(
        j.value(QStringLiteral("scroll")).toDouble(0.0)));
}

SearchAdapter *Editor::searchAdapter()
{
    return m_searchAdapter.get();
}

CursorPos Editor::cursorPosition() const
{
    return {cursorLine(), cursorColumn()};
}

bool Editor::setCursorPosition(CursorPos p)
{
    goToLine(p.line);
    return true;
}

QVector<FoldSpec> Editor::foldedHeadings() const
{
    QVector<FoldSpec> out;
    if (!m_foldingModel) return out;
    const auto paths = m_foldingModel->foldedPaths();
    const auto &headings = m_foldingModel->headings();
    for (const auto &path : paths) {
        for (const auto &h : headings) {
            if (h.path == path) {
                // Convert UTF-8 byte offset to 1-based line number.
                const QByteArray utf8 = toPlainText().toUtf8();
                int line = 1;
                for (int i = 0; i < h.info.sourceOffset && i < utf8.size(); ++i)
                    if (utf8[i] == '\n') ++line;
                out.append({line, h.info.level});
                break;
            }
        }
    }
    return out;
}

void Editor::setFoldedHeadings(const QVector<FoldSpec> &v)
{
    if (!m_foldingModel) return;
    m_foldingModel->unfoldAll();
    const auto &headings = m_foldingModel->headings();
    const QByteArray utf8 = toPlainText().toUtf8();
    for (const auto &spec : v) {
        // Convert 1-based line to UTF-8 byte offset.
        int line = 1;
        int offset = 0;
        for (int i = 0; i < utf8.size(); ++i) {
            if (line == spec.line) { offset = i; break; }
            if (utf8[i] == '\n') ++line;
        }
        for (const auto &h : headings) {
            if (h.info.level == spec.level && h.info.sourceOffset == offset) {
                m_foldingModel->fold(h.path);
                break;
            }
        }
    }
}

// =========================================================================
// LiveSearchAdapter bridges
// =========================================================================

int Editor::sourceOffsetAtCursor() const
{
    // Phase A approximation: byte offset to the start of the cursor's line.
    const QByteArray utf8 = toPlainText().toUtf8();
    const int line = cursorLine();  // 1-based
    int currentLine = 1;
    int offset = 0;
    for (int i = 0; i < utf8.size(); ++i) {
        if (currentLine == line) { offset = i; break; }
        if (utf8[i] == '\n') {
            ++currentLine;
            if (currentLine == line) { offset = i + 1; break; }
        }
    }
    return offset + cursorColumn() - 1;
}

void Editor::highlightSearchSpans(const QVector<TextSpan> &spans)
{
    // Phase A: spans carry source offsets but live highlighting works
    // per-item. Defer to the existing machinery by clearing and running
    // highlightAllMatches for the first span's text representation.
    // For now, clear when empty; otherwise no-op forwarding.
    if (spans.isEmpty()) {
        clearSearchHighlights();
    }
    // Full source-offset-driven highlighting lands in Phase C.
}

void Editor::clearSearchHighlightsPublic()
{
    clearSearchHighlights();
}

void Editor::scrollSourceSpanIntoView(TextSpan span)
{
    // Phase A: convert source offset to a line number and scroll there.
    const QByteArray utf8 = toPlainText().toUtf8();
    if (span.offset < 0 || span.offset > utf8.size()) return;
    int line = 1;
    for (int i = 0; i < span.offset && i < utf8.size(); ++i) {
        if (utf8[i] == '\n') ++line;
    }
    goToLine(line);
}

} // namespace Markoff
