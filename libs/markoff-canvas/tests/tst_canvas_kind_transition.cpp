// SPDX-License-Identifier: GPL-3.0-or-later
//
// T6 — kind transitions (exit E5), grown at P1.1 (heading levels, setext
// form, math display mode) once the inference rules moved to
// markoff-core's KindInference.
//
// Assertions run against the production widget (Markoff::Canvas::View)
// through its real event path: show it, click to place the caret, drive
// real QTest::keyClicks. The canvas convention decided in T6 (spec §9,
// overriding the plan's original "strip the prefix" step): a promoted
// block's buffer keeps whatever text matched the inference rule — only the
// kind changes — so a typed heading's buffer matches a loaded heading's.

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/KindInference.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::BlockKind;
using Markoff::Canvas::View;

namespace {

/// Read an int attr off a block, or `fallback` when absent/other-typed.
int intAttr(const Markoff::MarkoffDocument &doc, BlockId id,
            const Markoff::AttrName &name, int fallback = -1)
{
    const auto attrs = doc.blockAttrs(id);
    const auto it = attrs.constFind(name);
    if (it == attrs.constEnd()) return fallback;
    const int *v = std::get_if<int>(&it.value());
    return v ? *v : fallback;
}

/// Read a QString attr off a block, or an empty string when absent.
QString stringAttr(const Markoff::MarkoffDocument &doc, BlockId id,
                   const Markoff::AttrName &name)
{
    const auto attrs = doc.blockAttrs(id);
    const auto it = attrs.constFind(name);
    if (it == attrs.constEnd()) return {};
    const QString *v = std::get_if<QString>(&it.value());
    return v ? *v : QString();
}

/// Read a bool attr off a block, or `fallback` when absent/other-typed.
bool boolAttr(const Markoff::MarkoffDocument &doc, BlockId id,
              const Markoff::AttrName &name, bool fallback = false)
{
    const auto attrs = doc.blockAttrs(id);
    const auto it = attrs.constFind(name);
    if (it == attrs.constEnd()) return fallback;
    const bool *v = std::get_if<bool>(&it.value());
    return v ? *v : fallback;
}

/// Place the caret at byte 0 of `block` by clicking its rect, the way a
/// user would. Returns false if the click didn't land where expected.
bool clickAtBlockStart(View &view, BlockId block)
{
    const QRectF rect = view.blockRect(block);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 1, int(rect.y()) + 8));
    return view.caretBlock() == block && view.caretByteOffset() == 0;
}

}  // namespace

class TstCanvasKindTransition : public QObject {
    Q_OBJECT

private slots:
    void hash_space_promotes_heading_caret_survives();
    void atx_hashes_set_heading_level_data();
    void atx_hashes_set_heading_level();
    void setext_underline_promotes_with_level_data();
    void setext_underline_promotes_with_level();
    void dollar_promotes_inline_math_with_display_attr();
    void inference_reports_display_math_for_double_dollar();
    void typing_second_dollar_sets_display_mode();
};

void TstCanvasKindTransition::hash_space_promotes_heading_caret_survives()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(block), BlockKind::Paragraph);

    QVERIFY(clickAtBlockStart(view, block));

    QTest::keyClicks(&view, QStringLiteral("# "));

    // Kind promotes; the buffer keeps the matched "# " (no strip — T6's
    // reading of the T1 finding).
    QCOMPARE(doc.blockKind(block), BlockKind::Heading);
    QCOMPARE(doc.blockText(block), QByteArray("# Hello world."));

    // Caret is exactly where typing left it: byte 2, right after "# ".
    QCOMPARE(view.caretBlock(), block);
    QCOMPARE(view.caretByteOffset(), 2);

    QVERIFY(view.hasFocus());
}

void TstCanvasKindTransition::atx_hashes_set_heading_level_data()
{
    QTest::addColumn<QString>("typed");
    QTest::addColumn<int>("level");

    QTest::newRow("h1") << QStringLiteral("# ")      << 1;
    QTest::newRow("h2") << QStringLiteral("## ")     << 2;
    QTest::newRow("h3") << QStringLiteral("### ")    << 3;
    QTest::newRow("h4") << QStringLiteral("#### ")   << 4;
    QTest::newRow("h5") << QStringLiteral("##### ")  << 5;
    QTest::newRow("h6") << QStringLiteral("###### ") << 6;
}

void TstCanvasKindTransition::atx_hashes_set_heading_level()
{
    QFETCH(QString, typed);
    QFETCH(int, level);

    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QVERIFY(clickAtBlockStart(view, block));

    // The first '#' already promotes the block to a Heading; the rest have
    // to keep raising the level on an *already*-Heading block.
    QTest::keyClicks(&view, typed);

    QCOMPARE(doc.blockKind(block), BlockKind::Heading);
    QCOMPARE(doc.blockText(block), (typed + QStringLiteral("Hello world.")).toUtf8());
    QCOMPARE(intAttr(doc, block, Markoff::AttrNames::Level), level);
    QCOMPARE(stringAttr(doc, block, Markoff::AttrNames::HeadingForm),
             QStringLiteral("atx"));

    // The level attr is what the serializer emits from: without it, every
    // typed heading saves as an H1 regardless of how many hashes were typed.
    QCOMPARE(doc.serializeForSave(),
             (typed + QStringLiteral("Hello world.\n")).toUtf8());

    // The caret sits right after the typed marker, in the same block.
    QCOMPARE(view.caretBlock(), block);
    QCOMPARE(view.caretByteOffset(), typed.size());
}

