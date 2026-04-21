// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <markoff/CanonicalBuffer.h>
#include "../src/InMemoryCanonicalBuffer.h"

class TstCanonicalBuffer : public QObject {
    Q_OBJECT
private slots:
    void applyDelta_insert();
    void applyDelta_remove();
    void applyDelta_replace();
    void reset_clearsAll();
    void anchor_survivesPrecedingInsert();
    void anchor_survivesFollowingInsert();
    void anchor_leftBias_onStraddle();
    void anchor_rightBias_onStraddle();
    void anchor_released_staysResolvable_asInvalid();
    void anchor_rightBias_atInsertPoint();
    void anchor_leftBias_atInsertPoint();
    // Edge-case pass — C3 landing review §3
    void anchor_rightBias_atOffsetZero_pureInsert_advances();
    void anchor_leftBias_atOffsetZero_pureInsert_staysAtZero();
    void anchor_rightBias_atEndOfText_pureInsert_advances();
    void anchor_leftBias_atEndOfText_pureInsert_staysAtEnd();
    void anchor_leftBias_atDeleteEnd_followsAsNonStraddle();
    void anchor_rightBias_atDeleteStart_unaffected();
    void anchor_leftBias_atDeleteStart_unaffected();
    void anchor_rightBias_onReplace_clampsToInsertEnd();
    void anchor_leftBias_onReplace_clampsToDeleteStart();
};

void TstCanonicalBuffer::applyDelta_insert() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello world"));
    buf.applyDelta(5, 0, QStringLiteral(","));
    QCOMPARE(buf.toMarkdown(), QStringLiteral("hello, world"));
}

void TstCanonicalBuffer::applyDelta_remove() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello, world"));
    buf.applyDelta(5, 1, QString());
    QCOMPARE(buf.toMarkdown(), QStringLiteral("hello world"));
}

void TstCanonicalBuffer::applyDelta_replace() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello, world"));
    buf.applyDelta(0, 5, QStringLiteral("HELLO"));
    QCOMPARE(buf.toMarkdown(), QStringLiteral("HELLO, world"));
}

void TstCanonicalBuffer::reset_clearsAll() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("first"));
    const auto h = buf.createAnchor(3, Markoff::CursorBias::Left);
    buf.reset(QStringLiteral("second"));
    QCOMPARE(buf.toMarkdown(), QStringLiteral("second"));
    QCOMPARE(buf.length(), qsizetype(6));
    QCOMPARE(buf.resolveAnchor(h), qsizetype(-1));  // anchor invalidated by reset
}

void TstCanonicalBuffer::anchor_survivesPrecedingInsert() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello world"));
    const auto h = buf.createAnchor(6, Markoff::CursorBias::Left);
    buf.applyDelta(0, 0, QStringLiteral("> "));
    QCOMPARE(buf.resolveAnchor(h), qsizetype(8));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_survivesFollowingInsert() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello world"));
    const auto h = buf.createAnchor(5, Markoff::CursorBias::Left);
    buf.applyDelta(11, 0, QStringLiteral("!"));
    QCOMPARE(buf.resolveAnchor(h), qsizetype(5));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_leftBias_onStraddle() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("abcdef"));
    const auto h = buf.createAnchor(3, Markoff::CursorBias::Left);
    buf.applyDelta(2, 2, QStringLiteral("XY"));
    QCOMPARE(buf.resolveAnchor(h), qsizetype(2));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_rightBias_onStraddle() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("abcdef"));
    const auto h = buf.createAnchor(3, Markoff::CursorBias::Right);
    buf.applyDelta(2, 2, QStringLiteral("XY"));
    QCOMPARE(buf.resolveAnchor(h), qsizetype(4));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_released_staysResolvable_asInvalid() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello"));
    const auto h = buf.createAnchor(3, Markoff::CursorBias::Left);
    buf.releaseAnchor(h);
    QCOMPARE(buf.resolveAnchor(h), qsizetype(-1));
}

void TstCanonicalBuffer::anchor_rightBias_atInsertPoint() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello"));
    const auto h = buf.createAnchor(3, Markoff::CursorBias::Right);
    buf.applyDelta(3, 0, QStringLiteral("XX"));   // pure insert AT the anchor
    QCOMPARE(buf.resolveAnchor(h), qsizetype(5)); // right-bias advances past inserted text
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_leftBias_atInsertPoint() {
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello"));
    const auto h = buf.createAnchor(3, Markoff::CursorBias::Left);
    buf.applyDelta(3, 0, QStringLiteral("XX"));   // pure insert AT the anchor
    QCOMPARE(buf.resolveAnchor(h), qsizetype(3)); // left-bias stays before inserted text
    buf.releaseAnchor(h);
}

