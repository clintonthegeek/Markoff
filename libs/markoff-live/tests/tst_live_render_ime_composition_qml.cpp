// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audit L7 regression net — IME composition under D2.
//
// The audit (docs/specs/2026-05-21-textedit-interface-audit.md §3.5c)
// flagged the IME path as "untested entirely." Current implementation:
// LiveEditBinding defers contentsChange while inputMethodComposing is
// true; on commit (composing transitions back to false),
// flushPendingComposition issues a single wholesale block-content
// replace via d2ApplyBufferEdit(0, prevByteLen, postUtf8).
//
// Spec: docs/specs/2026-05-21-audit-L7-ime-composition.md.
//
// These tests pin the current behaviour. They DO NOT change anything
// about the IME path; they're a falsifiability net so the next
// refactor (probably the D5-collision spec) has a regression baseline.
//
// IME events are synthesized via QInputMethodEvent and delivered
// directly to the focused TextEdit through QCoreApplication::sendEvent.
// Qt's TextEdit handles inputMethodComposing internally; the property
// becomes true when a non-empty preedit string is set, false when the
// preedit clears.

#include "QmlIntegrationFixture.h"

#include <QApplication>
#include <QInputMethodEvent>
#include <QQuickItem>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestImeCompositionQml : public QObject {
    Q_OBJECT

    // Send a preedit string (still-composing text) to the focused
    // TextEdit at `row`. Empty preeditStr terminates composition.
    void sendPreedit(QmlIntegrationFixture &fx, int row,
                     const QString &preeditStr) {
        QQuickItem *te = fx.delegateTextEdit(row);
        QVERIFY(te);
        QList<QInputMethodEvent::Attribute> attrs;
        QInputMethodEvent ev(preeditStr, attrs);
        QCoreApplication::sendEvent(te, &ev);
        QCoreApplication::processEvents();
    }

    // Commit the given string (replaces preedit + inserts at caret) to
    // the focused TextEdit at `row`. Composition ends.
    void sendCommit(QmlIntegrationFixture &fx, int row,
                    const QString &commitStr) {
        QQuickItem *te = fx.delegateTextEdit(row);
        QVERIFY(te);
        QList<QInputMethodEvent::Attribute> attrs;
        QInputMethodEvent ev(QString(), attrs);
        ev.setCommitString(commitStr);
        QCoreApplication::sendEvent(te, &ev);
        QCoreApplication::processEvents();
    }

private slots:
    void commit_after_preedit_lands_in_d2_buffer();
    void preedit_then_replace_then_commit_records_single_edit();
    void preedit_then_empty_no_commit_leaves_d2_unchanged();
    void commit_into_non_empty_block_inserts_at_caret_after_swap();
    void composing_property_lifecycle_matches_qt_native();
};

void TestImeCompositionQml::commit_after_preedit_lands_in_d2_buffer() {
    QmlIntegrationFixture fx("seed\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    fx.placeCursorAtEndOf(0);
    QTest::qWait(20);

    QQuickItem *te = fx.delegateTextEdit(0);
    QVERIFY(te);

    // Begin a preedit. Block content in CRDT must remain unchanged
    // while composing.
    sendPreedit(fx, 0, QStringLiteral("ab"));
    QVERIFY(te->property("inputMethodComposing").toBool());
    QCOMPARE(fx.modelText(0), QStringLiteral("seed"));

    // Commit. Block content must reflect the committed text.
    sendCommit(fx, 0, QStringLiteral("ab"));
    QVERIFY(!te->property("inputMethodComposing").toBool());
    QCOMPARE(fx.modelText(0), QStringLiteral("seedab"));
}

void TestImeCompositionQml::preedit_then_replace_then_commit_records_single_edit() {
    QmlIntegrationFixture fx("seed\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    fx.placeCursorAtEndOf(0);
    QTest::qWait(20);

    // Preedit "a" — CRDT unchanged.
    sendPreedit(fx, 0, QStringLiteral("a"));
    QCOMPARE(fx.modelText(0), QStringLiteral("seed"));

    // Preedit "ab" (replaces, still composing) — CRDT still unchanged.
    sendPreedit(fx, 0, QStringLiteral("ab"));
    QCOMPARE(fx.modelText(0), QStringLiteral("seed"));

    // Commit "ab". CRDT now has the final text.
    sendCommit(fx, 0, QStringLiteral("ab"));
    QCOMPARE(fx.modelText(0), QStringLiteral("seedab"));
}

void TestImeCompositionQml::preedit_then_empty_no_commit_leaves_d2_unchanged() {
    // A cancelled composition: preedit starts, then the user dismisses
    // the IME (preedit becomes empty) without committing. The
    // wholesale-replace fires but with the original content, so net
    // effect should be zero.
    QmlIntegrationFixture fx("seed\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    fx.placeCursorAtEndOf(0);
    QTest::qWait(20);

    QQuickItem *te = fx.delegateTextEdit(0);
    QVERIFY(te);

    sendPreedit(fx, 0, QStringLiteral("a"));
    QVERIFY(te->property("inputMethodComposing").toBool());

    // Clear preedit with no commit.
    sendPreedit(fx, 0, QString());
    QVERIFY(!te->property("inputMethodComposing").toBool());

    QCOMPARE(fx.modelText(0), QStringLiteral("seed"));
}

void TestImeCompositionQml::commit_into_non_empty_block_inserts_at_caret_after_swap() {
    // Block has "hello" with cursor at end. Compose then commit "WORLD".
    // The wholesale-replace path computes the new block content from the
    // TextEdit's post-commit toPlainText() — which should include both
    // the pre-existing "hello" and the committed "WORLD".
    QmlIntegrationFixture fx("hello\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    fx.placeCursorAtEndOf(0);
    QTest::qWait(20);

    sendPreedit(fx, 0, QStringLiteral("WO"));
    QCOMPARE(fx.modelText(0), QStringLiteral("hello"));

    sendCommit(fx, 0, QStringLiteral("WORLD"));
    QCOMPARE(fx.modelText(0), QStringLiteral("helloWORLD"));
}

void TestImeCompositionQml::composing_property_lifecycle_matches_qt_native() {
    // Documents the lifecycle the L7 implementation relies on:
    //   * inputMethodComposing becomes true when a non-empty preedit is set
    //   * becomes false on commit
    //   * becomes false when preedit is cleared (no commit needed)
    QmlIntegrationFixture fx("x\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    fx.placeCursorAtEndOf(0);
    QTest::qWait(20);

    QQuickItem *te = fx.delegateTextEdit(0);
    QVERIFY(te);

    QVERIFY(!te->property("inputMethodComposing").toBool());

    sendPreedit(fx, 0, QStringLiteral("p"));
    QVERIFY(te->property("inputMethodComposing").toBool());

    sendCommit(fx, 0, QStringLiteral("p"));
    QVERIFY(!te->property("inputMethodComposing").toBool());

    sendPreedit(fx, 0, QStringLiteral("q"));
    QVERIFY(te->property("inputMethodComposing").toBool());

    sendPreedit(fx, 0, QString());
    QVERIFY(!te->property("inputMethodComposing").toBool());
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestImeCompositionQml)
#include "tst_live_render_ime_composition_qml.moc"
