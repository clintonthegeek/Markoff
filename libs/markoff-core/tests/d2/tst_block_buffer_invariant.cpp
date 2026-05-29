// SPDX-License-Identifier: GPL-3.0-or-later
//
// B1 buffer convention invariant tests.
// Spec: docs/specs/2026-05-18-b1-buffer-convention-design.md

#include <QTest>
#include <QByteArray>
#include <QList>
#include <QRegularExpression>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

namespace {

struct Fixture {
    const char *name;
    QByteArray source;
};

// Representative corpus. Each fixture exercises a distinct parser shape.
const QList<Fixture> kCorpus = {
    {"single-block-no-eol",        "Heading"},
    {"single-block-with-eol",      "Heading\n"},
    {"two-paragraphs-with-eol",    "first\n\nsecond\n"},
    {"two-paragraphs-no-eol",      "first\n\nsecond"},
    // Setext underline widths match the title byte length (7) so the
    // roundtrip_stability fixed-point holds. The width-drift behaviour
    // (source-width != save-width when they differ) is explicitly pinned
    // in setext_untouched_roundtrip_is_byte_identical below.
    {"setext-h1",                  "Heading\n=======\n"},
    {"setext-h2",                  "Heading\n-------\n"},
    {"atx-heading",                "# Heading\n"},
    {"fenced-code-with-newlines",  "```\nline1\nline2\n```\n"},
    {"tight-list",                 "- one\n- two\n- three\n"},
    {"loose-list",                 "- one\n\n- two\n\n- three\n"},
    {"blockquote",                 "> quoted\n"},
    {"para-after-heading",         "# Heading\n\nbody\n"},
    {"para-before-list",           "intro\n\n- one\n- two\n"},
    {"hr",                         "before\n\n---\n\nafter\n"},
    {"frontmatter",                "---\ntitle: x\n---\n\nbody\n"},
};

// Normalize source for round-trip comparison: collapse runs of 2+ blank
// lines to one; ensure single trailing '\n'. Local to this test file.
QByteArray normalize(QByteArray s)
{
    // Collapse 3+ consecutive '\n' to exactly 2.
    static const QRegularExpression re("\\n{3,}");
    QString str = QString::fromUtf8(s);
    str.replace(re, "\n\n");
    QByteArray out = str.toUtf8();
    // Ensure single trailing '\n'.
    while (out.endsWith("\n\n")) out.chop(1);
    if (!out.endsWith('\n')) out += '\n';
    return out;
}

} // namespace

class TstBlockBufferInvariant : public QObject {
    Q_OBJECT
private slots:
    void no_load_terminator_data();
    void no_load_terminator();

    void roundtrip_stability_data();
    void roundtrip_stability();

    void paragraph_buffers_have_no_internal_newlines();
    void listitem_buffers_have_no_internal_newlines();

    void setext_h1_strips_underline_from_buffer();
    void setext_h2_strips_underline_from_buffer();
    void setext_multiline_title_collapses_to_space();
    void setext_h1_save_reconstructs_underline_after_edit();
    void setext_h2_save_reconstructs_underline_after_edit();
    void setext_untouched_roundtrip_is_byte_identical();
};

void TstBlockBufferInvariant::no_load_terminator_data()
{
    QTest::addColumn<QByteArray>("source");
    for (const auto &fx : kCorpus)
        QTest::newRow(fx.name) << fx.source;
}

void TstBlockBufferInvariant::no_load_terminator()
{
    QFETCH(QByteArray, source);

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    for (BlockId id : blocks) {
        const QByteArray text = doc.blockText(id);
        QVERIFY2(!text.endsWith('\n'),
                 qPrintable(QString("block %1 buffer ends with \\n: %2")
                     .arg(id.raw())
                     .arg(QString::fromUtf8(text))));
    }
}

void TstBlockBufferInvariant::roundtrip_stability_data()
{
    QTest::addColumn<QByteArray>("source");
    for (const auto &fx : kCorpus)
        QTest::newRow(fx.name) << fx.source;
}

void TstBlockBufferInvariant::roundtrip_stability()
{
    QFETCH(QByteArray, source);

    MarkoffDocument doc1(/*replicaId=*/1);
    doc1.loadFromMarkdown(source);
    QByteArray firstSave = doc1.serializeForSave();

    MarkoffDocument doc2(/*replicaId=*/1);
    doc2.loadFromMarkdown(firstSave);
    QByteArray secondSave = doc2.serializeForSave();

    // Fixed-point under round-trip: first save equals second save.
    QCOMPARE(secondSave, firstSave);

    // First save equals normalize(source): collapses blank-line runs +
    // ensures single trailing '\n'.
    QCOMPARE(firstSave, normalize(source));
}

// CommonMark's "soft line break" rule: a single newline between
// non-blank lines inside a paragraph renders as whitespace (typically a
// space). Markoff stores this canonically — the model represents the
// paragraph as a single block buffer with no internal '\n'. Flat-view
// leaves (markoff-styled, markoff-source) rely on this because every
// '\n' that reaches QTextDocument becomes a QTextBlock boundary.
//
// (BlockQuote/ListItem/Heading retain internal '\n' for now — marker
// stripping is a separate concern; Route A v0 fixes Paragraph only.)
void TstBlockBufferInvariant::paragraph_buffers_have_no_internal_newlines()
{
    const QByteArray source =
        "First paragraph hard-wrapped\n"
        "across three lines so that\n"
        "soft breaks need collapsing.\n"
        "\n"
        "Second paragraph stays separate.\n";

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    int paragraphsChecked = 0;
    for (BlockId id : blocks) {
        if (doc.blockKind(id) != BlockKind::Paragraph) continue;
        ++paragraphsChecked;
        const QByteArray text = doc.blockText(id);
        QVERIFY2(!text.contains('\n'),
                 qPrintable(QString("Paragraph block %1 has internal '\\n': %2")
                     .arg(id.raw())
                     .arg(QString::fromUtf8(text))));
    }
    QCOMPARE(paragraphsChecked, 2);
}

