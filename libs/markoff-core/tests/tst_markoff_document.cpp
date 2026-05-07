// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QApplication>
#include <QCoreApplication>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>
#include <markoff/core/SessionParams.h>

using namespace Markoff;

static QByteArray fullText(const MarkoffDocument &doc) {
    QByteArray out;
    for (BlockId id : doc.iterateBlocks())
        out += doc.blockText(id);
    return out;
}

class TstMarkoffDocument : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void constructed_with_replica_id() {
        MarkoffDocument doc(42);
        QCOMPARE(doc.replicaId(), quint16(42));
    }

    void empty_document_has_zero_length() {
        MarkoffDocument doc(1);
        QCOMPARE(doc.visibleLength(), quint32(0));
        QVERIFY(doc.toMarkdownUtf8().isEmpty());
        QVERIFY(doc.toMarkdown().isEmpty());
    }

    void replica_ids_independent() {
        MarkoffDocument a(7);
        MarkoffDocument b(13);
        QCOMPARE(a.replicaId(), quint16(7));
        QCOMPARE(b.replicaId(), quint16(13));
    }

    void applyFlatEdit_inserts_text() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello\n");
        doc.applyFlatEdit(5, 5, "!", Origin::UserEdit);
        QCOMPARE(fullText(doc), QByteArray("hello!\n"));
    }

    void applyFlatEdit_replaces_range() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello world\n");
        // "world" = bytes 6..11, trailing \n at byte 11
        doc.applyFlatEdit(6, 11, "there", Origin::UserEdit);
        QCOMPARE(fullText(doc), QByteArray("hello there\n"));
    }

    void applyFlatEdit_deletes_range() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("abcdef\n");
        // Delete "cd" at bytes 2..4
        doc.applyFlatEdit(2, 4, "", Origin::UserEdit);
        QCOMPARE(fullText(doc), QByteArray("abef\n"));
    }

    void applyFlatEdit_emits_d2DocumentChanged() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        QSignalSpy spy(&doc, &MarkoffDocument::d2DocumentChanged);
        doc.applyFlatEdit(5, 5, "!", Origin::UserEdit);
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
    }

    void undoD2_reverses_applyFlatEdit() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("ab\n");
        doc.applyFlatEdit(2, 2, "c", Origin::UserEdit);
        QCOMPARE(fullText(doc), QByteArray("abc\n"));
        doc.undoD2();
        QCOMPARE(fullText(doc), QByteArray("ab\n"));
    }

    void redoD2_reapplies_undone_applyFlatEdit() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("ab\n");
        doc.applyFlatEdit(2, 2, "c", Origin::UserEdit);
        doc.undoD2();
        QCOMPARE(fullText(doc), QByteArray("ab\n"));
        doc.redoD2();
        QCOMPARE(fullText(doc), QByteArray("abc\n"));
    }

    void legacy_undo_with_no_history_returns_nullopt() {
        MarkoffDocument doc(1);
        QVERIFY(!doc.undo().has_value());
    }

    void anchor_at_resolves_to_offset() {
        MarkoffDocument doc(1);
        // resetContent seeds the legacy CRDT buffer used by anchorAt/resolveAnchor
        doc.resetContent(QByteArray("abcdef"), Origin::FirstOpen);
        const auto a = doc.anchorAt(3, CollabText::Crdt::Bias::Left);
        QCOMPARE(doc.resolveAnchor(a), quint32(3));
    }

    void reset_content_first_open_clears_undo() {
        MarkoffDocument doc(1);
        doc.resetContent(QByteArray("old"), Origin::FirstOpen);
        doc.resetContent(QByteArray("new"), Origin::UserRevertToSaved);
        QVERIFY(doc.undoDepth() > 0);
        doc.resetContent(QByteArray("new content"), Origin::FirstOpen);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("new content"));
        QCOMPARE(doc.undoDepth(), 0);
    }

    void reset_content_emits_document_reloaded() {
        MarkoffDocument doc(1);
        QSignalSpy spy(&doc, &MarkoffDocument::documentReloaded);
        doc.resetContent(QByteArray("hello"), Origin::FirstOpen);
        QCOMPARE(spy.count(), 1);
    }

    void reset_content_user_revert_pushes_undo_entry() {
        MarkoffDocument doc(1);
        doc.resetContent(QByteArray("draft"), Origin::FirstOpen);
        const int beforeDepth = doc.undoDepth();
        doc.resetContent(QByteArray("saved"), Origin::UserRevertToSaved);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("saved"));
        QVERIFY(doc.undoDepth() > beforeDepth);
        doc.undo();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("draft"));
    }

    void create_session_returns_owned_session() {
        MarkoffDocument doc(1);
        QSignalSpy spy(&doc, &MarkoffDocument::sessionCreated);
        Session *s = doc.createSession();
        QVERIFY(s != nullptr);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(doc.sessions().size(), 1);
    }

    void create_two_sessions_with_distinct_participants() {
        MarkoffDocument doc(1);
        SessionParams pa; pa.participantId = QStringLiteral("alice");
        SessionParams pb; pb.participantId = QStringLiteral("bob");
        Session *a = doc.createSession(pa);
        Session *b = doc.createSession(pb);
        QCOMPARE(doc.sessions().size(), 2);
        QCOMPARE(doc.sessionForParticipant("alice"), a);
        QCOMPARE(doc.sessionForParticipant("bob"),   b);
    }

    void destroy_session_removes_from_list() {
        MarkoffDocument doc(1);
        Session *s = doc.createSession();
        QSignalSpy spy(&doc, &MarkoffDocument::sessionDestroyed);
        doc.destroySession(s);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(doc.sessions().size(), 0);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    TstMarkoffDocument tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "tst_markoff_document.moc"
