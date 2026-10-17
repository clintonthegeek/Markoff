// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QStringList>

#include <markoff/core/FoldRef.h>

#include <array>
#include <cstdint>
using namespace Markoff;

namespace {
BlockId testBlockId(uint64_t counter)
{
    std::array<uint8_t, 16> actor{};
    actor.back() = 1;
    return *BlockId::fromStructuralIdentity(actor, counter);
}
}

class TstFoldRef : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void default_is_heading_kind() {
        FoldRef f;
        QCOMPARE(f.kind, FoldRef::Kind::Heading);
        QCOMPARE(f.headingLevel, 0);
    }

    void heading_with_path() {
        FoldRef f;
        f.kind = FoldRef::Kind::Heading;
        f.startPosition = ScopedTextPosition{testBlockId(17), QByteArrayLiteral("h")};
        f.headingPath = QStringList { "Intro", "Chapter 1", "Section A" };
        f.headingLevel = 3;
        QCOMPARE(f.headingPath.size(), 3);
        QCOMPARE(f.headingLevel, 3);
    }

    void block_kind() {
        FoldRef f;
        f.kind = FoldRef::Kind::Block;
        f.startPosition = ScopedTextPosition{testBlockId(18), QByteArrayLiteral("b")};
        QCOMPARE(f.kind, FoldRef::Kind::Block);
    }

    void json_roundtrip_heading() {
        FoldRef a;
        a.kind = FoldRef::Kind::Heading;
        a.startPosition = ScopedTextPosition{testBlockId(17), QByteArrayLiteral("heading")};
        a.headingPath = QStringList { "Intro", "Chapter 1" };
        a.headingLevel = 2;

        const QJsonObject json = a.toJson();
        const FoldRef b = FoldRef::fromJson(json);
        QCOMPARE(b.kind, a.kind);
        QCOMPARE(b.startPosition, a.startPosition);
        QCOMPARE(b.headingPath, a.headingPath);
        QCOMPARE(b.headingLevel, a.headingLevel);
    }

    void json_roundtrip_block() {
        FoldRef a;
        a.kind = FoldRef::Kind::Block;
        a.startPosition = ScopedTextPosition{testBlockId(19), QByteArrayLiteral("block")};

        const QJsonObject json = a.toJson();
        const FoldRef b = FoldRef::fromJson(json);
        QCOMPARE(b.kind, a.kind);
        QCOMPARE(b.startPosition, a.startPosition);
    }

    void malformed_scoped_position_is_rejected()
    {
        FoldRef a;
        a.kind = FoldRef::Kind::Heading;
        a.startPosition = ScopedTextPosition{
            testBlockId(23), QByteArray::fromHex("01020300ff")};
        auto malformed = a.toJson();
        auto position = malformed.value("startPosition").toObject();
        position.insert("token", QStringLiteral("%%"));
        malformed.insert("startPosition", position);
        QVERIFY(!FoldRef::fromJson(malformed).startPosition.has_value());
    }
};

QTEST_MAIN(TstFoldRef)
#include "tst_fold_ref.moc"
