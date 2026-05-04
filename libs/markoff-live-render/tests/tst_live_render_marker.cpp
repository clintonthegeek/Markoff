// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live-render/Marker.h>
#include <markoff/live-render/MarkerScrubber.h>

using namespace Markoff::LiveRender;

class TstMarker : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void constants();
    void predicate_singleMarker_returnsTrue();
    void predicate_markerRun_returnsTrue();
    void predicate_markerWithSoftBreaks_returnsTrue();
    void predicate_markerWithContent_returnsFalse();
    void predicate_emptyString_returnsFalse();
    void predicate_plainContent_returnsFalse();
};

void TstMarker::constants() {
    QCOMPARE(kMarkerChar.unicode(), quint16(0x200B));
    QCOMPARE(QString::fromUtf8(kMarkerUtf8), QString(kMarkerChar));
    QCOMPARE(kMarkerUtf8Len, 3);
}

void TstMarker::predicate_singleMarker_returnsTrue() {
    QVERIFY(MarkerScrubber::isMarkerOnly(QString(kMarkerChar)));
}

void TstMarker::predicate_markerRun_returnsTrue() {
    QString s; s.append(kMarkerChar); s.append(kMarkerChar);
    QVERIFY(MarkerScrubber::isMarkerOnly(s));
}

void TstMarker::predicate_markerWithSoftBreaks_returnsTrue() {
    // Spec §17 open question 1: predicate matches markers + soft-break
    // newlines so a marker paragraph that has been Shift-Enter'd into
    // multiple lines is still recognised as marker-only.
    QString s; s.append(kMarkerChar); s.append('\n'); s.append(kMarkerChar);
    QVERIFY(MarkerScrubber::isMarkerOnly(s));
}

void TstMarker::predicate_markerWithContent_returnsFalse() {
    QString s; s.append(kMarkerChar); s.append('x');
    QVERIFY(!MarkerScrubber::isMarkerOnly(s));
}

void TstMarker::predicate_emptyString_returnsFalse() {
    QVERIFY(!MarkerScrubber::isMarkerOnly(QString()));
}

void TstMarker::predicate_plainContent_returnsFalse() {
    QVERIFY(!MarkerScrubber::isMarkerOnly(QStringLiteral("hello")));
}

QTEST_APPLESS_MAIN(TstMarker)
#include "tst_live_render_marker.moc"
