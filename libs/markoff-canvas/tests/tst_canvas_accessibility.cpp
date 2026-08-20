// SPDX-License-Identifier: GPL-3.0-or-later
//
// G1 a11y arc — Phase A1 (tree, roles, registration).
//
// A1.1: CanvasAccessible container + factory registration. All in-process
// via QAccessible::queryAccessibleInterface (spec §7) — no AT-SPI bridge,
// no display, no --direct.

#include <QAccessible>
#include <QCoreApplication>
#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::Canvas::View;

namespace {

QByteArray threeParagraphFixture()
{
    return "First paragraph.\n\nSecond paragraph.\n\nThird paragraph.\n";
}

}  // namespace

class TstCanvasAccessibility : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void container_role_is_document();
    void child_count_tracks_block_count();
    void child_indices_round_trip();
    void child_count_tracks_document_reload();
};

void TstCanvasAccessibility::container_role_is_document()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    view.setDocument(&doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    QCOMPARE(iface->role(), QAccessible::Document);
}

void TstCanvasAccessibility::child_count_tracks_block_count()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    view.setDocument(&doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    QCOMPARE(iface->childCount(), view.blockCount());
    QCOMPARE(iface->childCount(), 3);
}

void TstCanvasAccessibility::child_indices_round_trip()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    view.setDocument(&doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    for (int i = 0; i < iface->childCount(); ++i) {
        QAccessibleInterface *child = iface->child(i);
        QVERIFY(child);
        QCOMPARE(iface->indexOfChild(child), i);
        QCOMPARE(child->parent(), iface);
    }
    QVERIFY(!iface->child(-1));
    QVERIFY(!iface->child(iface->childCount()));
}

void TstCanvasAccessibility::child_count_tracks_document_reload()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    view.setDocument(&doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    QCOMPARE(iface->childCount(), 3);

    // loadFromMarkdown() emits documentLoaded()/documentChanged() synchronously
    // but defers d2DocumentChanged() one event-loop spin (core's own doc
    // comment on the signal); View::onDocumentChanged() — which rebuilds the
    // block-index cache childCount() reads — is wired to d2DocumentChanged(),
    // not documentChanged(), so a reload past the first (which View primes by
    // hand in setDocument()) needs a spin before the container's childCount()
    // reflects it.
    doc.loadFromMarkdown("Just one paragraph.\n");
    QCoreApplication::processEvents();
    QCOMPARE(iface->childCount(), 1);

    doc.loadFromMarkdown("A\n\nB\n\nC\n\nD\n\nE\n");
    QCoreApplication::processEvents();
    QCOMPARE(iface->childCount(), 5);
}

QTEST_MAIN(TstCanvasAccessibility)
#include "tst_canvas_accessibility.moc"
