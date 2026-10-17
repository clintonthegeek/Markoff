// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/QuarantineRecordCodec.h>

#include <QDataStream>
#include <QIODevice>
#include <QString>

#include <algorithm>

namespace Markoff::VNext {
namespace {

constexpr qsizetype kMaximumRecordBytes = 26 * 1024 * 1024;
constexpr qsizetype kMaximumPayloadBytes = 8 * 1024 * 1024;
constexpr qsizetype kMaximumActionEnvelopeBytes = 16 * 1024 * 1024;
constexpr qsizetype kMaximumMessageBytes = 4096;
constexpr qsizetype kMaximumContextBytes = 1024;
constexpr qsizetype kMaximumActionIdBytes = 256;

bool validUtf8(const QByteArray& bytes, qsizetype maximum)
{
    return bytes.size() <= maximum && QString::fromUtf8(bytes).toUtf8() == bytes;
}

bool valid(const QuarantineRecord& record)
{
    const QByteArray document(record.document.data(), qsizetype(record.document.size()));
    const QByteArray stream(record.stream.data(), qsizetype(record.stream.size()));
    if (!validUtf8(document, 256) || document.isEmpty()
        || !validUtf8(stream, 256) || stream.isEmpty()
        || record.epoch == 0 || !record.actor.valid() || record.counter == 0
        || record.payload.isEmpty() || record.payload.size() > kMaximumPayloadBytes
        || record.originalActionEnvelope.size() > kMaximumActionEnvelopeBytes
        || record.deliverySequence == 0 || record.actionId.isEmpty()
        || record.actionId.size() > kMaximumActionIdBytes
        || record.bundleOperationCount == 0
        || record.bundleIndex >= record.bundleOperationCount
        || !record.reason)
        return false;
    const auto primitive = static_cast<uint8_t>(record.primitive);
    if (primitive < static_cast<uint8_t>(QuarantinePrimitive::text)
        || primitive > static_cast<uint8_t>(QuarantinePrimitive::map))
        return false;
    if (record.primitive != QuarantinePrimitive::map && record.targetBlock)
        return false;
    if (record.targetBlock
        && (!record.targetBlock->actor.valid() || record.targetBlock->counter == 0))
        return false;
    const auto message = record.reason.message();
    if (message.empty() || message.size() > size_t(kMaximumMessageBytes)) return false;
    if (const auto context = record.reason.context();
        context && context->size() > size_t(kMaximumContextBytes))
        return false;
    return true;
}

QByteArray actorBytes(const CollabText::ActorId& actor)
{
    const auto bytes = actor.bytes();
    return QByteArray(reinterpret_cast<const char*>(bytes.data()),
                      qsizetype(bytes.size()));
}

bool readActor(QDataStream& in, CollabText::ActorId& actor)
{
    QByteArray bytes;
    in >> bytes;
    if (in.status() != QDataStream::Ok
        || bytes.size() != qsizetype(CollabText::ActorId::kBytes))
        return false;
    std::array<uint8_t, CollabText::ActorId::kBytes> raw{};
    std::copy(bytes.cbegin(), bytes.cend(), raw.begin());
    actor = CollabText::ActorId::from_bytes(raw);
    return actor.valid();
}

} // namespace

CollabText::Result<QByteArray> QuarantineRecordCodec::encode(
    const QuarantineRecord& record)
{
    using namespace CollabText;
    if (!valid(record))
        return Error(ErrorCode::malformed, "quarantine record fields are invalid");
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_8);
    out.setByteOrder(QDataStream::BigEndian);
    out << QByteArrayLiteral("MQR") << quint16(2)
        << QByteArray(record.document.data(), qsizetype(record.document.size()))
        << quint64(record.epoch)
        << QByteArray(record.stream.data(), qsizetype(record.stream.size()))
        << quint8(record.primitive) << actorBytes(record.actor) << quint64(record.counter)
        << quint8(record.targetBlock.has_value());
    if (record.targetBlock)
        out << actorBytes(record.targetBlock->actor)
            << quint64(record.targetBlock->counter);
    out << record.payload << quint64(record.deliverySequence) << record.actionId
        << record.originalActionEnvelope
        << quint32(record.bundleIndex) << quint32(record.bundleOperationCount)
        << quint16(record.reason.code())
        << QByteArray(record.reason.message().data(), qsizetype(record.reason.message().size()));
    const auto context = record.reason.context();
    out << quint8(context.has_value());
    if (context)
        out << QByteArray(context->data(), qsizetype(context->size()));
    if (out.status() != QDataStream::Ok || bytes.size() > kMaximumRecordBytes)
        return Error(ErrorCode::limit_exceeded,
                     "encoded quarantine record exceeds its size limit");
    return bytes;
}

