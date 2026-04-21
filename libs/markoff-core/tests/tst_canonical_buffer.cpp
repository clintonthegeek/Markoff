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

QTEST_GUILESS_MAIN(TstCanonicalBuffer)
#include "tst_canonical_buffer.moc"
