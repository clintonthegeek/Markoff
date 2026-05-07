// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/AttrNames.h>

using namespace Markoff;

class TstD2ListRoundtrip : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void tight_ordered_one_block_per_item() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src = "1. one\n2. two\n3. three\n";
        doc.loadFromMarkdown(src);

        const auto ids = doc.iterateBlocks();
        QCOMPARE(static_cast<int>(ids.size()), 3);

        for (int i = 0; i < 3; ++i) {
            QCOMPARE(doc.blockKind(ids[i]), BlockKind::ListItem);
            const auto attrs = doc.blockAttrs(ids[i]);
            QCOMPARE(std::get<QString>(attrs.value(AttrNames::MarkerStyle)),
                     QStringLiteral("dot"));
            QCOMPARE(std::get<int>(attrs.value(AttrNames::MarkerNumber)),
                     i + 1);
            QCOMPARE(std::get<int>(attrs.value(AttrNames::IndentLevel)), 0);
            QCOMPARE(std::get<bool>(attrs.value(AttrNames::LooseRun)), false);
        }

        // Buffer text is content-only (no marker, no leading whitespace)
        QCOMPARE(doc.blockText(ids[0]), QByteArrayLiteral("one"));
        QCOMPARE(doc.blockText(ids[1]), QByteArrayLiteral("two"));
        QCOMPARE(doc.blockText(ids[2]), QByteArrayLiteral("three"));
    }

    void roundtrip_tight_ordered() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src = "1. one\n2. two\n3. three\n";
        doc.loadFromMarkdown(src);
        QCOMPARE(doc.serializeForSave(), src);
    }

    void roundtrip_unordered_minus() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src = "- a\n- b\n- c\n";
        doc.loadFromMarkdown(src);
        QCOMPARE(doc.serializeForSave(), src);
    }

    void roundtrip_nested() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src =
            "1. one\n"
            "   - sub a\n"
            "   - sub b\n"
            "2. two\n";
        doc.loadFromMarkdown(src);
        QCOMPARE(doc.serializeForSave(), src);
    }

    void roundtrip_task_list() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src = "- [ ] one\n- [x] two\n";
        doc.loadFromMarkdown(src);
        QCOMPARE(doc.serializeForSave(), src);
    }

    void roundtrip_loose_ordered() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src = "1. one\n\n2. two\n\n3. three\n";
        doc.loadFromMarkdown(src);
        QCOMPARE(doc.serializeForSave(), src);
    }
};

QTEST_GUILESS_MAIN(TstD2ListRoundtrip)
#include "tst_d2_list_roundtrip.moc"
