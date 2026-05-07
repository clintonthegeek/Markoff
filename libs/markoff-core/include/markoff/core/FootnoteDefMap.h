// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/CausalLwwMap.h>
#include <QByteArray>

namespace Markoff {

using FootnoteId     = QByteArray;
using FootnoteDefMap = CausalLwwMap<FootnoteId, QByteArray>;

}  // namespace Markoff