CollabText::Result<QuarantineRecord> QuarantineRecordCodec::decode(
    const QByteArray& bytes)
{
    using namespace CollabText;
    if (bytes.isEmpty() || bytes.size() > kMaximumRecordBytes)
        return Error(ErrorCode::limit_exceeded,
                     "quarantine record has an invalid size");
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_8);
    in.setByteOrder(QDataStream::BigEndian);
    QByteArray magic, document, stream, payload, actionId, message, context;
    quint16 version = 0, errorCode = 0;
    quint64 epoch = 0, counter = 0, deliverySequence = 0;
    quint8 primitive = 0, hasTarget = 0, hasContext = 0;
    quint32 bundleIndex = 0, bundleOperationCount = 0;
    in >> magic >> version >> document >> epoch >> stream >> primitive;
    QuarantineRecord record;
    if (in.status() != QDataStream::Ok || magic != QByteArrayLiteral("MQR")
        || (version != 1 && version != 2)
        || !validUtf8(document, 256) || document.isEmpty()
        || !validUtf8(stream, 256) || stream.isEmpty())
        return Error(ErrorCode::corrupt_record,
                     "quarantine record header is invalid");
    if (!readActor(in, record.actor))
        return Error(ErrorCode::corrupt_record,
                     "quarantine operation actor is invalid");
    in >> counter >> hasTarget;
    if (in.status() != QDataStream::Ok || hasTarget > 1)
        return Error(ErrorCode::corrupt_record,
                     "quarantine identity fields are truncated");
    if (hasTarget) {
        CollabText::ActorId targetActor;
        quint64 targetCounter = 0;
        if (!readActor(in, targetActor))
            return Error(ErrorCode::corrupt_record,
                         "quarantine target actor is invalid");
        in >> targetCounter;
        if (in.status() != QDataStream::Ok || targetCounter == 0)
            return Error(ErrorCode::corrupt_record,
                         "quarantine target identity is invalid");
        record.targetBlock = BlockIdentity{targetActor, targetCounter};
    }
    in >> payload >> deliverySequence >> actionId;
    if (version == 2) in >> record.originalActionEnvelope;
    in >> bundleIndex >> bundleOperationCount >> errorCode >> message >> hasContext;
    if (in.status() != QDataStream::Ok || hasContext > 1
        || (version != 1 && version != 2)
        || payload.size() > kMaximumPayloadBytes || actionId.size() > kMaximumActionIdBytes
        || record.originalActionEnvelope.size() > kMaximumActionEnvelopeBytes
        || message.isEmpty() || message.size() > kMaximumMessageBytes)
        return Error(ErrorCode::corrupt_record,
                     "quarantine envelope or reason is truncated");
    if (hasContext) in >> context;
    if (in.status() != QDataStream::Ok || !in.atEnd()
        || context.size() > kMaximumContextBytes
        || errorCode > static_cast<quint16>(ErrorCode::reentrant_mutation))
        return Error(ErrorCode::corrupt_record,
                     "quarantine reason or record trailer is invalid");
    record.document = document.toStdString();
    record.epoch = epoch;
    record.stream = stream.toStdString();
    record.primitive = static_cast<QuarantinePrimitive>(primitive);
    record.counter = counter;
    record.payload = std::move(payload);
    record.deliverySequence = deliverySequence;
    record.actionId = std::move(actionId);
    record.bundleIndex = bundleIndex;
    record.bundleOperationCount = bundleOperationCount;
    record.reason = hasContext
        ? Error(static_cast<ErrorCode>(errorCode), message.toStdString(), context.toStdString())
        : Error(static_cast<ErrorCode>(errorCode), message.toStdString());
    if (!valid(record))
        return Error(ErrorCode::corrupt_record,
                     "decoded quarantine record violates its contract");
    return record;
}

} // namespace Markoff::VNext
