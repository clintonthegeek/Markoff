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
};

QTEST_MAIN(TstInlineFormatHighlighter)
#include "tst_view_qml_inline_format_highlighter.moc"
