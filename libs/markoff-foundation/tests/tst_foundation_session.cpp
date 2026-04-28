// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QColor>
#include <QSignalSpy>
#include <QString>

#include <crdt/Anchor.h>
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

    void primary_selection_setter_round_trips() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        Selection sel;
        sel.anchor = CollabText::Crdt::Anchor(1, 10, CollabText::Crdt::Bias::Left);
        sel.active = CollabText::Crdt::Anchor(1, 12, CollabText::Crdt::Bias::Right);
        sel.kind   = Selection::Kind::Primary;

        QSignalSpy spy(s, &Session::primarySelectionChanged);
        s->setPrimarySelection(sel);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(s->primarySelection().anchor.char_value, sel.anchor.char_value);
        QCOMPARE(s->primarySelection().active.char_value, sel.active.char_value);
        delete s;
    }

    void primary_selection_idempotent_set_does_not_emit() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        Selection sel;
        sel.anchor = CollabText::Crdt::Anchor(1, 10, CollabText::Crdt::Bias::Left);
        sel.active = CollabText::Crdt::Anchor(1, 10, CollabText::Crdt::Bias::Left);
        s->setPrimarySelection(sel);

        QSignalSpy spy(s, &Session::primarySelectionChanged);
        s->setPrimarySelection(sel);   // identical assignment
        QCOMPARE(spy.count(), 0);
        delete s;
    }
};

QTEST_APPLESS_MAIN(TstFoundationSession)
#include "tst_foundation_session.moc"
