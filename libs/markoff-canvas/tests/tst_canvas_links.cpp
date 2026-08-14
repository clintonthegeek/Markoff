// SPDX-License-Identifier: GPL-3.0-or-later
//
// P4.2 — link/wikilink/tag activation + hover (contract-v2 plan).
//
// Click (read-only) / Ctrl+click (editing) on a link/wikilink/tag hit routes
// through View::linkActivationAt (the same hitTest()-derived CanvasCursor
// every other mouse path in this file uses — no second hit-test mechanism)
// into the consumer-owned Markoff::LinkService: `activate()` for clicks,
// `notifyHover()`/`notifyHoverLeft()` for hover transitions. This view emits
// no wrapper signals of its own — tests observe LinkService's own
// linkActivated/linkHovered/linkHoverLeft signals directly, same as live and
// styled do.
//
// Pixel positions below are NOT guessed offsets into the block's raw text —
// they are computed from the SAME projection map + font the production
// paint path builds (pointForFullQChar), so a click can be aimed at an
// EXACT QChar boundary (the first or last character of a span) rather than
// "somewhere in the middle". That precision is what makes
// activation_hits_first_and_last_qchar_of_span a real falsification target
// for an off-by-one in the span-covers-position check (plan's own named
// falsification for this task).

#include <QSignalSpy>
#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/DefaultLinkService.h>
#include <markoff/core/LinkActivation.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

// Private to src/, same directory as View's sources — reached directly here
// (not through the public View.h surface), same pattern as
// tst_canvas_inline_formatting.cpp / tst_canvas_projection.cpp: building an
// exact-pixel click target needs the same span-omission + font machinery
// the production paint path uses, not anything a widget consumer should see.
#include "../src/BlockPresentation.h"
#include "../src/InlineFormatting.h"
#include "../src/ProjectionMap.h"

using Markoff::BlockId;
using Markoff::Canvas::ProjectionMap;
using Markoff::Canvas::View;
using Markoff::DefaultLinkService;
using Markoff::LinkActivation;
using Markoff::LinkKind;
using Markoff::MarkoffDocument;

namespace {

// Viewport-coordinate point that lands exactly on `fullQChar` (a QChar
// index in the block's own RAW text, the same space SourceSpan::charOffset
// uses) — built from the identical ProjectionMap + font the production
// BlockLayoutCache uses for this block, so the click is byte-exact rather
// than "somewhere in the visible text". Assumes a single unwrapped line
// (true for every fixture below) and no active caret in `block` (so every
// delimiter run is hidden, matching the read-only/no-caret hover/click
// scenarios under test).
QPoint pointForFullQChar(MarkoffDocument &doc, const View &view, BlockId block, int fullQChar)
{
    const QByteArray text = doc.blockText(block);
    const auto spans = doc.inlineSpansFor(block);
    const auto omitted = Markoff::Canvas::Detail::omittedDelimiterRanges(spans, {});
    const ProjectionMap proj = ProjectionMap::build(text, omitted);
    const int layoutQChar = proj.fullQCharToLayoutQChar(fullQChar);

    const Markoff::Theme theme = Markoff::Theme::defaultLight();
    const auto style = Markoff::Canvas::presentationFor(doc, block, theme);

    QTextLayout layout(proj.layoutText());
    layout.setFont(style.font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    line.setLineWidth(100000);
    layout.endLayout();

    const qreal x = line.cursorToX(layoutQChar);
    const QRectF rect = view.blockRect(block);
    return QPoint(int(rect.x() + x) + 1, int(rect.y()) + 8);
}

}  // namespace

class TstCanvasLinks : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<LinkActivation>(); }

    void click_activates_external_link_when_readonly();
    void plain_click_does_not_activate_link_while_editing();
    void ctrl_click_activates_link_while_editing();
    void click_activates_wikilink();
    void click_activates_tag();
    void activation_hits_first_and_last_qchar_of_span();
    void hover_emits_signal_sets_cursor_and_caches_shape();
    void hover_leaves_on_move_off_link();
};

