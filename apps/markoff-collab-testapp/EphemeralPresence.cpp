// SPDX-License-Identifier: GPL-3.0-or-later
#include "EphemeralPresence.h"

#include <QtEndian>

#include <cstring>

namespace {

constexpr char kMagic[] = {'M', 'P', 'R'};
constexpr quint8 kVersion = 1;
constexpr qsizetype kMaxLabel = 256;
constexpr qsizetype kMaxToken = 64 * 1024;

void appendU16(QByteArray& out, quint16 value)
{
    const quint16 encoded = qToBigEndian(value);
    out.append(reinterpret_cast<const char*>(&encoded), sizeof(encoded));
}

void appendU32(QByteArray& out, quint32 value)
{
    const quint32 encoded = qToBigEndian(value);
    out.append(reinterpret_cast<const char*>(&encoded), sizeof(encoded));
}

void appendU64(QByteArray& out, quint64 value)
{
    const quint64 encoded = qToBigEndian(value);
    out.append(reinterpret_cast<const char*>(&encoded), sizeof(encoded));
}

bool take(const QByteArray& bytes, qsizetype& offset, qsizetype count, char* dest)
{
    if (count < 0 || offset < 0 || offset > bytes.size()
        || count > bytes.size() - offset)
        return false;
    if (count > 0)
        memcpy(dest, bytes.constData() + offset, static_cast<size_t>(count));
    offset += count;
    return true;
}

bool takeU16(const QByteArray& bytes, qsizetype& offset, quint16& value)
{
    quint16 encoded = 0;
    if (!take(bytes, offset, sizeof(encoded), reinterpret_cast<char*>(&encoded)))
        return false;
    value = qFromBigEndian(encoded);
    return true;
}

bool takeU32(const QByteArray& bytes, qsizetype& offset, quint32& value)
{
    quint32 encoded = 0;
    if (!take(bytes, offset, sizeof(encoded), reinterpret_cast<char*>(&encoded)))
        return false;
    value = qFromBigEndian(encoded);
    return true;
}

bool takeU64(const QByteArray& bytes, qsizetype& offset, quint64& value)
{
    quint64 encoded = 0;
    if (!take(bytes, offset, sizeof(encoded), reinterpret_cast<char*>(&encoded)))
        return false;
    value = qFromBigEndian(encoded);
    return true;
}

bool takeBytes(const QByteArray& bytes, qsizetype& offset, qsizetype count,
               QByteArray& out)
{
    if (count < 0 || offset > bytes.size() || count > bytes.size() - offset)
        return false;
    out = bytes.mid(offset, count);
    offset += count;
    return true;
}

} // namespace

std::optional<EphemeralPresence> ephemeralPresenceFromSelection(
    const Markoff::Selection& selection)
{
    if (selection.kind != Markoff::Selection::Kind::Presence
        || selection.participantId.isEmpty()
        || selection.participantId.size() > kMaxLabel
        || selection.participantLabel.size() > kMaxLabel
        || !selection.anchorPosition || !selection.activePosition
        || selection.anchorPosition->block != selection.activePosition->block
        || !selection.anchorPosition->isValid()
        || !selection.activePosition->isValid()
        || selection.anchorPosition->boundaryToken.size() > kMaxToken
        || selection.activePosition->boundaryToken.size() > kMaxToken)
        return std::nullopt;
    EphemeralPresence presence;
    presence.participantId = selection.participantId;
    presence.participantLabel = selection.participantLabel;
    presence.presenceColor = selection.presenceColor;
    presence.cursorVersion = selection.cursorVersion;
    presence.block = selection.anchorPosition->block;
    presence.anchorToken = selection.anchorPosition->boundaryToken;
    presence.activeToken = selection.activePosition->boundaryToken;
    return presence;
}

Markoff::Selection selectionFromEphemeralPresence(const EphemeralPresence& presence)
{
    Markoff::Selection selection;
    selection.kind = Markoff::Selection::Kind::Presence;
    selection.participantId = presence.participantId;
    selection.participantLabel = presence.participantLabel;
    selection.presenceColor = presence.presenceColor;
    selection.cursorVersion = presence.cursorVersion;
    selection.anchorPosition = Markoff::ScopedTextPosition{
        presence.block, presence.anchorToken};
    selection.activePosition = Markoff::ScopedTextPosition{
        presence.block, presence.activeToken};
    return selection;
}

