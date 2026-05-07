// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>
#include <markoff-parser/SourceSpan.h>

using namespace Markoff;

class TstD2InlineCache : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void firstRead_parsesAndCaches();
    void secondRead_returnsFromCache();
    void editIncrementsCounter_invalidatesCache();
    void onApplyBlockEdit_inlineSpansChangedSignalFires();
};

void TstD2InlineCache::firstRead_parsesAndCaches()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello **world**\n");
    auto blocks = doc.iterateBlocks();
    QVERIFY(!blocks.empty());
    BlockId blk = blocks.front();
    auto spans = doc.inlineSpansFor(blk);
    QVERIFY(!spans.isEmpty());
}

void TstD2InlineCache::secondRead_returnsFromCache()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello **world**\n");
    auto blocks = doc.iterateBlocks();
    QVERIFY(!blocks.empty());
    BlockId blk = blocks.front();
    auto first  = doc.inlineSpansFor(blk);
    auto second = doc.inlineSpansFor(blk);
    QCOMPARE(first.size(), second.size());
}

void TstD2InlineCache::editIncrementsCounter_invalidatesCache()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello\n");
    auto blocks = doc.iterateBlocks();
    QVERIFY(!blocks.empty());
    BlockId blk = blocks.front();
    auto before = doc.blockEditSequence(blk);
    doc.inlineSpansFor(blk);  // warm the cache
    // Edit the block
    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2ApplyBufferEdit(blk, 5, 0, "!", t);
    // After edit, edit sequence increments
    auto after = doc.blockEditSequence(blk);
    QVERIFY(after > before);
    // Verify spans can still be retrieved (no crash, re-parses cleanly)
    auto spans = doc.inlineSpansFor(blk);
    QVERIFY(!spans.isEmpty());
}

void TstD2InlineCache::onApplyBlockEdit_inlineSpansChangedSignalFires()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello\n");
    auto blocks = doc.iterateBlocks();
    QVERIFY(!blocks.empty());
    BlockId blk = blocks.front();
    auto *proxy = doc.bufferProxy(blk);
    QVERIFY(proxy);
    QSignalSpy spy(proxy, &Markoff::BufferProxy::inlineSpansChanged);
    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2ApplyBufferEdit(blk, 5, 0, "!", t);
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TstD2InlineCache)
#include "tst_d2_inline_cache.moc"
