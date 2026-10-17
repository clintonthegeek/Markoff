// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/FormatOps.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>

#include <memory>
#include <stdexcept>

using namespace Markoff;

namespace {
struct Fixture {
    std::unique_ptr<VNext::DocumentStorageProvider> provider;
    std::unique_ptr<MarkoffDocument> document;
};

Fixture open(const QString& key, const QByteArray& markdown)
{
    auto provider = VNext::makeEphemeralDocumentStorageProvider();
    if (!provider.ok()) throw std::runtime_error("provider creation failed");
    Fixture fixture{std::move(provider).value(), {}};
    auto document = MarkoffDocument::open(*fixture.provider,
        {key, 1}, VNext::StorageMode::ephemeral);
    if (!document.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(document).value();
    if (fixture.document->importInitialMarkdown(markdown).state
        != VNext::ActionPublicationState::committed)
        throw std::runtime_error("Markdown import failed");
    return fixture;
}
}

class TestFormatOpsVNext : public QObject {
    Q_OBJECT
private slots:
    void wraps_and_unwraps_as_single_undoable_actions()
    {
        auto fixture = open(QStringLiteral("format-wrap"),
                            QByteArrayLiteral("hello world\n"));
        auto& doc = *fixture.document;
        auto result = FormatOps::wrapToggle(&doc,
            QString::fromUtf8(doc.widgetFlatView()), {6, 11}, "**");
        QVERIFY(result);
        QCOMPARE(doc.serializeForSave(), QByteArrayLiteral("hello **world**\n"));
        doc.undoD2();
        QCOMPARE(doc.serializeForSave(), QByteArrayLiteral("hello world\n"));
        result = FormatOps::wrapToggle(&doc,
            QString::fromUtf8(doc.widgetFlatView()), {6, 11}, "**");
        QVERIFY(result);
        QCOMPARE(doc.serializeForSave(), QByteArrayLiteral("hello **world**\n"));
    }

    void wraps_selection_across_blocks_in_one_action()
    {
        auto fixture = open(QStringLiteral("format-wrap-cross"),
            QByteArrayLiteral("first\n\nsecond block\n"));
        auto& doc = *fixture.document;
        auto result = FormatOps::wrapToggle(&doc,
            QString::fromUtf8(doc.widgetFlatView()), {2, 10}, "_");
        QVERIFY(result);
        QCOMPARE(doc.serializeForSave(),
                 QByteArrayLiteral("fi_rst_\n\n_seco_nd block\n"));
        doc.undoD2();
        QCOMPARE(doc.serializeForSave(),
                 QByteArrayLiteral("first\n\nsecond block\n"));
    }

    void insert_link_and_change_heading_level()
    {
        auto fixture = open(QStringLiteral("format-link-heading"),
            QByteArrayLiteral("see docs\n\nsecond\n"));
        auto& doc = *fixture.document;
        auto link = FormatOps::insertLink(&doc,
            QString::fromUtf8(doc.widgetFlatView()), {4, 8});
        QVERIFY(link);
        QCOMPARE(doc.serializeForSave(),
                 QByteArrayLiteral("see [docs](url)\n\nsecond\n"));
        auto heading = FormatOps::setHeadingLevel(&doc,
            QString::fromUtf8(doc.widgetFlatView()), 18, 2);
        QVERIFY(heading);
        QCOMPARE(doc.serializeForSave(),
                 QByteArrayLiteral("see [docs](url)\n\n## second\n"));
    }

    void per_block_format_ops_use_local_offsets_and_durable_actions()
    {
        auto fixture = open(QStringLiteral("format-per-block"),
            QByteArrayLiteral("first\n\nsecond block\n"));
        auto& doc = *fixture.document;
        const auto blocks = doc.iterateBlocks();
        QVERIFY(blocks.size() == 2);
        auto wrapped = FormatOps::wrapToggleInBlock(&doc, blocks[1], {0, 6}, "_");
        QVERIFY(wrapped);
        QCOMPARE(doc.serializeForSave(), QByteArrayLiteral("first\n\n_second_ block\n"));
        wrapped = FormatOps::wrapToggleInBlock(&doc, blocks[1], *wrapped, "_");
        QVERIFY(wrapped);
        QCOMPARE(doc.serializeForSave(), QByteArrayLiteral("first\n\nsecond block\n"));
        doc.undoD2();
        QCOMPARE(doc.serializeForSave(), QByteArrayLiteral("first\n\n_second_ block\n"));
        doc.undoD2();
        QCOMPARE(doc.serializeForSave(), QByteArrayLiteral("first\n\nsecond block\n"));

        auto link = FormatOps::insertLinkInBlock(&doc, blocks[1], {0, 6});
        QVERIFY(link);
        QCOMPARE(doc.serializeForSave(), QByteArrayLiteral("first\n\n[second](url) block\n"));
        auto heading = FormatOps::setHeadingLevelInBlock(&doc, blocks[1], 4, 2);
        QVERIFY(heading);
        QCOMPARE(doc.serializeForSave(), QByteArrayLiteral("first\n\n## [second](url) block\n"));
    }

    void block_scoped_multi_range_formatting_is_one_action()
    {
        auto fixture = open(QStringLiteral("format-many-blocks"),
            QByteArrayLiteral("first\n\nsecond block\n"));
        auto& doc = *fixture.document;
        const auto blocks = doc.iterateBlocks();
        const QList<FormatOps::BlockByteRange> ranges{
            {blocks[0], {1, 3}}, {blocks[1], {0, 2}}};
        const auto wrapped = FormatOps::wrapToggleAcrossBlocks(&doc, ranges, "_");
        QVERIFY(wrapped);
        QCOMPARE(doc.serializeForSave(),
                 QByteArrayLiteral("f_ir_st\n\n_se_cond block\n"));
        doc.undoD2();
        QCOMPARE(doc.serializeForSave(), QByteArrayLiteral("first\n\nsecond block\n"));
        const auto links = FormatOps::insertLinkAcrossBlocks(&doc, ranges);
        QVERIFY(links);
        QCOMPARE(doc.serializeForSave(),
                 QByteArrayLiteral("f[ir](url)st\n\n[se](url)cond block\n"));
        doc.undoD2();
        QCOMPARE(doc.serializeForSave(), QByteArrayLiteral("first\n\nsecond block\n"));
    }
};

QTEST_MAIN(TestFormatOpsVNext)
#include "tst_format_ops_vnext.moc"
