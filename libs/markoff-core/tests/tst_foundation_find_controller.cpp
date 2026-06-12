// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/FindController.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

using namespace Markoff;

class TstFoundationFindController : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<Markoff::FindController::Match>(
            "Markoff::FindController::Match");
    }

    void inactive_setNeedle_does_not_compute_matches() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("Find me here\n");
        FindController fc(&doc);
        fc.setNeedle("Find");
        QCOMPARE(fc.matchCount(), 0);
        QCOMPARE(fc.currentMatchIndex(), -1);
    }

    void activate_with_empty_needle_emits_no_matches() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello\n");
        FindController fc(&doc);
        QSignalSpy matchesSpy(&fc, &FindController::matchesChanged);
        fc.activate();
        QVERIFY(fc.isActive());
        QCOMPARE(fc.matchCount(), 0);
        // matchesChanged emitted once on activate (transition to empty matches).
        QCOMPARE(matchesSpy.count(), 1);
    }

    void setNeedle_while_active_populates_matches_case_insensitively() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("Foo FOO foo bar foo\n");
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("foo");
        QCOMPARE(fc.matchCount(), 4);
        QCOMPARE(fc.currentMatchIndex(), 0);
    }

    void matches_span_multiple_blocks_in_document_order() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(
            "First paragraph has find here.\n\n"
            "Second paragraph has find twice find.\n\n"
            "Third paragraph: nothing.\n");
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("find");
        QCOMPARE(fc.matchCount(), 3);
    }

    void findNext_wraps() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("alpha alpha alpha\n");
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("alpha");
        QCOMPARE(fc.currentMatchIndex(), 0);
        fc.findNext(); QCOMPARE(fc.currentMatchIndex(), 1);
        fc.findNext(); QCOMPARE(fc.currentMatchIndex(), 2);
        fc.findNext(); QCOMPARE(fc.currentMatchIndex(), 0);
    }

    void findPrevious_wraps() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("beta beta beta\n");
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("beta");
        fc.findPrevious(); QCOMPARE(fc.currentMatchIndex(), 2);
        fc.findPrevious(); QCOMPARE(fc.currentMatchIndex(), 1);
    }

    void deactivate_clears_matches_and_index() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("gamma gamma\n");
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("gamma");
        QVERIFY(fc.matchCount() > 0);
        fc.deactivate();
        QCOMPARE(fc.matchCount(), 0);
        QCOMPARE(fc.currentMatchIndex(), -1);
        QVERIFY(!fc.isActive());
    }

    void setNeedle_emits_no_navigationRequested() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("zeta zeta\n");
        FindController fc(&doc);
        QSignalSpy navSpy(&fc, &FindController::navigationRequested);
        fc.activate();
        fc.setNeedle("zeta");
        QCOMPARE(navSpy.count(), 0);  // typing must never seek
    }

    void findNext_emits_navigationRequested_with_match() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("eta eta eta\n");
        FindController fc(&doc);
        QSignalSpy navSpy(&fc, &FindController::navigationRequested);
        fc.activate();
        fc.setNeedle("eta");
        QCOMPARE(navSpy.count(), 0);
        fc.findNext();
        QCOMPARE(navSpy.count(), 1);
    }

    void selectMatchAtOrAfter_lands_on_first_at_or_after() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("aa bb aa bb aa\n");  // "aa" at byte 0, 6, 12
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("aa");
        QCOMPARE(fc.matchCount(), 3);
        const auto block = fc.matches().at(0).block;
        fc.selectMatchAtOrAfter(block, 6);
        QCOMPARE(fc.currentMatchIndex(), 1);
        fc.selectMatchAtOrAfter(block, 7);   // first >= 7 is the match at 12
        QCOMPARE(fc.currentMatchIndex(), 2);
    }
    void selectMatchAtOrAfter_wraps_to_zero() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("aa aa\n");
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("aa");
        const auto block = fc.matches().at(0).block;
        fc.selectMatchAtOrAfter(block, 9999);  // past end → wrap
        QCOMPARE(fc.currentMatchIndex(), 0);
    }
};

QTEST_MAIN(TstFoundationFindController)
#include "tst_foundation_find_controller.moc"
