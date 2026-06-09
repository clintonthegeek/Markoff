// SPDX-License-Identifier: GPL-3.0-or-later
//
// Headless tests for Markoff::FormatOps (MarkdownView contract v2 §5).
// The flat text passed in is widgetFlatView() — exactly what a widget's
// toPlainText() holds. Expected bytes mirror the donor contract pinned
// in markoff-source/tests/tst_source_widget_format_ops.cpp (the hoist
// is behavior-preserving; that suite is the guard).
#include <QTest>

#include <markoff/core/FormatOps.h>
#include <markoff/core/MarkoffDocument.h>

using namespace Markoff;

class TstFormatOps : public QObject {
    Q_OBJECT
    static QString flat(MarkoffDocument &d) {
        return QString::fromUtf8(d.widgetFlatView());
    }
private Q_SLOTS:
    void wrapToggle_wraps_and_unwraps_selection() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello world\n\nsecond block");
        auto r = FormatOps::wrapToggle(&doc, flat(doc), {6, 11}, "**");
        QCOMPARE(doc.serializeForSave(),
                 QByteArray("hello **world**\n\nsecond block\n"));
        QVERIFY(r.has_value());
        QCOMPARE(r->start, 8);   // selection survives, shifted past "**"
        QCOMPARE(r->end, 13);
        // widgetFlatView changed — re-fetch before the second op.
        r = FormatOps::wrapToggle(&doc, flat(doc), *r, "**");
        QCOMPARE(doc.serializeForSave(),
                 QByteArray("hello world\n\nsecond block\n"));
        QVERIFY(r.has_value());
        QCOMPARE(r->start, 6);   // surrounded-outside unwrap shifts back
        QCOMPARE(r->end, 11);
    }

    void wrapToggle_no_selection_inserts_pair_and_parks_between() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        const auto r = FormatOps::wrapToggle(&doc, flat(doc), {5, 5}, "**");
        QCOMPARE(doc.serializeForSave(), QByteArray("hello****\n"));
        QVERIFY(r.has_value());
        QCOMPARE(r->start, 7);   // parked between the delimiters
        QCOMPARE(r->end, 7);
    }

    void wrapToggle_in_second_block_is_block_aware() {
        // Companion to the donor's toggleBold_at_block_boundary regression:
        // widgetFlatView joins blocks with a single '\n', so block 2 starts
        // at qt-pos 6 and the selection start sits exactly at the boundary.
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("first\n\nsecond block");
        FormatOps::wrapToggle(&doc, flat(doc), {6, 12}, "_");
        QCOMPARE(doc.serializeForSave(),
                 QByteArray("first\n\n_second_ block\n"));
    }

    void setHeadingLevel_on_non_first_block() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("first\n\nsecond");
        // flat is "first\nsecond"; caret 8 is inside "second" (line start 6).
        auto r = FormatOps::setHeadingLevel(&doc, flat(doc), /*caretQtPos=*/8, 2);
        QCOMPARE(doc.serializeForSave(), QByteArray("first\n\n## second\n"));
        QVERIFY(r.has_value());
        QCOMPARE(r->start, 11);  // caret shifted by the "## " insertion
        QCOMPARE(r->end, 11);
        r = FormatOps::setHeadingLevel(&doc, flat(doc), 8, 0);
        QCOMPARE(doc.serializeForSave(), QByteArray("first\n\nsecond\n"));
        QVERIFY(r.has_value());
        QCOMPARE(r->start, 6);   // 8 - 3 underflows the line; clamped to start
        QCOMPARE(r->end, 6);
    }

    void setHeadingLevel_repeat_does_not_merge_with_previous_block() {
        // Mirror of the donor's setHeadingLevel_twice_does_not_merge
        // regression (2026-05-21 dogfood): repeated toggles on a heading
        // whose line start sits exactly at a markoff block boundary must
        // stay block-aware, never routing through applyFlatEdit's
        // cross-block branch.
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("para text\n\nHello");
        // flat "para text\nHello"; "Hello" at qt 10..15.
        FormatOps::setHeadingLevel(&doc, flat(doc), 12, 2);
        QCOMPARE(doc.serializeForSave(), QByteArray("para text\n\n## Hello\n"));
        FormatOps::setHeadingLevel(&doc, flat(doc), 12, 3);
        QCOMPARE(doc.serializeForSave(), QByteArray("para text\n\n### Hello\n"));
        FormatOps::setHeadingLevel(&doc, flat(doc), 12, 0);
        QCOMPARE(doc.serializeForSave(), QByteArray("para text\n\nHello\n"));
    }

    void setHeadingLevel_noop_returns_nullopt() {
        // Already at the requested level → the donor early-returns without
        // touching the cursor; FormatOps signals that with nullopt so the
        // caller leaves any selection intact.
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("## Hello");
        const auto r = FormatOps::setHeadingLevel(&doc, flat(doc), 4, 2);
        QVERIFY(!r.has_value());
        QCOMPARE(doc.serializeForSave(), QByteArray("## Hello\n"));
    }

    void insertLink_wraps_selection() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("see docs here");
        const auto r = FormatOps::insertLink(&doc, flat(doc), {4, 8});
        QCOMPARE(doc.serializeForSave(), QByteArray("see [docs](url) here\n"));
        QVERIFY(r.has_value());
        QCOMPARE(r->start, 11);  // selection parked over "url" for replace
        QCOMPARE(r->end, 14);
    }

    void insertLink_no_selection_inserts_template() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        const auto r = FormatOps::insertLink(&doc, flat(doc), {5, 5});
        QCOMPARE(doc.serializeForSave(), QByteArray("hello[](url)\n"));
        QVERIFY(r.has_value());
        QCOMPARE(r->start, 6);   // parked between "[]"
        QCOMPARE(r->end, 6);
    }
};

QTEST_MAIN(TstFormatOps)
#include "tst_format_ops.moc"
