// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase A measurement test for spec §9.Q2.
// Verifies that Qt's negative QFont::letterSpacing(AbsoluteSpacing) does
// in fact collapse a glyph's effective advance. If this fails on the host
// platform, the implementation must fall back to setFontStretch(1) and
// the spec's Fallback A path.
#include <QFontMetricsF>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
#include <QTest>

class TestE2ZeroWidthMechanism : public QObject {
    Q_OBJECT
private slots:
    void negative_letter_spacing_absorbs_glyph_advance() {
        QTextDocument doc;
        doc.setPlainText("**hello**");

        // Measure baseline width (no formatting applied).
        const qreal baselineWidth = doc.idealWidth();

        // Apply hide format to the two leading and two trailing asterisks.
        QFont f = doc.defaultFont();
        const qreal asteriskAdvance =
            QFontMetricsF(f).horizontalAdvance(QChar(u'*'));
        f.setLetterSpacing(QFont::AbsoluteSpacing, -asteriskAdvance);
        QTextCharFormat hidden;
        hidden.setFont(f);

        QTextCursor c(&doc);
        c.setPosition(0);
        c.setPosition(2, QTextCursor::KeepAnchor);
        c.mergeCharFormat(hidden);
        c.setPosition(7);
        c.setPosition(9, QTextCursor::KeepAnchor);
        c.mergeCharFormat(hidden);

        // Re-measure after formatting.
        const qreal hiddenWidth = doc.idealWidth();

        // The four asterisks together have advance = 4 * asteriskAdvance.
        // After negative spacing, hiddenWidth should be roughly baselineWidth
        // - 4 * asteriskAdvance, within a 4px tolerance for kerning.
        const qreal expectedDelta = 4.0 * asteriskAdvance;
        const qreal actualDelta = baselineWidth - hiddenWidth;
        qInfo() << "baseline=" << baselineWidth << "hidden=" << hiddenWidth
                << "asteriskAdvance=" << asteriskAdvance
                << "expectedDelta=" << expectedDelta
                << "actualDelta=" << actualDelta;
        // If the mechanism does NOT collapse, actualDelta will be ~0 and
        // this test fails — flagging the platform issue described in
        // spec §9.Q2 / §2.4 Fallback A.
        QVERIFY2(actualDelta >= expectedDelta - 4.0,
                 qPrintable(QStringLiteral(
                     "negative letterSpacing did not absorb glyph advance "
                     "(expected delta ≥ %1, got %2). Fall back to "
                     "setFontStretch(1) per spec §2.4.")
                     .arg(expectedDelta - 4.0)
                     .arg(actualDelta)));
    }
};

QTEST_MAIN(TestE2ZeroWidthMechanism)
#include "tst_live_render_e2_zero_width_mechanism.moc"
