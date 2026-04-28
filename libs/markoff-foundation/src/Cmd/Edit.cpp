// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Cmd/Edit.h>
#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::Cmd {

void undo(MarkoffDocument *d) { if (d) (void)d->undo(); }
void redo(MarkoffDocument *d) { if (d) (void)d->redo(); }

}  // namespace Markoff::Cmd
