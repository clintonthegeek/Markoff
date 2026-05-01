// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtPlugin>

Q_IMPORT_PLUGIN(org_markoff_view_qmlPlugin)

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QShortcut>
#include <QVariant>

#include <memory>

#include <markoff-foundation/CodeBlockProcessorRegistry.h>
#include <markoff-foundation/CompletionRegistry.h>
#include <markoff-foundation/DefaultLinkService.h>
#include <markoff-foundation/EmojiCompletionProvider.h>
#include <markoff-foundation/Kf6SyntaxHighlightService.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/Theme.h>

#include <markoff/view/qml/CompletionPopupModel.h>

// ---------------------------------------------------------------------------
// CloseGuard — event filter that intercepts QEvent::Close on the root window
// and prompts to save if the document is dirty.
// ---------------------------------------------------------------------------
class CloseGuard : public QObject
{
    Q_OBJECT
public:
    CloseGuard(QQuickWindow *window,
               Markoff::MarkoffDocument *doc,
               const QString &filePath,
               bool *dirtyFlag,
               quint64 *lastSavedSeq,
               QObject *parent = nullptr)
        : QObject(parent)
        , m_window(window)
        , m_doc(doc)
        , m_filePath(filePath)
        , m_dirty(dirtyFlag)
        , m_lastSavedSeq(lastSavedSeq)
    {}

