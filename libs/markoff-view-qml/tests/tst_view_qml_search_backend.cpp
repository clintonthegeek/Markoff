// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/SearchEngine.h>
#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/SearchBackend.h>

using namespace Markoff::View::Qml;

class TstViewQmlSearchBackend : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void default_constructed_state() {
        SearchBackend sb;
        QCOMPARE(sb.editorBackend(), nullptr);
        QCOMPARE(sb.needle(), QString());
        QCOMPARE(sb.flags(), int(Markoff::SearchEngine::NoFlags));
        QCOMPARE(sb.matchCount(), 0);
    }

    void find_all_with_no_backend_returns_zero() {
        SearchBackend sb;
        sb.setNeedle("foo");
        QCOMPARE(sb.findAll(), 0);
    }

    void find_all_returns_match_count() {
        Markoff::MarkoffDocument doc(1);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArray("the quick brown fox jumps over the lazy fox");
        doc.applyLocalEdit({ ed });

        EditorBackend backend;
        backend.setDocument(&doc);
        SearchBackend sb;
        sb.setEditorBackend(&backend);
        sb.setNeedle("fox");

        QSignalSpy spy(&sb, &SearchBackend::matchCountChanged);
        const int n = sb.findAll();
        QCOMPARE(n, 2);
        QCOMPARE(sb.matchCount(), 2);
        QVERIFY(spy.count() >= 1);
    }

    void find_next_advances_and_find_prev_retreats() {
        Markoff::MarkoffDocument doc(1);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArray("the quick brown fox jumps over the lazy fox");
        doc.applyLocalEdit({ ed });

        EditorBackend backend;
        backend.setDocument(&doc);
        SearchBackend sb;
        sb.setEditorBackend(&backend);
        sb.setNeedle("fox");
        QCOMPARE(sb.findAll(), 2);

        QVERIFY(sb.findNext());
        QVERIFY(sb.findNext());
        QVERIFY(sb.findPrevious());
    }

    void clear_resets_match_count() {
        Markoff::MarkoffDocument doc(1);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArray("foo bar foo");
        doc.applyLocalEdit({ ed });

        EditorBackend backend;
        backend.setDocument(&doc);
        SearchBackend sb;
        sb.setEditorBackend(&backend);
        sb.setNeedle("foo");
        sb.findAll();
        QCOMPARE(sb.matchCount(), 2);

        sb.clear();
        QCOMPARE(sb.matchCount(), 0);
    }

    void case_sensitive_flag_changes_results() {
        Markoff::MarkoffDocument doc(1);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArray("Foo foo FOO");
        doc.applyLocalEdit({ ed });

        EditorBackend backend;
        backend.setDocument(&doc);
        SearchBackend sb;
        sb.setEditorBackend(&backend);
        sb.setNeedle("foo");

        sb.setFlags(int(Markoff::SearchEngine::NoFlags));
        const int caseInsens = sb.findAll();
        sb.setFlags(int(Markoff::SearchEngine::CaseSensitive));
        const int caseSens = sb.findAll();

        QVERIFY(caseInsens > caseSens);
    }
};

QTEST_MAIN(TstViewQmlSearchBackend)
#include "tst_view_qml_search_backend.moc"
