// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/FindController.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

using namespace Markoff;

namespace {
class DocumentFixture {
public:
    DocumentFixture(const QString& key, const QByteArray& source)
    {
        auto providerResult = VNext::makeEphemeralDocumentStorageProvider();
        if (!providerResult.ok()) throw std::runtime_error("provider creation failed");
        m_provider = std::move(providerResult).value();
        auto opened = MarkoffDocument::open(*m_provider, {key, 1}, VNext::StorageMode::ephemeral);
        if (!opened.ok()) throw std::runtime_error("document open failed");
        m_document = std::move(opened).value();
        if (m_document->importInitialMarkdown(source).state
            != VNext::ActionPublicationState::committed)
            throw std::runtime_error("initial Markdown import failed");
    }

    MarkoffDocument& document()
    {
        return *m_document;
    }

private:
    std::unique_ptr<VNext::DocumentStorageProvider> m_provider;
    std::unique_ptr<MarkoffDocument> m_document;
};
}

class TstFoundationFindController : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<Markoff::FindController::Match>(
            "Markoff::FindController::Match");
    }

    void inactive_setNeedle_does_not_compute_matches() {
        DocumentFixture fixture(QStringLiteral("find-inactive"), QByteArrayLiteral("Find me here\n"));
        auto& doc = fixture.document();
        FindController fc(&doc);
        fc.setNeedle("Find");
        QCOMPARE(fc.matchCount(), 0);
        QCOMPARE(fc.currentMatchIndex(), -1);
    }

    void activate_with_empty_needle_emits_no_matches() {
        DocumentFixture fixture(QStringLiteral("find-empty"), QByteArrayLiteral("hello\n"));
        auto& doc = fixture.document();
        FindController fc(&doc);
        QSignalSpy matchesSpy(&fc, &FindController::matchesChanged);
        fc.activate();
        QVERIFY(fc.isActive());
        QCOMPARE(fc.matchCount(), 0);
        // matchesChanged emitted once on activate (transition to empty matches).
        QCOMPARE(matchesSpy.count(), 1);
    }

    void setNeedle_while_active_populates_matches_case_insensitively() {
        DocumentFixture fixture(QStringLiteral("find-case"), QByteArrayLiteral("Foo FOO foo bar foo\n"));
        auto& doc = fixture.document();
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("foo");
        QCOMPARE(fc.matchCount(), 4);
        QCOMPARE(fc.currentMatchIndex(), 0);
    }

    void matches_span_multiple_blocks_in_document_order() {
        DocumentFixture fixture(QStringLiteral("find-blocks"), QByteArrayLiteral(
            "First paragraph has find here.\n\n"
            "Second paragraph has find twice find.\n\n"
            "Third paragraph: nothing.\n"));
        auto& doc = fixture.document();
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("find");
        QCOMPARE(fc.matchCount(), 3);
    }

    void findNext_wraps() {
        DocumentFixture fixture(QStringLiteral("find-next"), QByteArrayLiteral("alpha alpha alpha\n"));
        auto& doc = fixture.document();
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("alpha");
        QCOMPARE(fc.currentMatchIndex(), 0);
        fc.findNext(); QCOMPARE(fc.currentMatchIndex(), 1);
        fc.findNext(); QCOMPARE(fc.currentMatchIndex(), 2);
        fc.findNext(); QCOMPARE(fc.currentMatchIndex(), 0);
    }

    void findPrevious_wraps() {
        DocumentFixture fixture(QStringLiteral("find-prev"), QByteArrayLiteral("beta beta beta\n"));
        auto& doc = fixture.document();
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("beta");
        fc.findPrevious(); QCOMPARE(fc.currentMatchIndex(), 2);
        fc.findPrevious(); QCOMPARE(fc.currentMatchIndex(), 1);
    }

    void deactivate_clears_matches_and_index() {
        DocumentFixture fixture(QStringLiteral("find-deactivate"), QByteArrayLiteral("gamma gamma\n"));
        auto& doc = fixture.document();
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
        DocumentFixture fixture(QStringLiteral("find-no-navigation"), QByteArrayLiteral("zeta zeta\n"));
        auto& doc = fixture.document();
        FindController fc(&doc);
        QSignalSpy navSpy(&fc, &FindController::navigationRequested);
        fc.activate();
        fc.setNeedle("zeta");
        QCOMPARE(navSpy.count(), 0);  // typing must never seek
    }

    void findNext_emits_navigationRequested_with_match() {
        DocumentFixture fixture(QStringLiteral("find-nav"), QByteArrayLiteral("eta eta eta\n"));
        auto& doc = fixture.document();
        FindController fc(&doc);
        QSignalSpy navSpy(&fc, &FindController::navigationRequested);
        fc.activate();
        fc.setNeedle("eta");
        QCOMPARE(navSpy.count(), 0);
        fc.findNext();
        QCOMPARE(navSpy.count(), 1);
    }

    void selectMatchAtOrAfter_lands_on_first_at_or_after() {
        DocumentFixture fixture(QStringLiteral("find-select"), QByteArrayLiteral("aa bb aa bb aa\n"));
        auto& doc = fixture.document();
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
        DocumentFixture fixture(QStringLiteral("find-wrap"), QByteArrayLiteral("aa aa\n"));
        auto& doc = fixture.document();
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
