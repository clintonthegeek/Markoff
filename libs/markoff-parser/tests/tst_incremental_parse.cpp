// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff-parser/SourceSpan.h>
#include <markoff-parser/TreeSitterParser.h>

#include <QByteArray>
#include <QString>
#include <QStringList>

using namespace Markoff;

namespace {

/// Format a single span as a comparable string. Captures every field that
/// influences rendering so structural drift between fresh and incremental
/// parses surfaces as a string mismatch.
QString fingerprint(const SourceSpan &s)
{
    return QStringLiteral(
        "off=%1 len=%2 b=%3 i=%4 s=%5 c=%6 m=%7 md=%8 hi=%9 cm=%10 "
        "tag=%11 lk=%12 wl=%13 im=%14 fr=%15 hd=%16 hl=%17 bm=%18 "
        "lm=%19 fence=%20 cbc=%21 fm=%22 hr=%23 bq=%24 bd=%25 "
        "callo=%26 task=%27 del=%28")
        .arg(s.utf8Offset).arg(s.utf8Length)
        .arg(s.bold).arg(s.italic).arg(s.strikethrough).arg(s.code)
        .arg(s.math).arg(s.mathDisplay).arg(s.highlight).arg(s.comment)
        .arg(s.isTag).arg(s.isLink).arg(s.isWikilink).arg(s.isImage)
        .arg(s.isFootnoteRef).arg(s.isHeading).arg(s.headingLevel)
        .arg(s.isBlockquoteMarker).arg(s.isListMarker)
        .arg(s.isCodeBlockFence).arg(s.isCodeBlockContent)
        .arg(s.isFrontmatter).arg(s.isHorizontalRule).arg(s.isBlockquote)
        .arg(s.blockquoteDepth).arg(s.isCalloutMarker).arg(s.isTaskMarker)
        .arg(s.isDelimiter);
}

QString fingerprintAll(const QList<SourceSpan> &spans)
{
    QStringList out;
    out.reserve(spans.size());
    for (const auto &s : spans) out << fingerprint(s);
    return out.join(QLatin1Char('\n'));
}

}  // namespace

class TstIncrementalParse : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void noPriorTree_fallsThroughToFullParse();
    void noEdits_refreshesBufferWithoutBlockReparse();
    void singleInsertion_matchesFreshParse();
    void singleDeletion_matchesFreshParse();
    void replacement_matchesFreshParse();
    void multipleEdits_matchesFreshParse();
    void heavyTypingBurst_matchesFreshParse();
    void crossBlockEdit_matchesFreshParse();
    void freshParse_resetsReuseCountToZero();
    void parseIncremental_noPriorTree_reportsZeroReuse();
    void singleParagraphEdit_reusesUnchangedInlineRegions();
    void editInsideRegion_invalidatesOnlyThatRegion();
    void editsInTwoRegions_reusesTheRegionBetween();
    void noEdits_reusesAllInlineRegions();
    void blockChangedByteCount_initialAndIncremental();
};

// ---------------------------------------------------------------------------

