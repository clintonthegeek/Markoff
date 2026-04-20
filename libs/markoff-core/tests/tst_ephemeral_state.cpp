// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <markoff/EphemeralState.h>

using namespace Markoff;

class TstEphemeralState : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void defaultStateSerialises() {
        EphemeralState s;
        const QJsonObject j = s.toJson();
        QVERIFY(j.contains("scroll"));
        QCOMPARE(j.value("scroll").toDouble(), 0.0);
        QVERIFY(j.contains("mode"));
    }

    void nonDefaultRoundTrips() {
        EphemeralState a;
        a.scroll = 42.73f;
        a.cursor = {17, 4};
        a.viewMode = QStringLiteral("live");
        a.foldedHeadings = {{3, 1}, {12, 2}};
        QJsonObject extras;
        extras.insert("liveScrollOffsets", QJsonArray{1, 2, 3});
        a.extras = extras;

        const QJsonObject j = a.toJson();
        const EphemeralState b = EphemeralState::fromJson(j);

        QCOMPARE(b.scroll, a.scroll);
        QCOMPARE(b.cursor, a.cursor);
        QCOMPARE(b.viewMode, a.viewMode);
        QCOMPARE(b.foldedHeadings.size(), a.foldedHeadings.size());
        QCOMPARE(b.foldedHeadings[0], a.foldedHeadings[0]);
        QCOMPARE(b.extras.value("liveScrollOffsets").toArray().size(), 3);
    }

    void missingKeysDefault() {
        const EphemeralState s = EphemeralState::fromJson({});
        QCOMPARE(s.scroll, 0.0f);
        QCOMPARE(s.cursor, (CursorPos{0, 0}));
        QVERIFY(s.foldedHeadings.isEmpty());
    }
};

QTEST_MAIN(TstEphemeralState)
#include "tst_ephemeral_state.moc"
