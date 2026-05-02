// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextCharFormat>

#include <markoff/view/qml/InlineFormatHighlighter.h>

using namespace Markoff::View::Qml;

namespace {

/// Attach a highlighter directly to a QTextDocument (bypassing QQuickTextDocument).
/// Calls load() to set document text and highlighter source together so the
/// highlighter runs on the correct content.
struct Helper {
    QTextDocument doc;
    InlineFormatHighlighter h;

    Helper() : h(nullptr) {
        h.setDocument(&doc);
    }

    void load(const QString &text) {
        doc.setPlainText(text);
        h.setSource(text);
    }

    /// Return all QSyntaxHighlighter format ranges for the first text block.
    QList<QTextLayout::FormatRange> formats() const {
        const QTextBlock block = doc.firstBlock();
        if (!block.isValid() || !block.layout()) return {};
        return block.layout()->formats();
    }

    /// Return the merged QTextCharFormat for character at position `pos`
    /// (absolute document offset) from the syntax-highlighter format ranges.
    QTextCharFormat mergedFormatAt(int pos) const {
        QTextCharFormat merged;
        for (const QTextLayout::FormatRange &fr : formats()) {
            if (fr.start <= pos && pos < fr.start + fr.length)
                merged.merge(fr.format);
        }
        return merged;
    }

    bool anyBold(int start, int end) const {
        for (int i = start; i < end; ++i) {
            if (mergedFormatAt(i).fontWeight() >= QFont::Bold) return true;
        }
        return false;
    }

    bool anyItalic(int start, int end) const {
        for (int i = start; i < end; ++i) {
            if (mergedFormatAt(i).fontItalic()) return true;
        }
        return false;
    }

    bool anyStrikethrough(int start, int end) const {
        for (int i = start; i < end; ++i) {
            if (mergedFormatAt(i).fontStrikeOut()) return true;
        }
        return false;
    }

    bool anyCode(int start, int end) const {
        for (int i = start; i < end; ++i) {
            const QTextCharFormat fmt = mergedFormatAt(i);
            const QStringList families = fmt.fontFamilies().toStringList();
            if (!families.isEmpty()
                && families.first().toLower().contains(QStringLiteral("mono")))
                return true;
        }
        return false;
    }

    bool anyUnderline(int start, int end) const {
        for (int i = start; i < end; ++i) {
            if (mergedFormatAt(i).fontUnderline()) return true;
        }
        return false;
    }

    bool anyHighlight(int start, int end) const {
        for (int i = start; i < end; ++i) {
            const QTextCharFormat fmt = mergedFormatAt(i);
            const QColor bgColor = fmt.background().color();
            if (bgColor.isValid() && bgColor == QColor(0xff, 0xff, 0x00))
                return true;
        }
        return false;
    }
};

}  // namespace

