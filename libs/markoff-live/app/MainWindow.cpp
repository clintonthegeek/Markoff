// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"
#include "markoff/Editor.h"
#include "markoff/ResourceProvider.h"
#include "markoff/Theme.h"
#include <markoff-parser/Document.h>

#include <QAction>
#include <QDockWidget>
#include <QIcon>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextStream>
#include <QToolBar>
#include <QTreeWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    resize(1000, 750);

    m_editor = new Markoff::Editor(this);
    setCentralWidget(m_editor);

    // --- Menu bar ---
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::onOpen);
    fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::onSave);

    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_editor->action(Markoff::ActionId::Undo));
    editMenu->addAction(m_editor->action(Markoff::ActionId::Redo));
    editMenu->addSeparator();
    editMenu->addAction(m_editor->action(Markoff::ActionId::Cut));
    editMenu->addAction(m_editor->action(Markoff::ActionId::Copy));
    editMenu->addAction(m_editor->action(Markoff::ActionId::Paste));
    editMenu->addAction(m_editor->action(Markoff::ActionId::SelectAll));

    auto *formatMenu = menuBar()->addMenu(tr("F&ormat"));
    formatMenu->addAction(m_editor->action(Markoff::ActionId::ToggleBold));
    formatMenu->addAction(m_editor->action(Markoff::ActionId::ToggleItalic));
    formatMenu->addAction(m_editor->action(Markoff::ActionId::ToggleStrikethrough));
    formatMenu->addAction(m_editor->action(Markoff::ActionId::ToggleInlineCode));
    formatMenu->addSeparator();
    formatMenu->addAction(m_editor->action(Markoff::ActionId::IncreaseHeading));
    formatMenu->addAction(m_editor->action(Markoff::ActionId::DecreaseHeading));
    formatMenu->addSeparator();
    formatMenu->addAction(m_editor->action(Markoff::ActionId::InsertLink));
    formatMenu->addAction(m_editor->action(Markoff::ActionId::InsertWikiLink));
    formatMenu->addAction(m_editor->action(Markoff::ActionId::InsertImage));
    formatMenu->addSeparator();
    formatMenu->addAction(m_editor->action(Markoff::ActionId::InsertCodeBlock));
    formatMenu->addAction(m_editor->action(Markoff::ActionId::InsertBlockQuote));
    formatMenu->addAction(m_editor->action(Markoff::ActionId::InsertHorizontalRule));
    formatMenu->addAction(m_editor->action(Markoff::ActionId::ToggleCheckbox));
    formatMenu->addSeparator();
    formatMenu->addAction(m_editor->action(Markoff::ActionId::InsertTable));

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    m_readOnlyAction = viewMenu->addAction(tr("&Read Only"));
    m_readOnlyAction->setCheckable(true);
    m_readOnlyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(m_readOnlyAction, &QAction::toggled, this, &MainWindow::onToggleReadOnly);

    m_themeAction = viewMenu->addAction(tr("&Dark Theme"));
    m_themeAction->setCheckable(true);
    m_themeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(m_themeAction, &QAction::toggled, this, &MainWindow::onToggleTheme);

    viewMenu->addSeparator();
    m_sidebarAction = viewMenu->addAction(tr("&Document Info"));
    m_sidebarAction->setCheckable(true);
    m_sidebarAction->setChecked(true);
    m_sidebarAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(m_sidebarAction, &QAction::toggled, this, &MainWindow::onToggleSidebar);

    // --- Main toolbar: all registered editor actions ---
    auto *editBar = addToolBar(tr("Edit"));
    editBar->setMovable(false);
    editBar->addAction(m_editor->action(Markoff::ActionId::Undo));
    editBar->addAction(m_editor->action(Markoff::ActionId::Redo));
    editBar->addSeparator();
    editBar->addAction(m_editor->action(Markoff::ActionId::Cut));
    editBar->addAction(m_editor->action(Markoff::ActionId::Copy));
    editBar->addAction(m_editor->action(Markoff::ActionId::Paste));
    editBar->addSeparator();
    editBar->addAction(m_editor->action(Markoff::ActionId::Find));
    editBar->addAction(m_editor->action(Markoff::ActionId::Replace));
    editBar->addSeparator();
    editBar->addAction(m_editor->action(Markoff::ActionId::ZoomIn));
    editBar->addAction(m_editor->action(Markoff::ActionId::ZoomOut));

    auto *fmtBar = addToolBar(tr("Format"));
    fmtBar->setMovable(false);
    fmtBar->addAction(m_editor->action(Markoff::ActionId::ToggleBold));
    fmtBar->addAction(m_editor->action(Markoff::ActionId::ToggleItalic));
    fmtBar->addAction(m_editor->action(Markoff::ActionId::ToggleStrikethrough));
    fmtBar->addAction(m_editor->action(Markoff::ActionId::ToggleInlineCode));
    fmtBar->addSeparator();
    fmtBar->addAction(m_editor->action(Markoff::ActionId::IncreaseHeading));
    fmtBar->addAction(m_editor->action(Markoff::ActionId::DecreaseHeading));
    fmtBar->addSeparator();
    fmtBar->addAction(m_editor->action(Markoff::ActionId::InsertLink));
    fmtBar->addAction(m_editor->action(Markoff::ActionId::InsertWikiLink));
    fmtBar->addAction(m_editor->action(Markoff::ActionId::InsertImage));
    fmtBar->addSeparator();
    fmtBar->addAction(m_editor->action(Markoff::ActionId::InsertCodeBlock));
    fmtBar->addAction(m_editor->action(Markoff::ActionId::InsertBlockQuote));
    fmtBar->addAction(m_editor->action(Markoff::ActionId::InsertHorizontalRule));
    fmtBar->addAction(m_editor->action(Markoff::ActionId::ToggleCheckbox));
    fmtBar->addSeparator();
    fmtBar->addAction(m_editor->action(Markoff::ActionId::InsertTable));

    auto *viewBar = addToolBar(tr("View"));
    viewBar->setMovable(false);
    viewBar->addAction(m_editor->action(Markoff::ActionId::ToggleFoldAtCursor));
    viewBar->addAction(m_editor->action(Markoff::ActionId::FoldAll));
    viewBar->addAction(m_editor->action(Markoff::ActionId::UnfoldAll));
    viewBar->addSeparator();

    auto *fontLabel = new QLabel(tr(" Font: "), viewBar);
    viewBar->addWidget(fontLabel);
    auto *fontSpin = new QSpinBox(viewBar);
    fontSpin->setRange(6, 48);
    fontSpin->setValue(14);
    viewBar->addWidget(fontSpin);
    connect(fontSpin, &QSpinBox::valueChanged, m_editor, &Markoff::Editor::setFontSize);

    // --- Context toolbar: shown/hidden dynamically ---
    m_contextToolbar = addToolBar(tr("Context"));
    m_contextToolbar->setMovable(false);
    m_contextToolbar->hide();

    connect(m_editor, &Markoff::Editor::tableEntered, this, &MainWindow::onTableEntered);
    connect(m_editor, &Markoff::Editor::tableExited, this, &MainWindow::onTableExited);

    // --- Sidebar: document metadata ---
    m_metadataTree = new QTreeWidget;
    m_metadataTree->setHeaderHidden(true);
    m_metadataTree->setRootIsDecorated(true);
    m_metadataTree->setMinimumWidth(200);

    m_sidebarDock = new QDockWidget(tr("Document Info"), this);
    m_sidebarDock->setWidget(m_metadataTree);
    m_sidebarDock->setFeatures(QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, m_sidebarDock);

    connect(m_sidebarDock, &QDockWidget::visibilityChanged,
            m_sidebarAction, &QAction::setChecked);

    // --- Status bar ---
    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel);

    // --- Signals ---
    m_editor->setFontSize(14);

    connect(m_editor, &Markoff::Editor::textChanged, this, &MainWindow::updateStatusBar);
    connect(m_editor, &Markoff::Editor::cursorPositionChanged,
            this, [this](int line, int col) {
        Q_UNUSED(line) Q_UNUSED(col)
        updateStatusBar();
    });
    connect(m_editor, &Markoff::Editor::headingsChanged, this, [this]() { updateMetadata(); });
    connect(m_editor, &Markoff::Editor::linksChanged, this, [this]() { updateMetadata(); });
    connect(m_editor, &Markoff::Editor::tagsChanged, this, [this]() { updateMetadata(); });
    connect(m_editor, &Markoff::Editor::wordCountChanged, this, [this]() { updateStatusBar(); });

    connect(m_editor, &Markoff::Editor::linkClicked, this, [](const QString &target) {
        qDebug("Link clicked: %s", qPrintable(target));
    });

    updateTitle();
}

