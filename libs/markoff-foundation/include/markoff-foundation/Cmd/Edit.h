// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;
namespace Cmd {

MARKOFF_FOUNDATION_EXPORT void undo(MarkoffDocument &);
MARKOFF_FOUNDATION_EXPORT void redo(MarkoffDocument &);

}}  // namespace Markoff::Cmd
