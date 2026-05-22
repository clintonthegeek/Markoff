// SPDX-License-Identifier: GPL-3.0-or-later
#include "QmlIntegrationFixture.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <QtGlobal>


#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/parser/SourceSpan.h>

#include "MainController.h"  // from markoff-live-app-internal STATIC lib

namespace Markoff::Live::Test {

// ---- QML-exception trap ----
//
// QML signal handlers (Component.onCompleted, onActivated, onTriggered, etc.)
// catch JS exceptions and log them as qWarning before aborting the rest of
// the handler block. Without a trap, an API drift like commit 36bbbb9
// (removed `LiveSelectionView::setSession`) silently severs every action
// connection in Main.qml's onCompleted while the test suite stays green.
//
// This handler fires QFAIL on any qWarning whose message matches a
// known-fatal JS-exception shape. It is installed at fixture construction
// and restored at destruction; tests that don't go through the fixture
// see the default handler.
namespace {

QtMessageHandler g_prevHandler = nullptr;
bool             g_handlerInstalled = false;

bool isFatalQmlException(const QString &msg)
{
    // Cheap substring match — JS engine formats these consistently.
    return msg.contains(QLatin1String("TypeError:"))
        || msg.contains(QLatin1String("ReferenceError:"))
        || msg.contains(QLatin1String("SyntaxError:"))
        || msg.contains(QLatin1String("is not a function"))
        || msg.contains(QLatin1String("is not a signal"));
}

void qmlExceptionTrap(QtMsgType type,
                      const QMessageLogContext &ctx,
                      const QString &msg)
{
    if (type == QtWarningMsg && isFatalQmlException(msg)) {
        const QByteArray reason = QStringLiteral(
            "QML JS exception escaped a signal handler — production callsite "
            "is silently severed.\n  %1\n  at %2:%3")
            .arg(msg)
            .arg(QString::fromUtf8(ctx.file ? ctx.file : "(unknown)"))
            .arg(ctx.line)
            .toUtf8();
        // Forward first so the message still shows up in the log, then fail.
        if (g_prevHandler) g_prevHandler(type, ctx, msg);
        QTest::qFail(reason.constData(), ctx.file ? ctx.file : __FILE__,
                     ctx.line ? ctx.line : __LINE__);
        return;
    }
    if (g_prevHandler) g_prevHandler(type, ctx, msg);
}

void installTrap()
{
    if (g_handlerInstalled) return;
    g_prevHandler = qInstallMessageHandler(&qmlExceptionTrap);
    g_handlerInstalled = true;
}

void removeTrap()
{
    if (!g_handlerInstalled) return;
    qInstallMessageHandler(g_prevHandler);
    g_prevHandler = nullptr;
    g_handlerInstalled = false;
}

} // namespace

QmlIntegrationFixture::QmlIntegrationFixture(const QByteArray &markdown,
                                             int expectedRowCount)
{
    installTrap();

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
    // Request activation so `hasActiveFocus()` works on focused TextEdits in
    // headless test runs — without this, `forceActiveFocus()` is a no-op and
    // `focusedDelegate()` always returns null on offscreen QPA.
    m_window->requestActivate();
    (void)QTest::qWaitForWindowActive(m_window, 2000);

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
    // expectedRowCount == -1 means "any count" — the caller doesn't
    // know exactly how many blocks the doc parses to and will assert
    // its own bounds. Wait for at least one row to ensure the model
    // has populated.
    if (expectedRowCount == -1) {
        if (m_model->rowCount() == 0) {
            QSignalSpy spy(m_model, &QAbstractItemModel::rowsInserted);
            spy.wait(2000);
        }
        QVERIFY(m_model->rowCount() > 0);
    } else {
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
    }

    m_harness = std::make_unique<LiveRealisticInputHarness>(m_window);
}

QmlIntegrationFixture::~QmlIntegrationFixture()
{
    removeTrap();
}

QObject *QmlIntegrationFixture::binding()           { return m_binding; }
QAbstractItemModel *QmlIntegrationFixture::model()  { return m_model; }

// Non-failing variant used by wait helpers that poll before the ListView
// has necessarily been realised. Returns nullptr without asserting.
//
// LiveView.qml's root IS a ListView, but Qt 6.x QML compilation wraps it in
// a generated type (e.g. "LiveView_QMLTYPE_3") so qstrcmp against
// "QQuickListView" fails. Instead we walk the superclass chain via
// metaObject()->superClass() in a loop, which traverses into the C++ base classes.
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

QString QmlIntegrationFixture::modelKind(int row) {
    const auto roles = m_model->roleNames();
    int kindRole = -1;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == QByteArray("kind")) {
            kindRole = it.key();
            break;
        }
    }
    if (kindRole == -1) return {};
    return m_model->data(m_model->index(row, 0), kindRole).toString();
}

