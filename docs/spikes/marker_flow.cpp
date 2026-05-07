// SPDX-License-Identifier: GPL-3.0-or-later
// Spike §3.1(c): simulate the EOB-Enter → type → scrub flow at the parser
// level, demonstrating that:
//   1. After EOB-Enter inserts "\n\n<ZWSP>", the parser produces a real
//      paragraph block at the new position.
//   2. After the user types "x" at qtPos 0 of that paragraph, the parser
//      produces a paragraph containing "x<ZWSP>".
//   3. After the scrubber deletes the ZWSP, the paragraph contains "x".
//   4. None of these steps requires a hole layer — they are pure source
//      edits + parser-driven row updates.

#include <markoff/parser/Document.h>
#include <QString>
#include <QStringLiteral>
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

static void show(const char *step, const QString &src) {
    auto doc = Document::fromMarkdown(src);
    auto blocks = doc->topLevelBlocks();
    QByteArray u = src.toUtf8();
    std::printf("\n## %s\n", step);
    std::printf("  src bytes (%d): \"", int(u.size()));
    for (unsigned char c : u) {
        if (c == '\n') std::printf("\\n");
        else if (c < 0x20 || c >= 0x7f) std::printf("\\x%02x", c);
        else std::printf("%c", c);
    }
    std::printf("\"\n  blocks (%d):\n", int(blocks.size()));
    for (const auto &b : blocks) {
        QByteArray slice = u.mid(b.byteStart, b.byteEnd - b.byteStart);
        std::printf("    %-9s [%d,%d) = \"", kindName(b.kind), b.byteStart, b.byteEnd);
        for (unsigned char c : slice) {
            if (c == '\n') std::printf("\\n");
            else if (c < 0x20 || c >= 0x7f) std::printf("\\x%02x", c);
            else std::printf("%c", c);
        }
        std::printf("\"\n");
    }
}

int main() {
    const QString ZWSP = QString(QChar(0x200B));

    std::printf("=== EOB-Enter marker flow simulation ===\n");
    std::printf("Marker: U+200B ZERO WIDTH SPACE (3 UTF-8 bytes 0xE2 0x80 0x8B)\n");

    // Initial document: two paragraphs.
    QString src = QStringLiteral("alpha\n\nbeta\n");
    show("STEP 0: initial document", src);

    // --- Scenario A: EOB-Enter at end of last paragraph (no following block) ---
    std::printf("\n\n========== SCENARIO A: EOB-Enter on last paragraph ==========\n");
    // Cursor at end of "beta", which is byte 11 (before the trailing \n). Apply
    // applyLocalEdit("\n\n" + ZWSP) at byte 11.
    QString a1 = src;
    a1.insert(11, QStringLiteral("\n\n") + ZWSP);  // codepoint indexing OK; ASCII tail
    show("A1: after applyLocalEdit(\"\\n\\n<ZWSP>\") at byte 11", a1);

    // User types "x" at the start of the new paragraph (qtPos 0 of paragraph 3).
    // The new paragraph starts at byte 13 (after "alpha\n\nbeta\n\n"). Insert "x"
    // at byte 13.
    QString a2 = a1;
    a2.insert(13, QStringLiteral("x"));
    show("A2: after user types \"x\" at qtPos 0 of new paragraph", a2);

    // Scrub the ZWSP. The ZWSP is now at byte 14 (right after "x"). Remove 3 bytes.
    QString a3 = a2;
    a3.remove(13 + 1, 1);  // QString uses UTF-16 codepoint index; ZWSP is 1 unit
    show("A3: after scrubber deletes ZWSP (clean state)", a3);

    // --- Scenario B: EOB-Enter MID document (not at end) ---
    std::printf("\n\n========== SCENARIO B: EOB-Enter mid-document ==========\n");
    QString b0 = QStringLiteral("alpha\n\nbeta\n\ngamma\n");
    show("B0: initial 3-paragraph doc", b0);

    // Cursor at end of "alpha" (byte 5). EOB-Enter → insert "\n\n<ZWSP>" at byte 5.
    // Wait: "alpha" is followed by "\n\nbeta", so the existing separator IS the
    // \n\n before beta. End-of-paragraph for "alpha" = byte 5. Inserting
    // "\n\n<ZWSP>" makes "alpha\n\n<ZWSP>\n\nbeta\n\ngamma\n".
    QString b1 = b0;
    b1.insert(5, QStringLiteral("\n\n") + ZWSP);
    show("B1: after applyLocalEdit at byte 5 (between alpha and beta)", b1);

    // User types "y" at qtPos 0 of new paragraph (byte 7).
    QString b2 = b1;
    b2.insert(7, QStringLiteral("y"));
    show("B2: after user types \"y\"", b2);

    // Scrub.
    QString b3 = b2;
    b3.remove(8, 1);
    show("B3: after scrub", b3);

    // --- Scenario C: User abandons (no typing, focus moves elsewhere) ---
    std::printf("\n\n========== SCENARIO C: marker leakage path (no typing) ==========\n");
    QString c1 = b1;  // same as B1: marker-only paragraph
    show("C1: marker-only paragraph (this is what would persist if we don't scrub)",
         c1);

    // What if the user clicks back into the marker paragraph and types? Cursor
    // lands at qtPos 0 (before marker) or qtPos 1 (after marker)? Let's try both.
    QString c2_before = c1;
    c2_before.insert(7, QStringLiteral("z"));
    show("C2-before: cursor at qtPos 0, type \"z\" (z lands BEFORE marker)", c2_before);

    QString c2_after = c1;
    c2_after.insert(8, QStringLiteral("z"));
    show("C2-after: cursor at qtPos 1, type \"z\" (z lands AFTER marker)", c2_after);

    // --- Scenario D: stacked Enter (Enter twice in a row, both at EOB) ---
    std::printf("\n\n========== SCENARIO D: stacked Enter (Enter, Enter) ==========\n");
    QString d0 = QStringLiteral("alpha\n");
    show("D0: initial doc", d0);
    QString d1 = d0;
    d1.insert(5, QStringLiteral("\n\n") + ZWSP);
    show("D1: after first Enter", d1);
    // Now user presses Enter again at end of marker paragraph. EOB-Enter on the
    // marker paragraph (which is at byte [7,10)). Insert "\n\n<ZWSP>" at byte 10.
    QString d2 = d1;
    d2.insert(10, QStringLiteral("\n\n") + ZWSP);
    show("D2: after second Enter (two marker paragraphs in a row?)", d2);

    // --- Scenario E: save with marker present (literal source representation) ---
    std::printf("\n\n========== SCENARIO E: what the .md file looks like with marker ==========\n");
    QByteArray savedBytes = a1.toUtf8();
    std::printf("If the user saved A1 (no typing yet), the .md file would contain:\n  ");
    for (unsigned char c : savedBytes) {
        if (c == '\n') std::printf("\\n");
        else if (c < 0x20 || c >= 0x7f) std::printf("\\x%02x", c);
        else std::printf("%c", c);
    }
    std::printf("\n");
    std::printf("Other markdown editors / git-diff / `cat` / pandoc would see the\n");
    std::printf("ZWSP bytes (E2 80 8B) interleaved into the prose.\n");

    return 0;
}
