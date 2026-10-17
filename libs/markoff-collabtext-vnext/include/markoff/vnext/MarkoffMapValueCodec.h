// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/BlockIdentity.h>
#include <markoff/vnext/MarkoffMapCodec.h>

#include <markoff/core/BlockAttrsMap.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/LinkRefMap.h>

#include <collabtext/ActorId.h>

#include <QByteArray>

#include <optional>

namespace Markoff::VNext {

/// Typed value codecs used inside the opaque MMP/1 value field for Markoff's
/// existing block-kind, block-attribute and link-reference sibling maps.
class MarkoffMapValueCodec final {
public:
    [[nodiscard]] static CollabText::Result<QByteArray> encodeBlockKind(
        Markoff::BlockKind value);
    [[nodiscard]] static CollabText::Result<Markoff::BlockKind> decodeBlockKind(
        const QByteArray& bytes);

    [[nodiscard]] static CollabText::Result<QByteArray> encodeAttribute(
        const Markoff::AttrValue& value);
    [[nodiscard]] static CollabText::Result<Markoff::AttrValue> decodeAttribute(
        const QByteArray& bytes);

    [[nodiscard]] static CollabText::Result<QByteArray> encodeLinkRef(
        const Markoff::LinkRefValue& value);
    [[nodiscard]] static CollabText::Result<Markoff::LinkRefValue> decodeLinkRef(
        const QByteArray& bytes);

    /// Validate the route-specific target, key and typed value contract shared
    /// by the MAB codec and the live Markoff map stream.
    [[nodiscard]] static CollabText::Result<void> validateBinding(
        const MarkoffMapOperation& operation,
        const std::optional<BlockIdentity>& targetBlock);
};

} // namespace Markoff::VNext
