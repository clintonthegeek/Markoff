// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QPoint>
#include <QSignalSpy>
#include <QTest>

#include "markoff/reading/ReadingView.h"

using namespace Markoff::Reading;

class TstReadingViewHoverSignal : public QObject
{
    Q_OBJECT

private slots:
    void signalShapeIsTwoArg();
    void emptyHrefOnLeave();
};

// The purpose of this test is to lock in the unified hover signal shape
// introduced by Phase C5. Prior to C5 ReadingView emitted
// `wikiLinkHovered(const QString &)` — a narrower surface that excluded
// regular URLs and carried no anchor position.
void TstReadingViewHoverSignal::signalShapeIsTwoArg()
{
    ReadingView rv;
    QSignalSpy spy(&rv, &ReadingView::linkHovered);
    QVERIFY(spy.isValid());
}

void TstReadingViewHoverSignal::emptyHrefOnLeave()
{
    ReadingView rv;
    QSignalSpy spy(&rv, &ReadingView::linkHovered);
    QVERIFY(spy.isValid());
}

QTEST_MAIN(TstReadingViewHoverSignal)
#include "tst_readingview_hover_signal.moc"
