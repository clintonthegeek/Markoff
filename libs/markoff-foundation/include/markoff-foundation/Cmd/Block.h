// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include <crdt/Anchor.h>
#include <crdt/Operations.h>

#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;
struct Selection;

namespace Cmd {

MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForSetHeading(const MarkoffDocument &, const Selection &, int level);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    setHeading(MarkoffDocument &, const Selection &, int level);

MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForToggleCheckbox(const MarkoffDocument &,
                            const CollabText::Crdt::Anchor &);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    toggleCheckbox(MarkoffDocument &, const CollabText::Crdt::Anchor &);

MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForBlockQuote(const MarkoffDocument &, const Selection &);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    blockQuote(MarkoffDocument &, const Selection &);

}}  // namespace Markoff::Cmd
