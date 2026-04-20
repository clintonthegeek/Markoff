// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_EDITOR_H
#define MARKOFF_EDITOR_H

#include <QHash>
#include <QJsonObject>
#include <QTextDocument>
#include <memory>
#include <markoff/MarkdownView.h>
#include <markoff/Theme.h>
#include <markoff-parser/Document.h>

#include <markoff/TextSpan.h>

class QAction;
class QGraphicsScene;
class QGraphicsView;
class QScrollBar;
class QTimer;
class QVBoxLayout;

namespace Markoff {

/// Identifies editor commands that are exposed as QActions.
/// Host applications can retrieve these via Editor::action() and register
/// them with their own action collection / shortcut editor.
enum class ActionId {
    Undo, Redo,
    Cut, Copy, Paste, SelectAll,
    Find, FindNext, FindPrevious, Replace,
    ZoomIn, ZoomOut,
    ToggleBold, ToggleItalic, ToggleStrikethrough, ToggleInlineCode,
    InsertLink, InsertWikiLink, InsertImage,
    InsertCodeBlock, InsertBlockQuote, InsertHorizontalRule, InsertTable,
    IncreaseHeading, DecreaseHeading, ToggleCheckbox,
    ToggleFoldAtCursor, FoldAll, UnfoldAll,
    SetHeading1, SetHeading2, SetHeading3, SetHeading4, SetHeading5, SetHeading6,
};

class SelectionScene;
class SceneCoordinator;
class MarkdownTextItem;
class ResourceProvider;
class SearchBar;
class FoldingModel;
class FoldGutter;
class LinkRenderer;
class LiveSearchAdapter;

class Editor : public MarkdownView {
    Q_OBJECT

public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    // --- Actions ---
    /// Returns the QAction for the given editor command, or nullptr.
    QAction *action(ActionId id) const;
    /// Returns all registered actions (for bulk host-app integration).
    QList<QAction*> actions() const;

    // --- Content ---
    void setPlainText(const QString &text);
    void clear();
    QString toPlainText() const;
    /// Returns the live-preview parser Document (headings, links, tags).
    /// Distinct from MarkdownView::document() which returns the shared
    /// MarkoffDocument pointer set via setDocument().
    const Document *parsedDocument() const;

    // --- QGraphicsView passthrough (tests + internal helpers) ---
    QGraphicsScene *scene() const;
    QWidget *viewport() const;
    QScrollBar *verticalScrollBar() const;
    QScrollBar *horizontalScrollBar() const;
    QPointF mapToScene(const QPoint &p) const;
    QPointF mapToScene(int x, int y) const;
    QPoint mapFromScene(const QPointF &p) const;
    /// Access the composed QGraphicsView child (internal/test use).
    QGraphicsView *graphicsView() const { return m_view; }

    // --- Configuration ---
    void setTheme(const Theme &theme);
    Theme theme() const;

    void setResourceProvider(ResourceProvider *provider);

    /// Enable or disable editing. When read-only, the editor displays
    /// live-preview-formatted markdown but does not accept input.
    ///
    /// NOTE: Read-only mode does NOT prevent all user interaction with
    /// non-text block items. Future interactive block items may allow
    /// non-destructive display adjustments — such as column width resizing
    /// — that affect only the visual presentation and do not modify the
    /// underlying markdown. These are ephemeral viewport affordances for
    /// readability, not editing operations, and are not persisted or
    /// serialized.
    bool setReadOnly(bool readOnly) override;
    bool isReadOnly() const override;

    // --- MarkdownView overrides (Phase A forwarding) ---
    void setDocument(MarkoffDocument *doc) override;
    MarkoffDocument *document() const override;
    void setViewTheme(const Theme &theme) override;
    void setViewResourceProvider(ResourceProvider *rp) override;
    void setViewLinkResolver(LinkResolver *lr) override;
    float scrollPosition() const override;
    void setScrollPosition(float visualLine) override;
    void zoomIn() override;
    void zoomOut() override;
    void resetZoom() override;
    QJsonObject ephemeralState() const override;
    void setEphemeralState(const QJsonObject &) override;
    SearchAdapter *searchAdapter() override;
    bool hasCursor() const override { return true; }
    bool hasEditing() const override { return true; }
    bool hasFold() const override { return true; }
    CursorPos cursorPosition() const override;
    bool setCursorPosition(CursorPos) override;
    QVector<FoldSpec> foldedHeadings() const override;
    void setFoldedHeadings(const QVector<FoldSpec> &) override;

    // --- LiveSearchAdapter bridge (public so the adapter can call) ---
    int sourceOffsetAtCursor() const;
    void highlightSearchSpans(const QVector<TextSpan> &spans);
    void clearSearchHighlightsPublic();
    void scrollSourceSpanIntoView(TextSpan span);