// "[link](http://example.com)": '[' at byte 0, link_text "link" bytes 1-4,
// ')' the last byte. link_text's SourceSpan (isLink) covers full-QChar
// range [1, 5) — ASCII throughout, so QChar index == byte offset.
void TstCanvasLinks::click_activates_external_link_when_readonly()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("[link](http://example.com)\n");
    DefaultLinkService svc;
    QSignalSpy activated(&svc, &Markoff::LinkService::linkActivated);

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setLinkService(&svc);
    view.setReadOnly(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QPoint p = pointForFullQChar(doc, view, block, 2);  // middle of "link"

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, p);

    QCOMPARE(activated.count(), 1);
    const auto a = qvariant_cast<LinkActivation>(activated.at(0).at(0));
    QCOMPARE(a.kind, LinkKind::External);
    QCOMPARE(a.rawText, QStringLiteral("http://example.com"));
}

void TstCanvasLinks::plain_click_does_not_activate_link_while_editing()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("[link](http://example.com)\n");
    DefaultLinkService svc;
    QSignalSpy activated(&svc, &Markoff::LinkService::linkActivated);

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setLinkService(&svc);
    view.setReadOnly(false);  // editing — plain click must place the caret,
                              // not activate (spec §5.2 / plan P4.2).
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QPoint p = pointForFullQChar(doc, view, block, 2);

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, p);

    QCOMPARE(activated.count(), 0);
    QCOMPARE(view.caretBlock(), block);  // the click still placed the caret
}

void TstCanvasLinks::ctrl_click_activates_link_while_editing()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("[link](http://example.com)\n");
    DefaultLinkService svc;
    QSignalSpy activated(&svc, &Markoff::LinkService::linkActivated);

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setLinkService(&svc);
    view.setReadOnly(false);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QPoint p = pointForFullQChar(doc, view, block, 2);

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ControlModifier, p);

    QCOMPARE(activated.count(), 1);
    const auto a = qvariant_cast<LinkActivation>(activated.at(0).at(0));
    QCOMPARE(a.kind, LinkKind::External);
    QCOMPARE(a.rawText, QStringLiteral("http://example.com"));
}

// "[[Target]]": wiki_link span itself carries linkTarget.page — no
// LinkService::classify() round-trip needed (mirrors live's buildActivation).
void TstCanvasLinks::click_activates_wikilink()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("[[Target]]\n");
    DefaultLinkService svc;
    QSignalSpy activated(&svc, &Markoff::LinkService::linkActivated);

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setLinkService(&svc);
    view.setReadOnly(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    // "Target" is the visible layout text (brackets hidden); land in the
    // middle of it.
    const QPoint p = pointForFullQChar(doc, view, block, 4);

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, p);

    QCOMPARE(activated.count(), 1);
    const auto a = qvariant_cast<LinkActivation>(activated.at(0).at(0));
    QCOMPARE(a.kind, LinkKind::WikiLink);
    QCOMPARE(a.page, QStringLiteral("Target"));
}

// "before #atag after": tag span has no LinkTarget payload — its own char
// range IS the tag text, "#" included (see View::linkActivationAt's isTag
// branch doc comment).
void TstCanvasLinks::click_activates_tag()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("before #atag after\n");
    DefaultLinkService svc;
    QSignalSpy activated(&svc, &Markoff::LinkService::linkActivated);

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setLinkService(&svc);
    view.setReadOnly(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QCOMPARE(doc.blockText(block), QByteArray("before #atag after"));
    // The tag span covers "#atag" itself (charOffset 7, charLength 5,
    // '#' included — no LinkTarget payload the way link/wikilink spans
    // have). Land in the middle of it.
    const QPoint p = pointForFullQChar(doc, view, block, 10);

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, p);

    QCOMPARE(activated.count(), 1);
    const auto a = qvariant_cast<LinkActivation>(activated.at(0).at(0));
    QCOMPARE(a.kind, LinkKind::Tag);
    QCOMPARE(a.rawText, QStringLiteral("#atag"));
}

