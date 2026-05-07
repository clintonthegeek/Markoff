// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <crdt/Anchor.h>
#include <crdt/Buffer.h>
#include <crdt/IdList.h>
#include <crdt/IdListOperations.h>
#include <crdt/Operations.h>

using namespace CollabText::Crdt;

// ============================================================================
// TstD2Convergence
//
// Task 13: Convergence tests at the CRDT primitive level (IdList + Buffer).
// These tests verify the core CRDT properties used by D2:
//   - Structural convergence via IdList
//   - Per-block content convergence via Buffer
//   - Mixed structural + content ops
//   - Remove-vs-edit race (§3.5)
//   - Local undo of structural insert
// ============================================================================

class TstD2Convergence : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // 13.1: Two-replica structural ops
    void twoReplicas_crossingInsertsAtSameAnchor_bothSurvive();

    // 13.2: Two-replica per-block content edits
    void twoReplicas_concurrentBufferEdits_converge();

    // 13.3: Mixed structural + content
    void mixed_structuralAndContent_bothConverge();

    // 13.4: Remove-vs-edit race
    void removeVsEdit_race_editSurvivesInTombstone();

    // 13.5: Cross-CRDT undo (local undo of structural insert)
    void localUndo_structuralInsert_removesFromList();
};

// ── 13.1: Two-replica crossing inserts at the same anchor ───────────────────
//
// Replica A (id=1) and Replica B (id=2) each insert a block ID after the
// "start" anchor (Anchor::min()) concurrently. When ops are cross-applied,
// both replicas must end up with both block IDs in a deterministic order.

void TstD2Convergence::twoReplicas_crossingInsertsAtSameAnchor_bothSurvive()
{
    IdList listA(/*replica_id=*/1);
    IdList listB(/*replica_id=*/2);

    const uint64_t idA = 1000;
    const uint64_t idB = 2000;

    // Both replicas insert after the "start" sentinel (empty anchor = Anchor::min())
    auto opA = listA.insert_after(Anchor::min(), idA);
    auto opB = listB.insert_after(Anchor::min(), idB);

    // Cross-apply: A receives B's op, B receives A's op
    listA.apply_ops({opB});
    listB.apply_ops({opA});

    // Both lists must contain exactly 2 entries
    QCOMPARE(listA.size(), uint32_t(2));
    QCOMPARE(listB.size(), uint32_t(2));

    // Both lists must have both IDs
    auto idsA = listA.ids();
    auto idsB = listB.ids();

    QCOMPARE(idsA.size(), size_t(2));
    QCOMPARE(idsB.size(), size_t(2));

    bool aHasIdA = std::find(idsA.begin(), idsA.end(), idA) != idsA.end();
    bool aHasIdB = std::find(idsA.begin(), idsA.end(), idB) != idsA.end();
    QVERIFY(aHasIdA);
    QVERIFY(aHasIdB);

    // Both replicas must agree on the same order (deterministic tiebreak by replica ID)
    QCOMPARE(idsA, idsB);
}

// ── 13.2: Two-replica concurrent buffer edits ────────────────────────────────
//
// Two replicas each start with a fresh Buffer and independently insert a
// character. After cross-applying, both must have the same visible text.
// (Testing convergence property: two replicas converge to the same state.)

void TstD2Convergence::twoReplicas_concurrentBufferEdits_converge()
{
    Buffer bufA(/*replica_id=*/1);
    Buffer bufB(/*replica_id=*/2);

    // Both start empty. Replica A inserts "a" at offset 0; Replica B inserts "b" at offset 0.
    auto opA = bufA.apply_local_edit(
        {{0, 0}},   // range: insert at offset 0 (no deletion)
        {"a"});

    auto opB = bufB.apply_local_edit(
        {{0, 0}},
        {"b"});

    // Cross-apply
    bufA.apply_ops({opB});
    bufB.apply_ops({opA});

    // Both must have the same text
    std::string textA = bufA.text();
    std::string textB = bufB.text();

    QCOMPARE(textA.size(), size_t(2));
    QCOMPARE(textB.size(), size_t(2));

    // Convergence: both have the same content
    QCOMPARE(textA, textB);

    // Both characters are present (order is deterministic but we just check both are there)
    QVERIFY(textA.find('a') != std::string::npos);
    QVERIFY(textA.find('b') != std::string::npos);
}

// ── 13.3: Mixed structural + content ────────────────────────────────────────
//
// Replica A inserts a block ID into the IdList (structural).
// Concurrently, Replica B edits its own Buffer (content).
// After cross-applying both ops, both replicas must reflect:
//   - The structural change (the new block ID is visible in the list)
//   - The content change (the buffer has the updated text)