    // --- Editing actions ---
    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void selectAll();

    // --- Formatting actions ---
    void toggleBold();
    void toggleItalic();
    void toggleStrikethrough();
    void toggleInlineCode();
    void insertLink();
    void insertWikiLink();
    void insertImage();
    void insertCodeBlock();
    void insertBlockQuote();
    void insertHorizontalRule();
    void insertTable(int rows, int cols);
    /// Insert a table with an explicit header-row flag. When `hasHeader`
    /// is true the table is built with `rows` data rows plus a header
    /// row (matching the two-arg overload); when false the table is built
    /// with exactly `rows` rows and no separator. `rows` is always the
    /// count the caller asked for, not the count including a header.
    void insertTable(int rows, int cols, bool hasHeader);
    void increaseHeadingLevel();
    void decreaseHeadingLevel();
    /// Set the heading level of each selected block to exactly `level`
    /// (1–6). Pass 0 to strip heading markers. Blocks already at the
    /// target level are left unchanged.
    void setHeadingLevel(int level);
    /// Return the heading level (1–6) of the block under the cursor, or
    /// 0 if the block is not a heading. Uses the focused text item's
    /// current block — returns 0 when there is no focused item.
    int currentHeadingLevel() const;
    void toggleCheckbox();
    void insertCallout(const QString &type);
    /// Insert a callout with an explicit title. Empty title matches the
    /// single-arg overload.
    void insertCallout(const QString &type, const QString &title);

    /// True when the focused text item's cursor sits inside a GFM pipe table.
    /// Returns false when there is no focused editor or no cursor.
    bool cursorInTable() const;

    // --- Table operations (no-op if cursor not in a table) ---
    void tableInsertRowAbove();
    void tableInsertRowBelow();
    void tableInsertColumnLeft();
    void tableInsertColumnRight();
    void tableDeleteRow();
    void tableDeleteColumn();
    void tableSelectRow();
    void tableSelectColumn();

    // --- Cursor & navigation ---
    int cursorLine() const;
    int cursorColumn() const;
    QRect cursorScreenRect() const;
    void goToLine(int line);
    void scrollToHeading(const HeadingInfo &heading);

    // --- Font size (kept for test-app use) ---
    void setFontSize(int pointSize);

    // --- Scroll position (visual-line float) ---
    /// Current scroll position as a floating-point count of visual lines from
    /// the top of the scene. A "visual line" corresponds to a wrapped display
    /// line within a block item; block items that render as non-text shapes
    /// (tables, images, code blocks) contribute (height / lineSpacing)
    /// wrapped-line equivalents. Precision ±0.5 visual lines per the Cluster
    /// E Phase 2 contract — matches `Qutepart::scrollPositionVisualLine`'s
    /// surface so `NoteEditorWidget` has a single API across widgets.
    float scrollPositionVisualLine() const;
    /// Set the scroll position in visual-line float units. The viewport is
    /// positioned so the `floor(visualLine)`-th visual line of the scene
    /// lands at the viewport top, plus `frac(visualLine) * lineHeight`.
    void setScrollPositionVisualLine(float visualLine);

    // --- Search ---
    bool findText(const QString &text, QTextDocument::FindFlags flags = {});
    bool replaceText(const QString &find, const QString &replace,
                     QTextDocument::FindFlags flags = {});
    int replaceAll(const QString &find, const QString &replace,
                   QTextDocument::FindFlags flags = {});

    /// Show the embedded find bar (Ctrl+F).
    void showSearchBar();
    /// Show the embedded find+replace bar (Ctrl+H).
    void showReplaceBar();
    /// Hide the search bar and clear highlights.
    void hideSearchBar();

    // --- Folding ---
    QList<QStringList> headingPaths() const;
    bool isFolded(const QStringList &path) const;
    QList<QStringList> foldedPaths() const;

    void fold(const QStringList &path);
    void unfold(const QStringList &path);
    void toggleFold(const QStringList &path);
    void toggleFoldAtCursor();

    void foldAll();
    void unfoldAll();
    void foldAllAtLevel(int level);
    void unfoldAllAtLevel(int level);
    void foldLevel(int n);
    void unfoldLevel(int n);

    QJsonObject serializeFoldState() const;
    void restoreFoldState(const QJsonObject &state);

    void setGutterVisible(bool visible);
    bool isGutterVisible() const;

    /// Test-only accessor. Do not use from host code.
    SceneCoordinator *coordinatorForTesting() const { return m_coordinator; }

    // --- Link emission (Cluster J phase 3) ---
    /// Typed emission surface for wikilink / external-link activation and
    /// hover. Bridged internally from each `MarkdownTextItem`'s
    /// `TextControl::linkActivated` / `linkHovered` signals.
    LinkRenderer *linkRenderer() const;

