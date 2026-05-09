// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QFile>
#include <QTemporaryDir>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/BlockSerializer.h>
#include <markoff/core/BlockAttrsMap.h>

using namespace Markoff;

class TstD2Save : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // Task 8.1 — registry
    void registry_registerAndGet();

    // Task 8.2 — per-kind serializers: paragraph, heading, code-block, list-item
    void paragraphSerializer_returnsContentAsIs();
    void headingSerializer_prependsHashes();
    void codeBlockSerializer_wrapsInFences();
    void listItemSerializer_prependsMarker();

    // Task 8.3 — fallback
    void fallbackSerializer_unknownKind_returnsContent();

    // Task 8.4 — touch test
    void untouchedBlock_savesLoadTimeBytes();
    void editedBlock_savesCanonical();
    void kindChangedBlock_savesCanonical();
    void bornAfterLoadBlock_alwaysCanonical();

    // Task 8.5 — serializeForSave
    void save_writesFrontmatter_thenBlocks();
    void save_twoUntouchedBlocks_roundTrip();
    void save_noFrontmatter_singleBlock_roundTrip();

    // Task 8.6 — atomic write
    void saveToFile_atomicWrite_contentMatches();

    // Task 2B — form-aware serializeHeading
    void headingSerializer_atx_doesNotDoublePrefix();
    void headingSerializer_setext_emitsBufferVerbatim();
    void headingSerializer_setextH1_emitsBufferVerbatim();
};

// ── Task 8.1 ─────────────────────────────────────────────────────────────────

void TstD2Save::registry_registerAndGet()
{
    auto &reg = BuiltinBlockSerializerRegistry::instance();
    // Register a custom serializer for Paragraph, verify we get it back
    bool called = false;
    reg.registerSerializer(BlockKind::Paragraph,
        [&called](BlockKind, const QHash<AttrName, AttrValue> &, const QByteArray &content) {
            called = true;
            return QByteArray("CUSTOM:") + content;
        });
    // Retrieve and invoke it
    auto fn = reg.get(BlockKind::Paragraph);
    QVERIFY(fn != nullptr);
    QByteArray result = fn(BlockKind::Paragraph, {}, "test");
    QVERIFY(called);
    QCOMPARE(result, QByteArray("CUSTOM:test"));
    // Restore built-in paragraph serializer for subsequent tests
    reg.registerSerializer(BlockKind::Paragraph,
        [](BlockKind, const QHash<AttrName, AttrValue> &, const QByteArray &c) { return c; });
}

// ── Task 8.2 ─────────────────────────────────────────────────────────────────

void TstD2Save::paragraphSerializer_returnsContentAsIs()
{
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    auto fn = BuiltinBlockSerializerRegistry::instance().get(BlockKind::Paragraph);
    QByteArray result = fn(BlockKind::Paragraph, {}, "Hello world");
    QCOMPARE(result, QByteArray("Hello world"));
}

void TstD2Save::headingSerializer_prependsHashes()
{
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    auto fn = BuiltinBlockSerializerRegistry::instance().get(BlockKind::Heading);

    // Level 1
    QHash<AttrName, AttrValue> attrs1;
    attrs1["level"] = AttrValue{1};
    QCOMPARE(fn(BlockKind::Heading, attrs1, "Title"), QByteArray("# Title"));

    // Level 2
    QHash<AttrName, AttrValue> attrs2;
    attrs2["level"] = AttrValue{2};
    QCOMPARE(fn(BlockKind::Heading, attrs2, "Section"), QByteArray("## Section"));

    // No level attr → default 1
    QCOMPARE(fn(BlockKind::Heading, {}, "Default"), QByteArray("# Default"));
}

void TstD2Save::codeBlockSerializer_wrapsInFences()
{
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    auto fn = BuiltinBlockSerializerRegistry::instance().get(BlockKind::CodeBlock);

    // With info string
    QHash<AttrName, AttrValue> attrs;
    attrs["infoString"] = AttrValue{QString("cpp")};
    QByteArray result = fn(BlockKind::CodeBlock, attrs, "int x = 0;");
    QCOMPARE(result, QByteArray("```cpp\nint x = 0;\n```"));

    // Without info string
    QByteArray result2 = fn(BlockKind::CodeBlock, {}, "code here");
    QCOMPARE(result2, QByteArray("```\ncode here\n```"));
}

void TstD2Save::listItemSerializer_prependsMarker()
{
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    auto fn = BuiltinBlockSerializerRegistry::instance().get(BlockKind::ListItem);

    // Default marker
    QCOMPARE(fn(BlockKind::ListItem, {}, "Item text"), QByteArray("- Item text"));

    // Custom marker
    QHash<AttrName, AttrValue> attrs;
    attrs["marker"] = AttrValue{QString("*")};
    QCOMPARE(fn(BlockKind::ListItem, attrs, "Item text"), QByteArray("* Item text"));
}

// ── Task 8.3 ─────────────────────────────────────────────────────────────────

void TstD2Save::fallbackSerializer_unknownKind_returnsContent()
{
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    // HtmlBlock, Math, Mermaid, Table, Image are all passthrough
    for (BlockKind kind : { BlockKind::HtmlBlock, BlockKind::Math,
                             BlockKind::Mermaid, BlockKind::Table,
                             BlockKind::Image }) {
        auto fn = BuiltinBlockSerializerRegistry::instance().get(kind);
        QCOMPARE(fn(kind, {}, "raw content"), QByteArray("raw content"));
    }
    // Unregistered numeric kind (cast from a value not in the enum) → fallback
    BlockKind unknown = static_cast<BlockKind>(0xFF);
    auto fallback = BuiltinBlockSerializerRegistry::instance().get(unknown);
    QCOMPARE(fallback(unknown, {}, "raw content"), QByteArray("raw content"));
}

