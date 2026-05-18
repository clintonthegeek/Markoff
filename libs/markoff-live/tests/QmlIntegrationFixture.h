// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <memory>

#include <markoff/core/BlockId.h>

#include "LiveRealisticInputHarness.h"

class QAbstractItemModel;
class QQmlApplicationEngine;
class QQuickItem;
class QQuickWindow;
class QTemporaryFile;
class MainController;

namespace Markoff {
class MarkoffDocument;
class Session;
} // namespace Markoff

namespace Markoff::Live::Test {

/// Loads production Main.qml against a fresh MarkoffDocument and drives
/// it via LiveRealisticInputHarness. Tests use this to assert on three
/// layers (buffer / model / delegate) per the spec §5.1 convention.
class QmlIntegrationFixture {
public:
    /// Loads `markdown` into a fresh MarkoffDocument and brings the
    /// production Main.qml up against it. Blocks until the window is
    /// exposed and the model has `expectedRowCount` rows.
    /// On failure, the constructor uses QVERIFY/QCOMPARE to fail the
    /// enclosing test; tests must construct fixtures at slot scope.
    explicit QmlIntegrationFixture(const QByteArray &markdown,
                                   int expectedRowCount);
    ~QmlIntegrationFixture();

    QmlIntegrationFixture(const QmlIntegrationFixture &) = delete;
    QmlIntegrationFixture &operator=(const QmlIntegrationFixture &) = delete;

    Markoff::MarkoffDocument *document() const { return m_doc.get(); }
    Markoff::Session         *session()  const { return m_session; }
    QQmlApplicationEngine    *engine()   const { return m_engine.get(); }
    QQuickWindow             *window()   const { return m_window; }

    QObject            *binding();
    QAbstractItemModel *model();

    // Wait helpers (return false on timeout; tests should QVERIFY).
    bool waitForRowCount(int expected, int timeoutMs = 2000);
    bool waitForDelegateAt(int row, int timeoutMs = 2000);

    /// Returns the delegate (or null) that currently has activeFocus —
    /// either it or one of its children. Used by Tasks 8+ to assert
    /// focus migration after Enter / arrow keys.
    QQuickItem *focusedDelegate();

    // ---- Chokepoint-invariant helpers (Task 2+) ----

    /// Returns the row index that LiveCursorState.focusedAnchorRow reports.
    /// -1 means no focused anchor.
    int cursorStateCurrentRow();

    /// Returns LiveCursorState.focusedQtPos.
    int cursorStateCurrentQtPos();

    /// Place the cursor at (row, qtPos) via requestTextCaretAtRow and
    /// waits for the delegate at that row to receive active focus.
    void placeCursorAtPos(int row, int qtPos);

    /// Place the cursor at the end of the text at `row`.
    void placeCursorAtEndOf(int row);

    /// Set clipboard to `text` and send Ctrl+V to the window.
    void pasteText(const QString &text);

    /// Simulate a mouse click at the centre of the delegate at `row`.
    void clickOnBlock(int row);

    /// Returns all model block texts joined by '\n' with a trailing '\n'.
    QString documentText();

    // Resolved QML objects (cached on first access)
    QQuickItem *listView();
    QQuickItem *delegateAt(int row);
    QQuickItem *delegateTextEdit(int row); // recursive find of TextEdit child

    // Three-layer state (per spec §5.1)
    QByteArray bufferText(Markoff::BlockId id);
    QString    modelText(int row);
    QString    modelKind(int row);
    bool       waitForKindAt(int row, const QString &kind, int timeoutMs = 2000);
    QString    delegateText(int row);
    int        delegateCursorPos(int row);

    LiveRealisticInputHarness &harness() { return *m_harness; }

    /// Returns the window-coordinate centre of the first wikilink span in
    /// the block at row 0. Returns a null QPoint if no wikilink is found or
    /// the delegate is not yet realised.
    QPoint scenePointAtFirstWikilink();

private:
    quint16 m_replicaId = 0;
    std::unique_ptr<QTemporaryFile>           m_tmpFile;
    std::unique_ptr<Markoff::MarkoffDocument> m_doc;
    Markoff::Session                         *m_session = nullptr;
    std::unique_ptr<MainController>           m_mainController;
    std::unique_ptr<QQmlApplicationEngine>    m_engine;
    QQuickWindow                             *m_window = nullptr;
    QObject                                  *m_binding = nullptr;
    QAbstractItemModel                       *m_model = nullptr;
    QQuickItem                               *m_listView = nullptr;
    std::unique_ptr<LiveRealisticInputHarness> m_harness;
};

} // namespace Markoff::Live::Test
