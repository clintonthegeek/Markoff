// SPDX-License-Identifier: GPL-3.0-or-later
#include "QmlIntegrationFixture.h"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
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

// Non-failing variant used by wait helpers that poll before the ListView
// has necessarily been realised. Returns nullptr without asserting.
//
// LiveView.qml's root IS a ListView, but Qt 6.x QML compilation wraps it in
// a generated type (e.g. "LiveView_QMLTYPE_3") so qstrcmp against
// "QQuickListView" fails. Instead we walk the superclass chain via
// metaObject()->inherits(), which traverses into the C++ base classes.
// This identifies the LiveView item regardless of the generated type name.
static QQuickItem *findListViewInWindow(QQuickWindow *window) {
    for (QObject *child : window->findChildren<QObject *>()) {
        auto *item = qobject_cast<QQuickItem *>(child);
        if (!item) continue;
        // Walk superclass chain: LiveView.qml is a subtype of QQuickListView
        // but Qt 6.x QML compilation gives it a generated class name like
        // "LiveView_QMLTYPE_N", so qstrcmp against "QQuickListView" misses it.
        const QMetaObject *mo = item->metaObject();
        while (mo) {
            if (qstrcmp(mo->className(), "QQuickListView") == 0)
                return item;
            mo = mo->superClass();
        }
    }
    return nullptr;
}

QQuickItem *QmlIntegrationFixture::listView() {
    if (m_listView)
        return m_listView;
    m_listView = findListViewInWindow(m_window);
    if (!m_listView) {
        QTest::qFail("QQuickListView not found in window", __FILE__, __LINE__);
        return nullptr;
    }
    return m_listView;
}

// Walk the ListView's contentItem children looking for the delegate
// whose "modelIndex" Q_PROPERTY matches `row`. This avoids itemAtIndex
// which requires the item to be inside the visible viewport geometry —
// under the offscreen QPA that check can fail even for realised items.
// The delegates (ParagraphDelegate etc.) expose `property int modelIndex`
// so it is accessible as a QObject property.
static QQuickItem *findDelegateByRow(QQuickItem *lv, int row) {
    QVariant contentItemVar = lv->property("contentItem");
    QQuickItem *contentItem = contentItemVar.value<QQuickItem *>();
    if (!contentItem) return nullptr;
    for (QQuickItem *child : contentItem->childItems()) {
        QVariant indexProp = child->property("modelIndex");
        if (indexProp.isValid() && indexProp.toInt() == row)
            return child;
    }
    return nullptr;
}

QQuickItem *QmlIntegrationFixture::delegateAt(int row) {
    QQuickItem *lv = listView();
    if (!lv) return nullptr;
    return findDelegateByRow(lv, row);
}

// Returns true if `item` is a QQuickTextEdit (or a QML subtype of it).
// Qt 6.x QML compilation wraps QML components in generated types like
// "QQuickTextEdit_QML_N", so we must walk the superclass chain.
static bool isTextEditItem(QQuickItem *item) {
    const QMetaObject *mo = item->metaObject();
    while (mo) {
        if (qstrcmp(mo->className(), "QQuickTextEdit") == 0)
            return true;
        mo = mo->superClass();
    }
    return false;
}

// Recursive descent: find the first QQuickTextEdit-typed descendant.
static QQuickItem *findTextEditDescendant(QQuickItem *root) {
    if (!root) return nullptr;
    if (isTextEditItem(root))
        return root;
    for (QQuickItem *child : root->childItems()) {
        if (auto *found = findTextEditDescendant(child))
            return found;
    }
    return nullptr;
}

QQuickItem *QmlIntegrationFixture::delegateTextEdit(int row) {
    QQuickItem *d = delegateAt(row);
    return d ? findTextEditDescendant(d) : nullptr;
}

QByteArray QmlIntegrationFixture::bufferText(Markoff::BlockId id) {
    return m_doc->blockText(id);
}

QString QmlIntegrationFixture::modelText(int row) {
    // Look up by role name to avoid hardcoded numeric drift.
    const auto roles = m_model->roleNames();
    int textRole = -1;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == QByteArray("text")) {
            textRole = it.key();
            break;
        }
    }
    if (textRole == -1) {
        QTest::qFail("\"text\" role not found in model's roleNames()", __FILE__, __LINE__);
        return {};
    }
    return m_model->data(m_model->index(row, 0), textRole).toString();
}

QString QmlIntegrationFixture::delegateText(int row) {
    QQuickItem *te = delegateTextEdit(row);
    return te ? te->property("text").toString() : QString();
}

int QmlIntegrationFixture::delegateCursorPos(int row) {
    QQuickItem *te = delegateTextEdit(row);
    return te ? te->property("cursorPosition").toInt() : -1;
}

bool QmlIntegrationFixture::waitForRowCount(int expected, int timeoutMs) {
    if (m_model->rowCount() == expected)
        return true;
    QSignalSpy insSpy(m_model, &QAbstractItemModel::rowsInserted);
    QSignalSpy rmSpy(m_model, &QAbstractItemModel::rowsRemoved);
    QElapsedTimer t; t.start();
    while (m_model->rowCount() != expected && t.elapsed() < timeoutMs) {
        insSpy.wait(100);
        rmSpy.wait(50);
        QCoreApplication::processEvents();
    }
    return m_model->rowCount() == expected;
}

bool QmlIntegrationFixture::waitForDelegateAt(int row, int timeoutMs) {
    // Use the non-failing listView lookup so polling before the ListView
    // is realised does not cascade QTest::qFail calls.
    // Walk contentItem children directly rather than itemAtIndex — the latter
    // requires viewport visibility which may not hold under offscreen QPA.
    auto itemAt = [&]() -> QQuickItem * {
        QQuickItem *lv = m_listView ? m_listView : findListViewInWindow(m_window);
        if (!lv) return nullptr;
        if (!m_listView) m_listView = lv; // cache once found
        return findDelegateByRow(lv, row);
    };
    QElapsedTimer t; t.start();
    while (t.elapsed() < timeoutMs) {
        if (itemAt() != nullptr)
            return true;
        QTest::qWait(25);
        QCoreApplication::processEvents();
    }
    return itemAt() != nullptr;
}

QQuickItem *QmlIntegrationFixture::focusedDelegate() {
    for (int row = 0; row < m_model->rowCount(); ++row) {
        QQuickItem *d = delegateAt(row);
        if (!d) continue;
        if (d->hasActiveFocus())
            return d;
        QQuickItem *te = findTextEditDescendant(d);
        if (te && te->hasActiveFocus())
            return d;
    }
    return nullptr;
}

} // namespace Markoff::Live::Test