MainWindow::~MainWindow()
{
    delete m_resourceProvider;
}

void MainWindow::openFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    // Set up resource provider for the file's directory so images resolve.
    const QString dir = QFileInfo(path).absolutePath();
    delete m_resourceProvider;
    m_resourceProvider = new Markoff::FilesystemResourceProvider(dir);
    m_editor->setResourceProvider(m_resourceProvider);

    QTextStream stream(&file);
    m_editor->setPlainText(stream.readAll());
    m_filePath = path;
    updateTitle();
    updateMetadata();
}

void MainWindow::onOpen()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Markdown File"), QString(),
        tr("Markdown Files (*.md *.markdown);;All Files (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::onSave()
{
    if (m_filePath.isEmpty()) {
        m_filePath = QFileDialog::getSaveFileName(
            this, tr("Save Markdown File"), QString(),
            tr("Markdown Files (*.md *.markdown);;All Files (*)"));
        if (m_filePath.isEmpty())
            return;
    }
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream stream(&file);
    stream << m_editor->toPlainText();
    updateTitle();
}

void MainWindow::onToggleReadOnly()
{
    m_editor->setReadOnly(m_readOnlyAction->isChecked());
}

void MainWindow::onToggleTheme()
{
    m_darkTheme = m_themeAction->isChecked();
    m_editor->setTheme(m_darkTheme ? Markoff::Theme::defaultDark()
                                   : Markoff::Theme::defaultLight());
}

void MainWindow::onToggleSidebar()
{
    m_sidebarDock->setVisible(m_sidebarAction->isChecked());
}

void MainWindow::updateStatusBar()
{
    int line = m_editor->cursorLine();
    int col = m_editor->cursorColumn();

    const auto *doc = m_editor->parsedDocument();
    int words = doc ? doc->wordCount() : 0;

    const QString text = m_editor->toPlainText();
    int lines = text.count(QLatin1Char('\n')) + (text.isEmpty() ? 0 : 1);

    QString status = QStringLiteral("Ln %1, Col %2  |  %3 lines, %4 words, %5 chars")
        .arg(line).arg(col).arg(lines).arg(words).arg(text.size());

    if (m_editor->isReadOnly())
        status += QStringLiteral("  |  READ ONLY");

    m_statusLabel->setText(status);
}

void MainWindow::updateMetadata()
{
    m_metadataTree->clear();
    const auto *doc = m_editor->parsedDocument();
    if (!doc) return;

    // Headings
    auto headings = doc->headings();
    if (!headings.isEmpty()) {
        auto *headingRoot = new QTreeWidgetItem(m_metadataTree,
            {QStringLiteral("Headings (%1)").arg(headings.size())});
        for (const auto &h : headings) {
            QString prefix = QString(h.level, QLatin1Char('#'));
            new QTreeWidgetItem(headingRoot,
                {QStringLiteral("%1 %2").arg(prefix, h.text)});
        }
        headingRoot->setExpanded(true);
    }

    // Links
    auto links = doc->links();
    if (!links.isEmpty()) {
        auto *linkRoot = new QTreeWidgetItem(m_metadataTree,
            {QStringLiteral("Links (%1)").arg(links.size())});
        for (const auto &l : links) {
            QString typeStr;
            switch (l.type) {
            case Markoff::LinkInfo::Standard: typeStr = QStringLiteral("link"); break;
            case Markoff::LinkInfo::Wiki: typeStr = QStringLiteral("wiki"); break;
            case Markoff::LinkInfo::Image: typeStr = QStringLiteral("image"); break;
            case Markoff::LinkInfo::Embed: typeStr = QStringLiteral("embed"); break;
            }
            new QTreeWidgetItem(linkRoot,
                {QStringLiteral("[%1] %2").arg(typeStr, l.target)});
        }
        linkRoot->setExpanded(true);
    }

    // Tags
    auto tags = doc->tags();
    if (!tags.isEmpty()) {
        auto *tagRoot = new QTreeWidgetItem(m_metadataTree,
            {QStringLiteral("Tags (%1)").arg(tags.size())});
        for (const auto &t : tags) {
            new QTreeWidgetItem(tagRoot,
                {QStringLiteral("#%1").arg(t.name)});
        }
        tagRoot->setExpanded(true);
    }

    // Footnotes
    auto footnotes = doc->footnotes();
    if (!footnotes.isEmpty()) {
        auto *fnRoot = new QTreeWidgetItem(m_metadataTree,
            {QStringLiteral("Footnotes (%1)").arg(footnotes.size())});
        for (const auto &fn : footnotes) {
            new QTreeWidgetItem(fnRoot,
                {QStringLiteral("[^%1] %2").arg(fn.label, fn.content)});
        }
        fnRoot->setExpanded(true);
    }

    // Frontmatter
    QString fm = doc->frontmatter();
    if (!fm.isEmpty()) {
        auto *fmRoot = new QTreeWidgetItem(m_metadataTree,
            {QStringLiteral("Frontmatter")});
        for (const auto &line : fm.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            new QTreeWidgetItem(fmRoot, {line.trimmed()});
        }
        fmRoot->setExpanded(true);
    }
}

void MainWindow::onTableEntered(int rows, int cols)
{
    m_contextToolbar->clear();
    auto *label = m_contextToolbar->addAction(
        QIcon::fromTheme(QStringLiteral("table")),
        tr("Table (%1x%2)").arg(rows).arg(cols), this, []() {});
    label->setEnabled(false);
    m_contextToolbar->addSeparator();
    m_contextToolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-table-insert-row-above")),
        tr("Row Above"), m_editor, &Markoff::Editor::tableInsertRowAbove);
    m_contextToolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-table-insert-row-below")),
        tr("Row Below"), m_editor, &Markoff::Editor::tableInsertRowBelow);
    m_contextToolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-table-insert-column-left")),
        tr("Col Left"), m_editor, &Markoff::Editor::tableInsertColumnLeft);
    m_contextToolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-table-insert-column-right")),
        tr("Col Right"), m_editor, &Markoff::Editor::tableInsertColumnRight);
    m_contextToolbar->addSeparator();
    m_contextToolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-table-delete-row")),
        tr("Del Row"), m_editor, &Markoff::Editor::tableDeleteRow);
    m_contextToolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-table-delete-column")),
        tr("Del Col"), m_editor, &Markoff::Editor::tableDeleteColumn);
    m_contextToolbar->addSeparator();
    m_contextToolbar->addAction(QIcon::fromTheme(QStringLiteral("select-row")),
        tr("Sel Row"), m_editor, &Markoff::Editor::tableSelectRow);
    m_contextToolbar->addAction(QIcon::fromTheme(QStringLiteral("select-column")),
        tr("Sel Col"), m_editor, &Markoff::Editor::tableSelectColumn);
    m_contextToolbar->show();
}

void MainWindow::onTableExited()
{
    m_contextToolbar->hide();
    m_contextToolbar->clear();
}

void MainWindow::updateTitle()
{
    const QString name = m_filePath.isEmpty()
        ? QStringLiteral("[untitled]")
        : QFileInfo(m_filePath).fileName();
    setWindowTitle(QStringLiteral("Markoff \u2014 ") + name);
}
