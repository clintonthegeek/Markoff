// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#include <markoff/live/EditorWidget.h>

#include <QDebug>
#include <QLoggingCategory>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>

Q_LOGGING_CATEGORY(markoffLiveEditorWidget, "markoff.live.editorwidget")

namespace Markoff::Live {

struct EditorWidget::Private {
    LiveListModelBinding   *binding     = nullptr;
    QQuickWidget           *quickWidget = nullptr;
    QPointer<Session>       session;        // owned by the document
};

EditorWidget::EditorWidget(LiveListModelBinding::Capabilities caps,
                           QWidget *parent)
    : Markoff::MarkdownView(parent), d(std::make_unique<Private>())
{
    d->binding     = new LiveListModelBinding(caps, this);
    d->quickWidget = new QQuickWidget(this);
    d->quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    d->quickWidget->rootContext()->setContextProperty(
        QStringLiteral("modelBinding"), d->binding);
    d->quickWidget->setSource(QUrl(QStringLiteral(
        "qrc:/qt/qml/org/markoff/live/qml/EditorContent.qml")));

    qCInfo(markoffLiveEditorWidget)
        << "EditorWidget ctor: caps=" << int(caps)
        << "binding=" << d->binding
        << "qquickWidget=" << d->quickWidget
        << "setSource status=" << d->quickWidget->status();
    for (const auto &err : d->quickWidget->errors()) {
        qCWarning(markoffLiveEditorWidget) << "QML error:" << err.toString();
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(d->quickWidget);
}

EditorWidget::~EditorWidget()
{
    // Session owned by document — when EditorWidget outlives its document,
    // session is already null via QPointer. When document outlives the
    // widget (typical), explicitly destroy the session we created so the
    // document doesn't accumulate ghost sessions across leaf swaps.
    if (d->session && document()) {
        document()->destroySession(d->session);
    }
}

void EditorWidget::setDocument(Markoff::MarkoffDocument *doc)
{
    qCInfo(markoffLiveEditorWidget)
        << "setDocument doc=" << doc
        << " prev=" << document()
        << " visibleLength=" << (doc ? doc->visibleLength() : 0u);

    if (document() == doc) return;

    // Tear down old session.
    if (d->session && document()) {
        document()->destroySession(d->session);
    }
    d->session = nullptr;

    Markoff::MarkdownView::setDocument(doc);
    d->binding->setDocument(doc);

    if (doc) {
        d->session = doc->createSession();
        d->binding->setSession(d->session);
        qCInfo(markoffLiveEditorWidget)
            << "  → binding has doc, session=" << d->session;
    }
}

LiveListModelBinding *EditorWidget::binding() const noexcept
{
    return d->binding;
}

void EditorWidget::attachFindController(Markoff::FindController *fc)
{
    d->binding->attachFindController(fc);
}

void EditorWidget::detachFindController()
{
    d->binding->detachFindController();
}

}  // namespace Markoff::Live