void TstIncrementalParse::noPriorTree_fallsThroughToFullParse()
{
    TreeSitterParser p;
    QVERIFY(!p.hasTree());
    QByteArray src = QByteArrayLiteral("# Hello\n\nWorld");
    QVERIFY(p.parseIncremental({}, src));
    QVERIFY(p.hasTree());

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(src)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::noEdits_refreshesBufferWithoutBlockReparse()
{
    TreeSitterParser p;
    QByteArray src = QByteArrayLiteral("**bold**");
    QVERIFY(p.parse(QString::fromUtf8(src)));

    // Empty edits with the same buffer must succeed and leave spans intact.
    QVERIFY(p.parseIncremental({}, src));

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(src)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::singleInsertion_matchesFreshParse()
{
    QByteArray oldSrc = QByteArrayLiteral("# Heading\n\nplain paragraph.");
    QByteArray newSrc = QByteArrayLiteral("# Heading\n\nplain *italic* paragraph.");

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(oldSrc)));

    // The diff: at byte offset of "paragraph." we inserted "*italic* " before it.
    const int insertAt = oldSrc.indexOf(QByteArrayLiteral("paragraph."));
    QVERIFY(insertAt >= 0);
    ByteEdit e{ static_cast<quint32>(insertAt),
                static_cast<quint32>(insertAt),
                static_cast<quint32>(QByteArrayLiteral("*italic* ").size()) };
    QVERIFY(p.parseIncremental({e}, newSrc));

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(newSrc)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::singleDeletion_matchesFreshParse()
{
    QByteArray oldSrc = QByteArrayLiteral("# Heading\n\nThe **bold** word.");
    QByteArray newSrc = QByteArrayLiteral("# Heading\n\nThe word.");

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(oldSrc)));

    // Diff: drop "**bold** " (9 bytes) starting after "The ".
    const int delStart = oldSrc.indexOf(QByteArrayLiteral("**bold** "));
    QVERIFY(delStart >= 0);
    ByteEdit e{ static_cast<quint32>(delStart),
                static_cast<quint32>(delStart + 9),
                0u };
    QVERIFY(p.parseIncremental({e}, newSrc));

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(newSrc)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::replacement_matchesFreshParse()
{
    QByteArray oldSrc = QByteArrayLiteral("Some `inline code` here.");
    QByteArray newSrc = QByteArrayLiteral("Some **bold span** here.");

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(oldSrc)));

    // Replace "`inline code`" (13 bytes) with "**bold span**" (13 bytes).
    const int rStart = oldSrc.indexOf(QByteArrayLiteral("`inline code`"));
    QVERIFY(rStart >= 0);
    ByteEdit e{ static_cast<quint32>(rStart),
                static_cast<quint32>(rStart + 13),
                13u };
    QVERIFY(p.parseIncremental({e}, newSrc));

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(newSrc)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::multipleEdits_matchesFreshParse()
{
    QByteArray oldSrc = QByteArrayLiteral("# A\n\npara one\n\npara two\n\npara three.");
    // Two independent edits in old-frame coordinates: insert "**" pairs
    // around "one" (offset of "one" in old) and around "three" (offset of
    // "three" in old). Both edits given out-of-order; sort+reverse-apply
    // is the function's contract.
    const int oneAt   = oldSrc.indexOf(QByteArrayLiteral("one"));
    const int threeAt = oldSrc.indexOf(QByteArrayLiteral("three"));
    QVERIFY(oneAt >= 0 && threeAt >= 0);

    QByteArray newSrc = oldSrc;
    // Apply right-to-left to construct expected newSrc without invalidating
    // the left offset.
    newSrc.replace(threeAt, 5, QByteArrayLiteral("**three**"));
    newSrc.replace(oneAt,   3, QByteArrayLiteral("**one**"));

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(oldSrc)));

    QList<ByteEdit> edits = {
        // Deliberately give them out of order.
        ByteEdit{ static_cast<quint32>(threeAt),
                  static_cast<quint32>(threeAt + 5),
                  9u },
        ByteEdit{ static_cast<quint32>(oneAt),
                  static_cast<quint32>(oneAt + 3),
                  7u },
    };
    QVERIFY(p.parseIncremental(edits, newSrc));

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(newSrc)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::heavyTypingBurst_matchesFreshParse()
{
    // Simulate 50 sequential single-character appends to the document tail
    // — what `applyLocalEdit` would generate during fast typing on a
    // QPlainTextEdit.
    QByteArray src = QByteArrayLiteral("# Title\n\n");
    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(src)));

    for (int i = 0; i < 50; ++i) {
        const int at = src.size();
        src.append('a');
        ByteEdit e{ static_cast<quint32>(at),
                    static_cast<quint32>(at),
                    1u };
        QVERIFY(p.parseIncremental({e}, src));
    }

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(src)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::crossBlockEdit_matchesFreshParse()
{
    // Edit that reframes block structure: typing "# " in front of a
    // paragraph turns it into a heading. The whole block-structure rebuilds.
    QByteArray oldSrc = QByteArrayLiteral("para 1\n\npara 2\n\npara 3");
    QByteArray newSrc = QByteArrayLiteral("para 1\n\n# para 2\n\npara 3");

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(oldSrc)));

    const int at = oldSrc.indexOf(QByteArrayLiteral("para 2"));
    QVERIFY(at >= 0);
    ByteEdit e{ static_cast<quint32>(at), static_cast<quint32>(at), 2u };
    QVERIFY(p.parseIncremental({e}, newSrc));

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(newSrc)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::freshParse_resetsReuseCountToZero()
{
    TreeSitterParser p;
    QVERIFY(p.parse(QStringLiteral("# Heading\n\npara")));
    QCOMPARE(p.inlineTreeReuseCount(), 0);
}

void TstIncrementalParse::parseIncremental_noPriorTree_reportsZeroReuse()
{
    TreeSitterParser p;
    QVERIFY(p.parseIncremental({}, QByteArrayLiteral("# Heading\n\npara")));
    QCOMPARE(p.inlineTreeReuseCount(), 0);
}

