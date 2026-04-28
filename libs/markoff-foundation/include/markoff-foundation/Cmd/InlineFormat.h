// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include <crdt/Operations.h>

#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;
struct Selection;

namespace Cmd {

// Pure functions
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForToggleBold(const MarkoffDocument &, const Selection &);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForToggleItalic(const MarkoffDocument &, const Selection &);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForToggleStrikethrough(const MarkoffDocument &, const Selection &);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForToggleInlineCode(const MarkoffDocument &, const Selection &);

// Convenience wrappers
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    toggleBold(MarkoffDocument &, const Selection &);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    toggleItalic(MarkoffDocument &, const Selection &);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    toggleStrikethrough(MarkoffDocument &, const Selection &);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    toggleInlineCode(MarkoffDocument &, const Selection &);

}}  // namespace Markoff::Cmd
