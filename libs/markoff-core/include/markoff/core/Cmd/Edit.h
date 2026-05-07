// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {
class MarkoffDocument;
namespace Cmd {

MARKOFF_CORE_EXPORT void undo(MarkoffDocument &);
MARKOFF_CORE_EXPORT void redo(MarkoffDocument &);

}}  // namespace Markoff::Cmd
