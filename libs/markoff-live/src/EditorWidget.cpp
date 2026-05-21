// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#include <markoff/live/EditorWidget.h>

#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>

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
        // Force initial model population: setDocument only connects to
        // documentLoaded/d2DocumentChanged signals, but loadFromMarkdown
        // typically ran BEFORE this widget was constructed (the host
        // populates the document then hands it over). Without this nudge
        // the LiveBlockModel stays empty until the next user edit.
        doc->flushPendingD2Changed();
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