    /// Set the note path passed as `fromPath` on every emitted
    /// FileLinkRequest. The host app typically updates this whenever the
    /// current note changes.
    void setCurrentNotePath(const QString &path);
    QString currentNotePath() const;

    /// Test-only — synthesize a link activation at the editor level,
    /// bypassing QWidget click machinery. Accepts either a raw URL or a
    /// `wikilink://target` synthetic href (as produced by
    /// MarkdownHighlighter for `[[target]]` spans). Do not use from
    /// host code; present unconditionally because the alternative
    /// QT_TESTLIB_LIB guard only fires when Qt6::Test is linked into
    /// the defining translation unit, which is false for libmarkoff.
    void testActivateLink(const QString &href);
    /// Test-only — synthesize a link hover. Pass an empty href to
    /// simulate "leave".
    void testHoverLink(const QString &href);

Q_SIGNALS:
    void textChanged();
    void cursorPositionChanged(int line, int column);
    void undoAvailable(bool available);
    void redoAvailable(bool available);
    void modificationChanged(bool modified);
    void linkClicked(const QString &target);
    void linkHovered(const QString &target);
    void wikiLinkTrigger(int cursorPosition);
    void tagTrigger(int cursorPosition);
    void completionDismissHint();
    void headingsChanged(const QList<Markoff::HeadingInfo> &headings);
    void linksChanged(const QList<Markoff::LinkInfo> &links);
    void tagsChanged(const QList<Markoff::TagInfo> &tags);
    void wordCountChanged(int count);
    void foldStateChanged();
    void foldsAutoExpanded(const QList<QStringList> &paths);
    void tableEntered(int rows, int cols);
    void tableExited();
    void tableCursorMoved(int row, int col);
    /// Fired when the scroll position changes by at least a fractional
    /// visual line. Connected to `verticalScrollBar::valueChanged` internally
    /// with a tiny dead-band so micro-pixel jitter from viewport updates
    /// doesn't spam the signal.
    void scrollPositionVisualLineChanged(float visualLine);

protected:
    bool event(QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createActions();
    bool handleTabKey(QKeyEvent *e);
    void rebuildScene();
    void applyEffectiveFont();
    void ensureFocusedCursorVisible();
    void startAutoScroll(int mouseY);
    void stopAutoScroll();
    void doAutoScroll();
    void jumpToDocumentEdge(bool toStart, bool select);
    void pageUpDown(bool up, bool select);
    MarkdownTextItem *focusedTextItem() const;
    void onDocumentReparsed();
    void detectCompletionTriggers(const QString &insertedText);
    void wrapSelection(const QString &before, const QString &after);
    void insertAtCursor(const QString &text);
    void highlightAllMatches(const QString &text);
    void clearSearchHighlights();
    void updateMatchCount();
    void repositionSearchBar();
    void repositionFoldGutter();
    QTextDocument::FindFlags searchFlags() const;
    void handleMouseMoveOnViewport(QMouseEvent *e);
    void handleMouseReleaseOnViewport(QMouseEvent *e);
    void handleWheelOnViewport(QWheelEvent *e);

    QGraphicsView *m_view = nullptr;
    QHash<ActionId, QAction*> m_actions;

    SelectionScene *m_scene = nullptr;
    SceneCoordinator *m_coordinator = nullptr;
    QString m_sourceText;
    int m_fontSize = 14;
    int m_defaultFontSize = 14;
    QTimer *m_autoScrollTimer = nullptr;
    int m_autoScrollDelta = 0;
    bool m_autoScrollActive = false;
    bool m_readOnly = false;
    bool m_inTable = false;

    Theme m_theme;
    ResourceProvider *m_resourceProvider = nullptr;
    std::unique_ptr<Document> m_document;

    SearchBar *m_searchBar = nullptr;
    QString m_lastSearchText;
    int m_currentMatchIndex = -1;
    int m_totalMatchCount = 0;

    FoldingModel *m_foldingModel = nullptr;
    FoldGutter *m_foldGutter = nullptr;
    bool m_gutterVisible = true;

    // Cluster J phase 3 — typed link emission surface
    LinkRenderer *m_linkRenderer = nullptr;
    QString m_currentNotePath;
    void handleLinkActivated(const QString &href);
    void handleLinkHovered(const QString &href);
    void subscribeLinkSignalsForItems();
    void onCursorMoved();

    // MarkdownView bridge
    MarkoffDocument *m_markoffDoc = nullptr;
    std::unique_ptr<LiveSearchAdapter> m_searchAdapter;
};

} // namespace Markoff

Q_DECLARE_METATYPE(QList<QStringList>)

#endif // MARKOFF_EDITOR_H
