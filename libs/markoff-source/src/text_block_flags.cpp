// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "text_block_flags.h"
#include "text_block_user_data.h"
#include <qutepart.h>

namespace Qutepart {

bool hasFlag(const QTextBlock &block, int flag) {
    auto data = static_cast<TextBlockUserData *>(block.userData());
    if (!data) {
        return false;
    }
    auto state = data->state;
    return state != -1 && state & flag;
}

void setFlag(QTextBlock &block, int flag, bool value) {
    auto data = static_cast<TextBlockUserData *>(block.userData());
    if (!data) {
        return;
    }
    auto &state = data->state;
    if (state == -1) {
        state = 0;
    }

    if (value) {
        state |= flag;
    } else {
        state &= (~flag);
    }
}

} // namespace Qutepart
