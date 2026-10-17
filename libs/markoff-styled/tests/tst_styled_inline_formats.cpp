// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>
#include "CandidateDocumentFixture.h"

namespace {
QTextCharFormat formatAtChar(QTextDocument *doc, int charPos) {
    QTextCursor c(doc);
    c.setPosition(charPos);
    c.setPosition(charPos + 1, QTextCursor::KeepAnchor);
    return c.charFormat();
}
}  // namespace

class TstStyledInlineFormats : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void bold_span_sets_bold_weight() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("**bold** word"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        // Char position 2 is inside "bold" (after the **).
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 2);
        QCOMPARE(cf.fontWeight(), int(QFont::Bold));
    }

    void italic_span_sets_italic() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("*em* word"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 1);
        QVERIFY(cf.fontItalic());
    }

    void strikethrough_span_sets_strikeout() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("~~struck~~ word"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 2);
        QVERIFY(cf.fontStrikeOut());
    }

    void plain_text_has_no_emphasis() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("plain words"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 1);
        QCOMPARE(cf.fontWeight(), int(QFont::Normal));
        QVERIFY(!cf.fontItalic());
        QVERIFY(!cf.fontStrikeOut());
    }

    void inline_code_uses_monospace() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("a `inline` b"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        // 'i' in "inline" sits at char pos 3 ("a `[i]nline...").
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 3);
        QVERIFY(cf.fontFixedPitch() || cf.fontFamilies().toStringList().size() > 0);
    }

    void highlight_span_sets_background() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("==hl== word"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 2);
        QVERIFY(cf.background().style() != Qt::NoBrush);
    }

    void tag_span_distinct_foreground() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("a #tag b"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        // '#' at char 2, 't' at char 3.
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 3);
        QVERIFY(cf.foreground().style() != Qt::NoBrush);
    }

    void footnote_ref_distinct_foreground() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("a [^1] b"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 3);
        QVERIFY(cf.foreground().style() != Qt::NoBrush);
    }

    void link_span_is_anchored_and_underlined() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("[text](http://x) more"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        // "text" starts at char 1.
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 2);
        QVERIFY(cf.isAnchor());
        QVERIFY(cf.fontUnderline());
        QVERIFY(!cf.anchorHref().isEmpty());
    }

    void wikilink_span_is_anchored_and_underlined() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("[[target]] more"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        // 'target' starts at char 2.
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 3);
        QVERIFY(cf.isAnchor());
        QVERIFY(cf.fontUnderline());
    }
};

QTEST_MAIN(TstStyledInlineFormats)
#include "tst_styled_inline_formats.moc"
