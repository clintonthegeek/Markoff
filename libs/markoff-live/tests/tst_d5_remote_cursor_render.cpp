// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QAbstractListModel>
#include <QColor>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Cursor.h>
#include <markoff/live/LiveListModelBinding.h>

class TstD5RemoteCursorRender : public QObject {
    Q_OBJECT
private slots:
    void setRemoteCursor_addsOverlay() {
        Markoff::MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("hello\n"));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);

        auto *model = binding.remoteCursorsModel();
        QVERIFY(model != nullptr);
        QCOMPARE(model->rowCount(), 0);

        doc.setRemoteCursor(2, Markoff::NoCursor{}, QColor("#f00"), QStringLiteral("Bob"));
        QCOMPARE(model->rowCount(), 1);

        doc.clearRemoteCursor(2);
        QCOMPARE(model->rowCount(), 0);
    }
    void clearAllRemoteCursors_clearsModel() {
        Markoff::MarkoffDocument doc(quint16(1));
        doc.loadFromMarkdown(QByteArrayLiteral("hello\n"));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);

        doc.setRemoteCursor(2, Markoff::NoCursor{}, QColor("#f00"), "A");
        doc.setRemoteCursor(3, Markoff::NoCursor{}, QColor("#0f0"), "B");
        QCOMPARE(binding.remoteCursorsModel()->rowCount(), 2);
        doc.clearAllRemoteCursors();
        QCOMPARE(binding.remoteCursorsModel()->rowCount(), 0);
    }
};
QTEST_MAIN(TstD5RemoteCursorRender)
#include "tst_d5_remote_cursor_render.moc"
