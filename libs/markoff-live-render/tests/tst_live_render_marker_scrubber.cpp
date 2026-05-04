// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/live-render/Marker.h>
#include <markoff/live-render/MarkerScrubber.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>

using namespace Markoff;
using namespace Markoff::LiveRender;

class TstMarkerScrubber : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void scrubOnFocusOut_singleMarkerOnly_removesParagraphAndSeparator();
    void scrubOnFocusOut_blockNoLongerMarkerOnly_isNoOp();
    void scrubBeforeSave_runOfMarkers_collapsesAll();
    void scrubBeforeSave_noMarkers_returnsZero();
    void scrubAfterLoad_markersInLoadedSource_areRemoved();
private:
    static QString sourceOf(MarkoffDocument *doc);
};

QString TstMarkerScrubber::sourceOf(MarkoffDocument *doc) {
    return QString::fromUtf8(doc->toMarkdownUtf8());
}

void TstMarkerScrubber::scrubOnFocusOut_singleMarkerOnly_removesParagraphAndSeparator() {
    MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);

    // "alpha\n\n<marker>\n" — two paragraphs
    QByteArray content = QStringLiteral("alpha\n\n%1\n").arg(kMarkerChar).toUtf8();
    QSignalSpy parseSpy(&doc, &MarkoffDocument::parseUpdated);
    doc.resetContent(content, Origin::TestFixture);
    QTRY_COMPARE(binding.model()->rowCount(), 2);

    MarkerScrubber scrubber(&doc, binding.model());
    scrubber.scrubOnFocusOut(/*blockIndex=*/1);

    QTRY_COMPARE(binding.model()->rowCount(), 1);
    QCOMPARE(sourceOf(&doc), QStringLiteral("alpha\n"));
}

void TstMarkerScrubber::scrubOnFocusOut_blockNoLongerMarkerOnly_isNoOp() {
    MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);

    QSignalSpy parseSpy(&doc, &MarkoffDocument::parseUpdated);
    doc.resetContent("alpha\n\nbeta\n", Origin::TestFixture);
    QTRY_COMPARE(binding.model()->rowCount(), 2);

    MarkerScrubber scrubber(&doc, binding.model());
    scrubber.scrubOnFocusOut(/*blockIndex=*/1);

    QCOMPARE(binding.model()->rowCount(), 2);
    QCOMPARE(sourceOf(&doc), QStringLiteral("alpha\n\nbeta\n"));
}

void TstMarkerScrubber::scrubBeforeSave_runOfMarkers_collapsesAll() {
    MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);

    // "alpha\n\n<marker>\n\n<marker>\n\nbeta\n" — four paragraphs
    QByteArray content = QStringLiteral("alpha\n\n%1\n\n%1\n\nbeta\n")
                             .arg(kMarkerChar).toUtf8();
    QSignalSpy parseSpy(&doc, &MarkoffDocument::parseUpdated);
    doc.resetContent(content, Origin::TestFixture);
    QTRY_COMPARE(binding.model()->rowCount(), 4);

    MarkerScrubber scrubber(&doc, binding.model());
    int removed = scrubber.scrubBeforeSave();
    QVERIFY(removed > 0);
    QTRY_COMPARE(binding.model()->rowCount(), 2);
    QCOMPARE(sourceOf(&doc), QStringLiteral("alpha\n\nbeta\n"));
}

void TstMarkerScrubber::scrubBeforeSave_noMarkers_returnsZero() {
    MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);

    QSignalSpy parseSpy(&doc, &MarkoffDocument::parseUpdated);
    doc.resetContent("alpha\n\nbeta\n", Origin::TestFixture);
    QTRY_COMPARE(binding.model()->rowCount(), 2);

    MarkerScrubber scrubber(&doc, binding.model());
    QCOMPARE(scrubber.scrubBeforeSave(), 0);
    QCOMPARE(binding.model()->rowCount(), 2);
}

void TstMarkerScrubber::scrubAfterLoad_markersInLoadedSource_areRemoved() {
    MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);

    MarkerScrubber scrubber(&doc, binding.model());

    // Simulate a load: a file written by another tool that has a marker
    // paragraph (its own block, separated by \n\n from the surrounding content).
    QByteArray content = QStringLiteral("alpha\n\n%1\n\nbeta\n").arg(kMarkerChar).toUtf8();
    QSignalSpy parseSpy(&doc, &MarkoffDocument::parseUpdated);
    doc.resetContent(content, Origin::TestFixture);
    QTRY_VERIFY(binding.model()->rowCount() >= 1);

    int removed = scrubber.scrubAfterLoad();
    QVERIFY(removed > 0);
    QVERIFY(!sourceOf(&doc).contains(kMarkerChar));
}

QTEST_MAIN(TstMarkerScrubber)
#include "tst_live_render_marker_scrubber.moc"