void TstD2Convergence::mixed_structuralAndContent_bothConverge()
{
    // Structural CRDT: both replicas maintain a shared IdList
    IdList listA(1);
    IdList listB(2);

    // Content CRDT: one shared buffer (replica 1 owns it locally, replica 2 mirrors)
    Buffer bufA(1);  // Replica A's copy of block-X's buffer
    Buffer bufB(1);  // Replica B's copy of same buffer (same replica ID = same doc content)
    // Note: In practice B would have its own replica ID for its own edits, but here
    // we're testing that A's structural insert + B's content edit on an existing
    // shared buffer both survive cross-apply. B is a second participant editing
    // the same block buffer — use replica 2 for B's buffer edits.
    Buffer bufA2(1); // A's view of block content (starts with "hello")
    Buffer bufB2(2); // B's view of same block — different replica, same initial state

    // Seed both buffer replicas with "hello" from replica 1 (the initial content)
    auto seedOp = bufA2.apply_local_edit({{0, 0}}, {"hello"});
    bufB2.apply_ops({seedOp});

    // Concurrent ops:
    //   Replica A: inserts new block ID 42 into the IdList
    auto structOp = listA.insert_after(Anchor::min(), uint64_t(42));

    //   Replica B: appends "!" to the buffer (at end of "hello")
    auto contentOp = bufB2.apply_local_edit({{5, 5}}, {"!"});

    // Cross-apply structural op to B's list
    listB.apply_ops({structOp});

    // Cross-apply content op to A's buffer
    bufA2.apply_ops({contentOp});

    // Verify structural convergence: both lists see the new block
    QCOMPARE(listA.size(), uint32_t(1));
    QCOMPARE(listB.size(), uint32_t(1));
    QCOMPARE(listA.ids(), listB.ids());
    QCOMPARE(listA.ids()[0], uint64_t(42));

    // Verify content convergence: both buffer replicas have "hello!"
    QCOMPARE(bufA2.text(), std::string("hello!"));
    QCOMPARE(bufB2.text(), std::string("hello!"));
}

// ── 13.4: Remove-vs-edit race ────────────────────────────────────────────────
//
// D2 spec §3.5: "remove wins over concurrent edits on the structural level,
// but the edit survives in the buffer (tombstoned block with updated content)."
//
// Replica A deletes block-X from the IdList.
// Concurrently, Replica B edits block-X's buffer content.
// After convergence:
//   - Both replicas agree block-X is removed from the IdList (size=0)
//   - Block-X's buffer reflects B's edit (edit not lost, just orphaned)

void TstD2Convergence::removeVsEdit_race_editSurvivesInTombstone()
{
    // Both replicas start with block-X (id=100) in their lists
    IdList listA(1);
    IdList listB(2);

    const uint64_t blockXId = 100;

    // Insert block-X on A and sync to B
    auto insertOp = listA.insert_after(Anchor::min(), blockXId);
    listB.apply_ops({insertOp});

    QCOMPARE(listA.size(), uint32_t(1));
    QCOMPARE(listB.size(), uint32_t(1));

    // Both have a shared buffer for block-X, starting with "hello"
    Buffer bufA(1);  // Replica A's copy
    Buffer bufB(2);  // Replica B's copy
    auto seedOp = bufA.apply_local_edit({{0, 0}}, {"hello"});
    bufB.apply_ops({seedOp});

    QCOMPARE(bufA.text(), std::string("hello"));
    QCOMPARE(bufB.text(), std::string("hello"));

    // Concurrent ops:
    //   Replica A: removes block-X from the structural list
    Anchor anchorX = listA.anchor_of(blockXId);
    auto removeOp = listA.remove_at(anchorX);

    //   Replica B: edits block-X's buffer (appends "!")
    auto editOp = bufB.apply_local_edit({{5, 5}}, {"!"});

    // Cross-apply structural op: B learns about the removal
    listB.apply_ops({removeOp});

    // Cross-apply content op: A learns about B's edit to block-X's buffer
    bufA.apply_ops({editOp});

    // Structural: both agree block-X is removed
    QCOMPARE(listA.size(), uint32_t(0));
    QCOMPARE(listB.size(), uint32_t(0));

    // Content: edit survived — both buffers have "hello!" even though block is tombstoned
    QCOMPARE(bufA.text(), std::string("hello!"));
    QCOMPARE(bufB.text(), std::string("hello!"));
}

// ── 13.5: Local undo of structural insert ───────────────────────────────────
//
// Replica A inserts a block into the IdList, then undoes the insert.
// The block must be removed from the IdList after undo.

void TstD2Convergence::localUndo_structuralInsert_removesFromList()
{
    IdList listA(1);

    // Insert block
    listA.insert_after(Anchor::min(), uint64_t(999));
    QCOMPARE(listA.size(), uint32_t(1));
    QCOMPARE(listA.ids()[0], uint64_t(999));

    // Undo the insert
    auto undoOp = listA.undo();
    QVERIFY(undoOp.has_value());

    // Block is no longer visible in the list
    QCOMPARE(listA.size(), uint32_t(0));
    QVERIFY(listA.ids().empty());

    // Redo restores it
    auto redoOp = listA.redo();
    QVERIFY(redoOp.has_value());
    QCOMPARE(listA.size(), uint32_t(1));
    QCOMPARE(listA.ids()[0], uint64_t(999));
}

QTEST_GUILESS_MAIN(TstD2Convergence)
#include "tst_d2_convergence.moc"