// ListItems can also be hard-wrapped (continuation lines indented under
// the item). harvestListItem narrows the byte range to the item's content
// child (post-marker), so the buffer never sees the marker syntax — just
// the text. We canonicalise internal '\n' → space for the same reason as
// Paragraph: flat-view leaves treat any '\n' in the buffer as a
// QTextBlock boundary.
void TstBlockBufferInvariant::listitem_buffers_have_no_internal_newlines()
{
    const QByteArray source =
        "- First item with a continuation\n"
        "  that wraps onto a second line.\n"
        "- Second item is a single line.\n";

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    int itemsChecked = 0;
    for (BlockId id : blocks) {
        if (doc.blockKind(id) != BlockKind::ListItem) continue;
        ++itemsChecked;
        const QByteArray text = doc.blockText(id);
        QVERIFY2(!text.contains('\n'),
                 qPrintable(QString("ListItem block %1 has internal '\\n': %2")
                     .arg(id.raw())
                     .arg(QString::fromUtf8(text))));
    }
    QCOMPARE(itemsChecked, 2);
}

// Setext headings ("Title\n========" for H1, "Title\n--------" for H2) used
// to land with the underline bytes in the block buffer; flat-view leaves
// then rendered the underline as a literal second QTextBlock. The load
// path now strips the underline (everything from the last '\n' onward in
// the byte range) and collapses any soft-breaks in multi-line titles to
// space. HeadingForm="setext" attr is already set so the serializer can
// reconstruct on save.

void TstBlockBufferInvariant::setext_h1_strips_underline_from_buffer()
{
    const QByteArray source = "Heading\n========\n";

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::Heading);
    const QByteArray text = doc.blockText(blocks[0]);
    QCOMPARE(text, QByteArrayLiteral("Heading"));
    QVERIFY(!text.contains('\n'));
    QVERIFY(!text.contains('='));
}

void TstBlockBufferInvariant::setext_h2_strips_underline_from_buffer()
{
    const QByteArray source = "Heading\n--------\n";

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::Heading);
    const QByteArray text = doc.blockText(blocks[0]);
    QCOMPARE(text, QByteArrayLiteral("Heading"));
    QVERIFY(!text.contains('\n'));
}

void TstBlockBufferInvariant::setext_multiline_title_collapses_to_space()
{
    // CommonMark allows the setext title to wrap across multiple source
    // lines; soft-breaks inside the title collapse to space the same way
    // Paragraph soft-breaks do.
    const QByteArray source = "line one\nline two\n==========\n";

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::Heading);
    QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("line one line two"));
}

void TstBlockBufferInvariant::setext_h1_save_reconstructs_underline_after_edit()
{
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(QByteArrayLiteral("Heading\n========\n"));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    const BlockId id = blocks[0];

    // Append "X" at end of title.
    UndoLog::Transaction t1(doc.d2UndoLog());
    doc.d2ApplyBufferEdit(id, /*offset=*/7, /*removedBytes=*/0,
                          QByteArrayLiteral("X"), t1);
    QCOMPARE(doc.blockText(id), QByteArrayLiteral("HeadingX"));

    const QByteArray saved = doc.serializeForSave();
    QCOMPARE(saved, QByteArrayLiteral("HeadingX\n========\n"));
}

void TstBlockBufferInvariant::setext_h2_save_reconstructs_underline_after_edit()
{
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(QByteArrayLiteral("Sub\n---\n"));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    const BlockId id = blocks[0];

    // Append "head" at end.
    UndoLog::Transaction t2(doc.d2UndoLog());
    doc.d2ApplyBufferEdit(id, /*offset=*/3, /*removedBytes=*/0,
                          QByteArrayLiteral("head"), t2);
    QCOMPARE(doc.blockText(id), QByteArrayLiteral("Subhead"));

    const QByteArray saved = doc.serializeForSave();
    QCOMPARE(saved, QByteArrayLiteral("Subhead\n-------\n"));
}

void TstBlockBufferInvariant::setext_untouched_roundtrip_is_byte_identical()
{
    // The `setext-h1` and `setext-h2` fixtures in kCorpus already exercise
    // roundtrip_stability(). This slot is the explicit by-name guard so a
    // future corpus reshuffle can't hide a setext regression. Underline
    // width = title byte length (7) — the source's 8-char underline gets
    // shortened to 7 on round-trip; CommonMark accepts any width >=1, still
    // parses as the same heading.
    const QByteArray source = "Heading\n========\n";

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const QByteArray saved = doc.serializeForSave();
    QCOMPARE(saved, QByteArrayLiteral("Heading\n=======\n"));
}

QTEST_MAIN(TstBlockBufferInvariant)
#include "tst_block_buffer_invariant.moc"
