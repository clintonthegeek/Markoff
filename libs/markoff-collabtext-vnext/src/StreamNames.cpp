// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/StreamNames.h>

#include <array>
#include <utility>

namespace Markoff::VNext {
namespace {

using namespace CollabText;
constexpr std::string_view kBlockPrefix = "markoff/block/v1/";
constexpr std::string_view kMapPrefix = "markoff/map/v1/";
constexpr char kHex[] = "0123456789abcdef";

int lowerHexValue(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

Error invalidBlockName()
{
    return Error(ErrorCode::invalid_identity,
                 "block stream name is not a canonical v1 identity");
}

bool validMapName(std::string_view map)
{
    if (map.empty() || map.size() > 128 || map.front() < 'a' || map.front() > 'z')
        return false;
    for (const char ch : map) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
            || ch == '-' || ch == '_' || ch == '.')
            continue;
        return false;
    }
    return true;
}

} // namespace

Result<std::string> blockTextStreamName(const BlockIdentity& block)
{
    if (!block.actor.valid() || block.counter == 0)
        return Error(ErrorCode::invalid_identity, "invalid block identity");

    std::string result(kBlockPrefix);
    result += block.actor.to_string();
    result += '/';
    std::array<char, 16> counter{};
    uint64_t value = block.counter;
    for (int i = 15; i >= 0; --i) {
        counter[static_cast<size_t>(i)] = kHex[value & 0x0f];
        value >>= 4;
    }
    result.append(counter.data(), counter.size());
    return result;
}

Result<BlockIdentity> parseBlockTextStreamName(std::string_view stream)
{
    constexpr size_t kActorHexLength = ActorId::kBytes * 2;
    constexpr size_t kCounterHexLength = 16;
    constexpr size_t kExpectedLength = kBlockPrefix.size() + kActorHexLength + 1
                                     + kCounterHexLength;
    if (stream.size() != kExpectedLength || !stream.starts_with(kBlockPrefix))
        return invalidBlockName();

    const size_t actorStart = kBlockPrefix.size();
    const size_t separator = actorStart + kActorHexLength;
    if (stream[separator] != '/') return invalidBlockName();
    auto actor = ActorId::parse(stream.substr(actorStart, kActorHexLength));
    if (!actor || actor.value().to_string() != stream.substr(actorStart, kActorHexLength))
        return invalidBlockName();

    uint64_t counter = 0;
    const size_t counterStart = separator + 1;
    for (size_t i = counterStart; i < stream.size(); ++i) {
        const int digit = lowerHexValue(stream[i]);
        if (digit < 0) return invalidBlockName();
        counter = (counter << 4) | static_cast<uint64_t>(digit);
    }
    if (counter == 0) return invalidBlockName();
    return BlockIdentity{std::move(actor).value(), counter};
}

Result<std::string> mapStreamName(std::string_view map)
{
    if (!validMapName(map))
        return Error(ErrorCode::invalid_identity,
                     "map name must be a canonical lowercase route segment");
    std::string result(kMapPrefix);
    result.append(map);
    return result;
}

} // namespace Markoff::VNext
