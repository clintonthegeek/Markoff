// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QStringList>

#include <markoff-foundation/FoldRef.h>
#include <crdt/Anchor.h>

using namespace Markoff;
using CollabText::Crdt::Anchor;
using CollabText::Crdt::Bias;

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
        f.start = Anchor(1, 50, Bias::Left);
        f.headingPath = QStringList { "Intro", "Chapter 1", "Section A" };
        f.headingLevel = 3;
        QCOMPARE(f.headingPath.size(), 3);
        QCOMPARE(f.headingLevel, 3);
    }

    void block_kind() {
        FoldRef f;
        f.kind = FoldRef::Kind::Block;
        f.start = Anchor(2, 200, Bias::Left);
        QCOMPARE(f.kind, FoldRef::Kind::Block);
    }

    void json_roundtrip_heading() {
        FoldRef a;
        a.kind = FoldRef::Kind::Heading;
        a.start = Anchor(1, 50, Bias::Left);
        a.headingPath = QStringList { "Intro", "Chapter 1" };
        a.headingLevel = 2;

        const QJsonObject json = a.toJson();
        const FoldRef b = FoldRef::fromJson(json);
        QCOMPARE(b.kind, a.kind);
        QCOMPARE(b.start.replica_id, a.start.replica_id);
        QCOMPARE(b.start.char_value, a.start.char_value);
        QCOMPARE(b.headingPath, a.headingPath);
        QCOMPARE(b.headingLevel, a.headingLevel);
    }

    void json_roundtrip_block() {
        FoldRef a;
        a.kind = FoldRef::Kind::Block;
        a.start = Anchor(3, 300, Bias::Right);

        const QJsonObject json = a.toJson();
        const FoldRef b = FoldRef::fromJson(json);
        QCOMPARE(b.kind, a.kind);
        QCOMPARE(b.start.replica_id, a.start.replica_id);
        QCOMPARE(b.start.char_value, a.start.char_value);
    }
};

QTEST_MAIN(TstFoldRef)
#include "tst_fold_ref.moc"
