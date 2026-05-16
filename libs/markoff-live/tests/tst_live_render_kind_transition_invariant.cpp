// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QPointer>
#include <QQuickItem>
#include <QTest>

#include "QmlIntegrationFixture.h"

using namespace Markoff::Live::Test;

namespace {

void typeAscii(QmlIntegrationFixture &fix, char c) {
    QTest::keyClick(fix.window(), c);
    QTest::qWait(30);
    QCoreApplication::processEvents();
}

}  // namespace

/// Spec §6.1: within-class kind transitions preserve the TextEdit
/// QQuickItem identity. The same QObject pointer survives
/// paragraph→heading, heading→paragraph, paragraph→list-item.
class TestLiveRenderKindTransitionInvariant : public QObject {
    Q_OBJECT

private Q_SLOTS:

    void paragraph_to_heading_preserves_textedit_pointer() {
        QmlIntegrationFixture fix("hello", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        QPointer<QQuickItem> textEditBefore = fix.delegateTextEdit(0);
        QVERIFY(textEditBefore);
        const qreal contentYBefore = fix.listView()->property("contentY").toReal();

        fix.placeCursorAtPos(0, 0);
        QCOMPARE(fix.delegateCursorPos(0), 0);

        typeAscii(fix, '#');
        typeAscii(fix, ' ');
        QVERIFY(fix.waitForKindAt(0, QStringLiteral("heading"), 2000));

        // If the delegate was destroyed (beginResetModel), textEditBefore is
        // now null; if it was recreated, textEditAfter differs. Either way FAIL.
        QVERIFY2(!textEditBefore.isNull(),
                 "TextEdit was destroyed during paragraph→heading transition "
                 "(delegate recycled via beginResetModel)");
        QQuickItem *textEditAfter = fix.delegateTextEdit(0);
        QVERIFY(textEditAfter);
        QCOMPARE(textEditAfter, textEditBefore.data());  // SAME pointer.
        QCOMPARE(fix.delegateCursorPos(0), 2);

        const qreal contentYAfter = fix.listView()->property("contentY").toReal();
        QCOMPARE(contentYAfter, contentYBefore);
    }

    void heading_to_paragraph_preserves_textedit_pointer() {
        QmlIntegrationFixture fix("# foo", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        QPointer<QQuickItem> textEditBefore = fix.delegateTextEdit(0);
        QVERIFY(textEditBefore);

        fix.placeCursorAtPos(0, 1);
        QCOMPARE(fix.delegateCursorPos(0), 1);

        QTest::keyClick(fix.window(), Qt::Key_Backspace);
        QTest::qWait(30);
        QCoreApplication::processEvents();

        QVERIFY(fix.waitForKindAt(0, QStringLiteral("paragraph"), 2000));

        QVERIFY2(!textEditBefore.isNull(),
                 "TextEdit was destroyed during heading→paragraph transition "
                 "(delegate recycled via beginResetModel)");
        QQuickItem *textEditAfter = fix.delegateTextEdit(0);
        QVERIFY(textEditAfter);
        QCOMPARE(textEditAfter, textEditBefore.data());
    }

    void paragraph_to_listitem_preserves_textedit_pointer() {
        QmlIntegrationFixture fix("x", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        // Clear "x" so we can promote a clean empty paragraph.
        fix.placeCursorAtPos(0, 0);
        QTest::keyClick(fix.window(), Qt::Key_Delete);
        QTest::qWait(30);
        QCoreApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString(""), 2000);

        QPointer<QQuickItem> textEditBefore = fix.delegateTextEdit(0);
        QVERIFY(textEditBefore);

        typeAscii(fix, '-');
        typeAscii(fix, ' ');
        QVERIFY(fix.waitForKindAt(0, QStringLiteral("list-item"), 2000));

        QVERIFY2(!textEditBefore.isNull(),
                 "TextEdit was destroyed during paragraph→list-item transition "
                 "(delegate recycled via beginResetModel)");
        QQuickItem *textEditAfter = fix.delegateTextEdit(0);
        QVERIFY(textEditAfter);
        QCOMPARE(textEditAfter, textEditBefore.data());
    }

    /// Spec §6.2: cross-class kind transitions DO swap the delegate.
    /// Sanity check: paragraph→hr is a delegateClass change (text-inline
    /// → hr), so the standard Delete+Insert path runs and produces a
    /// different delegate.
    void paragraph_to_hr_swaps_delegate() {
        QmlIntegrationFixture fix("x", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        fix.placeCursorAtPos(0, 0);
        QTest::keyClick(fix.window(), Qt::Key_Delete);  // clear "x"
        QTest::qWait(30);
        QCoreApplication::processEvents();

        QQuickItem *delegateBefore = fix.delegateAt(0);
        QVERIFY(delegateBefore);
        const QByteArray classBefore = delegateBefore->metaObject()->className();

        // Type "---" to trigger paragraph→hr transition
        typeAscii(fix, '-');
        typeAscii(fix, '-');
        typeAscii(fix, '-');
        QVERIFY(fix.waitForKindAt(0, QStringLiteral("hr"), 2000));

        // After paragraph→hr, the delegate must be a DIFFERENT instance.
        QQuickItem *delegateAfter = fix.delegateAt(0);
        QVERIFY(delegateAfter);
        QVERIFY2(delegateAfter != delegateBefore,
                 "expected cross-class transition to swap delegate");
        const QByteArray classAfter = delegateAfter->metaObject()->className();
        QVERIFY2(classAfter.contains("HorizontalRule"),
                 qPrintable(QString("expected HorizontalRule delegate, got %1")
                            .arg(QString::fromUtf8(classAfter))));
        QVERIFY(classAfter != classBefore);
    }
};

QTEST_MAIN(TestLiveRenderKindTransitionInvariant)
#include "tst_live_render_kind_transition_invariant.moc"
