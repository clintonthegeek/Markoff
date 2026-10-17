// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace Markoff::VNext {

struct RouterBudget {
    size_t maxHeldOperations = 4096;
    size_t maxHeldBytes = 32U * 1024U * 1024U;
};

} // namespace Markoff::VNext
