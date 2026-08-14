// SPDX-License-Identifier: GPL-3.0-or-later
//
// T2 — caret, hit-test, typing (exit E1).
//
// Assertions run against the production widget (Markoff::Canvas::View)
// through its real event path: show it, click to place the caret, drive
// real QTest::keyClicks. No test-only render or edit entry point.

#include <QKeyEvent>
#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

namespace {

/// QTest::keyClicks() only round-trips through its internal ASCII/Latin-1
/// key table (qasciikey.cpp) — it asserts on 'é' (0xe9 is absent from the
/// table) and can't represent an emoji's surrogate pair as a single "key"
/// at all. Sending a QKeyEvent with an empty Qt::Key and the target text
/// directly is still the real event path (View::keyPressEvent reads
/// event->text(), never the key code, for printable input) — this is how
/// Qt's own tests drive non-Latin1 text input.
void sendTextKeyEvent(QWidget *w, const QString &text)
{
    QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &release);
}

}  // namespace

class TstCanvasTyping : public QObject {
    Q_OBJECT

private slots:
    void typing_updates_buffer_and_caret();
    void mouse_click_places_caret_by_hit_test();
    void backspace_and_delete_remove_clusters();
    void read_only_blocks_printable_and_backspace_but_not_navigation();
};

void TstCanvasTyping::typing_updates_buffer_and_caret()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();

    // Click right after "Hello" (byte 5) to place the caret there.
    const QRectF rect = view.blockRect(block);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 30, int(rect.y()) + 8));
    QCOMPARE(view.caretBlock(), block);

    const int startByte = view.caretByteOffset();

    // Plain ASCII.
    QTest::keyClicks(&view, QStringLiteral("X"));
    QVERIFY(doc.blockText(block).contains("X"));
    QCOMPARE(view.caretByteOffset(), startByte + 1);

    // Multi-byte char (é, 2 bytes UTF-8) then an emoji (4 bytes UTF-8,
    // a UTF-16 surrogate pair).
    const int afterX = view.caretByteOffset();
    sendTextKeyEvent(&view, QStringLiteral("é"));
    QCOMPARE(view.caretByteOffset(), afterX + 2);

    const int afterAccent = view.caretByteOffset();
    sendTextKeyEvent(&view, QStringLiteral("\U0001F600"));  // 😀
    QCOMPARE(view.caretByteOffset(), afterAccent + 4);

    const QByteArray text = doc.blockText(block);
    QVERIFY(text.contains(QStringLiteral("Xé\U0001F600").toUtf8()));

    // Rendered text matches the buffer after each keystroke: the layout
    // string is a 1:1 QChar substitution of the buffer (T1 finding), so
    // a successful realize + no crash on repaint is the check available
    // without a screenshot.
    view.repaint();
    QVERIFY(view.paintCount() > 0);
}

void TstCanvasTyping::mouse_click_places_caret_by_hit_test()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("First paragraph.\n\nSecond paragraph.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(2));

    const QRectF secondRect = view.blockRect(blocks[1]);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(secondRect.x()) + 5, int(secondRect.y()) + 8));

    QCOMPARE(view.caretBlock(), blocks[1]);
}

void TstCanvasTyping::backspace_and_delete_remove_clusters()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("abc\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QRectF rect = view.blockRect(block);
    // Click at the end of "abc".
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 60, int(rect.y()) + 8));
    QTest::keyClick(&view, Qt::Key_End);
    QCOMPARE(view.caretByteOffset(), 3);

    QTest::keyClick(&view, Qt::Key_Backspace);
    QCOMPARE(doc.blockText(block), QByteArray("ab"));
    QCOMPARE(view.caretByteOffset(), 2);

    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.caretByteOffset(), 0);

    QTest::keyClick(&view, Qt::Key_Delete);
    QCOMPARE(doc.blockText(block), QByteArray("b"));
    QCOMPARE(view.caretByteOffset(), 0);

    // Delete at end of block / Backspace at byte 0 are boundary cases
    // T3 owns (structural merge with the neighbouring block); T2 must
    // simply not underflow or crash.
    QTest::keyClick(&view, Qt::Key_Backspace);
    QCOMPARE(doc.blockText(block), QByteArray("b"));
    QCOMPARE(view.caretByteOffset(), 0);

    QTest::keyClick(&view, Qt::Key_End);
    QTest::keyClick(&view, Qt::Key_Delete);
    QCOMPARE(doc.blockText(block), QByteArray("b"));
}

// P3.3 — read-only gates. setReadOnly(true) must block printable
// insertion, Backspace/Delete, and Enter-split (StructuralKeyHandler),
// while caret navigation (arrow keys, Home/End) keeps working — spec
// §4.2's "navigation/selection/copy/find keep working" half of the
// contract.
void TstCanvasTyping::read_only_blocks_printable_and_backspace_but_not_navigation()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QRectF rect = view.blockRect(block);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 30, int(rect.y()) + 8));
    QTest::keyClick(&view, Qt::Key_End);
    const int caretBefore = view.caretByteOffset();

    view.setReadOnly(true);
    QVERIFY(view.isReadOnly());

    QTest::keyClicks(&view, QStringLiteral("X"));
    QCOMPARE(doc.blockText(block), QByteArray("Hello"));

    QTest::keyClick(&view, Qt::Key_Backspace);
    QCOMPARE(doc.blockText(block), QByteArray("Hello"));

    QTest::keyClick(&view, Qt::Key_Return);
    QCOMPARE(doc.iterateBlocks().size(), size_t(1));

    // Navigation is unaffected while read-only.
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.caretByteOffset(), 0);
    QTest::keyClick(&view, Qt::Key_End);
    QCOMPARE(view.caretByteOffset(), caretBefore);

    // The gate lifts cleanly.
    view.setReadOnly(false);
    QTest::keyClicks(&view, QStringLiteral("X"));
    QCOMPARE(doc.blockText(block), QByteArray("HelloX"));
}

QTEST_MAIN(TstCanvasTyping)
#include "tst_canvas_typing.moc"
