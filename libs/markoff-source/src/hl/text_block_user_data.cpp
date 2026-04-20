// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "text_block_user_data.h"

namespace Qutepart {

TextBlockUserData::TextBlockUserData(const QString &textTypeMap, const ContextStack &contexts)
    : textTypeMap(textTypeMap), contexts(contexts) {}

} // namespace Qutepart
