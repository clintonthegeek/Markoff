// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "TopLevelBlockScanner.h"

using namespace Markoff::Detail;

class TstFoundationTopLevelBlockScanner : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_input_yields_no_blocks() {
        const auto ranges = scanTopLevelBlockRanges(QByteArray{});
        QCOMPARE(ranges.size(), 0);
    }

    void single_paragraph() {
        const QByteArray src = "hello world";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{11});
    }

    void two_paragraphs_separated_by_blank_line() {
        const QByteArray src = "p1\n\np2";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 2);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{2});  // "p1"
        QCOMPARE(ranges[1].startByte, quint32{4});
        QCOMPARE(ranges[1].endByte,   quint32{6});  // "p2"
    }

    void fenced_code_block_kept_as_single_block() {
        const QByteArray src = "```\nint x;\n```";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{14});
    }

    void heading_then_paragraph_two_blocks() {
        const QByteArray src = "# Heading\n\nparagraph";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 2);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{9});   // "# Heading"
        QCOMPARE(ranges[1].startByte, quint32{11});  // after "\n\n"
        QCOMPARE(ranges[1].endByte,   quint32{20});
    }

    void leading_blank_lines_skipped() {
        const QByteArray src = "\n\nhello";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{2});
        QCOMPARE(ranges[0].endByte,   quint32{7});
    }

    void utf8_multibyte_paragraph() {
        // "héllo" — é is 2 bytes in UTF-8 (0xC3 0xA9). Total 6 bytes.
        const QByteArray src = QByteArray::fromRawData("h\xC3\xA9llo", 6);
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{6});
    }

    void unclosed_fence_extends_to_eof() {
        const QByteArray src = "```\nint x;";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{10});
    }

    void fence_open_with_info_string_then_close_without() {
        // "```python\nfoo\n```" is one block: open + content + close.
        const QByteArray src = "```python\nfoo\n```";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{17});
    }

    void fence_close_must_not_have_info_string() {
        // First "```python" opens. Second "```python" must NOT close
        // (it has an info string). Third "```" does close.
        const QByteArray src = "```python\nfoo\n```python\nbar\n```";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{31});
    }

    void tilde_fences_not_recognized() {
        // BlockWalker only handles backticks. `~~~` is NOT a fence.
        // So this is two paragraphs separated by `~~~`-as-content.
        // Actually: `~~~` is itself non-blank, so the whole thing is
        // ONE non-fence block (one continuous run of non-blank lines).
        const QByteArray src = "first\n~~~\nsecond";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{16});
    }

    void leading_spaces_before_fence_disqualify() {
        // BlockWalker's regex `^```` is anchored — no leading spaces
        // allowed. So "   ```\nfoo\n```" — the first line has 3 leading
        // spaces, NOT a fence. Treated as paragraph until blank or EOF.
        const QByteArray src = "   ```\nfoo\n```";
        const auto ranges = scanTopLevelBlockRanges(src);
        // No blank lines anywhere → all one non-fence block.
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{14});
    }

    void trailing_blank_lines_after_block_dropped() {
        const QByteArray src = "hello\n\n\n";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{5});
    }

    void crlf_line_endings_handled_as_lf_with_cr_in_blank_check() {
        // BlockWalker uses QStringList::split('\n') so CR remains in lines
        // but `trimmed().isEmpty()` treats a CR-only line as blank.
        // The foundation's isBlankLine treats `\r` as blank-character too.
        // So "p1\r\n\r\np2" is two blocks.
        const QByteArray src = "p1\r\n\r\np2";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 2);
        // First block content includes the trailing \r before the \n.
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{3});  // "p1\r"
        QCOMPARE(ranges[1].startByte, quint32{6});
        QCOMPARE(ranges[1].endByte,   quint32{8});  // "p2"
    }
};

QTEST_MAIN(TstFoundationTopLevelBlockScanner)
#include "tst_foundation_top_level_block_scanner.moc"