// --- Edge-case pass (C3 landing review §3) ---

void TstCanonicalBuffer::anchor_rightBias_atOffsetZero_pureInsert_advances() {
    // Right-bias anchor at offset 0; insert 2 chars at offset 0 — should
    // advance past the inserted text to offset 2.
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello"));
    const auto h = buf.createAnchor(0, Markoff::CursorBias::Right);
    buf.applyDelta(0, 0, QStringLiteral("XX"));
    QCOMPARE(buf.resolveAnchor(h), qsizetype(2));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_leftBias_atOffsetZero_pureInsert_staysAtZero() {
    // Left-bias anchor at offset 0; insert 2 chars at offset 0 — should stay
    // at 0 (precedes the inserted text).
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello"));
    const auto h = buf.createAnchor(0, Markoff::CursorBias::Left);
    buf.applyDelta(0, 0, QStringLiteral("XX"));
    QCOMPARE(buf.resolveAnchor(h), qsizetype(0));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_rightBias_atEndOfText_pureInsert_advances() {
    // Right-bias anchor at end-of-text (offset 5 in "hello"); appending "!"
    // — should advance to offset 6.
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello"));
    const auto h = buf.createAnchor(5, Markoff::CursorBias::Right);
    buf.applyDelta(5, 0, QStringLiteral("!"));
    QCOMPARE(buf.resolveAnchor(h), qsizetype(6));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_leftBias_atEndOfText_pureInsert_staysAtEnd() {
    // Left-bias anchor at end-of-text (offset 5 in "hello"); appending "!"
    // — should stay at 5.
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("hello"));
    const auto h = buf.createAnchor(5, Markoff::CursorBias::Left);
    buf.applyDelta(5, 0, QStringLiteral("!"));
    QCOMPARE(buf.resolveAnchor(h), qsizetype(5));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_leftBias_atDeleteEnd_followsAsNonStraddle() {
    // Buffer "abcdef", anchor at 4 (deleteEnd), delete chars 2..4 ("cd").
    // a.offset(4) >= deleteEnd(4) — treated as "follows the edit", not a straddle.
    // Shifts by delta -2 → offset 2.
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("abcdef"));
    const auto h = buf.createAnchor(4, Markoff::CursorBias::Left);
    buf.applyDelta(2, 2, QString());
    QCOMPARE(buf.resolveAnchor(h), qsizetype(2));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_rightBias_atDeleteStart_unaffected() {
    // Buffer "abcdef", anchor at 2 (deleteStart), delete chars 2..4 ("cd").
    // a.offset(2) <= deleteStart(2) — precedes-or-equals, unaffected → stays at 2.
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("abcdef"));
    const auto h = buf.createAnchor(2, Markoff::CursorBias::Right);
    buf.applyDelta(2, 2, QString());
    QCOMPARE(buf.resolveAnchor(h), qsizetype(2));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_leftBias_atDeleteStart_unaffected() {
    // Buffer "abcdef", anchor at 2 (deleteStart), delete chars 2..4 ("cd").
    // a.offset(2) <= deleteStart(2) — precedes-or-equals, unaffected → stays at 2.
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("abcdef"));
    const auto h = buf.createAnchor(2, Markoff::CursorBias::Left);
    buf.applyDelta(2, 2, QString());
    QCOMPARE(buf.resolveAnchor(h), qsizetype(2));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_rightBias_onReplace_clampsToInsertEnd() {
    // Buffer "abcdef", anchor at 3, replace offset=2 removedLength=3 ("cde") with "X".
    // deleteStart=2, deleteEnd=5, insertEnd=3.
    // Anchor(3) not pure-insert; 3 > deleteStart(2) and 3 < deleteEnd(5) → straddle.
    // Right-bias → clamps to insertEnd = 3.
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("abcdef"));
    const auto h = buf.createAnchor(3, Markoff::CursorBias::Right);
    buf.applyDelta(2, 3, QStringLiteral("X"));
    QCOMPARE(buf.resolveAnchor(h), qsizetype(3));
    buf.releaseAnchor(h);
}

void TstCanonicalBuffer::anchor_leftBias_onReplace_clampsToDeleteStart() {
    // Same setup as above; left-bias → clamps to deleteStart = 2.
    Markoff::InMemoryCanonicalBuffer buf;
    buf.reset(QStringLiteral("abcdef"));
    const auto h = buf.createAnchor(3, Markoff::CursorBias::Left);
    buf.applyDelta(2, 3, QStringLiteral("X"));
    QCOMPARE(buf.resolveAnchor(h), qsizetype(2));
    buf.releaseAnchor(h);
}

QTEST_GUILESS_MAIN(TstCanonicalBuffer)
#include "tst_canonical_buffer.moc"
