// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveFormatController.h>

class TestFormatStrikeInlineCode : public QObject {
    Q_OBJECT
private slots:
    void strikethrough_wraps_selection_with_tildes() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.cursorState());
        fc.setModel(binding.model());

        binding.cursorState()->begin(0, 0);
        binding.cursorState()->extend(0, 5);  // "hello"
        fc.toggleStrikethrough();

        const QByteArray blockUtf8 = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(blockUtf8.contains("~~hello~~"),
                 ("blockText was: " + blockUtf8).constData());
        QVERIFY(blockUtf8.contains(" world"));
    }

    void strikethrough_toggle_off_removes_tildes() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.cursorState());
        fc.setModel(binding.model());

        binding.cursorState()->begin(0, 0);
        binding.cursorState()->extend(0, 5);
        fc.toggleStrikethrough();
        // After first toggle, selection now sits inside the wrapped range,
        // not on the markers — re-toggle relies on the wrap-detection logic
        // inside wrapPerBlock. Just verify the unwrap branch by replacing
        // the selection on the post-wrap "hello" range and toggling again.
        // qtPos: "~~hello~~ world" → "hello" at 2..7.
        binding.cursorState()->begin(0, 2);
        binding.cursorState()->extend(0, 7);
        fc.toggleStrikethrough();

        const QByteArray blockUtf8 = doc.blockText(doc.iterateBlocks()[0]);
        QCOMPARE(blockUtf8.left(11), QByteArray("hello world"));
    }

    void inline_code_wraps_selection_with_backticks() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("call foo() here\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.cursorState());
        fc.setModel(binding.model());

        binding.cursorState()->begin(0, 5);
        binding.cursorState()->extend(0, 10);  // "foo()"
        fc.toggleInlineCode();

        const QByteArray blockUtf8 = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(blockUtf8.contains("`foo()`"),
                 ("blockText was: " + blockUtf8).constData());
        QVERIFY(blockUtf8.startsWith("call "));
        QVERIFY(blockUtf8.contains(" here"));
    }

    void inline_code_toggle_off_removes_backticks() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("call foo() here\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.cursorState());
        fc.setModel(binding.model());

        binding.cursorState()->begin(0, 5);
        binding.cursorState()->extend(0, 10);
        fc.toggleInlineCode();
        // qtPos: "call `foo()` here" → "foo()" at 6..11.
        binding.cursorState()->begin(0, 6);
        binding.cursorState()->extend(0, 11);
        fc.toggleInlineCode();

        const QByteArray blockUtf8 = doc.blockText(doc.iterateBlocks()[0]);
        QCOMPARE(blockUtf8.left(15), QByteArray("call foo() here"));
    }
};

QTEST_MAIN(TestFormatStrikeInlineCode)
#include "tst_live_render_format_strike_inline_code.moc"