// ── Task 8.4 ─────────────────────────────────────────────────────────────────

void TstD2Save::untouchedBlock_savesLoadTimeBytes()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello world\n");
    BlockId blk = doc.iterateBlocks().front();
    QVERIFY(!doc.isBlockTouched(blk));
    // Untouched block → serializeForSave uses the original load-time bytes
    QByteArray serialized = doc.serializeForSave();
    QVERIFY(serialized.contains("Hello world"));
    QCOMPARE(serialized, doc.blockLoadTimeBytes(blk));
}

void TstD2Save::editedBlock_savesCanonical()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello world\n");
    BlockId blk = doc.iterateBlocks().front();
    QVERIFY(!doc.isBlockTouched(blk));

    // Edit the block content
    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2ApplyBufferEdit(blk, 5, 0, "!", t);

    QVERIFY(doc.isBlockTouched(blk));
}

void TstD2Save::kindChangedBlock_savesCanonical()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello world\n");
    BlockId blk = doc.iterateBlocks().front();
    QVERIFY(!doc.isBlockTouched(blk));

    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2SetBlockKind(blk, BlockKind::Heading, t);

    QVERIFY(doc.isBlockTouched(blk));
    // Kind-changed block → serializeForSave uses canonical heading serializer
    QByteArray serialized = doc.serializeForSave();
    QVERIFY(serialized.startsWith("# "));
}

void TstD2Save::bornAfterLoadBlock_alwaysCanonical()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello world\n");
    // Insert a block born after load
    UndoLog::Transaction t(doc.d2UndoLog());
    BlockId newBlk = doc.d2InsertBlock(doc.iterateBlocks().front(),
                                        BlockKind::Paragraph, t);
    QVERIFY(doc.isBlockTouched(newBlk));
}

// ── Task 8.5 ─────────────────────────────────────────────────────────────────

void TstD2Save::save_writesFrontmatter_thenBlocks()
{
    MarkoffDocument doc(1);
    QByteArray src = "---\ntitle: Test\n---\n\nBody text\n";
    doc.loadFromMarkdown(src);

    QByteArray serialized = doc.serializeForSave();
    QVERIFY(serialized.startsWith("---\n"));
    QVERIFY(serialized.contains("title: Test"));
    QVERIFY(serialized.contains("Body text"));
}

void TstD2Save::save_twoUntouchedBlocks_roundTrip()
{
    QByteArray src = "Para one\n\nPara two\n";
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(src);

    QByteArray serialized = doc.serializeForSave();
    QCOMPARE(serialized, src);
}

void TstD2Save::save_noFrontmatter_singleBlock_roundTrip()
{
    QByteArray src = "Hello world\n";
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(src);

    QByteArray serialized = doc.serializeForSave();
    QCOMPARE(serialized, src);
}

// ── Task 8.6 ─────────────────────────────────────────────────────────────────

void TstD2Save::saveToFile_atomicWrite_contentMatches()
{
    MarkoffDocument doc(1);
    QByteArray src = "Para one\n\nPara two\n";
    doc.loadFromMarkdown(src);

    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString path = tmpDir.path() + "/test.md";

    bool ok = doc.save(path);
    QVERIFY(ok);

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray written = f.readAll();
    QCOMPARE(written, doc.serializeForSave());
    QCOMPARE(written, src);
}

// ── Task 2B ──────────────────────────────────────────────────────────────────

void TstD2Save::headingSerializer_atx_doesNotDoublePrefix()
{
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    auto fn = BuiltinBlockSerializerRegistry::instance().get(BlockKind::Heading);

    QHash<AttrName, AttrValue> attrs;
    attrs["level"] = AttrValue{2};

    QCOMPARE(fn(BlockKind::Heading, attrs, "## Heading"),
             QByteArray("## Heading"));
    QCOMPARE(fn(BlockKind::Heading, attrs, "Heading"),
             QByteArray("## Heading"));
}

void TstD2Save::headingSerializer_setext_emitsBufferVerbatim()
{
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    auto fn = BuiltinBlockSerializerRegistry::instance().get(BlockKind::Heading);

    QHash<AttrName, AttrValue> attrs;
    attrs["level"] = AttrValue{2};
    attrs["headingForm"] = AttrValue{QString("setext")};

    QCOMPARE(fn(BlockKind::Heading, attrs, "Heading\n---"),
             QByteArray("Heading\n---"));
}

void TstD2Save::headingSerializer_setextH1_emitsBufferVerbatim()
{
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    auto fn = BuiltinBlockSerializerRegistry::instance().get(BlockKind::Heading);

    QHash<AttrName, AttrValue> attrs;
    attrs["level"] = AttrValue{1};
    attrs["headingForm"] = AttrValue{QString("setext")};

    QCOMPARE(fn(BlockKind::Heading, attrs, "Title\n==="),
             QByteArray("Title\n==="));
}

QTEST_GUILESS_MAIN(TstD2Save)
#include "tst_d2_save.moc"
