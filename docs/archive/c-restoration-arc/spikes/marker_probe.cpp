// SPDX-License-Identifier: GPL-3.0-or-later
// Spike §3.1(c): probe whether tree-sitter accepts various invisible /
// near-invisible characters as a paragraph block.
//
// For each candidate:
//   - parse "hello\n\n<MARKER>"
//   - print blocks.size(), kinds, byte ranges
// We need a configuration that produces TWO blocks (paragraph "hello"
// and a paragraph containing the marker).

#include <markoff/parser/Document.h>
#include <QString>
#include <QStringLiteral>
#include <QDebug>
#include <cstdio>

using Markoff::Document;
using Markoff::TopLevelBlock;
using Kind = TopLevelBlock::Kind;

static const char *kindName(Kind k) {
    switch (k) {
        case Kind::Paragraph: return "Paragraph";
        case Kind::AtxHeading: return "AtxHeading";
        case Kind::SetextHeading: return "SetextHeading";
        case Kind::FencedCodeBlock: return "FencedCodeBlock";
        case Kind::IndentedCodeBlock: return "IndentedCodeBlock";
        case Kind::BlockQuote: return "BlockQuote";
        case Kind::ListTight: return "ListTight";
        case Kind::ListLoose: return "ListLoose";
        case Kind::ThematicBreak: return "ThematicBreak";
        case Kind::HtmlBlock: return "HtmlBlock";
        case Kind::LinkReferenceDefinition: return "LinkReferenceDefinition";
        case Kind::Table: return "Table";
        case Kind::Other: return "Other";
    }
    return "?";
}

static void probe(const char *label, const QString &marker) {
    const QString src = QStringLiteral("hello\n\n") + marker;
    auto doc = Document::fromMarkdown(src);
    auto blocks = doc->topLevelBlocks();

    QByteArray utf8 = src.toUtf8();
    std::printf("\n=== %s ===\n", label);
    std::printf("  src bytes: %d  (codepoint count: %d)\n",
                int(utf8.size()), int(src.size()));
    std::printf("  marker bytes: ");
    QByteArray mu = marker.toUtf8();
    for (unsigned char c : mu) std::printf("%02x ", c);
    std::printf("(len=%d)\n", int(mu.size()));
    std::printf("  blocks.size() = %d\n", int(blocks.size()));
    for (int i = 0; i < blocks.size(); ++i) {
        const auto &b = blocks[i];
        QByteArray slice = utf8.mid(b.byteStart, b.byteEnd - b.byteStart);
        std::printf("    [%d] kind=%s byte=[%d,%d) len=%d text=%s\n",
                    i, kindName(b.kind), b.byteStart, b.byteEnd,
                    int(slice.size()),
                    slice.toPercentEncoding(QByteArray(), "\\\"").constData());
    }
}

// Also probe "<MARKER>\n" (no preceding paragraph), to see whether the marker
// alone makes a block.
static void probeAlone(const char *label, const QString &marker) {
    const QString src = marker + QStringLiteral("\n");
    auto doc = Document::fromMarkdown(src);
    auto blocks = doc->topLevelBlocks();

    QByteArray utf8 = src.toUtf8();
    std::printf("\n--- alone: %s ---\n", label);
    std::printf("  src bytes: %d  blocks.size() = %d\n",
                int(utf8.size()), int(blocks.size()));
    for (int i = 0; i < blocks.size(); ++i) {
        const auto &b = blocks[i];
        QByteArray slice = utf8.mid(b.byteStart, b.byteEnd - b.byteStart);
        std::printf("    [%d] kind=%s byte=[%d,%d) text=%s\n",
                    i, kindName(b.kind), b.byteStart, b.byteEnd,
                    slice.toPercentEncoding(QByteArray(), "\\\"").constData());
    }
}

int main() {
    // Baseline: no marker.
    probe("BASELINE: hello\\n\\n (no marker)", QStringLiteral(""));

    // Pure ASCII control tests.
    probe("ASCII space",       QStringLiteral(" "));
    probe("two ASCII spaces",  QStringLiteral("  "));
    probe("ASCII letter 'x'",  QStringLiteral("x"));

    // Whitespace candidates.
    probe("U+00A0 NBSP",            QString(QChar(0x00A0)));
    probe("U+2007 figure space",    QString(QChar(0x2007)));
    probe("U+2009 thin space",      QString(QChar(0x2009)));
    probe("U+202F narrow nbsp",     QString(QChar(0x202F)));

    // Invisible / format chars.
    probe("U+200B ZWSP",            QString(QChar(0x200B)));
    probe("U+2060 word joiner",     QString(QChar(0x2060)));
    probe("U+FEFF ZWNBSP/BOM",      QString(QChar(0xFEFF)));
    probe("U+034F CGJ",             QString(QChar(0x034F)));
    probe("U+180E mongolian-vs",    QString(QChar(0x180E)));

    // Paired surrogate / non-BMP (variation selector).
    probe("U+E0100 var-selector-17",
          QString::fromUcs4(reinterpret_cast<const char32_t*>(U"\U000E0100"), 1));

    // What about wrapping marker in spaces/HTML comments?
    probe("HTML comment",           QStringLiteral("<!---->"));

    // Aloneness probes (marker by itself, no preceding "hello").
    probeAlone("U+200B ZWSP", QString(QChar(0x200B)));
    probeAlone("U+00A0 NBSP", QString(QChar(0x00A0)));
    probeAlone("ASCII space", QStringLiteral(" "));
    probeAlone("U+2060 WJ", QString(QChar(0x2060)));

    // Sanity: paragraph followed by another paragraph with content.
    probe("CONTROL: real second paragraph 'world'", QStringLiteral("world"));

    // What about typing the marker and then ENTER (the actual cursor target)?
    probe("ZWSP then a normal char", QString(QChar(0x200B)) + QStringLiteral("a"));

    return 0;
}
