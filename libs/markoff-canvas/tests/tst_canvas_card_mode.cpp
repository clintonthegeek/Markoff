// SPDX-License-Identifier: GPL-3.0-or-later
//
// Card (embedded) mode: a host that embeds the canvas leaf in a card-sized
// rectangle owns the geometry, the scrolling and the chrome. These cases pin
// the contract the host relies on.
#include <QCoreApplication>
#include <QScrollBar>
#include <QTest>
#include <QWheelEvent>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>

#include <memory>
#include <stdexcept>

using namespace Markoff;

namespace {

int g_serial = 0;

struct Fixture {
    std::unique_ptr<VNext::DocumentStorageProvider> provider;
    std::unique_ptr<MarkoffDocument> document;
};

Fixture openDocument(const QByteArray &markdown)
{
    auto provider = VNext::makeEphemeralDocumentStorageProvider();
    if (!provider.ok()) throw std::runtime_error("provider creation failed");
    Fixture fixture{std::move(provider).value(), {}};
    auto opened = MarkoffDocument::open(*fixture.provider,
        {QStringLiteral("canvas-card-%1").arg(++g_serial), 1},
        VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    if (fixture.document->importInitialMarkdown(markdown).state
        != VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial import failed");
    return fixture;
}

/// A wheel gesture over the card's viewport — the shape a real wheel event
/// takes at the leaf (viewport coordinates, no focus needed).
void sendWheel(QWidget *viewport, int delta)
{
    QWheelEvent event(QPointF(10, 10), QPointF(10, 10), QPoint(0, 0), QPoint(0, delta),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(viewport, &event);
}

}  // namespace

class TstCanvasCardMode : public QObject {
    Q_OBJECT

private slots:
    // The card arrives read-only: it is not the thing being edited until the
    // user picks it. The host promotes it with the ordinary setReadOnly(false).
    void cardArrivesReadOnlyUntilTheHostPromotesIt()
    {
        auto fixture = openDocument(QByteArrayLiteral("hello"));
        Markoff::Canvas::EditorWidget card;
        card.setDocument(fixture.document.get());
        card.show();
        QCoreApplication::processEvents();

        QVERIFY(!card.isReadOnly());
        card.setEmbeddedMode(true);
        QVERIFY2(card.isReadOnly(), "a card did not arrive read-only");
        QVERIFY(!card.hasEditing());

        // The host promotes the card being edited.
        card.setReadOnly(false);
        QVERIFY(!card.isReadOnly());
        QVERIFY(card.hasEditing());
    }

    // The scrollbar is hidden in a card, but the RANGE stays live so the
    // host can still position the window through the ordinary API.
    void cardHidesTheScrollbarButKeepsTheRangeLive()
    {
        auto fixture = openDocument(QByteArrayLiteral(
            "line one\n\nline two\n\nline three\n\nline four\n\nline five\n\n"
            "line six\n\nline seven\n\nline eight\n\nline nine\n\nline ten"));
        Markoff::Canvas::EditorWidget card;
        card.resize(400, 120);
        card.setDocument(fixture.document.get());
        card.show();
        QCoreApplication::processEvents();

        // A short viewport over a taller document: there is something to
        // scroll before the mode is on.
        QVERIFY(card.scrollPositionVisualLine() >= 0.0f);

        card.setEmbeddedMode(true);
        card.repaint();
        QCoreApplication::processEvents();
        // The bar is hidden, but its range stays live so the host can still
        // position the window. (Address the inner View's VERTICAL bar: a
        // findChild for "a scrollbar" would also match the horizontal one,
        // whose range is legitimately empty.)
        auto *inner = card.findChild<Markoff::Canvas::View *>();
        QVERIFY(inner != nullptr);
        QScrollBar *bar = inner->verticalScrollBar();
        QVERIFY2(!bar->isVisible(), "the card still shows a scrollbar");
        QVERIFY2(bar->maximum() > 0, "the card's scroll range went dead");

        // The host drives the window: the write is observable and the
        // reported position follows it.
        const float before = card.scrollPositionVisualLine();
        card.setScrollPositionVisualLine(1.0f);
        QCoreApplication::processEvents();
        QVERIFY2(card.scrollPositionVisualLine() > before,
                 "the host could not position the card window");
    }

    // A card does not scroll itself: the wheel is left for the host (a
    // QGraphicsProxyWidget forwards it to the scene, which owns the canvas
    // zoom). The card's own scroll position must not move.
    void cardDoesNotScrollItselfOnTheWheel()
    {
        auto fixture = openDocument(QByteArrayLiteral(
            "line one\n\nline two\n\nline three\n\nline four\n\nline five\n\nline six"));
        Markoff::Canvas::View view;
        view.resize(400, 120);
        view.setDocument(fixture.document.get());
        view.show();
        QCoreApplication::processEvents();

        view.setEmbeddedMode(true);
        view.repaint();
        QCoreApplication::processEvents();
        QScrollBar *bar = view.verticalScrollBar();
        QVERIFY(bar->maximum() > 0);   // the range stays live for the host
        const int before = bar->value();

        sendWheel(view.viewport(), -120);
        QCoreApplication::processEvents();
        QCOMPARE(bar->value(), before);

        sendWheel(view.viewport(), 120);
        QCoreApplication::processEvents();
        QCOMPARE(bar->value(), before);
    }

    // Chrome with no meaning at card size is suppressed: the inline title
    // band takes no space and is not hit.
    void cardSuppressesTheTitleBand()
    {
        auto fixture = openDocument(QByteArrayLiteral("# Heading\n\nbody"));
        Markoff::Canvas::View view;
        view.resize(420, 300);
        view.setDocument(fixture.document.get());
        view.setInlineTitle(QStringLiteral("A Card Title"));
        view.setInlineTitleVisible(true);
        view.show();
        QCoreApplication::processEvents();

        // With the band on, the first block sits below it.
        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t{2});
        const QRectF withBand = view.blockRect(blocks[0]);
        QVERIFY2(withBand.top() > 0.0,
                 "the title band did not take space above the first block");

        view.setEmbeddedMode(true);
        QCoreApplication::processEvents();
        const QRectF withoutBand = view.blockRect(blocks[0]);
        QVERIFY2(qFuzzyCompare(withoutBand.top(), 0.0),
                 qPrintable(QStringLiteral("the band still takes %1px")
                                .arg(withoutBand.top())));
    }

    // Folding is inert in a card: the affordance is not drawn and the
    // toggle does nothing, so a card never hides content behind a body the
    // user cannot expand.
    void cardSuppressesFolding()
    {
        auto fixture = openDocument(QByteArrayLiteral(
            "# Heading\n\nbody one\n\nbody two\n\nbody three"));
        Markoff::Canvas::View view;
        view.resize(420, 400);
        view.setDocument(fixture.document.get());
        view.show();
        QCoreApplication::processEvents();

        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t{4});
        const Markoff::BlockId head = blocks[0];
        const Markoff::BlockId body = blocks[1];
        QVERIFY(view.isBlockFoldable(head));

        // Fold it the ordinary way: the body is hidden.
        view.toggleFold(head);
        QCoreApplication::processEvents();
        QVERIFY2(view.isBlockHidden(body), "the fold did not hide the body");

        // In a card the fold state is not applied — the body is visible.
        view.setEmbeddedMode(true);
        QCoreApplication::processEvents();
        QVERIFY2(!view.isBlockHidden(body),
                 "a card hid content behind a fold the user cannot expand");

        // And folding is inert: the toggle does nothing.
        view.toggleFold(head);
        QCoreApplication::processEvents();
        QVERIFY2(!view.isBlockHidden(body),
                 "a card folded a body behind an affordance it does not draw");
    }

    // Leaving embedded mode restores what the host had: the band comes back
    // and the fold state is as the session holds it.
    void leavingCardModeRestoresTheHostsChrome()
    {
        auto fixture = openDocument(QByteArrayLiteral(
            "# Heading\n\nbody one\n\nbody two"));
        Markoff::Canvas::View view;
        view.resize(420, 300);
        view.setDocument(fixture.document.get());
        view.setInlineTitle(QStringLiteral("A Card Title"));
        view.setInlineTitleVisible(true);
        view.show();
        QCoreApplication::processEvents();

        const auto blocks = fixture.document->iterateBlocks();
        const Markoff::BlockId head = blocks[0];
        const Markoff::BlockId body = blocks[1];
        view.toggleFold(head);
        QCoreApplication::processEvents();
        QVERIFY(view.isBlockHidden(body));

        view.setEmbeddedMode(true);
        QCoreApplication::processEvents();
        QVERIFY(!view.isBlockHidden(body));

        view.setEmbeddedMode(false);
        QCoreApplication::processEvents();
        QVERIFY2(view.isBlockHidden(body),
                 "leaving card mode did not restore the fold state");
        QVERIFY2(view.blockRect(blocks[0]).top() > 0.0,
                 "leaving card mode did not restore the title band");
    }
};

QTEST_MAIN(TstCanvasCardMode)
#include "tst_canvas_card_mode.moc"