std::optional<QByteArray> encodeEphemeralPresence(const EphemeralPresence& presence)
{
    const QByteArray id = presence.participantId.toUtf8();
    const QByteArray label = presence.participantLabel.toUtf8();
    if (id.isEmpty() || id.size() > kMaxLabel || label.size() > kMaxLabel
        || presence.block.isNull()
        || presence.anchorToken.isEmpty() || presence.activeToken.isEmpty()
        || presence.anchorToken.size() > kMaxToken
        || presence.activeToken.size() > kMaxToken
        || presence.participantId != QString::fromUtf8(id)
        || presence.participantLabel != QString::fromUtf8(label))
        return std::nullopt;

    QByteArray out;
    out.reserve(4 + id.size() + label.size()
        + presence.anchorToken.size() + presence.activeToken.size() + 48);
    out.append(kMagic, 3);
    out.append(static_cast<char>(kVersion));
    appendU16(out, static_cast<quint16>(id.size()));
    out.append(id);
    appendU16(out, static_cast<quint16>(label.size()));
    out.append(label);
    appendU32(out, static_cast<quint32>(presence.presenceColor.rgba()));
    appendU64(out, presence.cursorVersion);
    const auto actor = presence.block.actorBytes();
    out.append(reinterpret_cast<const char*>(actor.data()),
               static_cast<qsizetype>(actor.size()));
    appendU64(out, presence.block.counter());
    appendU32(out, static_cast<quint32>(presence.anchorToken.size()));
    out.append(presence.anchorToken);
    appendU32(out, static_cast<quint32>(presence.activeToken.size()));
    out.append(presence.activeToken);
    return out;
}

std::optional<EphemeralPresence> decodeEphemeralPresence(const QByteArray& bytes)
{
    if (bytes.size() < 4 || bytes.at(0) != kMagic[0] || bytes.at(1) != kMagic[1]
        || bytes.at(2) != kMagic[2] || static_cast<quint8>(bytes.at(3)) != kVersion)
        return std::nullopt;
    qsizetype offset = 4;
    quint16 idSize = 0;
    quint16 labelSize = 0;
    if (!takeU16(bytes, offset, idSize) || idSize == 0 || idSize > kMaxLabel)
        return std::nullopt;
    QByteArray id;
    if (!takeBytes(bytes, offset, idSize, id))
        return std::nullopt;
    if (!takeU16(bytes, offset, labelSize) || labelSize > kMaxLabel)
        return std::nullopt;
    QByteArray label;
    if (!takeBytes(bytes, offset, labelSize, label))
        return std::nullopt;
    const QString participantId = QString::fromUtf8(id);
    const QString participantLabel = QString::fromUtf8(label);
    if (participantId.toUtf8() != id || participantLabel.toUtf8() != label)
        return std::nullopt;

    quint32 rgba = 0;
    quint64 version = 0;
    if (!takeU32(bytes, offset, rgba) || !takeU64(bytes, offset, version))
        return std::nullopt;
    std::array<uint8_t, 16> actor{};
    if (!take(bytes, offset, static_cast<qsizetype>(actor.size()),
              reinterpret_cast<char*>(actor.data())))
        return std::nullopt;
    quint64 counter = 0;
    if (!takeU64(bytes, offset, counter))
        return std::nullopt;
    const auto block = Markoff::BlockId::fromStructuralIdentity(actor, counter);
    if (!block)
        return std::nullopt;
    quint32 anchorSize = 0;
    quint32 activeSize = 0;
    if (!takeU32(bytes, offset, anchorSize) || anchorSize == 0
        || anchorSize > kMaxToken)
        return std::nullopt;
    QByteArray anchor;
    if (!takeBytes(bytes, offset, anchorSize, anchor))
        return std::nullopt;
    if (!takeU32(bytes, offset, activeSize) || activeSize == 0
        || activeSize > kMaxToken)
        return std::nullopt;
    QByteArray active;
    if (!takeBytes(bytes, offset, activeSize, active) || offset != bytes.size())
        return std::nullopt;

    EphemeralPresence presence;
    presence.participantId = participantId;
    presence.participantLabel = participantLabel;
    presence.presenceColor = QColor::fromRgba(rgba);
    presence.cursorVersion = version;
    presence.block = *block;
    presence.anchorToken = std::move(anchor);
    presence.activeToken = std::move(active);
    return presence;
}
