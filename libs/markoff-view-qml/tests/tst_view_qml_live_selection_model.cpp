// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/view/qml/LiveSelectionModel.h>

using namespace Markoff::View::Qml;

class TstLiveSelectionModel : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initially_no_selection() {
        LiveSelectionModel m;
        QVERIFY(!m.hasSelection());
        QCOMPARE(m.anchorBlock(), -1);
        QCOMPARE(m.activeBlock(), -1);
    }

    void begin_then_extend_in_same_block() {
        LiveSelectionModel m;
        QSignalSpy spy(&m, &LiveSelectionModel::selectionChanged);
        m.begin(0, 5);
        QVERIFY(!m.hasSelection());  // zero-length
        QCOMPARE(spy.count(), 1);
        m.extend(0, 12);
        QVERIFY(m.hasSelection());
        QCOMPARE(spy.count(), 2);
        const QPoint r = m.rangeForBlock(0);
        QCOMPARE(r.x(), 5);
        QCOMPARE(r.y(), 12);
    }

    void rangeForBlock_returns_minus_one_for_unselected_blocks() {
        LiveSelectionModel m;
        m.begin(2, 0);
        m.extend(2, 5);
        QCOMPARE(m.rangeForBlock(0), QPoint(-1, -1));
        QCOMPARE(m.rangeForBlock(1), QPoint(-1, -1));
        QCOMPARE(m.rangeForBlock(3), QPoint(-1, -1));
    }

    void multi_block_forward_selection() {
        LiveSelectionModel m;
        m.begin(1, 3);
        m.extend(3, 7);
        QCOMPARE(m.rangeForBlock(1).x(), 3);
        QCOMPARE(m.rangeForBlock(1).y(), INT32_MAX);
        QCOMPARE(m.rangeForBlock(2).x(), 0);
        QCOMPARE(m.rangeForBlock(2).y(), INT32_MAX);
        QCOMPARE(m.rangeForBlock(3).x(), 0);
        QCOMPARE(m.rangeForBlock(3).y(), 7);
    }

    void multi_block_backward_selection_normalizes() {
        LiveSelectionModel m;
        m.begin(3, 7);
        m.extend(1, 3);
        QCOMPARE(m.rangeForBlock(1).x(), 3);
        QCOMPARE(m.rangeForBlock(1).y(), INT32_MAX);
        QCOMPARE(m.rangeForBlock(3).x(), 0);
        QCOMPARE(m.rangeForBlock(3).y(), 7);
    }

    void zero_length_selection_returns_no_range() {
        LiveSelectionModel m;
        m.begin(0, 5);
        QCOMPARE(m.rangeForBlock(0), QPoint(-1, -1));
    }

    void clear_resets_state() {
        LiveSelectionModel m;
        m.begin(0, 0);
        m.extend(2, 4);
        QSignalSpy spy(&m, &LiveSelectionModel::selectionChanged);
        m.clear();
        QVERIFY(!m.hasSelection());
        QCOMPARE(m.anchorBlock(), -1);
        QCOMPARE(m.activeBlock(), -1);
        QCOMPARE(spy.count(), 1);
    }

    void clear_when_already_empty_does_not_emit() {
        LiveSelectionModel m;
        QSignalSpy spy(&m, &LiveSelectionModel::selectionChanged);
        m.clear();
        QCOMPARE(spy.count(), 0);
    }

    void collectSelectedText_single_block() {
        LiveSelectionModel m;
        m.begin(1, 0);
        m.extend(1, 5);
        QStringList blocks = { "abc", "Hello world", "xyz" };
        QCOMPARE(m.collectSelectedText(blocks), QStringLiteral("Hello"));
    }

    void collectSelectedText_multi_block() {
        LiveSelectionModel m;
        m.begin(0, 6);
        m.extend(2, 3);
        QStringList blocks = { "Hello world", "middle", "abcdef" };
        QCOMPARE(m.collectSelectedText(blocks),
                 QStringLiteral("world\nmiddle\nabc"));
    }

    void extend_without_begin_treats_as_begin() {
        LiveSelectionModel m;
        m.extend(2, 4);
        QCOMPARE(m.anchorBlock(), 2);
        QCOMPARE(m.anchorOffset(), 4);
        QCOMPARE(m.activeBlock(), 2);
        QCOMPARE(m.activeOffset(), 4);
    }

    void no_emit_when_extend_repeats_same_position() {
        LiveSelectionModel m;
        m.begin(0, 5);
        QSignalSpy spy(&m, &LiveSelectionModel::selectionChanged);
        m.extend(0, 5);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_APPLESS_MAIN(TstLiveSelectionModel)
#include "tst_view_qml_live_selection_model.moc"
