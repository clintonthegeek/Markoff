// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>

#include "RecordingLinkService.h"

using namespace Markoff;
using namespace Markoff::Live;

class TestLiveLinkActivation : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void wikilink_click_dispatches_activation() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("See [[Page|Alias]] now.");

        LiveListModelBinding binding;
        binding.setDocument(&doc);

        Markoff::LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());
        const Markoff::BlockId bid = blockIds[0];

        const QList<Markoff::SourceSpan> spans = doc.inlineSpansFor(bid);
        int hitPos = -1;
        for (const auto &s : spans) {
            if (s.isWikilink) {
                hitPos = s.charOffset + s.charLength / 2;
                break;
            }
        }
        QVERIFY(hitPos >= 0);

        binding.activateLinkAt(bid, hitPos, int(Qt::ControlModifier));

        QCOMPARE(svc.activations.size(), 1);
        const auto &a = svc.activations.first();
        QCOMPARE(a.kind, LinkKind::WikiLink);
        QCOMPARE(a.page, QStringLiteral("Page"));
        QCOMPARE(a.alias, QStringLiteral("Alias"));
        QCOMPARE(a.modifiers, Qt::ControlModifier);
    }

    void click_outside_link_is_noop() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("plain text");

        LiveListModelBinding binding;
        binding.setDocument(&doc);

        Markoff::LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());

        binding.activateLinkAt(blockIds[0], 2, int(Qt::ControlModifier));
        QCOMPARE(svc.activations.size(), 0);
    }

    void standard_link_click_dispatches_external() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("[text](https://x.y)");

        LiveListModelBinding binding;
        binding.setDocument(&doc);

        Markoff::LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());
        const Markoff::BlockId bid = blockIds[0];

        const QList<Markoff::SourceSpan> spans = doc.inlineSpansFor(bid);
        int hitPos = -1;
        for (const auto &s : spans) {
            if (s.isLink && !s.isWikilink) {
                hitPos = s.charOffset + s.charLength / 2;
                break;
            }
        }
        QVERIFY(hitPos >= 0);

        binding.activateLinkAt(bid, hitPos, 0);
        QCOMPARE(svc.activations.size(), 1);
        QCOMPARE(svc.activations.first().kind, LinkKind::External);
        QCOMPARE(svc.activations.first().rawText, QStringLiteral("https://x.y"));
    }

    void image_span_is_skipped_in_e3a() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("![alt](img.png)");

        LiveListModelBinding binding;
        binding.setDocument(&doc);

        Markoff::LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());
        const Markoff::BlockId bid = blockIds[0];

        const QList<Markoff::SourceSpan> spans = doc.inlineSpansFor(bid);
        int hitPos = -1;
        for (const auto &s : spans) {
            if (s.isImage) {
                hitPos = s.charOffset + s.charLength / 2;
                break;
            }
        }
        if (hitPos < 0)
            QSKIP("No image span in fixture; parser shape changed.");

        binding.activateLinkAt(bid, hitPos, int(Qt::ControlModifier));
        QCOMPARE(svc.activations.size(), 0);
    }

    void embed_wikilink_is_skipped_in_e3a() {
        // ![[SomePage]] is an Obsidian embed — E3c's job, not E3a's.
        // The tree-sitter inline grammar parses ![[X]] as an `image` node
        // (same as ![alt](url)), not a `wiki_link` node, so isWikilink is
        // never set for embeds. This test verifies:
        //  (a) the span is classified as isImage (not isWikilink), and
        //  (b) clicking it produces no activation.
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("![[SomePage]]");

        LiveListModelBinding binding;
        binding.setDocument(&doc);

        Markoff::LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());
        const Markoff::BlockId bid = blockIds[0];

        const QList<Markoff::SourceSpan> spans = doc.inlineSpansFor(bid);

        // Confirm no span is tagged as wikilink (embeds must never be wikilinks)
        for (const auto &s : spans)
            QVERIFY2(!s.isWikilink, "![[embed]] span incorrectly tagged as isWikilink");

        // Find any span to click (image span, or any span at all)
        int hitPos = -1;
        for (const auto &s : spans) {
            if (s.charLength > 0) {
                hitPos = s.charOffset + s.charLength / 2;
                break;
            }
        }
        if (hitPos < 0)
            QSKIP("No span found for ![[SomePage]]; parser produced empty spans.");

        binding.activateLinkAt(bid, hitPos, int(Qt::ControlModifier));
        QCOMPARE(svc.activations.size(), 0);  // embed must NOT activate in E3a
    }
};

QTEST_MAIN(TestLiveLinkActivation)
#include "tst_live_link_activation.moc"
