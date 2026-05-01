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
};

QTEST_MAIN(TstFoundationTopLevelBlockScanner)
#include "tst_foundation_top_level_block_scanner.moc"
