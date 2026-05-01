// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QColor>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>

#include <crdt/Anchor.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Session.h>
#include <markoff-foundation/SessionParams.h>
#include <markoff-foundation/TextAnchor.h>

using namespace Markoff;

namespace {
/// Build a TextAnchor with the same wire identity as a CRDT anchor.
/// The Selection field type changed to TextAnchor in T10/T11; tests
/// retain CRDT construction for clarity at the seed and convert here.
TextAnchor ta(quint16 r, quint32 cv, CollabText::Crdt::Bias bias)
{
    return TextAnchor{r, cv, bias == CollabText::Crdt::Bias::Right ? quint8(1) : quint8(0)};
}
}  // namespace

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
        sel.anchor = ta(1, 10, CollabText::Crdt::Bias::Left);
        sel.active = ta(1, 12, CollabText::Crdt::Bias::Right);
        sel.kind   = Selection::Kind::Primary;

        QSignalSpy spy(s, &Session::primarySelectionChanged);
        s->setPrimarySelection(sel);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(s->primarySelection().anchor.charValue, sel.anchor.charValue);
        QCOMPARE(s->primarySelection().active.charValue, sel.active.charValue);
        delete s;
    }

    void primary_selection_idempotent_set_does_not_emit() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        Selection sel;
        sel.anchor = ta(1, 10, CollabText::Crdt::Bias::Left);
        sel.active = ta(1, 10, CollabText::Crdt::Bias::Left);
        s->setPrimarySelection(sel);

        QSignalSpy spy(s, &Session::primarySelectionChanged);
        s->setPrimarySelection(sel);   // identical assignment
        QCOMPARE(spy.count(), 0);
        delete s;
    }

    void set_secondary_selections_replaces_list() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        Selection a; a.kind = Selection::Kind::Secondary;
        a.anchor = ta(1, 10, CollabText::Crdt::Bias::Left);
        a.active = ta(1, 11, CollabText::Crdt::Bias::Right);
        Selection b; b.kind = Selection::Kind::SearchMatch;
        b.anchor = ta(1, 20, CollabText::Crdt::Bias::Left);
        b.active = ta(1, 23, CollabText::Crdt::Bias::Right);

        QSignalSpy spy(s, &Session::secondarySelectionsChanged);
        s->setSecondarySelections({ a, b });
        QCOMPARE(spy.count(), 1);
        QCOMPARE(s->secondarySelections().size(), 2);
        delete s;
    }

    void add_secondary_selection_appends() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        Selection a; a.kind = Selection::Kind::Secondary;
        a.anchor = ta(1, 10, CollabText::Crdt::Bias::Left);
        a.active = ta(1, 11, CollabText::Crdt::Bias::Right);
        s->addSecondarySelection(a);
        QCOMPARE(s->secondarySelections().size(), 1);
        s->addSecondarySelection(a);
        QCOMPARE(s->secondarySelections().size(), 2);
        delete s;
    }

    void clear_of_kind_preserves_other_kinds() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        Selection sec; sec.kind = Selection::Kind::Secondary;
        sec.anchor = ta(1, 1, CollabText::Crdt::Bias::Left);
        Selection sm;  sm.kind  = Selection::Kind::SearchMatch;
        sm.anchor  = ta(1, 5, CollabText::Crdt::Bias::Left);
        Selection pres; pres.kind = Selection::Kind::Presence;
        pres.anchor = ta(1, 9, CollabText::Crdt::Bias::Left);
        s->setSecondarySelections({ sec, sm, pres });

        s->clearSecondarySelectionsOfKind(Selection::Kind::SearchMatch);
        QCOMPARE(s->secondarySelections().size(), 2);
        for (const Selection &x : s->secondarySelections())
            QVERIFY(x.kind != Selection::Kind::SearchMatch);
        delete s;
    }

    void set_top_visible_emits_scroll_changed() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        QSignalSpy spy(s, &Session::scrollChanged);
        const auto a = CollabText::Crdt::Anchor(1, 100, CollabText::Crdt::Bias::Left);
        s->setTopVisible(a, 0.25);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(s->topVisibleAnchor().char_value, quint32(100));
        QCOMPARE(s->topVisibleFraction(), 0.25);
        delete s;
    }

    void set_folded_regions_replaces_list() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        FoldRef f; f.kind = FoldRef::Kind::Heading;
        f.start = CollabText::Crdt::Anchor(1, 50, CollabText::Crdt::Bias::Left);
        f.headingPath << QStringLiteral("Intro");
        f.headingLevel = 1;

        QSignalSpy spy(s, &Session::foldedRegionsChanged);
        s->setFoldedRegions({ f });
        QCOMPARE(spy.count(), 1);
        QCOMPARE(s->foldedRegions().size(), 1);
        delete s;
    }

    void toggle_fold_adds_then_removes() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        FoldRef f; f.kind = FoldRef::Kind::Heading;
        f.start = CollabText::Crdt::Anchor(1, 50, CollabText::Crdt::Bias::Left);
        f.headingPath << QStringLiteral("Intro");
        f.headingLevel = 1;

        s->toggleFold(f);
        QCOMPARE(s->foldedRegions().size(), 1);
        s->toggleFold(f);
        QCOMPARE(s->foldedRegions().size(), 0);
        delete s;
    }

    void copy_state_from_transfers_ephemeral_state() {
        MarkoffDocument doc(1);
        Session *src = new Session(&doc, SessionParams{
            .participantId = QStringLiteral("alice")});
        Selection p;
        p.anchor = ta(1, 5, CollabText::Crdt::Bias::Left);
        p.active = ta(1, 8, CollabText::Crdt::Bias::Right);
        src->setPrimarySelection(p);
        src->setTopVisible(CollabText::Crdt::Anchor(1, 100,
                           CollabText::Crdt::Bias::Left), 0.5);

        Session *dst = new Session(&doc, SessionParams{
            .participantId = QStringLiteral("bob")});
        dst->copyStateFrom(*src);

        QCOMPARE(dst->primarySelection().anchor.charValue, quint32(5));
        QCOMPARE(dst->topVisibleAnchor().char_value,        quint32(100));
        QCOMPARE(dst->topVisibleFraction(),                 0.5);
        // Identity is NOT copied.
        QCOMPARE(dst->participantId(), QStringLiteral("bob"));
        QVERIFY(dst->id() != src->id());
        delete src; delete dst;
    }

    void session_json_roundtrip() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{
            .participantId    = QStringLiteral("alice"),
            .participantLabel = QStringLiteral("Alice"),
            .presenceColor    = QColor(Qt::cyan)});
        Selection p;
        p.anchor = ta(1, 5, CollabText::Crdt::Bias::Left);
        p.active = ta(1, 8, CollabText::Crdt::Bias::Right);
        s->setPrimarySelection(p);

        const QJsonObject json = s->toJson();
        Session *t = new Session(&doc, SessionParams{});
        t->fromJson(json);
        QCOMPARE(t->primarySelection().anchor.charValue, quint32(5));
        QCOMPARE(t->participantId(), QStringLiteral("alice"));
        delete s; delete t;
    }
};

QTEST_APPLESS_MAIN(TstFoundationSession)
#include "tst_foundation_session.moc"
