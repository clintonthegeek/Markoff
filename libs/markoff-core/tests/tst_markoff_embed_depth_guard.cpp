// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#include "markoff/EmbedDepthGuard.h"

#include <QtTest/QtTest>

using namespace Markoff;

class tst_MarkoffEmbedDepthGuard : public QObject
{
    Q_OBJECT

private slots:
    void allowsBelowMaxDepth();
    void blocksAtOrAboveMaxDepth();
    void placeholderRoundTrip();
    void placeholderTargetRejectsNonPlaceholder();
};

void tst_MarkoffEmbedDepthGuard::allowsBelowMaxDepth()
{
    EmbedDepthGuard guard;
    for (int d = 0; d < EmbedDepthGuard::kMaxDepth; ++d)
        QVERIFY(guard.allow(d));
}

void tst_MarkoffEmbedDepthGuard::blocksAtOrAboveMaxDepth()
{
    EmbedDepthGuard guard;
    QVERIFY(!guard.allow(EmbedDepthGuard::kMaxDepth));
    QVERIFY(!guard.allow(EmbedDepthGuard::kMaxDepth + 1));
}

void tst_MarkoffEmbedDepthGuard::placeholderRoundTrip()
{
    const QString target = QStringLiteral("note.md#heading");
    const QString ph = EmbedDepthGuard::placeholder(target);
    QCOMPARE(EmbedDepthGuard::placeholderTarget(ph), target);
}

void tst_MarkoffEmbedDepthGuard::placeholderTargetRejectsNonPlaceholder()
{
    QCOMPARE(EmbedDepthGuard::placeholderTarget(QStringLiteral("plain")),
             QString{});
    QCOMPARE(EmbedDepthGuard::placeholderTarget(
                 QStringLiteral("[embed depth cap: x")),
             QString{});
}

QTEST_MAIN(tst_MarkoffEmbedDepthGuard)
#include "tst_markoff_embed_depth_guard.moc"
