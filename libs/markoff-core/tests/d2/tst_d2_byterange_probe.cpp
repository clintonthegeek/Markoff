// SPDX-License-Identifier: GPL-3.0-or-later
// Temporary diagnostic: probe what blockLoadTimeBytes actually contains
// for multi-block documents. Used to design inter-block separator logic.
// This test always passes; output goes to stdout.
#include <QTest>
#include <QDebug>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/BlockKind.h>

using namespace Markoff;

class TstD2ByteRangeProbe : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void twoParagraphs_printBlockBytes();
    void headingAndParagraph_printBlockBytes();
    void codeBlock_printBlockBytes();
    void frontmatterAndParagraph_printBlockBytes();
};

static void printBlocks(MarkoffDocument &doc, const QByteArray &src)
{
    qDebug() << "Source (repr):" << src;
    qDebug() << "Source size:" << src.size();
    auto blocks = doc.iterateBlocks();
    qDebug() << "Block count:" << blocks.size();
    QByteArray reconstructed;
    for (size_t i = 0; i < blocks.size(); ++i) {
        QByteArray bytes = doc.blockLoadTimeBytes(blocks[i]);
        qDebug() << "Block" << i << "kind=" << (int)doc.blockKind(blocks[i])
                 << "bytes=" << bytes << "size=" << bytes.size();
        if (i > 0) reconstructed += "|SEP|";
        reconstructed += bytes;
    }
    qDebug() << "Reconstructed (no sep):" << reconstructed;
}

void TstD2ByteRangeProbe::twoParagraphs_printBlockBytes()
{
    QByteArray src = "Para one\n\nPara two\n";
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(src);
    printBlocks(doc, src);
    QVERIFY(true);
}

void TstD2ByteRangeProbe::headingAndParagraph_printBlockBytes()
{
    QByteArray src = "# Heading\n\nParagraph\n";
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(src);
    printBlocks(doc, src);
    QVERIFY(true);
}

void TstD2ByteRangeProbe::codeBlock_printBlockBytes()
{
    QByteArray src = "```cpp\nint x = 0;\n```\n";
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(src);
    printBlocks(doc, src);
    QVERIFY(true);
}

void TstD2ByteRangeProbe::frontmatterAndParagraph_printBlockBytes()
{
    QByteArray src = "---\ntitle: Test\n---\n\nBody text\n";
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(src);
    printBlocks(doc, src);
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(TstD2ByteRangeProbe)
#include "tst_d2_byterange_probe.moc"
