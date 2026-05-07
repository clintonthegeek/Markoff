// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/CausalLwwMap.h>
#include <QByteArray>

namespace Markoff {

using FrontmatterKey   = QByteArray;
using FrontmatterValue = QByteArray;
using FrontmatterMap   = CausalLwwMap<FrontmatterKey, FrontmatterValue>;

}  // namespace Markoff
