// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff/parser/Document.h>

using TLB = Markoff::TopLevelBlock;

class TstParserListItems : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void tight_ordered_emits_one_block_per_item() {
        const QString src = QStringLiteral("1. one\n2. two\n3. three\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(blocks[i].kind, TLB::Kind::ListItem);
            QCOMPARE(blocks[i].markerStyle, QStringLiteral("dot"));
            QCOMPARE(blocks[i].markerNumber, i + 1);
            QCOMPARE(blocks[i].indentDepth, 0);
            QCOMPARE(blocks[i].looseRun, false);
        }
    }

    void unordered_minus_emits_per_item() {
        const QString src = QStringLiteral("- a\n- b\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].kind, TLB::Kind::ListItem);
        QCOMPARE(blocks[1].kind, TLB::Kind::ListItem);
        QCOMPARE(blocks[0].markerStyle, QStringLiteral("minus"));
        QCOMPARE(blocks[1].markerStyle, QStringLiteral("minus"));
        QCOMPARE(blocks[0].markerNumber, 0);
    }

    void nested_two_deep_indent() {
        const QString src = QStringLiteral(
            "1. one\n"
            "   - sub a\n"
            "   - sub b\n"
            "2. two\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 4);
        QCOMPARE(blocks[0].indentDepth, 0);
        QCOMPARE(blocks[0].markerStyle, QStringLiteral("dot"));
        QCOMPARE(blocks[1].indentDepth, 1);
        QCOMPARE(blocks[1].markerStyle, QStringLiteral("minus"));
        QCOMPARE(blocks[2].indentDepth, 1);
        QCOMPARE(blocks[2].markerStyle, QStringLiteral("minus"));
        QCOMPARE(blocks[3].indentDepth, 0);
        QCOMPARE(blocks[3].markerNumber, 2);
    }

    void task_list_checked_unchecked() {
        const QString src = QStringLiteral(
            "- [ ] one\n"
            "- [x] two\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].markerStyle, QStringLiteral("task"));
        QCOMPARE(blocks[0].checked, false);
        QCOMPARE(blocks[1].markerStyle, QStringLiteral("task"));
        QCOMPARE(blocks[1].checked, true);
    }

    void tight_list_followed_by_another_block_stays_tight() {
        // Dogfood regression 2026-05-21 (Corbomite Vault smoke test):
        // tight lists followed by a blank line + next block were being
        // re-emitted as loose lists on save. Root cause: isListLoose
        // scanned the list node's byte range for any "\n\n" sequence,
        // which catches the blank line that separates the list from the
        // following block — not blank lines between items inside the list.
        const QString src = QStringLiteral(
            "- one\n"
            "- two\n"
            "\n"
            "next paragraph\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(blocks[0].kind, TLB::Kind::ListItem);
        QCOMPARE(blocks[1].kind, TLB::Kind::ListItem);
        QCOMPARE(blocks[0].looseRun, false);
        QCOMPARE(blocks[1].looseRun, false);
    }

    void tight_ordered_list_followed_by_another_block_stays_tight() {
        // Same regression, numbered-list variant matching the dogfood diff
        // in Mike's Obsidian-Based Writing Workflow.md.
        const QString src = QStringLiteral(
            "1. one\n"
            "2. two\n"
            "\n"
            "## next heading\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(blocks[0].kind, TLB::Kind::ListItem);
        QCOMPARE(blocks[1].kind, TLB::Kind::ListItem);
        QCOMPARE(blocks[0].looseRun, false);
        QCOMPARE(blocks[1].looseRun, false);
    }

    void loose_list_marks_all_items_loose() {
        const QString src = QStringLiteral(
            "1. one\n"
            "\n"
            "2. two\n"
            "\n"
            "3. three\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 3);
        for (const auto &b : blocks)
            QCOMPARE(b.looseRun, true);
    }

    void paren_marker() {
        const QString src = QStringLiteral("1) one\n2) two\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].markerStyle, QStringLiteral("paren"));
        QCOMPARE(blocks[0].markerNumber, 1);
        QCOMPARE(blocks[1].markerStyle, QStringLiteral("paren"));
        QCOMPARE(blocks[1].markerNumber, 2);
    }

    void byte_range_is_content_only() {
        // For "1. hello", byteStart..byteEnd should cover "hello" (5 bytes),
        // not "1. hello" (8 bytes). The marker is in attrs, not the buffer.
        const QString src = QStringLiteral("1. hello\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 1);
        const QByteArray bodyUtf8 = src.toUtf8();
        const QByteArray content = bodyUtf8.mid(blocks[0].byteStart,
                                                 blocks[0].byteEnd - blocks[0].byteStart);
        QCOMPARE(content, QByteArrayLiteral("hello"));
    }
};

QTEST_GUILESS_MAIN(TstParserListItems)
#include "tst_parser_list_items.moc"