void TstCanvasKindTransition::setext_underline_promotes_with_level_data()
{
    QTest::addColumn<QString>("underline");
    QTest::addColumn<int>("level");

    QTest::newRow("equals-h1") << QStringLiteral("=") << 1;
    QTest::newRow("dashes-h2") << QStringLiteral("-") << 2;
}

void TstCanvasKindTransition::setext_underline_promotes_with_level()
{
    QFETCH(QString, underline);
    QFETCH(int, level);

    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QVERIFY(clickAtBlockStart(view, block));

    // End of the line, soft break (Shift+Enter — a literal '\n' in the same
    // block, not a split), then the underline character.
    QTest::keyClick(&view, Qt::Key_End);
    QCOMPARE(view.caretByteOffset(), 12);
    QTest::keyClick(&view, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(doc.blockKind(block), BlockKind::Paragraph);  // not yet setext
    QTest::keyClicks(&view, underline);

    QCOMPARE(doc.blockKind(block), BlockKind::Heading);
    QCOMPARE(intAttr(doc, block, Markoff::AttrNames::Level), level);
    QCOMPARE(stringAttr(doc, block, Markoff::AttrNames::HeadingForm),
             QStringLiteral("setext"));

    // Setext is the one promotion that rewrites the buffer: the loaded
    // representation of a setext heading is content-only (the underline is
    // rebuilt from `level` at save time), so a typed one must match it —
    // otherwise saving doubles the underline.
    QCOMPARE(doc.blockText(block), QByteArray("Hello world."));
    QCOMPARE(view.caretBlock(), block);
    QCOMPARE(view.caretByteOffset(), 12);

    const QByteArray rule(12, underline.at(0).toLatin1());
    QCOMPARE(doc.serializeForSave(), "Hello world.\n" + rule + "\n");

    // Same bytes a loaded setext heading of that level would have.
    Markoff::MarkoffDocument loaded;
    loaded.loadFromMarkdown("Hello world.\n" + rule + "\n");
    const BlockId loadedBlock = loaded.iterateBlocks().front();
    QCOMPARE(loaded.blockText(loadedBlock), doc.blockText(block));
    QCOMPARE(intAttr(loaded, loadedBlock, Markoff::AttrNames::Level), level);
}

void TstCanvasKindTransition::dollar_promotes_inline_math_with_display_attr()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QVERIFY(clickAtBlockStart(view, block));

    QTest::keyClicks(&view, QStringLiteral("$"));

    QCOMPARE(doc.blockKind(block), BlockKind::Math);
    // The display-mode attr is written in the promoting transaction, not
    // left to default (P1.1 / T6 finding).
    QVERIFY(doc.blockAttrs(block).contains(Markoff::AttrNames::DisplayMode));
    QCOMPARE(boolAttr(doc, block, Markoff::AttrNames::DisplayMode, true), false);
}

void TstCanvasKindTransition::inference_reports_display_math_for_double_dollar()
{
    // The FIRST '$' always promotes to inline Math before a second '$' can
    // arrive (promotion only runs on Paragraphs), so the buffer-level
    // inference rule itself distinguishes the two forms rather than typing
    // ever landing on Math with two markers already present. P5.3 (below)
    // uses this same distinction via a re-infer-within-Math path
    // (View::updateCaretMathDisplayMode) to reach display mode by typing.
    const Markoff::KindInference inline_ = Markoff::inferBlockKind(QStringLiteral("$x$"));
    QCOMPARE(inline_.kind, BlockKind::Math);
    QCOMPARE(inline_.mathDisplay, false);

    const Markoff::KindInference display = Markoff::inferBlockKind(QStringLiteral("$$x$$"));
    QCOMPARE(display.kind, BlockKind::Math);
    QCOMPARE(display.mathDisplay, true);
}

// P5.3: closes the P1.1 finding ("display math is unreachable by typing...
// left for whoever needs display math"). The second '$' arrives while the
// block is already Math (not Paragraph), so promoteCaretBlockKind's normal
// guard would skip it entirely without the dedicated Math branch added
// this task (View::updateCaretMathDisplayMode, mirrors
// updateCaretHeadingLevel's raise-only shape).
void TstCanvasKindTransition::typing_second_dollar_sets_display_mode()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QVERIFY(clickAtBlockStart(view, block));

    QTest::keyClicks(&view, QStringLiteral("$"));
    QCOMPARE(doc.blockKind(block), BlockKind::Math);
    QCOMPARE(boolAttr(doc, block, Markoff::AttrNames::DisplayMode, true), false);

    QTest::keyClicks(&view, QStringLiteral("$"));
    QCOMPARE(doc.blockKind(block), BlockKind::Math);
    QCOMPARE(boolAttr(doc, block, Markoff::AttrNames::DisplayMode, false), true);

    // Content typed after the display promotion doesn't need to re-infer
    // again (already display) — buffer just keeps growing normally (the
    // caret is at byte 2, right after the two typed '$'s, ahead of the
    // pre-existing "Hello world." text — same "typed prefix" shape every
    // other promotion test in this file already exercises).
    QTest::keyClicks(&view, QStringLiteral("x^2$$"));
    QCOMPARE(doc.blockText(block), QByteArray("$$x^2$$Hello world."));
    QCOMPARE(boolAttr(doc, block, Markoff::AttrNames::DisplayMode, false), true);
}

QTEST_MAIN(TstCanvasKindTransition)
#include "tst_canvas_kind_transition.moc"
