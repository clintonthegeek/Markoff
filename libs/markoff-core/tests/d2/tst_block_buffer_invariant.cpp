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
    {"setext-h1",                  "Heading\n========\n"},
    {"setext-h2",                  "Heading\n--------\n"},
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

QTEST_MAIN(TstBlockBufferInvariant)
#include "tst_block_buffer_invariant.moc"
