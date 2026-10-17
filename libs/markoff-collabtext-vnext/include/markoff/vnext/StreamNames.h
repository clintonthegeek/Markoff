// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/BlockIdentity.h>

#include <string>
#include <string_view>

namespace Markoff::VNext {

inline constexpr std::string_view kOrderStreamName = "markoff/order/v1";

/// Canonical names within a document scope. The host document key is a
/// separate PositionScope component and must not be embedded in this string.
[[nodiscard]] CollabText::Result<std::string> blockTextStreamName(
    const BlockIdentity& block);
[[nodiscard]] CollabText::Result<BlockIdentity> parseBlockTextStreamName(
    std::string_view stream);
[[nodiscard]] CollabText::Result<std::string> mapStreamName(std::string_view map);

} // namespace Markoff::VNext