void TstIncrementalParse::singleParagraphEdit_reusesUnchangedInlineRegions()
{
    // Three paragraphs. Edit only the middle one. Expect the two outer
    // inline regions to be reused.
    QByteArray oldSrc = QByteArrayLiteral(
        "para alpha here.\n\npara beta here.\n\npara gamma here.");
    QByteArray newSrc = QByteArrayLiteral(
        "para alpha here.\n\npara **beta** here.\n\npara gamma here.");

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(oldSrc)));

    const int at = oldSrc.indexOf(QByteArrayLiteral("beta"));
    QVERIFY(at >= 0);
    // Replace "beta" (4 bytes) with "**beta**" (8 bytes).
    ByteEdit e{ static_cast<quint32>(at),
                static_cast<quint32>(at + 4),
                8u };
    QVERIFY(p.parseIncremental({e}, newSrc));

    QCOMPARE(p.inlineTreeReuseCount(), 2);

    // Span output must still match a fresh parse exactly.
    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(newSrc)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::editInsideRegion_invalidatesOnlyThatRegion()
{
    QByteArray oldSrc = QByteArrayLiteral(
        "first paragraph.\n\nsecond paragraph here.\n\nthird paragraph.");
    // Insert "**" pair inside "second" — this edit overlaps the second
    // paragraph's inline region, so only that one must rebuild; first and
    // third are reused.
    QByteArray newSrc = QByteArrayLiteral(
        "first paragraph.\n\nsecond **paragraph** here.\n\nthird paragraph.");

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(oldSrc)));

    const int at = oldSrc.indexOf(QByteArrayLiteral("paragraph here"));
    QVERIFY(at >= 0);
    ByteEdit e{ static_cast<quint32>(at),
                static_cast<quint32>(at + 9),  // length of "paragraph"
                13u };  // length of "**paragraph**"
    QVERIFY(p.parseIncremental({e}, newSrc));

    QCOMPARE(p.inlineTreeReuseCount(), 2);

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(newSrc)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::editsInTwoRegions_reusesTheRegionBetween()
{
    QByteArray oldSrc = QByteArrayLiteral(
        "alpha line.\n\nbeta line.\n\ngamma line.");
    // Bold "alpha" and "gamma" (in old-frame coords, out of order to
    // exercise the sort path). beta line is untouched, must reuse.
    const int alphaAt = oldSrc.indexOf(QByteArrayLiteral("alpha"));
    const int gammaAt = oldSrc.indexOf(QByteArrayLiteral("gamma"));
    QVERIFY(alphaAt >= 0 && gammaAt >= 0);

    QByteArray newSrc = oldSrc;
    newSrc.replace(gammaAt, 5, QByteArrayLiteral("**gamma**"));
    newSrc.replace(alphaAt, 5, QByteArrayLiteral("**alpha**"));

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(oldSrc)));

    QList<ByteEdit> edits = {
        ByteEdit{ static_cast<quint32>(gammaAt),
                  static_cast<quint32>(gammaAt + 5),
                  9u },
        ByteEdit{ static_cast<quint32>(alphaAt),
                  static_cast<quint32>(alphaAt + 5),
                  9u },
    };
    QVERIFY(p.parseIncremental(edits, newSrc));

    QCOMPARE(p.inlineTreeReuseCount(), 1);  // beta line reused

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(newSrc)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::noEdits_reusesAllInlineRegions()
{
    QByteArray src = QByteArrayLiteral(
        "# Heading\n\nfirst paragraph.\n\nsecond paragraph.");

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(src)));

    QVERIFY(p.parseIncremental({}, src));

    // 3 inline regions: heading text, paragraph 1, paragraph 2.
    QCOMPARE(p.inlineTreeReuseCount(), 3);

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(src)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::blockChangedByteCount_initialAndIncremental()
{
    Markoff::TreeSitterParser parser;
    QVERIFY(parser.parse(QStringLiteral("# Heading\n\nA paragraph.\n")));
    // After a fresh parse there is no previous tree to compare against.
    QCOMPARE(parser.blockChangedByteCount(), -1);

    // Insert a single character at the end of the paragraph.
    Markoff::ByteEdit edit;
    edit.oldStart = 23;          // before final '\n'
    edit.oldEnd   = 23;
    edit.newLength = 1;
    QByteArray newBuf = QByteArrayLiteral("# Heading\n\nA paragraph!.\n");
    QVERIFY(parser.parseIncremental({edit}, newBuf));

    const int changed = parser.blockChangedByteCount();
    QVERIFY2(changed >= 0, "after parseIncremental the counter must be set");
    QVERIFY2(changed <= 64, "a one-char edit on a tiny doc must produce a small changed-bytes total");
}

QTEST_APPLESS_MAIN(TstIncrementalParse)
#include "tst_incremental_parse.moc"
