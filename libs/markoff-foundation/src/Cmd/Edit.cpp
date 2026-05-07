// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Cmd/Edit.h>
#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::Cmd {

void undo(MarkoffDocument &d) { d.undoD2(); }
void redo(MarkoffDocument &d) { d.redoD2(); }

}  // namespace Markoff::Cmd
