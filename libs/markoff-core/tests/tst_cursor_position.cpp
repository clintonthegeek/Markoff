// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <markoff/CursorPosition.h>
#include <markoff/MarkoffDocument.h>   // for trackCursor/resolveCursor (Task 6)

// NOTE: Full cursor-anchor integration via MarkoffDocument tested in tst_cursor_anchor.
// This test covers only the CursorPosition handle's RAII + move semantics.
// Registered in CMake at Task 6 when MarkoffDocument API is available.

class TstCursorPosition : public QObject {
    Q_OBJECT
private slots:
    void default_constructed_isInvalid();
    void moved_from_isInvalid();
    void moved_to_takesOver();
};

void TstCursorPosition::default_constructed_isInvalid() {
    Markoff::CursorPosition p;
    QVERIFY(!p.isValid());
}

void TstCursorPosition::moved_from_isInvalid() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(2, Markoff::CursorBias::Left);
    QVERIFY(p.isValid());
    auto q = std::move(p);
    QVERIFY(!p.isValid());
    QVERIFY(q.isValid());
}

void TstCursorPosition::moved_to_takesOver() {
    Markoff::MarkoffDocument doc;
    doc.resetContent(QStringLiteral("hello"), Markoff::Origin::FirstOpen);
    auto p = doc.trackCursor(2, Markoff::CursorBias::Left);
    Markoff::CursorPosition q;
    q = std::move(p);
    QCOMPARE(doc.resolveCursor(q), qsizetype(2));
    QCOMPARE(doc.resolveCursor(p), qsizetype(-1));  // moved-from resolves invalid
}

QTEST_GUILESS_MAIN(TstCursorPosition)
#include "tst_cursor_position.moc"
