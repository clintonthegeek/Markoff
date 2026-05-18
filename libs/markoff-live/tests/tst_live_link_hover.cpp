// SPDX-License-Identifier: GPL-3.0-or-later
#include <QPoint>
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>

#include "RecordingLinkService.h"

using namespace Markoff;
using namespace Markoff::Live;

class TestLiveLinkHover : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void enter_link_emits_hover() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("See [[Page]] then [[Other]].");
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());
        const Markoff::BlockId bid = blockIds[0];

        const QList<Markoff::SourceSpan> spans = doc.inlineSpansFor(bid);
        int posPage = -1;
        for (const auto &s : spans)
            if (s.isWikilink && s.linkTarget.page == QStringLiteral("Page"))
                posPage = s.charOffset + s.charLength / 2;
        QVERIFY(posPage >= 0);

        QVERIFY(binding.hoverLinkAt(bid, posPage, int(Qt::ControlModifier), QPoint(0, 0)));
        QCOMPARE(svc.hovers.size(), 1);
        QCOMPARE(svc.hovers.first().page, QStringLiteral("Page"));
    }

    void same_span_repeated_hover_no_duplicate() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("[[Page]]");
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());
        const Markoff::BlockId bid = blockIds[0];

        const QList<Markoff::SourceSpan> spans = doc.inlineSpansFor(bid);
        int pos = -1;
        for (const auto &s : spans)
            if (s.isWikilink) { pos = s.charOffset + s.charLength / 2; break; }
        QVERIFY(pos >= 0);

        binding.hoverLinkAt(bid, pos,     int(Qt::ControlModifier), QPoint());
        binding.hoverLinkAt(bid, pos + 1, int(Qt::ControlModifier), QPoint());
        QCOMPARE(svc.hovers.size(), 1);
        QCOMPARE(svc.hoverLefts.size(), 0);
    }

    void cross_link_hover_emits_left_then_hover() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("[[Page]] and [[Other]]");
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());
        const Markoff::BlockId bid = blockIds[0];

        const QList<Markoff::SourceSpan> spans = doc.inlineSpansFor(bid);
        int posPage = -1, posOther = -1;
        for (const auto &s : spans) {
            if (s.isWikilink && s.linkTarget.page == QStringLiteral("Page"))
                posPage = s.charOffset + s.charLength / 2;
            if (s.isWikilink && s.linkTarget.page == QStringLiteral("Other"))
                posOther = s.charOffset + s.charLength / 2;
        }
        QVERIFY(posPage >= 0 && posOther >= 0);

        binding.hoverLinkAt(bid, posPage,  int(Qt::ControlModifier), QPoint());
        binding.hoverLinkAt(bid, posOther, int(Qt::ControlModifier), QPoint());

        QCOMPARE(svc.hovers.size(), 2);
        QCOMPARE(svc.hoverLefts.size(), 1);
    }

    void clear_emits_left() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("[[Page]]");
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        LiveTest::RecordingLinkService svc;
        binding.setLinkService(&svc);

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());
        const Markoff::BlockId bid = blockIds[0];

        const QList<Markoff::SourceSpan> spans = doc.inlineSpansFor(bid);
        int pos = -1;
        for (const auto &s : spans)
            if (s.isWikilink) { pos = s.charOffset + s.charLength / 2; break; }
        QVERIFY(pos >= 0);

        binding.hoverLinkAt(bid, pos, int(Qt::ControlModifier), QPoint());
        binding.clearLinkHover();
        QCOMPARE(svc.hoverLefts.size(), 1);
        binding.clearLinkHover();  // idempotent
        QCOMPARE(svc.hoverLefts.size(), 1);
    }
};

QTEST_MAIN(TestLiveLinkHover)
#include "tst_live_link_hover.moc"