// The plan's own named falsification target for this task: an off-by-one
// in the hit-test span range. Exercised directly by clicking the FIRST and
// LAST QChar of the link_text span (full-QChar [1, 5) for "link") — a
// boundary shift in either direction (`<` vs `<=`, `>=` vs `>`) drops one of
// these two clicks while a middle-of-span click (the other tests above)
// would still pass, which is why this case exists as its own test rather
// than folding into click_activates_external_link_when_readonly.
void TstCanvasLinks::activation_hits_first_and_last_qchar_of_span()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("[link](http://example.com)\n");
    DefaultLinkService svc;
    QSignalSpy activated(&svc, &Markoff::LinkService::linkActivated);

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setLinkService(&svc);
    view.setReadOnly(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();

    // First QChar of link_text ('l', full-QChar 1).
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      pointForFullQChar(doc, view, block, 1));
    QCOMPARE(activated.count(), 1);

    // Last QChar of link_text ('k', full-QChar 4 — charOffset(1) + charLength(4) - 1).
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      pointForFullQChar(doc, view, block, 4));
    QCOMPARE(activated.count(), 2);
}

void TstCanvasLinks::hover_emits_signal_sets_cursor_and_caches_shape()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("before [link](http://example.com) after\n");
    DefaultLinkService svc;
    QSignalSpy hovered(&svc, &Markoff::LinkService::linkHovered);
    QSignalSpy hoverLeft(&svc, &Markoff::LinkService::linkHoverLeft);

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setLinkService(&svc);
    view.setReadOnly(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QCOMPARE(doc.blockText(block), QByteArray("before [link](http://example.com) after"));
    // "before " is 7 bytes; '[' at 7, "link" at [8, 12).
    const QPoint onLink = pointForFullQChar(doc, view, block, 10);

    QTest::mouseMove(view.viewport(), onLink);

    QCOMPARE(hovered.count(), 1);
    const auto a = qvariant_cast<LinkActivation>(hovered.at(0).at(0));
    QCOMPARE(a.rawText, QStringLiteral("http://example.com"));
    QCOMPARE(view.viewport()->cursor().shape(), Qt::PointingHandCursor);

    // A second move that is STILL over the same link must not re-fire
    // notifyHover, nor touch the cursor a second time (the "styled smell"
    // this task was told to avoid: setCursor() on every MouseMove within
    // the same link — libs/markoff-styled/src/LinkInteraction.cpp:handleMove).
    QTest::mouseMove(view.viewport(), onLink + QPoint(1, 0));
    QCOMPARE(hovered.count(), 1);
    QCOMPARE(hoverLeft.count(), 0);
    QCOMPARE(view.viewport()->cursor().shape(), Qt::PointingHandCursor);
}

void TstCanvasLinks::hover_leaves_on_move_off_link()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("before [link](http://example.com) after\n");
    DefaultLinkService svc;
    QSignalSpy hovered(&svc, &Markoff::LinkService::linkHovered);
    QSignalSpy hoverLeft(&svc, &Markoff::LinkService::linkHoverLeft);

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setLinkService(&svc);
    view.setReadOnly(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QPoint onLink = pointForFullQChar(doc, view, block, 10);
    // "after" starts at full-QChar 35 ("before [link](http://example.com) "
    // is 35 bytes); land in the middle of it.
    const QPoint offLink = pointForFullQChar(doc, view, block, 37);

    QTest::mouseMove(view.viewport(), onLink);
    QCOMPARE(hovered.count(), 1);
    QCOMPARE(view.viewport()->cursor().shape(), Qt::PointingHandCursor);

    QTest::mouseMove(view.viewport(), offLink);
    QCOMPARE(hoverLeft.count(), 1);
    QCOMPARE(hoverLeft.at(0).at(0).toString(), QStringLiteral("http://example.com"));
    // unsetCursor() reverts to the viewport's ordinary (unset) cursor — this
    // view never sets an I-beam override, so "not a link" means "default
    // arrow", not a text-editing cursor (a leaf-local choice; nothing this
    // task's scope covers changes it).
    QCOMPARE(view.viewport()->cursor().shape(), Qt::ArrowCursor);
}

QTEST_MAIN(TstCanvasLinks)
#include "tst_canvas_links.moc"
