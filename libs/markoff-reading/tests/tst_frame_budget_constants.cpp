// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins the three Obsidian-documented reading-pipeline contract constants.
// These values are NOT hints — they are the wire contract, so they must not
// drift without updating the audit docs + all cross-compat tests.

#include "markoff/reading/ReadingViewConstants.h"

#include <QTest>

class TestFrameBudgetConstants : public QObject
{
    Q_OBJECT

private slots:
    void asyncParseThresholdIsExactly10240Bytes();
    void frameBudgetIsExactly5Milliseconds();
    void frameBudgetIsExactly10Sections();
};

void TestFrameBudgetConstants::asyncParseThresholdIsExactly10240Bytes()
{
    QCOMPARE(Markoff::Reading::kAsyncParseThresholdBytes, 10240);
}

void TestFrameBudgetConstants::frameBudgetIsExactly5Milliseconds()
{
    QCOMPARE(Markoff::Reading::kFrameBudgetMs, 5);
}

void TestFrameBudgetConstants::frameBudgetIsExactly10Sections()
{
    QCOMPARE(Markoff::Reading::kFrameBudgetSections, 10);
}

QTEST_MAIN(TestFrameBudgetConstants)
#include "tst_frame_budget_constants.moc"