bool QmlIntegrationFixture::waitForKindAt(int row, const QString &kind, int timeoutMs) {
    if (modelKind(row) == kind) return true;
    // Kind transitions may arrive via dataChanged OR modelReset (kind-only swap
    // path in LiveBlockModel uses beginResetModel/endResetModel). Use time-
    // based polling with QTest::qWait to catch both.
    QElapsedTimer t; t.start();
    while (modelKind(row) != kind && t.elapsed() < timeoutMs) {
        QTest::qWait(25);
        QCoreApplication::processEvents();
    }
    const QString actual = modelKind(row);
    if (actual != kind) {
        qWarning() << "[waitForKindAt] timeout: row=" << row
                   << "expected=" << kind << "actual=" << actual
                   << "rowCount=" << m_model->rowCount();
    }
    return actual == kind;
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

int QmlIntegrationFixture::cursorStateCurrentRow() {
    QObject *cs = m_binding ? m_binding->property("cursorState").value<QObject *>() : nullptr;
    if (!cs) return -1;
    return cs->property("focusedAnchorRow").toInt();
}

int QmlIntegrationFixture::cursorStateCurrentQtPos() {
    QObject *cs = m_binding ? m_binding->property("cursorState").value<QObject *>() : nullptr;
    if (!cs) return -1;
    return cs->property("focusedQtPos").toInt();
}

void QmlIntegrationFixture::placeCursorAtPos(int row, int qtPos) {
    QObject *cs = m_binding ? m_binding->property("cursorState").value<QObject *>() : nullptr;
    QVERIFY2(cs, "no cursorState on binding");
    QMetaObject::invokeMethod(cs, "requestTextCaretAtRow",
                              Qt::DirectConnection,
                              Q_ARG(int, row), Q_ARG(int, qtPos));
    QTest::qWait(50);
    QCoreApplication::processEvents();
}

void QmlIntegrationFixture::placeCursorAtEndOf(int row) {
    placeCursorAtPos(row, modelText(row).length());
}

void QmlIntegrationFixture::pasteText(const QString &text) {
    QApplication::clipboard()->setText(text);
    QTest::qWait(20);
    m_harness->keyClick(Qt::Key_V, Qt::ControlModifier);
}

void QmlIntegrationFixture::clickOnBlock(int row) {
    QVERIFY2(waitForDelegateAt(row, 2000),
             qPrintable(QString("delegate at row %1 not found").arg(row)));
    QQuickItem *d = delegateAt(row);
    if (!d) return;
    QVariant contentItemVar = listView()->property("contentItem");
    QQuickItem *contentItem = contentItemVar.value<QQuickItem *>();
    const qreal offsetX = contentItem ? contentItem->x() : 0.0;
    const qreal offsetY = contentItem ? contentItem->y() : 0.0;
    const int cx = static_cast<int>(d->x() + d->width() / 2 + offsetX);
    const int cy = static_cast<int>(d->y() + d->height() / 2 + offsetY);
    QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, QPoint(cx, cy));
    QTest::qWait(50);
    QCoreApplication::processEvents();
}

QString QmlIntegrationFixture::documentText() {
    const auto roles = m_model->roleNames();
    int textRole = -1;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == QByteArray("text")) { textRole = it.key(); break; }
    }
    if (textRole == -1) return {};
    QString result;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (i > 0) result += QLatin1Char('\n');
        result += m_model->data(m_model->index(i, 0), textRole).toString();
    }
    if (!result.isEmpty() && !result.endsWith(QLatin1Char('\n')))
        result += QLatin1Char('\n');
    return result;
}

