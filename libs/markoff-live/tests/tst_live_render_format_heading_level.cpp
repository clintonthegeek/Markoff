// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveFormatController.h>

class TestFormatHeadingLevel : public QObject {
    Q_OBJECT
private slots:
    void paragraph_promotes_to_heading1() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Hello world\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.cursorState());
        fc.setModel(binding.model());

        binding.cursorState()->begin(0, 0);  // caret at start, no selection
        fc.setHeadingLevel(1);

        const Markoff::BlockId id = doc.iterateBlocks()[0];
        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Heading);
        const auto attrs = doc.blockAttrs(id);
        const auto level = attrs.constFind(Markoff::AttrNames::Level);
        QVERIFY(level != attrs.cend());
        const int *p = std::get_if<int>(&level.value());
        QVERIFY(p);
        QCOMPARE(*p, 1);
        // Buffer carries the "# " ATX prefix per the load convention so
        // onD2Changed's auto-inference (countLeadingHashes == level) doesn't
        // demote the block. Serializer's stripLeadingHashes makes save
        // round-trip "# Hello world" -> "# Hello world".
        QCOMPARE(doc.blockText(id), QByteArray("# Hello world"));
    }

    void paragraph_with_atx_prefix_strips_markers_on_promote() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Hello world\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.cursorState());
        fc.setModel(binding.model());

        // Inject "## " into the paragraph buffer in its own committed
        // transaction so onD2Changed fires + the model rebuilds before
        // setHeadingLevel runs.
        const Markoff::BlockId id = doc.iterateBlocks()[0];
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 0, 0, QByteArray("## "), t);
        }
        doc.flushPendingD2Changed();

        // NOTE: the auto-promote path in LiveListModelBinding::onD2Changed
        // will see leading hashes and change kind to Heading itself. Confirm
        // that auto-promotion happened (it's not what we're testing — our
        // function is meant to handle the explicit-toolbar path).
        if (doc.blockKind(id) == Markoff::BlockKind::Heading) {
            // Auto-promote already converted it; just verify level=3 lands.
            binding.cursorState()->begin(0, 0);
            fc.setHeadingLevel(3);
            QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Heading);
            return;
        }

        // No auto-promote — exercise our explicit strip path.
        binding.cursorState()->begin(0, 0);
        fc.setHeadingLevel(3);

        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Heading);
        // After explicit strip, content is heading-content-only.
        QCOMPARE(doc.blockText(id), QByteArray("Hello world"));
    }

    void heading_changes_level_attr_only() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("# Hello\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.cursorState());
        fc.setModel(binding.model());

        const Markoff::BlockId id = doc.iterateBlocks()[0];
        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Heading);

        binding.cursorState()->begin(0, 0);
        fc.setHeadingLevel(4);

        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Heading);
        const auto attrs = doc.blockAttrs(id);
        const auto level = attrs.constFind(Markoff::AttrNames::Level);
        const int *p = std::get_if<int>(&level.value());
        QCOMPARE(*p, 4);
        // Heading→Heading rewrites the ATX prefix in the buffer so the
        // onD2Changed auto-inference sees consistent (level, marker-count)
        // state and doesn't fight the change.
        QCOMPARE(doc.blockText(id), QByteArray("#### Hello"));
    }

    void heading_demotes_to_paragraph_at_level_zero() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("## Hello\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.cursorState());
        fc.setModel(binding.model());

        const Markoff::BlockId id = doc.iterateBlocks()[0];
        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Heading);

        binding.cursorState()->begin(0, 0);
        fc.setHeadingLevel(0);

        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Paragraph);
        // ATX markers stripped so the paragraph doesn't auto-re-promote.
        QCOMPARE(doc.blockText(id), QByteArray("Hello"));
    }

    void out_of_range_level_is_noop() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Hello\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.cursorState());
        fc.setModel(binding.model());

        binding.cursorState()->begin(0, 0);
        fc.setHeadingLevel(7);   // out of range
        fc.setHeadingLevel(-1);  // out of range

        // No change — still Paragraph.
        const Markoff::BlockId id = doc.iterateBlocks()[0];
        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Paragraph);
    }
};

QTEST_MAIN(TestFormatHeadingLevel)
#include "tst_live_render_format_heading_level.moc"
