// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

#include <crdt/Anchor.h>
#include <crdt/Operations.h>

#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;

namespace Cmd {

MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForInsertTable(const MarkoffDocument &, const CollabText::Crdt::Anchor &,
                         int rows, int cols, bool hasHeader);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForInsertLink(const MarkoffDocument &, const CollabText::Crdt::Anchor &,
                        const QString &linkText, const QString &target);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForInsertImage(const MarkoffDocument &, const CollabText::Crdt::Anchor &,
                         const QString &alt, const QString &target);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForInsertHorizontalRule(const MarkoffDocument &,
                                  const CollabText::Crdt::Anchor &);

MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    insertTable(MarkoffDocument &, const CollabText::Crdt::Anchor &,
                 int rows, int cols, bool hasHeader = true);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    insertLink(MarkoffDocument &, const CollabText::Crdt::Anchor &,
                const QString &linkText, const QString &target);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    insertImage(MarkoffDocument &, const CollabText::Crdt::Anchor &,
                 const QString &alt, const QString &target);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    insertHorizontalRule(MarkoffDocument &, const CollabText::Crdt::Anchor &);

}}  // namespace Markoff::Cmd
