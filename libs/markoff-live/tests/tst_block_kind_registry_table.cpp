// SPDX-License-Identifier: GPL-3.0-or-later
//
// E4 Task A5 — Verify the BlockKindRegistry descriptor for "table" carries
// the right capabilities for an L8 interactive multi-cell block.
//
// Spec: docs/specs/2026-05-22-e4-tables-design.md §5.1.

#include <QTest>

#include <markoff/live/BlockKind.h>
#include <markoff/live/BlockKindDescriptor.h>
#include <markoff/live/BlockKindRegistry.h>

class TstBlockKindRegistryTable : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void table_descriptor_is_registered();
    void table_supports_BlockSelected_and_BlockInternalEdit();
    void table_consumes_navigation_and_structural_keys();
    void table_declares_editing_cell_internal_mode();
    void table_accepts_text_role_updates();
    void table_is_not_block_only();
    void table_delegate_url_points_to_TableDelegate_qml();
};

using Markoff::Live::BlockKindRegistry;
using Markoff::Live::BlockKindDescriptor;
namespace BK = Markoff::Live::BlockKind;

void TstBlockKindRegistryTable::table_descriptor_is_registered()
{
    BlockKindRegistry reg;
    const BlockKindDescriptor *d = reg.find(BK::Table);
    QVERIFY(d != nullptr);
    QCOMPARE(d->id, BK::Table);
}

void TstBlockKindRegistryTable::table_supports_BlockSelected_and_BlockInternalEdit()
{
    BlockKindRegistry reg;
    const BlockKindDescriptor *d = reg.find(BK::Table);
    QVERIFY(d);
    QVERIFY2(d->supportedCursorVariants.contains(QStringLiteral("BlockSelected")),
             "Table must allow BlockSelected for whole-block selection/delete cascade");
    QVERIFY2(d->supportedCursorVariants.contains(QStringLiteral("BlockInternalEdit")),
             "Table must allow BlockInternalEdit for in-cell editing");
}

void TstBlockKindRegistryTable::table_consumes_navigation_and_structural_keys()
{
    BlockKindRegistry reg;
    const BlockKindDescriptor *d = reg.find(BK::Table);
    QVERIFY(d);
    const auto &keys = d->consumedStructuralKeys;
    QVERIFY(keys.contains(Qt::Key_Tab));
    QVERIFY(keys.contains(Qt::Key_Backtab));
    QVERIFY(keys.contains(Qt::Key_Return));
    QVERIFY(keys.contains(Qt::Key_Enter));
    QVERIFY(keys.contains(Qt::Key_Backspace));
    QVERIFY(keys.contains(Qt::Key_Delete));
    QVERIFY(keys.contains(Qt::Key_Up));
    QVERIFY(keys.contains(Qt::Key_Down));
    QVERIFY(keys.contains(Qt::Key_Escape));
}

void TstBlockKindRegistryTable::table_declares_editing_cell_internal_mode()
{
    BlockKindRegistry reg;
    const BlockKindDescriptor *d = reg.find(BK::Table);
    QVERIFY(d);
    QVERIFY(d->internalEditModes.contains(QStringLiteral("editing-cell")));
}

void TstBlockKindRegistryTable::table_accepts_text_role_updates()
{
    BlockKindRegistry reg;
    const BlockKindDescriptor *d = reg.find(BK::Table);
    QVERIFY(d);
    QCOMPARE(d->acceptsTextRoleUpdates, true);
}

void TstBlockKindRegistryTable::table_is_not_block_only()
{
    BlockKindRegistry reg;
    const BlockKindDescriptor *d = reg.find(BK::Table);
    QVERIFY(d);
    // Updated 2026-05-22 (E4 §E): Table is now block-only for the
    // structural-handler adjacency fence + generic delete cascade,
    // even though it has internal text-bearing cells. The cell-level
    // text editing is handled by TableDelegate's per-cell TextEdits
    // and TableEditBinding; the block-only flag only affects the
    // adjacency fence and the BlockSelected{table} key handlers.
    // Test name kept for blame trail; the renamed-to-fit slot lives
    // in tst_live_render_table_cell_edit (E1/E2 coverage).
    QCOMPARE(d->isBlockOnly, true);
}

void TstBlockKindRegistryTable::table_delegate_url_points_to_TableDelegate_qml()
{
    BlockKindRegistry reg;
    const BlockKindDescriptor *d = reg.find(BK::Table);
    QVERIFY(d);
    QVERIFY2(d->delegateUrl.endsWith(QStringLiteral("TableDelegate.qml")),
             qPrintable(QStringLiteral("delegateUrl=%1").arg(d->delegateUrl)));
}

QTEST_MAIN(TstBlockKindRegistryTable)
#include "tst_block_kind_registry_table.moc"
