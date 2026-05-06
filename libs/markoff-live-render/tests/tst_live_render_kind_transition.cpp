// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "../src/KindTransition.h"
#include <markoff/live-render/BlockKind.h>

using namespace Markoff::LiveRender;

class TstKindTransition : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void heading_h1()           { QCOMPARE(inferBlockKind(QStringLiteral("# Hello")),    BlockKind::Heading); }
    void heading_h6()           { QCOMPARE(inferBlockKind(QStringLiteral("###### x")),   BlockKind::Heading); }
    void heading_no_space()     { QCOMPARE(inferBlockKind(QStringLiteral("#Hello")),      BlockKind::Paragraph); }
    void code_block_backtick()  { QCOMPARE(inferBlockKind(QStringLiteral("```cpp")),     BlockKind::CodeBlock); }
    void code_block_tilde()     { QCOMPARE(inferBlockKind(QStringLiteral("~~~")),        BlockKind::CodeBlock); }
    void hr_dashes()            { QCOMPARE(inferBlockKind(QStringLiteral("---")),        BlockKind::HorizontalRule); }
    void hr_stars()             { QCOMPARE(inferBlockKind(QStringLiteral("***")),        BlockKind::HorizontalRule); }
    void hr_underscores()       { QCOMPARE(inferBlockKind(QStringLiteral("___")),        BlockKind::HorizontalRule); }
    void image_detected()       { QCOMPARE(inferBlockKind(QStringLiteral("![alt](url)")), BlockKind::Image); }
    void math_display()         { QCOMPARE(inferBlockKind(QStringLiteral("$$x^2$$")),   BlockKind::Math); }
    void math_inline()          { QCOMPARE(inferBlockKind(QStringLiteral("$x$")),        BlockKind::Math); }
    void list_dash()            { QCOMPARE(inferBlockKind(QStringLiteral("- item")),     BlockKind::ListItem); }
    void list_star()            { QCOMPARE(inferBlockKind(QStringLiteral("* item")),     BlockKind::ListItem); }
    void list_plus()            { QCOMPARE(inferBlockKind(QStringLiteral("+ item")),     BlockKind::ListItem); }
    void list_ordered_dot()     { QCOMPARE(inferBlockKind(QStringLiteral("1. item")),   BlockKind::ListItem); }
    void list_ordered_paren()   { QCOMPARE(inferBlockKind(QStringLiteral("2) item")),   BlockKind::ListItem); }
    void blockquote()           { QCOMPARE(inferBlockKind(QStringLiteral("> quote")),   BlockKind::Blockquote); }
    void paragraph_plain()      { QCOMPARE(inferBlockKind(QStringLiteral("just text")), BlockKind::Paragraph); }
    void empty_is_paragraph()   { QCOMPARE(inferBlockKind(QString{}),                   BlockKind::Paragraph); }
    void display_mode_true() {
        bool d = false;
        inferBlockKind(QStringLiteral("$$x$$"), &d);
        QVERIFY(d);
    }
    void display_mode_false() {
        bool d = true;
        inferBlockKind(QStringLiteral("$x$"), &d);
        QVERIFY(!d);
    }
    void count_leading_hashes_h3()   { QCOMPARE(countLeadingHashes(QStringLiteral("### x")), 3); }
    void count_leading_hashes_none() { QCOMPARE(countLeadingHashes(QStringLiteral("hello")),  0); }
    void count_leading_hashes_7()    { QCOMPARE(countLeadingHashes(QStringLiteral("####### x")), 0); }  // >6 = not heading
};
QTEST_MAIN(TstKindTransition)
#include "tst_live_render_kind_transition.moc"
