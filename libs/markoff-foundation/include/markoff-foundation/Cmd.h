// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

#include <markoff-foundation/Cmd/D2.h>
#include <markoff-foundation/Cmd/Edit.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;
class Session;
struct Selection;

namespace Cmd {

using EditsFn = std::function<QList<MarkoffEdit>(const MarkoffDocument &,
                                                  const Selection &)>;

MARKOFF_FOUNDATION_EXPORT void applyToAllPrimaryAndSecondaries(
    MarkoffDocument &doc, Session &session, const EditsFn &fn);

}}  // namespace Markoff::Cmd
