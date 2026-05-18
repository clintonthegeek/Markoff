// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/DefaultLinkService.h>

using namespace Markoff;

class TstFoundationLinkService : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void classify_external_https() {
        DefaultLinkService s;
        QCOMPARE(s.classify("https://example.com"), LinkKind::External);
    }

    void classify_external_http() {
        DefaultLinkService s;
        QCOMPARE(s.classify("http://example.com"), LinkKind::External);
    }

    void classify_external_mailto() {
        DefaultLinkService s;
        QCOMPARE(s.classify("mailto:foo@example.com"), LinkKind::External);
    }

    void classify_unknown_for_unresolved_text() {
        DefaultLinkService s;
        QCOMPARE(s.classify("note title"), LinkKind::Unknown);
    }

    void resolve_returns_literal_qurl() {
        DefaultLinkService s;
        QCOMPARE(s.resolve("https://example.com").toString(),
                 QStringLiteral("https://example.com"));
    }

    void activate_emits_link_activated() {
        DefaultLinkService s;
        QSignalSpy spy(&s, &LinkService::linkActivated);
        LinkActivation a;
        a.rawText = "https://x";
        a.resolvedTarget = QUrl("https://x");
        a.kind = LinkKind::External;
        s.activate(a);
        QCOMPARE(spy.count(), 1);
    }

    void activation_carries_structured_fields() {
        Markoff::LinkActivation a;
        a.rawText = QStringLiteral("[[Page|Alias]]");
        a.kind = Markoff::LinkKind::WikiLink;
        a.page = QStringLiteral("Page");
        a.alias = QStringLiteral("Alias");
        a.modifiers = Qt::ControlModifier;

        QVariant v = QVariant::fromValue(a);
        auto round = v.value<Markoff::LinkActivation>();
        QCOMPARE(round.page, QStringLiteral("Page"));
        QCOMPARE(round.alias, QStringLiteral("Alias"));
        QCOMPARE(int(round.modifiers), int(Qt::ControlModifier));
    }
};

QTEST_APPLESS_MAIN(TstFoundationLinkService)
#include "tst_foundation_link_service.moc"
