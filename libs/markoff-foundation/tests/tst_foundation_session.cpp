// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QColor>
#include <QString>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Session.h>
#include <markoff-foundation/SessionParams.h>

using namespace Markoff;

class TstFoundationSession : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void session_carries_construction_params() {
        MarkoffDocument doc(1);
        SessionParams params;
        params.participantId    = QStringLiteral("alice");
        params.participantLabel = QStringLiteral("Alice");
        params.presenceColor    = QColor(Qt::magenta);

        // createSession is wired in Task 23; for the skeleton task we
        // construct a Session directly with a parent document.
        Session *s = new Session(&doc, params);
        QCOMPARE(s->participantId(),    QStringLiteral("alice"));
        QCOMPARE(s->participantLabel(), QStringLiteral("Alice"));
        QCOMPARE(s->presenceColor().name(), QColor(Qt::magenta).name());
        QVERIFY(!s->id().isEmpty());
        delete s;
    }

    void session_default_params_have_empty_identity() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        QVERIFY(s->participantId().isEmpty());
        QVERIFY(s->participantLabel().isEmpty());
        delete s;
    }
};

QTEST_APPLESS_MAIN(TstFoundationSession)
#include "tst_foundation_session.moc"
