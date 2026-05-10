// SPDX-License-Identifier: GPL-3.0-or-later
#include "QmlIntegrationFixture.h"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>

#include "MainController.h"  // from markoff-live-app-internal STATIC lib

namespace Markoff::Live::Test {

QmlIntegrationFixture::QmlIntegrationFixture(const QByteArray &markdown,
                                             int expectedRowCount)
{
    m_replicaId =
        static_cast<quint16>(QRandomGenerator::global()->generate() & 0xFFFF);

    m_tmpFile = std::make_unique<QTemporaryFile>();
    QVERIFY2(m_tmpFile->open(), "QTemporaryFile open failed");

    m_doc = std::make_unique<Markoff::MarkoffDocument>(m_replicaId);
    m_doc->loadFromMarkdown(markdown);
    m_doc->markSaved(m_doc->d2EditSequence());

    m_session = m_doc->createSession();

    m_mainController = std::make_unique<MainController>(
        m_doc.get(), m_tmpFile->fileName());

    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->rootContext()->setContextProperty(
        QStringLiteral("ctxDocument"), m_doc.get());
    m_engine->rootContext()->setContextProperty(
        QStringLiteral("ctxSession"), m_session);
    m_engine->rootContext()->setContextProperty(
        QStringLiteral("ctxMain"), m_mainController.get());

    m_engine->loadFromModule("org.markoff.live.app", "Main");
    QVERIFY2(!m_engine->rootObjects().isEmpty(),
             "loadFromModule produced no root object — check qml module URI");

    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    QVERIFY2(m_window != nullptr, "root object is not a QQuickWindow");

    QVERIFY2(QTest::qWaitForWindowExposed(m_window, 5000),
             "window did not expose within 5s under offscreen QPA");

    // Walk children looking for LiveListModelBinding: has fontScale, model,
    // and document properties.
    for (QObject *child : m_window->findChildren<QObject *>()) {
        const QMetaObject *mo = child->metaObject();
        if (mo->indexOfProperty("fontScale") != -1
            && mo->indexOfProperty("model") != -1
            && mo->indexOfProperty("document") != -1) {
            m_binding = child;
            break;
        }
    }
    QVERIFY2(m_binding != nullptr, "LiveListModelBinding not found in QML tree");

    m_model = qobject_cast<QAbstractItemModel *>(
        m_binding->property("model").value<QObject *>());
    QVERIFY2(m_model != nullptr, "binding.model is not a QAbstractItemModel");

    // Wait for the expected row count (load may have parsed async).
    if (m_model->rowCount() != expectedRowCount) {
        QSignalSpy spy(m_model, &QAbstractItemModel::rowsInserted);
        const int deadline = 2000;
        QElapsedTimer t; t.start();
        while (m_model->rowCount() != expectedRowCount && t.elapsed() < deadline) {
            spy.wait(100);
            QCoreApplication::processEvents();
        }
    }
    QCOMPARE(m_model->rowCount(), expectedRowCount);

    m_harness = std::make_unique<LiveRealisticInputHarness>(m_window);
}

QmlIntegrationFixture::~QmlIntegrationFixture() = default;

QObject *QmlIntegrationFixture::binding()           { return m_binding; }
QAbstractItemModel *QmlIntegrationFixture::model()  { return m_model; }

} // namespace Markoff::Live::Test