class TstInlineFormatHighlighter : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void bold_applied_to_inner_text() {
        // "**hello**" — the word "hello" (chars 2..7) should be bold.
        Helper h;
        h.load(QStringLiteral("**hello**"));
        QVERIFY(h.anyBold(2, 7));
    }

    void italic_applied_to_inner_text() {
        // "*world*" — "world" (chars 1..6) should be italic.
        Helper h;
        h.load(QStringLiteral("*world*"));
        QVERIFY(h.anyItalic(1, 6));
    }

    void inline_code_applied() {
        // "`code`" — "code" (chars 1..5) should have monospace formatting.
        Helper h;
        h.load(QStringLiteral("`code`"));
        QVERIFY(h.anyCode(1, 5));
    }

    void highlight_applied() {
        // "==highlighted==" — "highlighted" (chars 2..13) should have yellow background.
        Helper h;
        h.load(QStringLiteral("==highlighted=="));
        QVERIFY(h.anyHighlight(2, 13));
    }

    void strikethrough_applied() {
        // "~~del~~" — "del" (chars 2..5) should be strikethrough.
        Helper h;
        h.load(QStringLiteral("~~del~~"));
        QVERIFY(h.anyStrikethrough(2, 5));
    }

    void link_text_underlined() {
        // "[text](url)" — "text" (chars 1..5) should be underlined.
        Helper h;
        h.load(QStringLiteral("[text](url)"));
        QVERIFY(h.anyUnderline(1, 5));
    }

    void plain_text_no_formatting() {
        // Plain text has no formatting spans (bold/italic/etc.).
        Helper h;
        h.load(QStringLiteral("just plain text"));
        QVERIFY(!h.anyBold(0, 15));
        QVERIFY(!h.anyItalic(0, 15));
        QVERIFY(!h.anyStrikethrough(0, 15));
    }

    void empty_source_no_crash() {
        Helper h;
        h.load(QString());
        QCOMPARE(h.doc.toPlainText(), QString());
    }

    void source_change_triggers_rehighlight() {
        Helper h;
        h.load(QStringLiteral("plain"));
        QVERIFY(!h.anyBold(0, 5));

        h.load(QStringLiteral("**bold**"));
        // "bold" at offset 2..6.
        QVERIFY(h.anyBold(2, 6));
    }

    void mixed_bold_italic() {
        // "**bold *italic***" — bold covers the whole inside; italic covers "italic".
        Helper h;
        h.load(QStringLiteral("**bold *italic***"));
        QVERIFY(h.anyBold(2, 15));
        QVERIFY(h.anyItalic(8, 14));
    }

    // --- Speculative open-delimiter tests ---

    void speculative_bold_open_delimiter() {
        // "**hello" — unclosed ** → "hello" (chars 2..7) is speculatively bold.
        Helper h;
        h.load(QStringLiteral("**hello"));
        QVERIFY(h.anyBold(2, 7));
    }

    void speculative_closes_when_parser_confirms() {
        // "**hello**" — parser confirms bold; chars 2..7 must be bold.
        Helper h;
        h.load(QStringLiteral("**hello**"));
        QVERIFY(h.anyBold(2, 7));
    }

    void speculative_italic_open_delimiter() {
        // "*world" — unclosed * → "world" (chars 1..6) is speculatively italic.
        Helper h;
        h.load(QStringLiteral("*world"));
        QVERIFY(h.anyItalic(1, 6));
    }

    void speculative_underscore_italic_open_delimiter() {
        // "_italic" — unclosed _ → "italic" (chars 1..7) is speculatively italic.
        Helper h;
        h.load(QStringLiteral("_italic"));
        QVERIFY(h.anyItalic(1, 7));
    }

    void speculative_code_open_delimiter() {
        // "`code" — unclosed ` → "code" (chars 1..5) is speculatively code-styled.
        Helper h;
        h.load(QStringLiteral("`code"));
        QVERIFY(h.anyCode(1, 5));
    }

    void speculative_strikethrough_open_delimiter() {
        // "~~strike" — unclosed ~~ → "strike" (chars 2..8) is speculatively strikethrough.
        Helper h;
        h.load(QStringLiteral("~~strike"));
        QVERIFY(h.anyStrikethrough(2, 8));
    }

    void speculative_highlight_open_delimiter() {
        // "==bright" — unclosed == → "bright" (chars 2..8) is speculatively highlighted.
        Helper h;
        h.load(QStringLiteral("==bright"));
        QVERIFY(h.anyHighlight(2, 8));
    }

    void speculative_double_underscore_bold() {
        // "__text" — unclosed __ → "text" (chars 2..6) is speculatively bold.
        Helper h;
        h.load(QStringLiteral("__text"));
        QVERIFY(h.anyBold(2, 6));
    }

    /// C-9: bold formatting on the first visual line of a multi-line
    /// paragraph must NOT bleed into subsequent QTextBlocks (visual lines).
    /// QSyntaxHighlighter::highlightBlock is invoked once per QTextBlock;
    /// without translating predictions through currentBlock().position()
    /// each line was getting the same setFormat(charStart, ..., fmt) call,
    /// duplicating the styling at the same in-line offsets on every line.
    void multiline_bold_does_not_repeat_per_visual_line() {
        // First line has bold "**hello**" (chars 0..9 with delimiters,
        // content 2..7). Second line is plain. The highlighter must
        // bold chars 2..7 of LINE 1 only — chars 2..7 of LINE 2 must
        // remain unstyled.
        QTextDocument doc;
        InlineFormatHighlighter h(nullptr);
        h.setDocument(&doc);

        const QString text = QStringLiteral("**hello** more\nplain second line");
        doc.setPlainText(text);
        h.setSource(text);

        const QTextBlock first = doc.firstBlock();
        const QTextBlock second = first.next();
        QVERIFY(second.isValid());

        // Line 1: bold expected on the inner "hello".
        QTextCharFormat fmt0;
        for (const auto &fr : first.layout()->formats()) {
            if (fr.start <= 2 && 2 < fr.start + fr.length) fmt0.merge(fr.format);
        }
        QVERIFY(fmt0.fontWeight() >= QFont::Bold);

        // Line 2: chars 2..7 must NOT be bold.
        for (int i = 2; i < 7; ++i) {
            QTextCharFormat fmt;
            for (const auto &fr : second.layout()->formats()) {
                if (fr.start <= i && i < fr.start + fr.length) fmt.merge(fr.format);
            }
            QVERIFY2(fmt.fontWeight() < QFont::Bold,
                     qPrintable(QStringLiteral("line2 char %1 should not be bold").arg(i)));
        }
    }

    /// A confirmed inline format on line 1 (parser span) must not repeat
    /// at the same in-line offset on line 2 of the same paragraph. The
    /// pre-fix bug: setFormat(charOffset, charLength, fmt) ran per
    /// QTextBlock with charOffset relative to the whole-block source —
    /// so on QTextBlock 1 (line 2) it bolded the chars at indices
    /// [charOffset, charOffset+charLength) of LINE 2 instead of leaving
    /// line 2 unformatted.
    void multiline_confirmed_bold_does_not_repeat() {
        QTextDocument doc;
        InlineFormatHighlighter h(nullptr);
        h.setDocument(&doc);

        // Closed bold on line 1; line 2 plain. The parser confirms bold
        // on chars 2..7 of the whole-block source. After the fix, that
        // range only lands on line 1.
        const QString text = QStringLiteral("**hello** rest of line one\nplain second line here");
        doc.setPlainText(text);
        h.setSource(text);

        const QTextBlock first  = doc.firstBlock();
        const QTextBlock second = first.next();
        QVERIFY(second.isValid());

        // Line 2 chars 2..7 must NOT be bold.
        for (int i = 2; i < 7; ++i) {
            QTextCharFormat fmt;
            for (const auto &fr : second.layout()->formats()) {
                if (fr.start <= i && i < fr.start + fr.length) fmt.merge(fr.format);
            }
            QVERIFY2(fmt.fontWeight() < QFont::Bold,
                     qPrintable(QStringLiteral("line2 char %1 should not be bold").arg(i)));
        }
    }

    void speculative_code_does_not_format_nested() {
        // "**before** `inside` **after" — "inside" is confirmed code (monospace),
        // "after" is speculatively bold from the unclosed final **.
        // Key check: "inside" is NOT bold (it's inside code), "after" IS bold.
        Helper h;
        h.load(QStringLiteral("**before** `inside` **after"));
        // "after" starts at index 22 ("**after" → ** at 20, content at 22)
        QVERIFY(h.anyBold(22, 27));
        // "inside" is at chars 12..18 — should be code-styled, not bold
        QVERIFY(h.anyCode(12, 18));
        QVERIFY(!h.anyBold(12, 18));
    }
};

QTEST_MAIN(TstInlineFormatHighlighter)
#include "tst_view_qml_inline_format_highlighter.moc"