    bool save()
    {
        QFile out(m_filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::critical(nullptr,
                                  tr("Save failed"),
                                  tr("Could not write to %1: %2")
                                      .arg(m_filePath, out.errorString()));
            return false;
        }
        out.write(m_doc->toMarkdownUtf8());
        out.close();
        *m_lastSavedSeq = m_doc->editSequence();
        *m_dirty = false;
        return true;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_window && event->type() == QEvent::Close && *m_dirty) {
            event->ignore();
            QMessageBox mb;
            mb.setWindowTitle(tr("Unsaved changes"));
            mb.setText(tr("The document has unsaved changes."));
            mb.setIcon(QMessageBox::Warning);
            QPushButton *saveBtn    = mb.addButton(tr("Save"),    QMessageBox::AcceptRole);
            QPushButton *discardBtn = mb.addButton(tr("Discard"), QMessageBox::DestructiveRole);
            mb.addButton(tr("Cancel"), QMessageBox::RejectRole);
            mb.setDefaultButton(saveBtn);
            mb.exec();

            if (mb.clickedButton() == static_cast<QAbstractButton *>(saveBtn)) {
                if (save())
                    m_window->close(); // retry close — dirty is now false
            } else if (mb.clickedButton() == static_cast<QAbstractButton *>(discardBtn)) {
                *m_dirty = false;
                m_window->close(); // retry close — dirty is now false
            }
            // Cancel: leave the event ignored, window stays open
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QQuickWindow             *m_window;
    Markoff::MarkoffDocument *m_doc;
    QString                   m_filePath;
    bool                     *m_dirty;
    quint64                  *m_lastSavedSeq;
};

#include "main.moc"

int main(int argc, char *argv[])
{
    // Pin the Qt Quick Controls style. Bypasses Plasma's Breeze override
    // (which has a known TextArea.qml warning that's noisy but harmless)
    // and gives us predictable cross-platform behavior. Must be called
    // BEFORE QApplication construction.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QApplication app(argc, argv);

    const QStringList cliArgs = QCoreApplication::arguments();
    const bool startInLiveMode = cliArgs.contains(QStringLiteral("--live"));

    // Find the first non-flag argument as the file path.
    QString filePath;
    for (int i = 1; i < cliArgs.size(); ++i) {
        const QString &a = cliArgs.at(i);
        if (a.startsWith(QStringLiteral("--"))) continue;
        filePath = a;
        break;
    }
    if (filePath.isEmpty()) {
        qWarning("Usage: %s [--live] <markdown-file>", argv[0]);
        return 1;
    }

    // Read the file.
    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly)) {
        qWarning("Failed to open %s: %s",
                 qUtf8Printable(filePath), qUtf8Printable(in.errorString()));
        return 1;
    }
    const QByteArray content = in.readAll();
    in.close();

    // Build foundation services (heap-owned via unique_ptr; held alive for app lifetime).
    auto syntax         = std::make_unique<Markoff::Kf6SyntaxHighlightService>();
    auto codeProcessors = std::make_unique<Markoff::CodeBlockProcessorRegistry>();
    auto links          = std::make_unique<Markoff::DefaultLinkService>();
    auto completion     = std::make_unique<Markoff::CompletionRegistry>();
    completion->registerProvider(std::make_shared<Markoff::EmojiCompletionProvider>());

    // Document (random replica id for first-open).
    const quint16 replicaId =
        static_cast<quint16>(QRandomGenerator::global()->generate() & 0xFFFF);
    auto doc = std::make_unique<Markoff::MarkoffDocument>(replicaId);
    doc->resetContent(content, Markoff::Origin::FirstOpen);

    // T14.1 — dirty tracking state.
    quint64 lastSavedSequence = doc->editSequence();
    bool    dirty             = false;

    // CompletionPopupModel (heap-owned; QML uses it via context property).
    auto popupModel = std::make_unique<Markoff::View::Qml::CompletionPopupModel>();
    popupModel->registerProvider(std::make_shared<Markoff::EmojiCompletionProvider>());

    const QString baseName = QFileInfo(filePath).fileName();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("ctxDocument", doc.get());
    engine.rootContext()->setContextProperty(
        "ctxTheme", QVariant::fromValue(Markoff::Theme::defaultLight()));
    engine.rootContext()->setContextProperty("ctxCompletionModel", popupModel.get());
    engine.rootContext()->setContextProperty(QStringLiteral("startInLiveMode"),
                                             QVariant(startInLiveMode));
    // T14.2 — expose dirty flag before QML loads.
    engine.rootContext()->setContextProperty(QStringLiteral("ctxDirty"),
                                             QVariant(dirty));

    engine.loadFromModule("org.markoff.view.qml.app", "Main");
    if (engine.rootObjects().isEmpty()) return 2;

    // Grab the root QQuickWindow.
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) {
        qWarning("Root object is not a QQuickWindow");
        return 3;
    }

    // Set initial window title.
    window->setTitle(QStringLiteral("%1 — Markoff").arg(baseName));

    // Helper: recompute dirty and push updates to QML + window title.
    auto updateDirty = [&]() {
        const bool nowDirty = (doc->editSequence() != lastSavedSequence);
        if (nowDirty == dirty) return;
        dirty = nowDirty;
        engine.rootContext()->setContextProperty(QStringLiteral("ctxDirty"),
                                                 QVariant(dirty));
        if (dirty)
            window->setTitle(QStringLiteral("%1 [modified] — Markoff").arg(baseName));
        else
            window->setTitle(QStringLiteral("%1 — Markoff").arg(baseName));
    };

    // T14.1 — connect contentsChanged to the dirty check.
    QObject::connect(doc.get(), &Markoff::MarkoffDocument::contentsChanged,
                     window, [&](const QList<Markoff::MarkoffEdit> &) {
                         updateDirty();
                     });

    // T14.3 — Ctrl+S save shortcut.
    auto *saveShortcut = new QShortcut(QKeySequence::Save, window);
    QObject::connect(saveShortcut, &QShortcut::activated, window, [&]() {
        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning("Save failed: %s", qUtf8Printable(out.errorString()));
            return;
        }
        out.write(doc->toMarkdownUtf8());
        out.close();
        lastSavedSequence = doc->editSequence();
        updateDirty();
    });

    // T14.4 — close-prompt event filter.
    auto *guard = new CloseGuard(window, doc.get(), filePath, &dirty,
                                 &lastSavedSequence, window);
    window->installEventFilter(guard);

    return app.exec();
}