QPoint QmlIntegrationFixture::scenePointAtFirstWikilink()
{
    // Step 1: find the best wikilink span to click in row 0's block.
    // Prefer a non-delimiter wikilink span (the content text, e.g. "Page"),
    // which will have a wider rendered width and a clear charLength > 0.
    // Fall back to the first wikilink span (even a 1-char delimiter) if no
    // non-delimiter span is found.
    const auto blockIds = m_doc->iterateBlocks();
    if (blockIds.empty())
        return {};
    const Markoff::BlockId bid = blockIds[0];
    const QList<Markoff::SourceSpan> spans = m_doc->inlineSpansFor(bid);

    int spanOffset = -1;
    int spanLen    = 0;
    for (const auto &s : spans) {
        if (!s.isWikilink) continue;
        if (!s.isDelimiter && s.charLength > 0) {
            // Prefer non-delimiter (content) span — its characters are always
            // visible (not zero-width), giving a reliable click target.
            spanOffset = s.charOffset;
            spanLen    = s.charLength;
            break;
        }
        if (spanOffset < 0) {
            // First wikilink span seen — save as fallback.
            spanOffset = s.charOffset;
            spanLen    = s.charLength;
        }
    }
    if (spanOffset < 0)
        return {};

    // Step 2: get the TextEdit item for row 0.
    QQuickItem *te = delegateTextEdit(0);
    if (!te)
        return {};

    // Step 3: use positionToRectangle for the mid-point of the chosen span to
    // get a rect in TextEdit-local space that is inside the wikilink's visible
    // text area.
    const int charPos = spanOffset + spanLen / 2;
    QRectF localRect;
    const bool ok = QMetaObject::invokeMethod(
        te, "positionToRectangle",
        Qt::DirectConnection,
        Q_RETURN_ARG(QRectF, localRect),
        Q_ARG(int, charPos));
    if (!ok)
        return {};

    // Step 4: Map TextEdit-local centre → scene (= window) coordinates.
    // Note: the LiveView.qml hit() path calls delegate.positionAt(x, y)
    // which subtracts leftPadding before calling edit.positionAt — the
    // double-accounting cancels because positionToRectangle already encodes
    // the padding offset in its return value, and mapToScene includes the
    // TextEdit's position (which starts at the delegate origin for paragraph
    // delegates with no leftMargin).
    const QPointF scenePt = te->mapToScene(localRect.center());
    return scenePt.toPoint();
}

void QmlIntegrationFixture::simulateCtrlHoverAt(const QPoint &windowPos)
{
    QVERIFY2(m_window, "window is null");
    // Strategy: qt_handleMouseEvent with MouseMove + ControlModifier does not
    // reliably reach MouseArea.onPositionChanged under offscreen QPA (pure hover
    // events are not delivered without a prior cursor grab). However, Qt Quick
    // DOES deliver hover position to MouseArea.onPositionChanged with pressed=false
    // just before processing a MouseButtonPress at the new position — using the
    // same implicit-hover delivery that makes the Ctrl+click test exercise the
    // hover path.
    //
    // Qt Quick tracks a single global cursor position across all windows. If the
    // cursor is already at windowPos when mouseClick fires (e.g. because a prior
    // test's click left it there), the "implicit hover at new position" step is
    // skipped and onPositionChanged never fires at windowPos. We work around this
    // by first moving the cursor to a far-away corner (0, 0) — guaranteed to be
    // different from any wikilink position — so the subsequent Ctrl+click always
    // has a non-zero delta and generates the implicit hover at windowPos.
    //
    // The click also fires onClicked (activateLinkAt), but hover tests only assert
    // on svc.hovers, not svc.activations.
    const QPoint corner(1, 1);  // far from any typical wikilink position
    QTest::mouseMove(m_window, corner);
    QCoreApplication::processEvents();
    QTest::mouseClick(m_window, Qt::LeftButton, Qt::ControlModifier, windowPos);
    QCoreApplication::processEvents();
    QTest::qWait(50);
    QCoreApplication::processEvents();
}

} // namespace Markoff::Live::Test
