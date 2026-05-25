// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Cmd/Edit.h>
#include <markoff/core/MarkoffDocument.h>

namespace Markoff::Cmd {

void undo(MarkoffDocument &d) { d.undoD2(); }
void redo(MarkoffDocument &d) { d.redoD2(); }

}  // namespace Markoff::Cmd
