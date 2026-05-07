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
};

QTEST_APPLESS_MAIN(TstFoundationLinkService)
#include "tst_foundation_link_service.moc"
