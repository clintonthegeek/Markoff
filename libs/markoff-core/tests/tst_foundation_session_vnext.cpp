// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/Session.h>

#include <array>

using namespace Markoff;

namespace {
ScopedTextPosition position(quint64 counter, QByteArray token)
{
    std::array<uint8_t, 16> actor{};
    actor[0] = 1;
    return {*BlockId::fromStructuralIdentity(actor, counter), std::move(token)};
}
}

class TstFoundationSessionVNext : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void selectionAndScrollRoundTripUseScopedPositions()
    {
        Session source(nullptr, {});
        Selection selection;
        selection.anchorPosition = position(1, QByteArrayLiteral("anchor"));
        selection.activePosition = position(1, QByteArrayLiteral("active"));
        source.setPrimarySelection(selection);

        QSignalSpy scrollSpy(&source, &Session::scrollPositionChanged);
        const auto top = position(2, QByteArrayLiteral("top"));
        source.setTopVisible(top, 0.25);
        QCOMPARE(scrollSpy.count(), 1);
        QCOMPARE(source.topVisiblePosition(), std::optional<ScopedTextPosition>(top));

        const auto json = source.toJson();
        QVERIFY(!json.contains("topAnchor"));
        QVERIFY(json.contains("topPosition"));
        const auto selectionJson = json.value("primary").toObject();
        QVERIFY(!selectionJson.contains("anchor"));
        QVERIFY(selectionJson.contains("anchorPosition"));

        Session restored(nullptr, {});
        restored.fromJson(json);
        QCOMPARE(restored.primarySelection().anchorPosition, selection.anchorPosition);
        QCOMPARE(restored.primarySelection().activePosition, selection.activePosition);
        QCOMPARE(restored.topVisiblePosition(), std::optional<ScopedTextPosition>(top));
        QCOMPARE(restored.topVisibleFraction(), 0.25);
    }

    void legacyScrollAnchorJsonIsIgnored()
    {
        Session restored(nullptr, {});
        restored.fromJson({
            {"topAnchor", QJsonObject{{"version", 1}, {"replica", "1"},
                                       {"character", "24"}}},
            {"topFraction", 0.5},
            {"primary", QJsonObject{
                {"anchor", QJsonObject{{"version", 1}, {"replica", "1"},
                                        {"character", "2"}}},
                {"active", QJsonObject{{"version", 1}, {"replica", "1"},
                                        {"character", "2"}}},
            }},
        });
        QVERIFY(!restored.topVisiblePosition());
        QCOMPARE(restored.topVisibleFraction(), 0.5);
        QVERIFY(!restored.primarySelection().anchorPosition);
        QVERIFY(!restored.primarySelection().activePosition);

        const auto encoded = restored.toJson();
        QVERIFY(!encoded.contains("topAnchor"));
        QVERIFY(!encoded.value("primary").toObject().contains("anchor"));
    }
};

QTEST_MAIN(TstFoundationSessionVNext)
#include "tst_foundation_session_vnext.moc"
