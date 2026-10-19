// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockSerializer.h>
#include <markoff/parser/Document.h>
#include <markoff/parser/TreeSitterParser.h>


#include <markoff/vnext/ProviderBackedDocumentState.h>
#include <markoff/vnext/ActionBundleCodec.h>
#include <markoff/vnext/MarkoffMapValueCodec.h>
#include <markoff/vnext/StreamNames.h>
#include <markoff/vnext/ActionStager.h>
#include <markoff/vnext/OwnerHistoryCodec.h>
#include <markoff/vnext/CrossStreamHistoryCodec.h>
#include <markoff/vnext/ScopedTextStream.h>
#include <markoff/vnext/ReplicatedMapStream.h>
#include <markoff/vnext/ScopedIdListStream.h>
#include <markoff/vnext/MarkoffMapCodec.h>
#include <markoff/vnext/SourceSnapshotCodec.h>
#include <markoff/vnext/CounterGapCodec.h>
#include <markoff/vnext/QuarantineRecordCodec.h>

#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <tuple>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <QSaveFile>
#include <QTextDocumentFragment>
#include <utility>

namespace Markoff {

struct MarkoffDocument::Private {
    struct SourceBlock {
        qsizetype start = 0;
        qsizetype end = 0;
        QByteArray originalText;
    };

    Private(std::unique_ptr<VNext::ProviderBackedDocumentState> providerState,
            const BlockSerializerRegistry *blockRegistry)
        : lifecycle(std::move(providerState))
        , serializerRegistry(blockRegistry)
        , ownerThread(std::this_thread::get_id())
    {}

    std::unique_ptr<VNext::ProviderBackedDocumentState> lifecycle;
    const BlockSerializerRegistry *serializerRegistry = nullptr;
    QList<Session *> sessions;
    QHash<BlockId, quint64> blockEditSequences;
    quint64 structuralEditSequence = 0;
    mutable QHash<BlockId, QPair<quint64, QList<SourceSpan>>> inlineSpanCache;
    QByteArray originalMarkdown;
    QHash<BlockId, SourceBlock> sourceBlocks;
    std::vector<BlockId> sourceBlockOrder;
    bool hasOriginalMarkdown = false;
    bool sourcePristine = false;
    std::thread::id ownerThread;
    struct DeferredRemoteAction {
        QByteArray actionId;
        QByteArray bytes;
    };
    std::deque<DeferredRemoteAction> deferredRemoteActions;
    size_t deferredRemoteActionBytes = 0;
    RemoteActionQueueBudget remoteActionQueueBudget;
};

namespace {


std::optional<VNext::BlockIdentity> toVNextIdentity(BlockId block)
{
    if (block.isNull())
        return std::nullopt;
    return VNext::BlockIdentity{
        CollabText::ActorId::from_bytes(block.actorBytes()), block.counter()};
}

std::optional<BlockId> fromVNextIdentity(const VNext::BlockIdentity& identity)
{
    return BlockId::fromStructuralIdentity(identity.actor.bytes(), identity.counter);
}

QByteArray initialTextForSourceBlock(const QByteArray& body,
                                    const Markoff::TopLevelBlock& block)
{
    QByteArray content = body.mid(block.byteStart, block.byteEnd - block.byteStart);
    if (content.endsWith("\r\n")) content.chop(2);
    else if (content.endsWith('\n')) content.chop(1);
    if (block.blockQuoteDepth > 0) {
        content.replace("\r\n", "\n");
        QByteArray stripped;
        for (const auto& line : content.split('\n')) {
            QByteArray current = line;
            for (int depth = 0; depth < block.blockQuoteDepth; ++depth) {
                int pos = 0;
                while (pos < 3 && pos < current.size() && current[pos] == ' ') ++pos;
                if (pos < current.size() && current[pos] == '>') {
                    current.remove(0, pos + 1);
                    if (current.startsWith(' ')) current.remove(0, 1);
                }
            }
            if (!stripped.isEmpty()) stripped.append('\n');
            stripped.append(current);
        }
        content = stripped;
    }
    if (block.kind == Markoff::TopLevelBlock::Kind::SetextHeading) {
        const auto underline = content.lastIndexOf('\n');
        if (underline >= 0) content.truncate(underline);
    }
    if (block.kind == Markoff::TopLevelBlock::Kind::Paragraph
        || block.kind == Markoff::TopLevelBlock::Kind::ListItem
        || block.kind == Markoff::TopLevelBlock::Kind::SetextHeading) {
        content.replace("\r\n", "\n");
        content.replace('\n', ' ');
    }
    return content;
}

QString decodeMarkdownReferenceValue(QString raw)
{
    QString unescaped;
    unescaped.reserve(raw.size());
    for (qsizetype i = 0; i < raw.size(); ++i) {
        if (raw.at(i) == QLatin1Char('\\') && i + 1 < raw.size()) {
            const QChar next = raw.at(i + 1);
            if (next.unicode() <= 0x7f
                && QStringLiteral("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~")
                    .contains(next)) {
                unescaped.append(next);
                ++i;
                continue;
            }
        }
        unescaped.append(raw.at(i));
    }

    // QTextDocument supplies Qt's HTML5 named/numeric entity table. Escape
    // literal markup while leaving entity references available for decoding.
    QString html;
    html.reserve(unescaped.size());
    for (qsizetype i = 0; i < unescaped.size(); ++i) {
        const QChar ch = unescaped.at(i);
        if (ch == QLatin1Char('&')) {
            const qsizetype semicolon = unescaped.indexOf(QLatin1Char(';'), i + 1);
            bool entity = false;
            if (semicolon > i + 1) {
                const QString candidate = unescaped.mid(i, semicolon - i + 1);
                entity = candidate.startsWith(QStringLiteral("&#"))
                    || (candidate.size() > 2
                        && candidate.at(1).isLetterOrNumber());
                if (entity) {
                    html.append(candidate);
                    i = semicolon;
                    continue;
                }
            }
            html.append(QStringLiteral("&amp;"));
        } else if (ch == QLatin1Char('<')) {
            html.append(QStringLiteral("&lt;"));
        } else if (ch == QLatin1Char('>')) {
            html.append(QStringLiteral("&gt;"));
        } else {
            html.append(ch);
        }
    }
    return QTextDocumentFragment::fromHtml(html).toPlainText();
}

QString normalizeMarkdownReferenceLabel(const QString& raw)
{
    const QString folded = raw.toCaseFolded();
    QString normalized;
    normalized.reserve(folded.size());
    bool pendingSpace = false;
    for (const QChar ch : folded) {
        const bool whitespace = ch == QLatin1Char(' ')
            || ch == QLatin1Char('\t')
            || ch == QLatin1Char('\r')
            || ch == QLatin1Char('\n');
        if (whitespace) {
            pendingSpace = !normalized.isEmpty();
            continue;
        }
        if (pendingSpace) normalized.append(QLatin1Char(' '));
        normalized.append(ch);
        pendingSpace = false;
    }
    return normalized;
}

std::optional<std::pair<QByteArray, LinkRefValue>> parseLinkReferenceDefinition(
    const Markoff::TopLevelBlock& block)
{
    // CommonMark caps the source label contents (excluding brackets) at 999
    // Unicode characters. Matching then case-folds and collapses only ASCII
    // spaces, tabs and line endings; escapes and entity references stay raw.
    if (block.linkReferenceLabel.toUcs4().size() > 999) return std::nullopt;
    const QString normalized = normalizeMarkdownReferenceLabel(
        block.linkReferenceLabel);
    if (normalized.isEmpty()) return std::nullopt;
    if (block.linkReferenceDestination.isEmpty()) return std::nullopt;
    QString title = decodeMarkdownReferenceValue(block.linkReferenceTitle);
    title.replace(QStringLiteral("\r\n"), QStringLiteral(" "));
    title.replace(QLatin1Char('\n'), QLatin1Char(' '));
    title.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return std::pair{normalized.toUtf8(), LinkRefValue{
        decodeMarkdownReferenceValue(block.linkReferenceDestination),
        title}};
}

template<typename State>
void rebuildSourceBlocks(State& state, const std::vector<BlockId>& identities)
{
    state.sourceBlocks.clear();
    state.sourceBlockOrder.clear();
    if (!state.hasOriginalMarkdown || identities.empty()) return;
    const QString sourceText = QString::fromUtf8(state.originalMarkdown);
    const auto extracted = Markoff::Document::extract(sourceText);
    const QByteArray body = extracted.body.toUtf8();
    const auto parsed = Markoff::Document::fromMarkdown(extracted.body);
    if (!parsed) return;

    QList<Markoff::TopLevelBlock> sourceBlocks;
    for (const auto& block : parsed->topLevelBlocks()) {
        if (block.kind != Markoff::TopLevelBlock::Kind::LinkReferenceDefinition)
            sourceBlocks.push_back(block);
    }
    if (sourceBlocks.size() != qsizetype(identities.size())) return;
    const qsizetype bodyOffset = state.originalMarkdown.size() - body.size();
    state.sourceBlockOrder = identities;
    for (qsizetype i = 0; i < sourceBlocks.size(); ++i) {
        const auto& block = sourceBlocks[i];
        state.sourceBlocks.insert(identities[size_t(i)], {
            bodyOffset + block.byteStart,
            bodyOffset + block.byteEnd,
            initialTextForSourceBlock(body, block)});
    }
}

QByteArray serializeAcceptedBlock(const MarkoffDocument& document, BlockId id)
{
    const auto kind = document.blockKind(id);
    const auto attrs = document.blockAttrs(id);
    const auto text = document.blockText(id);
    if (kind == BlockKind::ListItem) {
        QByteArray output;
        int indent = 0;
        if (const auto it = attrs.constFind(AttrNames::IndentLevel); it != attrs.cend())
            if (const auto* value = std::get_if<int>(&it.value()))
                indent = std::clamp(*value, 0, 64);
        output += QByteArray(indent, ' ');
        QString style = QStringLiteral("minus");
        if (const auto it = attrs.constFind(AttrNames::MarkerStyle); it != attrs.cend())
            if (const auto* value = std::get_if<QString>(&it.value())) style = *value;
        int number = 1;
        if (const auto it = attrs.constFind(AttrNames::MarkerNumber); it != attrs.cend())
            if (const auto* value = std::get_if<int>(&it.value())) number = *value;
        if (style == QStringLiteral("dot")) output += QByteArray::number(number) + '.';
        else if (style == QStringLiteral("paren")) output += QByteArray::number(number) + ')';
        else if (style == QStringLiteral("plus")) output += '+';
        else if (style == QStringLiteral("star")) output += '*';
        else if (style == QStringLiteral("task")) {
            bool checked = false;
            if (const auto it = attrs.constFind(AttrNames::Checked); it != attrs.cend())
                if (const auto* value = std::get_if<bool>(&it.value())) checked = *value;
            output += checked ? QByteArrayLiteral("- [x]") : QByteArrayLiteral("- [ ]");
        } else output += '-';
        output += ' ';
        if (const auto it = attrs.constFind(AttrNames::BlockQuoteDepth); it != attrs.cend())
            if (const auto* value = std::get_if<int>(&it.value())) {
                QByteArray prefix;
                for (int level = 0; level < std::clamp(*value, 0, 16); ++level)
                    prefix += "> ";
                if (!prefix.isEmpty()) output.prepend(prefix);
            }
        output += text;
        return output;
    }
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    if (const auto* registry = document.serializerRegistry())
        return registry->serialize(kind, attrs, text);
    return BuiltinBlockSerializerRegistry::instance().get(kind)(kind, attrs, text);
}

CollabText::Result<uint64_t> nextDeliverySequence(
    const VNext::StorageRecoveryState& recovery, const std::string& document,
    uint64_t epoch)
{
    uint64_t next = 1;
    for (const auto& accepted : recovery.acceptedRecords) {
        if (accepted.format != QByteArrayLiteral("MAB/3"))
            continue;
        auto bundle = VNext::ActionBundleCodec::decode(accepted.bytes, document, epoch);
        if (!bundle.ok())
            return bundle.error();
        if (bundle.value().deliverySequence >= next) {
            if (bundle.value().deliverySequence == std::numeric_limits<uint64_t>::max())
                return CollabText::Error(CollabText::ErrorCode::limit_exceeded,
                    "document delivery sequence is exhausted");
            next = bundle.value().deliverySequence + 1;
        }
    }
    return next;
}

CollabText::Result<VNext::OwnerHistorySnapshot> ownerHistoryForAction(
    const VNext::StorageRecoveryState& recovery,
    VNext::DocumentStorageSession& storage, const std::string& document,
    uint64_t epoch, const QByteArray& actionId,
    VNext::ActionHistoryEvent event,
    std::optional<VNext::LocalHistoryRecord> updatedStream = std::nullopt)
{
    using namespace VNext;
    OwnerHistorySnapshot history;
    if (recovery.localHistory) {
        const auto committed = std::find_if(recovery.acceptedRecords.cbegin(),
            recovery.acceptedRecords.cend(), [&](const AcceptedStorageRecord& record) {
                return record.transactionId == recovery.localHistory->committedTransactionId
                    && record.committedRevision == recovery.localHistory->acceptedRevision;
            });
        if (committed == recovery.acceptedRecords.cend())
            return CollabText::Error(CollabText::ErrorCode::pin_conflict,
                "owner history does not identify its committed action");
        auto decoded = OwnerHistoryCodec::decode(recovery.localHistory->bytes,
            document, epoch, storage.actor(), recovery.localHistory->committedTransactionId,
            committed->priorRevision.token);
        if (!decoded.ok())
            return decoded.error();
        history = std::move(decoded).value();
    } else {
        history.document = document;
        history.epoch = epoch;
        history.owner = storage.actor();
        history.actionIndex.document = document;
        history.actionIndex.epoch = epoch;
        history.actionIndex.owner = storage.actor();
    }

    history.transactionId = actionId;
    history.acceptedPrior = recovery.acceptedRevision.token;
    history.actionIndex.document = document;
    history.actionIndex.epoch = epoch;
    history.actionIndex.owner = storage.actor();
    history.actionIndex.acceptedPrior = recovery.acceptedRevision.token;
    if (updatedStream) {
        const auto found = std::find_if(history.streams.begin(), history.streams.end(),
            [&](const LocalHistoryRecord& item) {
                return item.scope == updatedStream->scope
                    && item.actor == updatedStream->actor;
            });
        if (found == history.streams.end())
            history.streams.push_back(std::move(*updatedStream));
        else
            found->snapshot = std::move(updatedStream->snapshot);
    }
    event.actionId = actionId;
    history.actionIndex.events.push_back(std::move(event));
    return history;
}

VNext::ActionPublicationResult publishMapValue(
    VNext::ProviderBackedDocumentState& lifecycle, const std::string& mapName,
    const QByteArray& key, std::optional<VNext::BlockIdentity> target,
    const QByteArray& value, bool tombstone = false)
{
    using namespace VNext;
    if (!lifecycle.canStartOrdinaryAction())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::awaiting_state,
                "document lifecycle does not permit a map action")};
    const auto& scope = lifecycle.storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    auto recoveryResult = lifecycle.storage().readRecoveryState();
    if (!recoveryResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, recoveryResult.error()};
    auto recovery = std::move(recoveryResult).value();
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok())
        return {ActionPublicationState::rejected, std::nullopt, sequence.error()};
    auto stream = lifecycle.stagingMapStream(
        MapScope{document, mapName, scope.epoch});
    if (!stream.ok())
        return {ActionPublicationState::rejected, std::nullopt, stream.error()};
    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId,
                        ActionKind::edit);
    auto local = tombstone ? stream.value()->remove(key, target)
                           : stream.value()->set(key, value, target);
    if (!local.ok())
        return {ActionPublicationState::rejected, std::nullopt, local.error()};
    auto staged = stager.add(local.value());
    if (!staged.ok()) {
        (void)lifecycle.publishStagedAction(stager, OwnerHistorySnapshot{});
        return {ActionPublicationState::rejected, std::nullopt, staged.error()};
    }
    auto association = toHistoryAssociation(local.value());
    if (!association.ok()) {
        (void)lifecycle.publishStagedAction(stager, OwnerHistorySnapshot{});
        return {ActionPublicationState::rejected, std::nullopt, association.error()};
    }
    ActionHistoryEvent event;
    event.kind = ActionKind::edit;
    event.groups.push_back(std::move(association).value());
    auto history = ownerHistoryForAction(recovery, lifecycle.storage(), document,
        scope.epoch, actionId, std::move(event));
    if (!history.ok()) {
        (void)lifecycle.publishStagedAction(stager, OwnerHistorySnapshot{});
        return {ActionPublicationState::rejected, std::nullopt, history.error()};
    }
    return lifecycle.publishStagedAction(stager, history.value());
}

const VNext::AcceptedStorageRecord* acceptedAction(
    const VNext::StorageRecoveryState& recovery, const QByteArray& actionId)
{
    const auto it = std::find_if(recovery.acceptedRecords.cbegin(),
        recovery.acceptedRecords.cend(), [&](const auto& record) {
            return record.transactionId == actionId
                && record.format == QByteArrayLiteral("MAB/3");
        });
    return it == recovery.acceptedRecords.cend() ? nullptr : &*it;
}

bool mapGroupTargetsBlock(const VNext::StorageRecoveryState& recovery,
    const VNext::ActionHistoryEvent& event,
    const VNext::HistoryGroupAssociation& group,
    const VNext::BlockIdentity& target, const std::string& document, uint64_t epoch)
{
    if (group.primitive != VNext::OperationPrimitive::markoffMap
        || !group.hasMapPriorState) return false;
    const auto* record = acceptedAction(recovery, event.actionId);
    if (!record) return false;
    auto bundle = VNext::ActionBundleCodec::decode(record->bytes, document, epoch);
    if (!bundle.ok()) return false;
    for (const auto& operation : bundle.value().operations) {
        if (operation.primitive == VNext::OperationPrimitive::markoffMap
            && operation.stream == group.stream
            && operation.targetBlock == std::optional<VNext::BlockIdentity>(target)
            && operation.producer == group.groupId.actor
            && operation.counter == group.groupId.counter)
            return true;
    }
    return false;
}

bool isOrderListGroup(const VNext::HistoryGroupAssociation& group)
{
    return group.primitive == VNext::OperationPrimitive::idList
        && group.stream == VNext::kOrderStreamName;
}

bool actionIsCurrentlyUndone(const std::vector<VNext::ActionHistoryEvent>& events,
                             const QByteArray& actionId)
{
    std::map<QByteArray, QByteArray> undoParents;
    bool undone = false;
    for (const auto& event : events) {
        if (event.kind == VNext::ActionKind::undo && event.parentActionId == actionId) {
            undoParents.emplace(event.actionId, event.parentActionId);
            undone = true;
        } else if (event.kind == VNext::ActionKind::redo) {
            const auto undo = undoParents.find(event.parentActionId);
            if (undo != undoParents.end()) undone = false;
        }
    }
    return undone;
}

bool actionIsRetired(const VNext::CrossStreamHistoryIndex& index,
                     const VNext::ActionHistoryEvent& event)
{
    for (const auto& group : event.groups) {
        const bool named = std::any_of(index.retirements.begin(), index.retirements.end(),
            [&](const VNext::RetiredHistoryAssociation& retired) {
                return retired.stream == group.stream
                    && retired.primitive == group.primitive
                    && retired.groupId.actor == group.groupId.actor
                    && retired.groupId.counter == group.groupId.counter;
            });
        if (named)
            return true;
    }
    return false;
}


/// The action index local undo/redo walk, or nullopt when the document has no
/// committed owner history yet. Shared so availability queries cannot drift
/// from what undoD2()/redoD2() actually select.
std::optional<VNext::CrossStreamHistoryIndex> decodedActionIndex(
    const VNext::ProviderBackedDocumentState& lifecycle)
{
    const auto& recovery = lifecycle.recovery();
    if (!recovery.localHistory) return std::nullopt;
    const auto committed = std::find_if(recovery.acceptedRecords.cbegin(),
        recovery.acceptedRecords.cend(), [&](const VNext::AcceptedStorageRecord& record) {
            return record.transactionId == recovery.localHistory->committedTransactionId
                && record.committedRevision == recovery.localHistory->acceptedRevision;
        });
    if (committed == recovery.acceptedRecords.cend()) return std::nullopt;
    auto history = VNext::OwnerHistoryCodec::decode(recovery.localHistory->bytes,
        lifecycle.storage().scope().documentKey.toUtf8().toStdString(),
        lifecycle.storage().scope().epoch, lifecycle.storage().actor(),
        recovery.localHistory->committedTransactionId, committed->priorRevision.token);
    if (!history.ok()) return std::nullopt;
    return std::move(history).value().actionIndex;
}

/// The predicate undoD2() selects its next action with.
bool eventIsUndoable(const VNext::CrossStreamHistoryIndex& index,
                     const VNext::ActionHistoryEvent& event)
{
    return (event.kind == VNext::ActionKind::edit
            || event.kind == VNext::ActionKind::replacement)
        && !actionIsCurrentlyUndone(index.events, event.actionId)
        && !actionIsRetired(index, event);
}

/// The predicate redoD2() selects its next action with.
bool eventIsRedoable(const VNext::CrossStreamHistoryIndex& index,
                     const VNext::ActionHistoryEvent& event)
{
    if (event.kind != VNext::ActionKind::undo) return false;
    return std::none_of(index.events.begin(), index.events.end(),
        [&](const VNext::ActionHistoryEvent& later) {
            return later.kind == VNext::ActionKind::redo
                && later.parentActionId == event.actionId;
        });
}

VNext::ActionPublicationResult publishDocumentMapCompensation(
    VNext::ProviderBackedDocumentState& lifecycle,
    const VNext::StorageRecoveryState& recovery,
    const VNext::ActionHistoryEvent& parent,
    const VNext::HistoryGroupAssociation& group,
    VNext::ActionKind kind)
{
    using namespace VNext;
    const auto& scope = lifecycle.storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    if (group.primitive != OperationPrimitive::markoffMap || !group.hasMapPriorState
        || group.stream.empty() || group.stream.starts_with("markoff/block/"))
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::invalid_identity,
                "document map compensation requires an untargeted map group")};
    const auto* parentRecord = acceptedAction(recovery, parent.actionId);
    if (!parentRecord)
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::history_unavailable,
                "document map compensation action is absent from the accepted prefix")};
    auto parentBundle = ActionBundleCodec::decode(parentRecord->bytes, document, scope.epoch);
    if (!parentBundle.ok()) return {ActionPublicationState::rejected, std::nullopt,
                                    parentBundle.error()};
    const auto source = std::find_if(parentBundle.value().operations.begin(),
        parentBundle.value().operations.end(), [&](const ActionOperation& operation) {
            return operation.primitive == OperationPrimitive::markoffMap
                && operation.stream == group.stream && !operation.targetBlock
                && operation.producer == group.groupId.actor
                && operation.counter == group.groupId.counter;
        });
    if (source == parentBundle.value().operations.end())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::history_unavailable,
                "document map history group has no matching accepted operation")};
    auto sourceMap = MarkoffMapCodec::decode(source->payload);
    constexpr std::string_view prefix = "markoff/map/v1/";
    if (!sourceMap.ok() || !group.stream.starts_with(prefix))
        return {ActionPublicationState::rejected, std::nullopt,
            sourceMap.ok() ? CollabText::Error(CollabText::ErrorCode::wrong_stream,
                "document map history route is not canonical") : sourceMap.error()};

    QByteArray currentBytes;
    MarkoffMapOperation currentOperation;
    for (auto accepted = recovery.acceptedRecords.rbegin();
         accepted != recovery.acceptedRecords.rend() && currentBytes.isEmpty(); ++accepted) {
        if (accepted->format != QByteArrayLiteral("MAB/3")) continue;
        auto bundle = ActionBundleCodec::decode(accepted->bytes, document, scope.epoch);
        if (!bundle.ok()) return {ActionPublicationState::rejected, std::nullopt, bundle.error()};
        for (const auto& operation : bundle.value().operations) {
            if (operation.primitive != OperationPrimitive::markoffMap
                || operation.stream != group.stream || operation.targetBlock) continue;
            auto candidate = MarkoffMapCodec::decode(operation.payload);
            if (candidate.ok() && candidate.value().map == sourceMap.value().map
                && candidate.value().key == sourceMap.value().key) {
                currentBytes = operation.payload;
                currentOperation = std::move(candidate).value();
                break;
            }
        }
    }
    if (currentBytes.isEmpty())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::history_unavailable,
                "document map key has no accepted current operation")};
    auto map = lifecycle.stagingMapStream(MapScope{document,
        group.stream.substr(prefix.size()), scope.epoch});
    if (!map.ok()) return {ActionPublicationState::rejected, std::nullopt, map.error()};
    LocalMapOperation local;
    local.scope = map.value()->scope(); local.operation = std::move(currentOperation);
    local.bytes = currentBytes; local.priorValueExisted = group.mapPriorValueExisted;
    local.priorValue = group.mapPriorValue;
    auto inverse = map.value()->compensate(local);
    if (!inverse.ok()) return {ActionPublicationState::rejected, std::nullopt, inverse.error()};
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok()) return {ActionPublicationState::rejected, std::nullopt, sequence.error()};
    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId, kind);
    auto staged = stager.add(inverse.value());
    if (!staged.ok()) {
        return lifecycle.publishStagedAction(stager, OwnerHistorySnapshot{});
    }
    auto association = toHistoryAssociation(inverse.value());
    if (!association.ok()) {
        (void)lifecycle.publishStagedAction(stager, OwnerHistorySnapshot{});
        return {ActionPublicationState::rejected, std::nullopt, association.error()};
    }
    ActionHistoryEvent event;
    event.kind = kind; event.parentActionId = parent.actionId;
    event.groups.push_back(std::move(association).value());
    auto owner = ownerHistoryForAction(recovery, lifecycle.storage(), document,
        scope.epoch, actionId, std::move(event));
    if (!owner.ok()) {
        (void)lifecycle.publishStagedAction(stager, OwnerHistorySnapshot{});
        return {ActionPublicationState::rejected, std::nullopt, owner.error()};
    }
    return lifecycle.publishStagedAction(stager, owner.value());
}

std::optional<Markoff::BlockId> blockForAction(
    const VNext::StorageRecoveryState& recovery,
    const VNext::ActionHistoryEvent& event,
    const std::string& document, uint64_t epoch)
{
    for (const auto& group : event.groups) {
        if (group.primitive == VNext::OperationPrimitive::text) {
            auto identity = VNext::parseBlockTextStreamName(group.stream);
            if (identity.ok()) return Markoff::BlockId::fromStructuralIdentity(
                identity.value().actor.bytes(), identity.value().counter);
        }
        if (group.primitive == VNext::OperationPrimitive::markoffMap) {
            const auto* record = acceptedAction(recovery, event.actionId);
            if (!record) continue;
            auto bundle = VNext::ActionBundleCodec::decode(record->bytes, document, epoch);
            if (!bundle.ok()) continue;
            for (const auto& operation : bundle.value().operations) {
                if (operation.primitive == VNext::OperationPrimitive::markoffMap
                    && operation.stream == group.stream && operation.targetBlock) {
                    return Markoff::BlockId::fromStructuralIdentity(
                        operation.targetBlock->actor.bytes(),
                        operation.targetBlock->counter);
                }
            }
        }
    }
    return std::nullopt;
}

CollabText::Result<void> finalizePendingReservations(
    VNext::ProviderBackedDocumentState& lifecycle)
{
    using namespace VNext;
    constexpr std::string_view mapPrefix = "markoff/map/v1/";
    for (;;) {
        auto recoveryResult = lifecycle.storage().readRecoveryState();
        if (!recoveryResult.ok()) return recoveryResult.error();
        const auto& recovery = recoveryResult.value();
        if (!recovery.unresolvedTransactions.empty())
            return CollabText::Error(CollabText::ErrorCode::history_unavailable,
                "cannot finalize allocation gaps while a transaction is unresolved");
        if (recovery.unresolvedReservations.empty()) return {};

        const auto reservation = recovery.unresolvedReservations.front();
        const auto stream = reservation.stream.toStdString();
        PreparedStorageTransaction gapTransaction;
        gapTransaction.transactionId = QUuid::createUuid().toRfc4122();
        gapTransaction.expectedRevision = recovery.acceptedRevision;
        gapTransaction.reservations = {reservation};
        if (stream.starts_with(mapPrefix)) {
            const std::string map = stream.substr(mapPrefix.size());
            const auto canonical = mapStreamName(map);
            if (!canonical.ok() || canonical.value() != stream)
                return CollabText::Error(CollabText::ErrorCode::wrong_stream,
                    "unresolved map reservation has a noncanonical route");
            const MapCounterGap gap{lifecycle.storage().scope().documentKey
                    .toUtf8().toStdString(), map,
                lifecycle.storage().scope().epoch, reservation.actor,
                reservation.first, reservation.count,
                recovery.acceptedRevision.token};
            auto encoded = MapCounterGapCodec::encode(gap);
            if (!encoded.ok()) return encoded.error();
            gapTransaction.sharedRecordFormat = QByteArrayLiteral("MMG/1");
            gapTransaction.sharedRecord = std::move(encoded).value();
        } else {
            const bool list = stream == kOrderStreamName;
            if (!list && !parseBlockTextStreamName(stream).ok())
                return CollabText::Error(CollabText::ErrorCode::wrong_stream,
                    "unresolved reservation has a noncanonical text/list route");
            const CollabText::PositionScope scope{
                lifecycle.storage().scope().documentKey.toUtf8().toStdString(),
                stream, list ? CollabText::Primitive::list : CollabText::Primitive::text,
                lifecycle.storage().scope().epoch};
            const ScopedCounterGap gap{scope,
                CollabText::Crdt::CounterGap{reservation.actor, reservation.first,
                    reservation.count, CollabText::Crdt::Global{}},
                recovery.acceptedRevision.token};
            auto encoded = CounterGapCodec::encode(gap);
            if (!encoded.ok()) return encoded.error();
            gapTransaction.sharedRecordFormat = QByteArrayLiteral("MTG/1");
            gapTransaction.sharedRecord = std::move(encoded).value();
        }
        const auto result = lifecycle.publishGap(gapTransaction);
        if (result.state != ActionPublicationState::committed) {
            if (result.diagnostic) return *result.diagnostic;
            return CollabText::Error(CollabText::ErrorCode::history_unavailable,
                "could not finalize an unresolved allocation reservation");
        }
    }
}

} // namespace

MarkoffDocument::MarkoffDocument(std::unique_ptr<Private> state)
    : QObject(nullptr)
    , d(std::move(state))
{
    qRegisterMetaType<BlockId>("Markoff::BlockId");
    qRegisterMetaType<QList<BlockId>>("QList<Markoff::BlockId>");
}

CollabText::Result<std::unique_ptr<MarkoffDocument>> MarkoffDocument::open(
    VNext::DocumentStorageProvider &provider,
    const VNext::DocumentStorageScope &scope,
    VNext::StorageMode mode,
    const BlockSerializerRegistry *registry)
{
    auto lifecycle = VNext::ProviderBackedDocumentState::open(provider, scope, mode);
    if (!lifecycle.ok())
        return lifecycle.error();
    auto finalized = finalizePendingReservations(*lifecycle.value());
    if (!finalized.ok()) return finalized.error();

    auto privateState = std::make_unique<Private>(
        std::move(lifecycle).value(), registry);
    const auto& storageRecovery = privateState->lifecycle->recovery();
    std::map<QByteArray, RemoteActionQuarantine> heldWaits;
    const auto isWaitReason = [](CollabText::ErrorCode code) {
        return code == CollabText::ErrorCode::history_unavailable
            || code == CollabText::ErrorCode::unknown_stream;
    };
    for (const auto& encoded : storageRecovery.quarantinedOperations) {
        auto record = VNext::QuarantineRecordCodec::decode(encoded);
        if (!record.ok()) return record.error();
        if (record.value().originalActionEnvelope.isEmpty()) continue;
        if (isWaitReason(record.value().reason.code())) {
            heldWaits[record.value().actionId] = {
                record.value().actionId, record.value().originalActionEnvelope,
                record.value().reason};
        } else {
            heldWaits.erase(record.value().actionId);
        }
    }
    std::vector<std::pair<quint64, Private::DeferredRemoteAction>> recoveredWaits;
    for (auto it = heldWaits.begin(); it != heldWaits.end();) {
        const auto accepted = std::find_if(storageRecovery.acceptedRecords.begin(),
            storageRecovery.acceptedRecords.end(),
            [&](const VNext::AcceptedStorageRecord& item) {
                return item.transactionId == it->first
                    && item.format == QByteArrayLiteral("MAB/3")
                    && item.bytes == it->second.originalMab;
            });
        if (accepted != storageRecovery.acceptedRecords.end()) {
            it = heldWaits.erase(it);
            continue;
        }
        auto bundle = VNext::ActionBundleCodec::decode(it->second.originalMab,
            scope.documentKey.toUtf8().toStdString(), scope.epoch);
        if (!bundle.ok() || bundle.value().actionId != it->first)
            return bundle.ok()
                ? CollabText::Error(CollabText::ErrorCode::corrupt_record,
                    "deferred quarantine action ID does not match its MAB")
                : bundle.error();
        recoveredWaits.push_back({bundle.value().deliverySequence,
            {it->first, it->second.originalMab}});
        ++it;
    }
    std::stable_sort(recoveredWaits.begin(), recoveredWaits.end(),
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
    for (auto& [deliverySequence, deferred] : recoveredWaits) {
        Q_UNUSED(deliverySequence)
        const size_t bytes = static_cast<size_t>(deferred.bytes.size());
        if (privateState->deferredRemoteActions.size()
                >= privateState->remoteActionQueueBudget.maxActions
            || bytes > privateState->remoteActionQueueBudget.maxBytes
                - std::min(privateState->deferredRemoteActionBytes,
                    privateState->remoteActionQueueBudget.maxBytes))
            return CollabText::Error(CollabText::ErrorCode::limit_exceeded,
                "recovered remote waits exceed the configured document queue budget");
        privateState->deferredRemoteActions.push_back(std::move(deferred));
        privateState->deferredRemoteActionBytes += bytes;
    }
    const std::string document = scope.documentKey.toUtf8().toStdString();
    size_t acceptedActions = 0;
    bool latestActionHasSourceSnapshot = false;
    for (const auto& accepted : privateState->lifecycle->recovery().acceptedRecords) {
        if (accepted.format != QByteArrayLiteral("MAB/3"))
            continue;
        auto bundle = VNext::ActionBundleCodec::decode(
            accepted.bytes, document, scope.epoch);
        if (!bundle.ok())
            return bundle.error();
        ++acceptedActions;
        latestActionHasSourceSnapshot = false;
        if (!bundle.value().sourceSnapshot.isEmpty()) {
            auto source = VNext::SourceSnapshotCodec::decode(
                bundle.value().sourceSnapshot, document, scope.epoch);
            if (!source.ok())
                return source.error();
            privateState->originalMarkdown = source.value().originalMarkdown;
            privateState->hasOriginalMarkdown = true;
            latestActionHasSourceSnapshot = true;
        }
    }
    std::vector<BlockId> initialBlockIds;
    if (const auto* list = privateState->lifecycle->projection().idList()) {
        const auto entries = list->visibleEntries();
        if (!entries.ok()) return entries.error();
        for (const auto& entry : entries.value())
            if (const auto identity = fromVNextIdentity(entry.identity))
                initialBlockIds.push_back(*identity);
    }
    rebuildSourceBlocks(*privateState, initialBlockIds);
    privateState->sourcePristine = privateState->hasOriginalMarkdown
        && acceptedActions > 0 && latestActionHasSourceSnapshot;
    auto documentObject = std::unique_ptr<MarkoffDocument>(
        new MarkoffDocument(std::move(privateState)));
    documentObject->d->lifecycle->setOnLocalActionCommitted(
        [document = documentObject.get()](const QByteArray& actionId,
                                           const QByteArray& bytes) {
            emit document->localActionEnvelopeCommitted(actionId, bytes);
        });
    documentObject->d->lifecycle->setOnStateChanged(
        [document = documentObject.get()] {
            emit document->providerStateChanged();
        });
    return documentObject;
}

VNext::ProviderDocumentState MarkoffDocument::providerState() const noexcept
{
    return d->lifecycle->state();
}

bool MarkoffDocument::canStartOrdinaryAction() const noexcept
{
    return d->lifecycle->canStartOrdinaryAction();
}

CollabText::Result<void> MarkoffDocument::recoverPendingAllocations()
{
    if (d->lifecycle->state() != VNext::ProviderDocumentState::recoveryRequired)
        return CollabText::Error(CollabText::ErrorCode::awaiting_state,
            "document has no pending allocation recovery");
    return finalizePendingReservations(*d->lifecycle);
}

void MarkoffDocument::compensateReplacementAction(
    const VNext::ActionHistoryEvent& target, bool redo)
{
    using namespace VNext;
    const auto& recovery = d->lifecycle->recovery();
    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    const ActionKind actionKind = redo ? ActionKind::redo : ActionKind::undo;
    const AcceptedStorageRecord* actionRecord = acceptedAction(recovery, target.actionId);
    if (!actionRecord) return;
    auto targetBundle = ActionBundleCodec::decode(actionRecord->bytes, document, scope.epoch);
    if (!targetBundle.ok()) return;

    QByteArray sourceSnapshot;
    if (redo) {
        const auto* replacementRecord = acceptedAction(recovery, target.parentActionId);
        if (!replacementRecord) return;
        auto sourceBundle = ActionBundleCodec::decode(
            replacementRecord->bytes, document, scope.epoch);
        if (!sourceBundle.ok()) return;
        if (sourceBundle.value().kind == ActionKind::replacement) {
            if (sourceBundle.value().sourceSnapshot.isEmpty()) return;
            sourceSnapshot = sourceBundle.value().sourceSnapshot;
        }
    } else if (!redo && target.kind == ActionKind::replacement) {
        if (target.kind != ActionKind::replacement
            || target.sourceSnapshotBefore.isEmpty()) return;
        sourceSnapshot = target.sourceSnapshotBefore;
    }
    if (!sourceSnapshot.isEmpty()) {
        auto checkedSource = SourceSnapshotCodec::decode(sourceSnapshot, document, scope.epoch);
        if (!checkedSource.ok()) return;
    }

    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok()) return;
    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId, actionKind);
    if (!sourceSnapshot.isEmpty()) stager.setSourceSnapshot(sourceSnapshot);
    struct Rollback {
        ProviderBackedDocumentState& lifecycle;
        const ActionStager& stager;
        bool armed = true;
        ~Rollback()
        {
            if (armed && !stager.bundle().operations.empty())
                (void)lifecycle.publishStagedAction(stager, OwnerHistorySnapshot{});
        }
    } rollback{*d->lifecycle, stager};

    ActionHistoryEvent event;
    event.kind = actionKind;
    event.parentActionId = target.actionId;
    std::vector<LocalHistoryRecord> updatedStreams;
    const auto rememberStream = [&](const LocalHistoryRecord& record) {
        const auto found = std::find_if(updatedStreams.begin(), updatedStreams.end(),
            [&](const LocalHistoryRecord& existing) {
                return existing.scope == record.scope && existing.actor == record.actor;
            });
        if (found == updatedStreams.end()) updatedStreams.push_back(record);
        else found->snapshot = record.snapshot;
    };

    const auto applyGroup = [&](const HistoryGroupAssociation& group) -> bool {
        if (group.primitive == OperationPrimitive::text) {
            auto identity = parseBlockTextStreamName(group.stream);
            if (!identity.ok()) return false;
            auto stream = d->lifecycle->stagingTextStream(identity.value());
            if (!stream.ok()) return false;
            auto operation = redo ? stream.value()->redoGroup(group.groupId)
                                 : stream.value()->undoGroup(group.groupId);
            if (!operation.ok() || !operation.value()
                || !stager.add(*operation.value()).ok()) return false;
            event.groups.push_back({group.stream, group.primitive, group.groupId,
                {{operation.value()->identity.actor, operation.value()->identity.counter}}});
            rememberStream({stream.value()->scope(), d->lifecycle->storage().actor(),
                            stream.value()->localHistorySnapshot()});
            return true;
        }
        if (group.primitive == OperationPrimitive::idList) {
            auto list = d->lifecycle->stagingIdList();
            if (!list.ok()) return false;
            auto operation = redo ? list.value()->redoGroup(group.groupId)
                                 : list.value()->undoGroup(group.groupId);
            if (!operation.ok() || !operation.value()
                || !stager.add(*operation.value()).ok()) return false;
            event.groups.push_back({group.stream, group.primitive, group.groupId,
                {{operation.value()->identity.actor, operation.value()->identity.counter}}});
            rememberStream({list.value()->scope(), d->lifecycle->storage().actor(),
                            list.value()->localHistorySnapshot()});
            return true;
        }

        const auto operation = std::find_if(targetBundle.value().operations.begin(),
            targetBundle.value().operations.end(), [&](const ActionOperation& candidate) {
                return candidate.primitive == OperationPrimitive::markoffMap
                    && candidate.stream == group.stream
                    && candidate.producer == group.groupId.actor
                    && candidate.counter == group.groupId.counter;
            });
        constexpr std::string_view prefix = "markoff/map/v1/";
        if (operation == targetBundle.value().operations.end()
            || !group.stream.starts_with(prefix)) return false;
        auto decoded = MarkoffMapCodec::decode(operation->payload);
        if (!decoded.ok()) return false;
        auto map = d->lifecycle->stagingMapStream(MapScope{
            document, group.stream.substr(prefix.size()), scope.epoch});
        if (!map.ok()) return false;
        auto compensation = group.mapPriorValueExisted
            ? map.value()->set(decoded.value().key, group.mapPriorValue,
                               operation->targetBlock)
            : map.value()->remove(decoded.value().key, operation->targetBlock);
        if (!compensation.ok() || !stager.add(compensation.value()).ok()) return false;
        auto association = toHistoryAssociation(compensation.value());
        if (!association.ok()) return false;
        event.groups.push_back(std::move(association).value());
        return true;
    };

    if (redo) {
        for (const auto& group : target.groups)
            if (!applyGroup(group)) return;
    } else {
        for (auto it = target.groups.rbegin(); it != target.groups.rend(); ++it)
            if (!applyGroup(*it)) return;
    }
    if (stager.bundle().operations.empty()) return;
    auto owner = ownerHistoryForAction(recovery, d->lifecycle->storage(), document,
        scope.epoch, actionId, std::move(event), updatedStreams.empty()
            ? std::nullopt : std::optional<LocalHistoryRecord>(updatedStreams.front()));
    if (!owner.ok()) return;
    for (size_t i = 1; i < updatedStreams.size(); ++i) {
        const auto& item = updatedStreams[i];
        const auto found = std::find_if(owner.value().streams.begin(),
            owner.value().streams.end(), [&](const LocalHistoryRecord& existing) {
                return existing.scope == item.scope && existing.actor == item.actor;
            });
        if (found == owner.value().streams.end()) owner.value().streams.push_back(item);
        else found->snapshot = item.snapshot;
    }
    rollback.armed = false;
    const auto publication = d->lifecycle->publishStagedAction(stager, owner.value());
    if (publication.state != ActionPublicationState::committed
        && publication.state != ActionPublicationState::committedHistoryPending)
        return;
    if (!sourceSnapshot.isEmpty()) {
        const auto restoredSource = SourceSnapshotCodec::decode(
            sourceSnapshot, document, scope.epoch);
        if (!restoredSource.ok()) return;
        d->originalMarkdown = restoredSource.value().originalMarkdown;
        d->hasOriginalMarkdown = true;
        d->sourcePristine = true;
        std::vector<BlockId> identities;
        if (const auto* list = d->lifecycle->projection().idList()) {
            const auto entries = list->visibleEntries();
            if (entries.ok())
                for (const auto& entry : entries.value())
                    if (const auto id = fromVNextIdentity(entry.identity))
                        identities.push_back(*id);
        }
        rebuildSourceBlocks(*d, identities);
        emit documentLoaded();
    } else {
        d->sourcePristine = false;
    }
    emit d2DocumentChanged();
    emit documentChanged();
    QList<BlockId> changedBlocks;
    for (const auto block : iterateBlocks()) changedBlocks.push_back(block);
    emit blocksChanged(changedBlocks);
}

void MarkoffDocument::compensateTextGroups(
    const VNext::ActionHistoryEvent& target, bool redo)
{
    using namespace VNext;
    if (target.groups.empty()
        || !std::all_of(target.groups.begin(), target.groups.end(),
            [](const HistoryGroupAssociation& group) {
                return group.primitive == OperationPrimitive::text;
            })) return;
    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    const auto& recovery = d->lifecycle->recovery();
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok()) return;
    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    const ActionKind actionKind = redo ? ActionKind::redo : ActionKind::undo;
    ActionStager stager(document, scope.epoch, sequence.value(), actionId, actionKind);
    ActionHistoryEvent event;
    event.kind = actionKind;
    event.parentActionId = target.actionId;
    std::vector<LocalHistoryRecord> updatedStreams;
    std::vector<BlockId> changedBlocks;
    const auto apply = [&](const HistoryGroupAssociation& group) -> bool {
        auto identity = parseBlockTextStreamName(group.stream);
        if (!identity.ok()) return false;
        auto stream = d->lifecycle->stagingTextStream(identity.value());
        if (!stream.ok()) return false;
        auto operation = redo ? stream.value()->redoGroup(group.groupId)
                              : stream.value()->undoGroup(group.groupId);
        if (!operation.ok() || !operation.value()
            || !stager.add(*operation.value()).ok()) return false;
        event.groups.push_back({group.stream, OperationPrimitive::text,
            group.groupId, {{operation.value()->identity.actor,
                             operation.value()->identity.counter}}});
        LocalHistoryRecord snapshot{stream.value()->scope(),
            d->lifecycle->storage().actor(), stream.value()->localHistorySnapshot()};
        const auto found = std::find_if(updatedStreams.begin(), updatedStreams.end(),
            [&](const LocalHistoryRecord& item) {
                return item.scope == snapshot.scope && item.actor == snapshot.actor;
            });
        if (found == updatedStreams.end()) updatedStreams.push_back(std::move(snapshot));
        else found->snapshot = std::move(snapshot.snapshot);
        if (const auto block = fromVNextIdentity(identity.value()))
            if (std::find(changedBlocks.begin(), changedBlocks.end(), *block)
                == changedBlocks.end()) changedBlocks.push_back(*block);
        return true;
    };
    bool applied = true;
    if (redo) {
        for (const auto& group : target.groups)
            if (!(applied = apply(group))) break;
    } else {
        for (auto it = target.groups.rbegin(); it != target.groups.rend(); ++it)
            if (!(applied = apply(*it))) break;
    }
    if (!applied || stager.bundle().operations.empty()) {
        if (!stager.bundle().operations.empty())
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
        for (const auto block : changedBlocks) emit tentativeBlockReverted(block);
        return;
    }
    auto owner = ownerHistoryForAction(recovery, d->lifecycle->storage(), document,
        scope.epoch, actionId, std::move(event), updatedStreams.front());
    if (!owner.ok()) {
        (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
        for (const auto block : changedBlocks) emit tentativeBlockReverted(block);
        return;
    }
    for (size_t i = 1; i < updatedStreams.size(); ++i) {
        const auto& item = updatedStreams[i];
        const auto found = std::find_if(owner.value().streams.begin(),
            owner.value().streams.end(), [&](const LocalHistoryRecord& record) {
                return record.scope == item.scope && record.actor == item.actor;
            });
        if (found == owner.value().streams.end()) owner.value().streams.push_back(item);
        else found->snapshot = item.snapshot;
    }
    for (const auto block : changedBlocks) emit tentativeBlockChanged(block);
    const auto publication = d->lifecycle->publishStagedAction(stager, owner.value());
    if (publication.state != ActionPublicationState::committed
        && publication.state != ActionPublicationState::committedHistoryPending) {
        if (publication.state == ActionPublicationState::absent
            || publication.state == ActionPublicationState::rejected)
            for (const auto block : changedBlocks) emit tentativeBlockReverted(block);
        return;
    }
    for (const auto block : changedBlocks) emit tentativeBlockCommitted(block);
    for (const auto block : changedBlocks) ++d->blockEditSequences[block];
    d->sourcePristine = false;
    QList<BlockId> changed;
    for (const auto block : changedBlocks) changed.push_back(block);
    emit blocksChanged(changed);
    emit d2DocumentChanged();
    emit documentChanged();
}

bool MarkoffDocument::canUndoD2() const
{
    const auto index = decodedActionIndex(*d->lifecycle);
    if (!index) return false;
    return std::any_of(index->events.rbegin(), index->events.rend(),
        [&](const VNext::ActionHistoryEvent& event) {
            return eventIsUndoable(*index, event);
        });
}

bool MarkoffDocument::canRedoD2() const
{
    const auto index = decodedActionIndex(*d->lifecycle);
    if (!index) return false;
    return std::any_of(index->events.rbegin(), index->events.rend(),
        [&](const VNext::ActionHistoryEvent& event) {
            return eventIsRedoable(*index, event);
        });
}

qsizetype MarkoffDocument::d2UndoDepth() const
{
    const auto index = decodedActionIndex(*d->lifecycle);
    if (!index) return 0;
    return std::count_if(index->events.begin(), index->events.end(),
        [&](const VNext::ActionHistoryEvent& event) {
            return eventIsUndoable(*index, event);
        });
}

void MarkoffDocument::undoD2()
{
    using namespace VNext;
    const auto& recovery = d->lifecycle->recovery();
    if (!recovery.localHistory) return;
    const auto document = d->lifecycle->storage().scope().documentKey.toUtf8().toStdString();
    const auto committed = std::find_if(recovery.acceptedRecords.cbegin(),
        recovery.acceptedRecords.cend(), [&](const AcceptedStorageRecord& record) {
            return record.transactionId == recovery.localHistory->committedTransactionId
                && record.committedRevision == recovery.localHistory->acceptedRevision;
        });
    if (committed == recovery.acceptedRecords.cend()) return;
    auto history = OwnerHistoryCodec::decode(recovery.localHistory->bytes, document,
        d->lifecycle->storage().scope().epoch, d->lifecycle->storage().actor(),
        recovery.localHistory->committedTransactionId, committed->priorRevision.token);
    if (!history.ok()) return;
    for (auto it = history.value().actionIndex.events.rbegin();
         it != history.value().actionIndex.events.rend(); ++it) {
        if (!eventIsUndoable(history.value().actionIndex, *it))
            continue;
        if (it->kind == ActionKind::replacement) {
            compensateReplacementAction(*it, false);
            return;
        }
        if (it->groups.size() > 1
            && std::any_of(it->groups.begin(), it->groups.end(),
                [](const HistoryGroupAssociation& group) {
                    return group.primitive == OperationPrimitive::text;
                })
            && std::any_of(it->groups.begin(), it->groups.end(),
                [](const HistoryGroupAssociation& group) {
                    return group.primitive != OperationPrimitive::text;
                })) {
            compensateReplacementAction(*it, false);
            return;
        }
        if (it->groups.size() > 1
            && std::all_of(it->groups.begin(), it->groups.end(),
                [](const HistoryGroupAssociation& group) {
                    return group.primitive == OperationPrimitive::text;
                })) {
            compensateTextGroups(*it, false);
            return;
        }
        if (const auto block = blockForAction(recovery, *it, document,
                d->lifecycle->storage().scope().epoch)) {
            undoForBlock(*block);
            return;
        }
        if (it->groups.size() == 1
            && it->groups.front().primitive == OperationPrimitive::markoffMap) {
            const auto publication = publishDocumentMapCompensation(*d->lifecycle,
                recovery, *it, it->groups.front(), ActionKind::undo);
            if (publication.state == ActionPublicationState::committed
                || publication.state == ActionPublicationState::committedHistoryPending) {
                d->sourcePristine = false; emit d2DocumentChanged(); emit documentChanged();
            }
            return;
        }
    }
}

void MarkoffDocument::redoD2()
{
    using namespace VNext;
    const auto& recovery = d->lifecycle->recovery();
    if (!recovery.localHistory) return;
    const auto document = d->lifecycle->storage().scope().documentKey.toUtf8().toStdString();
    const auto committed = std::find_if(recovery.acceptedRecords.cbegin(),
        recovery.acceptedRecords.cend(), [&](const AcceptedStorageRecord& record) {
            return record.transactionId == recovery.localHistory->committedTransactionId
                && record.committedRevision == recovery.localHistory->acceptedRevision;
        });
    if (committed == recovery.acceptedRecords.cend()) return;
    auto history = OwnerHistoryCodec::decode(recovery.localHistory->bytes, document,
        d->lifecycle->storage().scope().epoch, d->lifecycle->storage().actor(),
        recovery.localHistory->committedTransactionId, committed->priorRevision.token);
    if (!history.ok()) return;
    for (auto it = history.value().actionIndex.events.rbegin();
         it != history.value().actionIndex.events.rend(); ++it) {
        if (!eventIsRedoable(history.value().actionIndex, *it)) continue;
        const auto* parentRecord = acceptedAction(recovery, it->parentActionId);
        if (parentRecord) {
            auto parent = ActionBundleCodec::decode(parentRecord->bytes,
                                                     document,
                                                     d->lifecycle->storage().scope().epoch);
            if (parent.ok() && parent.value().kind == ActionKind::replacement) {
                compensateReplacementAction(*it, true);
                return;
            }
            if (parent.ok() && parent.value().kind == ActionKind::edit
                && it->groups.size() > 1
                && std::any_of(it->groups.begin(), it->groups.end(),
                    [](const HistoryGroupAssociation& group) {
                        return group.primitive == OperationPrimitive::text;
                    })
                && std::any_of(it->groups.begin(), it->groups.end(),
                    [](const HistoryGroupAssociation& group) {
                        return group.primitive != OperationPrimitive::text;
                    })) {
                compensateReplacementAction(*it, true);
                return;
            }
            if (parent.ok() && parent.value().kind == ActionKind::edit
                && it->groups.size() > 1
                && std::all_of(it->groups.begin(), it->groups.end(),
                    [](const HistoryGroupAssociation& group) {
                        return group.primitive == OperationPrimitive::text;
                    })) {
                compensateTextGroups(*it, true);
                return;
            }
        }
        if (const auto block = blockForAction(recovery, *it, document,
                d->lifecycle->storage().scope().epoch)) {
            redoForBlock(*block);
            return;
        }
        if (it->groups.size() == 1
            && it->groups.front().primitive == OperationPrimitive::markoffMap) {
            const auto publication = publishDocumentMapCompensation(*d->lifecycle,
                recovery, *it, it->groups.front(), ActionKind::redo);
            if (publication.state == ActionPublicationState::committed
                || publication.state == ActionPublicationState::committedHistoryPending) {
                d->sourcePristine = false; emit d2DocumentChanged(); emit documentChanged();
            }
            return;
        }
    }
}

bool MarkoffDocument::canUndoForBlock(BlockId block) const
{
    using namespace VNext;
    const auto identity = toVNextIdentity(block);
    if (!identity || !d->lifecycle->canStartOrdinaryAction()) return false;
    const auto route = blockTextStreamName(*identity);
    if (!route.ok()) return false;
    const auto& recovery = d->lifecycle->recovery();
    if (!recovery.localHistory) return false;
    const auto committed = std::find_if(recovery.acceptedRecords.cbegin(),
        recovery.acceptedRecords.cend(), [&](const AcceptedStorageRecord& record) {
            return record.transactionId == recovery.localHistory->committedTransactionId
                && record.committedRevision == recovery.localHistory->acceptedRevision;
        });
    if (committed == recovery.acceptedRecords.cend()) return false;
    const auto history = OwnerHistoryCodec::decode(recovery.localHistory->bytes,
        d->lifecycle->storage().scope().documentKey.toUtf8().toStdString(),
        d->lifecycle->storage().scope().epoch, d->lifecycle->storage().actor(),
        recovery.localHistory->committedTransactionId, committed->priorRevision.token);
    if (!history.ok()) return false;
    const auto document = d->lifecycle->storage().scope().documentKey.toUtf8().toStdString();
    for (auto it = history.value().actionIndex.events.rbegin();
         it != history.value().actionIndex.events.rend(); ++it) {
        if (it->kind != ActionKind::edit) continue;
        if (actionIsCurrentlyUndone(history.value().actionIndex.events, it->actionId)) continue;
        if (actionIsRetired(history.value().actionIndex, *it)) continue;
        const bool hasList = std::any_of(it->groups.begin(), it->groups.end(), isOrderListGroup);
        const bool pairedMap = hasList && it->groups.size() == 2
            && std::any_of(it->groups.begin(), it->groups.end(),
            [&](const auto& group) { return mapGroupTargetsBlock(recovery, *it, group,
                *identity, document, d->lifecycle->storage().scope().epoch); });
        const bool groupedBlockMaps = it->groups.size() > 1
            && std::all_of(it->groups.begin(), it->groups.end(), [&](const auto& group) {
                return group.primitive == OperationPrimitive::markoffMap
                    && mapGroupTargetsBlock(recovery, *it, group, *identity,
                        document, d->lifecycle->storage().scope().epoch);
            });
        const bool eligible = std::any_of(it->groups.begin(), it->groups.end(), [&](const auto& group) {
            if (group.primitive == OperationPrimitive::text && group.stream == route.value())
                return true;
            return mapGroupTargetsBlock(recovery, *it, group, *identity, document,
                d->lifecycle->storage().scope().epoch)
                && (it->groups.size() == 1 || pairedMap || groupedBlockMaps);
        });
        if (eligible) return true;
    }
    return false;
}

void MarkoffDocument::undoForBlock(BlockId block)
{
    using namespace VNext;
    if (!canUndoForBlock(block)) return;
    const auto identity = *toVNextIdentity(block);
    const auto route = blockTextStreamName(identity);
    const auto scope = d->lifecycle->storage().scope();
    const auto document = scope.documentKey.toUtf8().toStdString();
    auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok()) return;
    auto recovery = std::move(recoveryResult).value();
    const auto committed = std::find_if(recovery.acceptedRecords.cbegin(),
        recovery.acceptedRecords.cend(), [&](const AcceptedStorageRecord& record) {
            return record.transactionId == recovery.localHistory->committedTransactionId
                && record.committedRevision == recovery.localHistory->acceptedRevision;
        });
    if (committed == recovery.acceptedRecords.cend()) return;
    auto history = OwnerHistoryCodec::decode(recovery.localHistory->bytes, document,
        scope.epoch, d->lifecycle->storage().actor(),
        recovery.localHistory->committedTransactionId, committed->priorRevision.token);
    if (!history.ok()) return;
    const ActionHistoryEvent* target = nullptr;
    const HistoryGroupAssociation* targetGroup = nullptr;
    for (auto it = history.value().actionIndex.events.rbegin();
         it != history.value().actionIndex.events.rend() && !target; ++it) {
        if (it->kind != ActionKind::edit) continue;
        if (actionIsCurrentlyUndone(history.value().actionIndex.events, it->actionId)) continue;
        if (actionIsRetired(history.value().actionIndex, *it)) continue;
        const bool hasList = std::any_of(it->groups.begin(), it->groups.end(), isOrderListGroup);
        const bool pairedMap = hasList && it->groups.size() == 2
            && std::any_of(it->groups.begin(), it->groups.end(),
            [&](const auto& group) { return mapGroupTargetsBlock(recovery, *it, group,
                identity, document, scope.epoch); });
        const bool groupedBlockMaps = it->groups.size() > 1
            && std::all_of(it->groups.begin(), it->groups.end(), [&](const auto& group) {
                return group.primitive == OperationPrimitive::markoffMap
                    && mapGroupTargetsBlock(recovery, *it, group, identity,
                        document, scope.epoch);
            });
        for (const auto& group : it->groups) {
            if ((group.primitive == OperationPrimitive::text && group.stream == route.value())
                || (mapGroupTargetsBlock(recovery, *it, group, identity, document, scope.epoch)
                    && (it->groups.size() == 1 || pairedMap || groupedBlockMaps))) {
                target = &*it; targetGroup = &group; break;
            }
        }
    }
    if (!target || !targetGroup) return;
    uint64_t sequence = 1;
    auto next = nextDeliverySequence(recovery, document, scope.epoch);
    if (!next.ok()) return;
    sequence = next.value();
    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence, actionId, ActionKind::undo);
    ActionHistoryEvent event;
    event.kind = ActionKind::undo;
    event.parentActionId = target->actionId;
    std::vector<LocalHistoryRecord> updatedStreams;
    const auto beforeOrder = iterateBlocks();
    QHash<BlockId, int> beforeRows;
    for (qsizetype row = 0; row < beforeOrder.size(); ++row)
        beforeRows.insert(beforeOrder[row], static_cast<int>(row));
    const auto rollbackUndo = [&] {
        if (!stager.bundle().operations.empty())
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
    };
    const auto* record = acceptedAction(recovery, target->actionId);
    if (!record) return;
    auto bundle = ActionBundleCodec::decode(record->bytes, document, scope.epoch);
    if (!bundle.ok()) return;
    constexpr std::string_view mapPrefix = "markoff/map/v1/";
    for (const auto& group : target->groups) {
        if (group.primitive == OperationPrimitive::text) {
            auto blockIdentity = parseBlockTextStreamName(group.stream);
            if (!blockIdentity.ok()) { rollbackUndo(); return; }
            auto stream = d->lifecycle->stagingTextStream(blockIdentity.value());
            if (!stream.ok()) { rollbackUndo(); return; }
            auto undone = stream.value()->undoGroup(group.groupId);
            if (!undone.ok() || !undone.value()
                || !stager.add(*undone.value()).ok()) { rollbackUndo(); return; }
            event.groups.push_back({group.stream, OperationPrimitive::text,
                group.groupId, {{undone.value()->identity.actor,
                                 undone.value()->identity.counter}}});
            updatedStreams.push_back({stream.value()->scope(),
                d->lifecycle->storage().actor(), stream.value()->localHistorySnapshot()});
        } else if (isOrderListGroup(group)) {
            auto list = d->lifecycle->stagingIdList();
            if (!list.ok()) { rollbackUndo(); return; }
            auto undone = list.value()->undoGroup(group.groupId);
            if (!undone.ok() || !undone.value()
                || !stager.add(*undone.value()).ok()) { rollbackUndo(); return; }
            event.groups.push_back({group.stream, OperationPrimitive::idList,
                group.groupId, {{undone.value()->identity.actor,
                                 undone.value()->identity.counter}}});
            updatedStreams.push_back({list.value()->scope(),
                d->lifecycle->storage().actor(), list.value()->localHistorySnapshot()});
        } else if (group.primitive == OperationPrimitive::markoffMap) {
            const auto source = std::find_if(bundle.value().operations.begin(),
                bundle.value().operations.end(), [&](const ActionOperation& operation) {
                    return operation.primitive == OperationPrimitive::markoffMap
                        && operation.stream == group.stream
                        && operation.producer == group.groupId.actor
                        && operation.counter == group.groupId.counter;
                });
            if (source == bundle.value().operations.end()
                || !group.stream.starts_with(mapPrefix)) { rollbackUndo(); return; }
            auto decoded = MarkoffMapCodec::decode(source->payload);
            if (!decoded.ok()) { rollbackUndo(); return; }
            auto map = d->lifecycle->stagingMapStream(MapScope{document,
                group.stream.substr(mapPrefix.size()), scope.epoch});
            if (!map.ok()) { rollbackUndo(); return; }
            QByteArray currentBytes;
            MarkoffMapOperation currentOperation;
            for (auto accepted = recovery.acceptedRecords.rbegin();
                 accepted != recovery.acceptedRecords.rend() && currentBytes.isEmpty(); ++accepted) {
                if (accepted->format != QByteArrayLiteral("MAB/3")) continue;
                auto acceptedBundle = ActionBundleCodec::decode(
                    accepted->bytes, document, scope.epoch);
                if (!acceptedBundle.ok()) { rollbackUndo(); return; }
                for (const auto& operation : acceptedBundle.value().operations) {
                    if (operation.primitive != OperationPrimitive::markoffMap
                        || operation.stream != group.stream
                        || operation.targetBlock != source->targetBlock) continue;
                    auto candidate = MarkoffMapCodec::decode(operation.payload);
                    if (candidate.ok() && candidate.value().map == decoded.value().map
                        && candidate.value().key == decoded.value().key) {
                        currentBytes = operation.payload;
                        currentOperation = std::move(candidate).value();
                        break;
                    }
                }
            }
            if (currentBytes.isEmpty()) { rollbackUndo(); return; }
            LocalMapOperation local;
            local.scope = map.value()->scope();
            local.targetBlock = source->targetBlock;
            local.operation = std::move(currentOperation);
            local.bytes = currentBytes;
            local.priorValueExisted = group.mapPriorValueExisted;
            local.priorValue = group.mapPriorValue;
            auto compensation = map.value()->compensate(local);
            if (!compensation.ok() || !stager.add(compensation.value()).ok())
                { rollbackUndo(); return; }
            auto association = toHistoryAssociation(compensation.value());
            if (!association.ok()) { rollbackUndo(); return; }
            event.groups.push_back(std::move(association).value());
        }
    }
    if (event.groups.empty()) { rollbackUndo(); return; }
    auto owner = ownerHistoryForAction(recovery, d->lifecycle->storage(), document,
        scope.epoch, actionId, std::move(event), updatedStreams.empty()
            ? std::nullopt : std::optional<LocalHistoryRecord>(updatedStreams.front()));
    if (!owner.ok()) { (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{}); return; }
    for (size_t i = 1; i < updatedStreams.size(); ++i) {
        const auto& item = updatedStreams[i];
        const auto found = std::find_if(owner.value().streams.begin(),
            owner.value().streams.end(), [&](const LocalHistoryRecord& current) {
                return current.scope == item.scope && current.actor == item.actor;
            });
        if (found == owner.value().streams.end()) owner.value().streams.push_back(item);
        else found->snapshot = item.snapshot;
    }
    emit tentativeBlockChanged(block);
    const auto publication = d->lifecycle->publishStagedAction(stager, owner.value());
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        QList<BlockId> changed{block};
        bool structureChanged = false;
        for (const auto& group : target->groups) {
            if (group.primitive == OperationPrimitive::text) {
                auto textIdentity = parseBlockTextStreamName(group.stream);
                if (textIdentity.ok())
                    if (auto changedBlock = fromVNextIdentity(textIdentity.value()))
                        if (!changed.contains(*changedBlock)) changed.push_back(*changedBlock);
            } else if (isOrderListGroup(group)) {
                structureChanged = true;
            } else if (group.primitive == OperationPrimitive::markoffMap) {
                structureChanged |= group.stream == "markoff/map/v1/kind-tag";
                if (const auto* targetRecord = acceptedAction(recovery, target->actionId)) {
                    auto targetBundle = ActionBundleCodec::decode(targetRecord->bytes,
                        document, scope.epoch);
                    if (targetBundle.ok())
                        for (const auto& operation : targetBundle.value().operations)
                            if (operation.primitive == OperationPrimitive::markoffMap
                                && operation.stream == group.stream
                                && operation.producer == group.groupId.actor
                                && operation.counter == group.groupId.counter
                                && operation.targetBlock)
                                if (auto changedBlock = fromVNextIdentity(*operation.targetBlock))
                                    if (!changed.contains(*changedBlock))
                                        changed.push_back(*changedBlock);
                }
            }
        }
        const auto afterOrder = iterateBlocks();
        QHash<BlockId, int> afterRows;
        for (qsizetype row = 0; row < afterOrder.size(); ++row)
            afterRows.insert(afterOrder[row], static_cast<int>(row));
        for (const auto changedBlock : afterOrder)
            if (!beforeRows.contains(changedBlock) && !changed.contains(changedBlock))
                changed.push_back(changedBlock);
        for (const auto changedBlock : beforeOrder)
            if (!afterRows.contains(changedBlock) && !changed.contains(changedBlock))
                changed.push_back(changedBlock);
        for (const auto changedBlock : changed) ++d->blockEditSequences[changedBlock];
        if (structureChanged || beforeOrder != afterOrder) ++d->structuralEditSequence;
        for (const auto changedBlock : beforeOrder)
            if (!afterRows.contains(changedBlock))
                emit blockRemoved(changedBlock, beforeRows.value(changedBlock));
        for (const auto changedBlock : afterOrder)
            if (!beforeRows.contains(changedBlock))
                emit blockInserted(changedBlock, afterRows.value(changedBlock));
        emit tentativeBlockCommitted(block);
        emit blocksChanged(changed);
        emit d2DocumentChanged(); emit documentChanged();
    } else if (publication.state == ActionPublicationState::rejected
               || publication.state == ActionPublicationState::absent) {
        emit tentativeBlockReverted(block);
    }
    return;
}

void MarkoffDocument::redoForBlock(BlockId block)
{
    using namespace VNext;
    const auto identityValue = toVNextIdentity(block);
    if (!identityValue || !d->lifecycle->canStartOrdinaryAction()) return;
    const auto identity = *identityValue;
    const auto route = blockTextStreamName(identity);
    if (!route.ok()) return;
    const auto scope = d->lifecycle->storage().scope();
    const auto document = scope.documentKey.toUtf8().toStdString();
    auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok()) return;
    auto recovery = std::move(recoveryResult).value();
    if (!recovery.localHistory) return;
    const auto committed = std::find_if(recovery.acceptedRecords.cbegin(),
        recovery.acceptedRecords.cend(), [&](const AcceptedStorageRecord& record) {
            return record.transactionId == recovery.localHistory->committedTransactionId
                && record.committedRevision == recovery.localHistory->acceptedRevision;
        });
    if (committed == recovery.acceptedRecords.cend()) return;
    auto history = OwnerHistoryCodec::decode(recovery.localHistory->bytes, document,
        scope.epoch, d->lifecycle->storage().actor(),
        recovery.localHistory->committedTransactionId, committed->priorRevision.token);
    if (!history.ok()) return;
    const ActionHistoryEvent* target = nullptr;
    for (auto it = history.value().actionIndex.events.rbegin();
         it != history.value().actionIndex.events.rend(); ++it) {
        if (it->kind != ActionKind::undo) continue;
        if (std::any_of(history.value().actionIndex.events.begin(),
            history.value().actionIndex.events.end(), [&](const ActionHistoryEvent& later) {
                return later.kind == ActionKind::redo && later.parentActionId == it->actionId;
            })) continue;
        const bool textTarget = std::any_of(it->groups.begin(), it->groups.end(),
            [&](const auto& group) {
                return group.primitive == OperationPrimitive::text
                    && group.stream == route.value();
            });
        const bool mapTarget = std::any_of(it->groups.begin(), it->groups.end(),
            [&](const auto& group) {
                return mapGroupTargetsBlock(recovery, *it, group, identity,
                                            document, scope.epoch);
            });
        const bool pairedStructural = it->groups.size() == 2 && mapTarget
            && std::any_of(it->groups.begin(), it->groups.end(), isOrderListGroup);
        const bool groupedMapAction = it->groups.size() > 1
            && std::all_of(it->groups.begin(), it->groups.end(), [&](const auto& group) {
                return group.primitive == OperationPrimitive::markoffMap
                    && mapGroupTargetsBlock(recovery, *it, group, identity,
                        document, scope.epoch);
            });
        if (textTarget || mapTarget
            && (it->groups.size() == 1 || pairedStructural || groupedMapAction)) {
            target = &*it; break;
        }
    }
    if (!target) return;
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok()) return;
    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId, ActionKind::redo);
    ActionHistoryEvent event;
    event.kind = ActionKind::redo;
    event.parentActionId = target->actionId;
    std::vector<LocalHistoryRecord> updatedStreams;
    const auto beforeOrder = iterateBlocks();
    QHash<BlockId, int> beforeRows;
    for (qsizetype row = 0; row < beforeOrder.size(); ++row)
        beforeRows.insert(beforeOrder[row], static_cast<int>(row));
    for (const auto& group : target->groups) {
        if (group.primitive == OperationPrimitive::text) {
            auto blockIdentity = parseBlockTextStreamName(group.stream);
            if (!blockIdentity.ok()) goto rollback_redo;
            auto stream = d->lifecycle->stagingTextStream(blockIdentity.value());
            if (!stream.ok()) goto rollback_redo;
            auto redone = stream.value()->redoGroup(group.groupId);
            if (!redone.ok() || !redone.value()) goto rollback_redo;
            if (!stager.add(*redone.value()).ok()) goto rollback_redo;
            event.groups.push_back({group.stream, group.primitive, group.groupId,
                {{redone.value()->identity.actor, redone.value()->identity.counter}}});
            updatedStreams.push_back({stream.value()->scope(), d->lifecycle->storage().actor(),
                stream.value()->localHistorySnapshot()});
        } else if (isOrderListGroup(group)) {
            auto list = d->lifecycle->stagingIdList();
            if (!list.ok()) goto rollback_redo;
            auto redone = list.value()->redoGroup(group.groupId);
            if (!redone.ok() || !redone.value()) goto rollback_redo;
            if (!stager.add(*redone.value()).ok()) goto rollback_redo;
            event.groups.push_back({group.stream, group.primitive, group.groupId,
                {{redone.value()->identity.actor, redone.value()->identity.counter}}});
            updatedStreams.push_back({list.value()->scope(), d->lifecycle->storage().actor(),
                list.value()->localHistorySnapshot()});
        } else if (group.primitive == OperationPrimitive::markoffMap) {
            const auto* record = acceptedAction(recovery, target->actionId);
            if (!record) goto rollback_redo;
            auto bundle = ActionBundleCodec::decode(record->bytes, document, scope.epoch);
            if (!bundle.ok()) goto rollback_redo;
            const auto source = std::find_if(bundle.value().operations.begin(),
                bundle.value().operations.end(), [&](const ActionOperation& operation) {
                    return operation.primitive == OperationPrimitive::markoffMap
                        && operation.stream == group.stream
                        && operation.producer == group.groupId.actor
                        && operation.counter == group.groupId.counter;
                });
            if (source == bundle.value().operations.end()) goto rollback_redo;
            auto decoded = MarkoffMapCodec::decode(source->payload);
            constexpr std::string_view prefix = "markoff/map/v1/";
            if (!decoded.ok() || !group.stream.starts_with(prefix)) goto rollback_redo;
            const auto mapTargetBlock = source->targetBlock;
            QByteArray currentBytes;
            MarkoffMapOperation currentOperation;
            for (auto accepted = recovery.acceptedRecords.rbegin();
                 accepted != recovery.acceptedRecords.rend() && currentBytes.isEmpty();
                 ++accepted) {
                if (accepted->format != QByteArrayLiteral("MAB/3")) continue;
                auto acceptedBundle = ActionBundleCodec::decode(
                    accepted->bytes, document, scope.epoch);
                if (!acceptedBundle.ok()) goto rollback_redo;
                for (const auto& operation : acceptedBundle.value().operations) {
                    if (operation.primitive != OperationPrimitive::markoffMap
                        || operation.stream != group.stream
                        || operation.targetBlock != mapTargetBlock)
                        continue;
                    auto candidate = MarkoffMapCodec::decode(operation.payload);
                    if (candidate.ok() && candidate.value().map == decoded.value().map
                        && candidate.value().key == decoded.value().key) {
                        currentBytes = operation.payload;
                        currentOperation = std::move(candidate).value();
                        break;
                    }
                }
            }
            if (currentBytes.isEmpty()) goto rollback_redo;
            auto map = d->lifecycle->stagingMapStream(MapScope{document,
                group.stream.substr(prefix.size()), scope.epoch});
            if (!map.ok()) goto rollback_redo;
            LocalMapOperation local;
            local.scope = map.value()->scope(); local.targetBlock = mapTargetBlock;
            local.operation = std::move(currentOperation); local.bytes = currentBytes;
            local.priorValueExisted = group.mapPriorValueExisted;
            local.priorValue = group.mapPriorValue;
            auto compensation = map.value()->compensate(local);
            if (!compensation.ok() || !stager.add(compensation.value()).ok()) goto rollback_redo;
            auto association = toHistoryAssociation(compensation.value());
            if (!association.ok()) goto rollback_redo;
            event.groups.push_back(std::move(association).value());
        }
    }
    if (stager.bundle().operations.empty()) return;
    {
        auto owner = ownerHistoryForAction(recovery, d->lifecycle->storage(), document,
            scope.epoch, actionId, std::move(event), updatedStreams.empty()
                ? std::nullopt : std::optional<LocalHistoryRecord>(updatedStreams.front()));
        if (!owner.ok()) goto rollback_redo;
        for (size_t i = 1; i < updatedStreams.size(); ++i) {
            const auto& item = updatedStreams[i];
            auto found = std::find_if(owner.value().streams.begin(), owner.value().streams.end(),
                [&](const LocalHistoryRecord& old) {
                    return old.scope == item.scope && old.actor == item.actor;
                });
            if (found == owner.value().streams.end()) owner.value().streams.push_back(item);
            else found->snapshot = item.snapshot;
        }
        emit tentativeBlockChanged(block);
        const auto publication = d->lifecycle->publishStagedAction(stager, owner.value());
        if (publication.state == ActionPublicationState::committed
            || publication.state == ActionPublicationState::committedHistoryPending) {
            d->sourcePristine = false;
            QList<BlockId> changed{block};
            bool structureChanged = false;
            const auto* undoRecord = acceptedAction(recovery, target->actionId);
            for (const auto& group : target->groups) {
                if (group.primitive == OperationPrimitive::text) {
                    auto textIdentity = parseBlockTextStreamName(group.stream);
                    if (textIdentity.ok())
                        if (auto changedBlock = fromVNextIdentity(textIdentity.value()))
                            if (!changed.contains(*changedBlock)) changed.push_back(*changedBlock);
                } else if (isOrderListGroup(group)) {
                    structureChanged = true;
                } else if (group.primitive == OperationPrimitive::markoffMap) {
                    structureChanged |= group.stream == "markoff/map/v1/kind-tag";
                    if (undoRecord) {
                        auto undoBundle = ActionBundleCodec::decode(
                            undoRecord->bytes, document, scope.epoch);
                        if (undoBundle.ok()) for (const auto& operation : undoBundle.value().operations)
                            if (operation.primitive == OperationPrimitive::markoffMap
                                && operation.stream == group.stream
                                && operation.producer == group.groupId.actor
                                && operation.counter == group.groupId.counter
                                && operation.targetBlock)
                                if (auto changedBlock = fromVNextIdentity(*operation.targetBlock))
                                    if (!changed.contains(*changedBlock))
                                        changed.push_back(*changedBlock);
                    }
                }
            }
            const auto afterOrder = iterateBlocks();
            QHash<BlockId, int> afterRows;
            for (qsizetype row = 0; row < afterOrder.size(); ++row)
                afterRows.insert(afterOrder[row], static_cast<int>(row));
            for (const auto changedBlock : afterOrder)
                if (!beforeRows.contains(changedBlock) && !changed.contains(changedBlock))
                    changed.push_back(changedBlock);
            for (const auto changedBlock : beforeOrder)
                if (!afterRows.contains(changedBlock) && !changed.contains(changedBlock))
                    changed.push_back(changedBlock);
            for (const auto changedBlock : changed) ++d->blockEditSequences[changedBlock];
            if (structureChanged || beforeOrder != afterOrder) ++d->structuralEditSequence;
            for (const auto changedBlock : beforeOrder)
                if (!afterRows.contains(changedBlock))
                    emit blockRemoved(changedBlock, beforeRows.value(changedBlock));
            for (const auto changedBlock : afterOrder)
                if (!beforeRows.contains(changedBlock))
                    emit blockInserted(changedBlock, afterRows.value(changedBlock));
            emit tentativeBlockCommitted(block);
            emit blocksChanged(changed); emit d2DocumentChanged(); emit documentChanged();
        } else if (publication.state == ActionPublicationState::rejected
                   || publication.state == ActionPublicationState::absent) {
            emit tentativeBlockReverted(block);
        }
        return;
    }
rollback_redo:
    (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
}

VNext::ActionPublicationResult MarkoffDocument::importInitialMarkdown(
    const QByteArray& source)
{
    return applyMarkdownSource(source, false);
}

VNext::ActionPublicationResult MarkoffDocument::acceptRemoteAction(
    const QByteArray& mabBytes)
{
    using namespace VNext;
    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    auto bundle = ActionBundleCodec::decode(mabBytes, document, scope.epoch);
    if (!bundle.ok())
        return {ActionPublicationState::rejected, std::nullopt, bundle.error()};
    auto identities = ActionBundleCodec::validateOperationIdentities(bundle.value());
    if (!identities.ok())
        return {ActionPublicationState::rejected, std::nullopt, identities.error()};

    const auto prior = std::find_if(d->lifecycle->recovery().acceptedRecords.begin(),
        d->lifecycle->recovery().acceptedRecords.end(), [&](const AcceptedStorageRecord& record) {
            return record.transactionId == bundle.value().actionId;
        });
    if (prior != d->lifecycle->recovery().acceptedRecords.end()) {
        if (prior->format != QByteArrayLiteral("MAB/3") || prior->bytes != mabBytes)
            return {ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::pin_conflict,
                    "remote action ID was already accepted with different MAB bytes")};
        return {ActionPublicationState::committed, prior->committedRevision, std::nullopt};
    }

    PreparedStorageTransaction transaction;
    transaction.transactionId = bundle.value().actionId;
    transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    transaction.sharedRecord = mabBytes;
    transaction.remoteAcceptance = true;
    transaction.expectedRevision = d->lifecycle->recovery().acceptedRevision;
    transaction.operationIdentities.reserve(bundle.value().operations.size());
    for (const auto& operation : bundle.value().operations)
        transaction.operationIdentities.push_back({
            QString::fromUtf8(operation.stream.data(),
                              static_cast<qsizetype>(operation.stream.size())),
            operation.producer, operation.counter});

    const auto beforeOrder = iterateBlocks();
    QHash<BlockId, QByteArray> beforeText;
    for (const auto block : beforeOrder) beforeText.insert(block, blockText(block));

    const auto publication = d->lifecycle->acceptRemoteAction(transaction);
    if (publication.state != ActionPublicationState::committed)
        return publication;

    const auto afterOrder = iterateBlocks();
    QHash<BlockId, int> beforeRows;
    QHash<BlockId, int> afterRows;
    for (qsizetype i = 0; i < beforeOrder.size(); ++i)
        beforeRows.insert(beforeOrder[i], static_cast<int>(i));
    for (qsizetype i = 0; i < afterOrder.size(); ++i)
        afterRows.insert(afterOrder[i], static_cast<int>(i));

    QList<BlockId> changed;
    QSet<BlockId> changedSet;
    auto markChanged = [&](BlockId block) {
        if (!block.isNull() && !changedSet.contains(block)) {
            changedSet.insert(block);
            changed.push_back(block);
        }
    };
    for (const auto& operation : bundle.value().operations)
        if (operation.targetBlock)
            if (const auto block = fromVNextIdentity(*operation.targetBlock))
                markChanged(*block);

    bool structureChanged = beforeOrder != afterOrder;
    for (const auto& operation : bundle.value().operations)
        if (operation.primitive == OperationPrimitive::markoffMap
            && operation.stream == "markoff/map/v1/kind-tag")
            structureChanged = true;
    for (const auto block : afterOrder) {
        if (!beforeText.contains(block) || beforeText.value(block) != blockText(block)) {
            markChanged(block);
            ++d->blockEditSequences[block];
        }
    }
    for (const auto block : beforeOrder)
        if (!afterRows.contains(block)) markChanged(block);
    for (const auto block : afterOrder)
        if (!beforeRows.contains(block)) markChanged(block);

    if (structureChanged) ++d->structuralEditSequence;
    d->sourcePristine = false;
    if (!bundle.value().sourceSnapshot.isEmpty()) {
        auto source = SourceSnapshotCodec::decode(bundle.value().sourceSnapshot,
                                                  document, scope.epoch);
        if (!source.ok())
            return {ActionPublicationState::uncertain, publication.committedRevision,
                    source.error()};
        d->originalMarkdown = source.value().originalMarkdown;
        d->hasOriginalMarkdown = true;
        d->sourcePristine = true;
        std::vector<BlockId> identities(afterOrder.begin(), afterOrder.end());
        rebuildSourceBlocks(*d, identities);
    }

    for (const auto block : beforeOrder)
        if (!afterRows.contains(block)) emit blockRemoved(block, beforeRows.value(block));
    for (const auto block : afterOrder)
        if (!beforeRows.contains(block)) emit blockInserted(block, afterRows.value(block));
    if (!changed.isEmpty()) emit blocksChanged(changed);
    emit d2DocumentChanged();
    emit documentChanged();
    return publication;
}

RemoteActionRouteResult MarkoffDocument::routeRemoteAction(const QByteArray& mabBytes)
{
    using namespace VNext;
    RemoteActionRouteResult routed;
    if (std::this_thread::get_id() != d->ownerThread) {
        routed.state = RemoteActionRouteState::rejected;
        routed.diagnostic.emplace(CollabText::ErrorCode::unsupported_operation,
            "remote action routing must run on the document owner thread");
        return routed;
    }
    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    auto bundle = ActionBundleCodec::decode(mabBytes, document, scope.epoch);
    if (!bundle.ok()) {
        routed.state = RemoteActionRouteState::rejected;
        routed.diagnostic = bundle.error();
        return routed;
    }
    auto validIdentities = ActionBundleCodec::validateOperationIdentities(bundle.value());
    if (!validIdentities.ok()) {
        routed.state = RemoteActionRouteState::rejected;
        routed.diagnostic = validIdentities.error();
        return routed;
    }

    const auto persistQuarantine = [this](const ActionBundleCodec::Bundle& rejected,
                                          const QByteArray& originalEnvelope,
                                          const CollabText::Error& reason)
        -> CollabText::Result<void> {
        const auto& quarantineScope = d->lifecycle->storage().scope();
        for (size_t index = 0; index < rejected.operations.size(); ++index) {
            const auto& operation = rejected.operations[index];
            VNext::QuarantineRecord record;
            record.document = quarantineScope.documentKey.toUtf8().toStdString();
            record.epoch = quarantineScope.epoch;
            record.stream = operation.stream;
            record.primitive = operation.primitive == VNext::OperationPrimitive::idList
                ? VNext::QuarantinePrimitive::list
                : operation.primitive == VNext::OperationPrimitive::markoffMap
                    ? VNext::QuarantinePrimitive::map
                    : VNext::QuarantinePrimitive::text;
            record.actor = operation.producer;
            record.counter = operation.counter;
            if (record.primitive == VNext::QuarantinePrimitive::map)
                record.targetBlock = operation.targetBlock;
            record.payload = operation.payload;
            record.deliverySequence = rejected.deliverySequence;
            record.actionId = rejected.actionId;
            record.originalActionEnvelope = originalEnvelope;
            record.bundleIndex = uint32_t(index);
            record.bundleOperationCount = uint32_t(rejected.operations.size());
            record.reason = reason;
            auto encoded = VNext::QuarantineRecordCodec::encode(record);
            if (!encoded.ok()) return encoded.error();
            auto stored = d->lifecycle->storage().storeQuarantinedOperation(
                encoded.value());
            if (!stored.ok()) return stored.error();
        }
        return {};
    };

    for (const auto& pending : d->deferredRemoteActions) {
        if (pending.actionId == bundle.value().actionId) {
            if (pending.bytes == mabBytes) {
                // A sender retransmits an unreceipted action after reconnect.
                // Use that delivery to retry the durable wait: its dependency
                // may have committed while this receiver was offline.
                routed = retryDeferredRemoteActions();
                if (routed.committed.empty()
                    && routed.quarantined.empty()
                    && routed.state == RemoteActionRouteState::committed)
                    routed.state = RemoteActionRouteState::deferred;
                return routed;
            }
            routed.state = RemoteActionRouteState::rejected;
            routed.diagnostic.emplace(CollabText::ErrorCode::pin_conflict,
                "deferred remote action ID was offered with different MAB bytes");
            return routed;
        }
        auto previous = ActionBundleCodec::decode(pending.bytes, document, scope.epoch);
        if (!previous.ok()) {
            routed.state = RemoteActionRouteState::uncertain;
            routed.diagnostic = previous.error();
            return routed;
        }
        for (const auto& operation : bundle.value().operations) {
            const auto conflict = std::find_if(previous.value().operations.begin(),
                previous.value().operations.end(), [&](const ActionOperation& held) {
                    return held.stream == operation.stream
                        && held.producer == operation.producer
                        && held.counter == operation.counter;
                });
            if (conflict != previous.value().operations.end()) {
                routed.state = RemoteActionRouteState::rejected;
                routed.diagnostic.emplace(CollabText::ErrorCode::pin_conflict,
                    "deferred MABs reuse a scoped operation identity");
                return routed;
            }
        }
    }

    auto result = acceptRemoteAction(mabBytes);
    auto isDeferred = [](const VNext::ActionPublicationResult& publication) {
        return publication.state == VNext::ActionPublicationState::rejected
            && publication.diagnostic
            && (publication.diagnostic->code()
                    == CollabText::ErrorCode::history_unavailable
                || publication.diagnostic->code()
                    == CollabText::ErrorCode::unknown_stream);
    };
    if (result.state == ActionPublicationState::committed) {
        if (result.committedRevision)
            routed.committed.push_back({bundle.value().actionId,
                                        *result.committedRevision});
        routed.state = RemoteActionRouteState::committed;
        auto retried = retryDeferredRemoteActions();
        routed.committed.insert(routed.committed.end(), retried.committed.begin(),
                                retried.committed.end());
        routed.quarantined.insert(routed.quarantined.end(), retried.quarantined.begin(),
                                  retried.quarantined.end());
        if (retried.state == RemoteActionRouteState::uncertain) {
            routed.state = retried.state;
            routed.diagnostic = std::move(retried.diagnostic);
        }
        return routed;
    }

    if (isDeferred(result)) {
        const size_t charged = static_cast<size_t>(mabBytes.size());
        if (d->deferredRemoteActions.size() >= d->remoteActionQueueBudget.maxActions
            || charged > d->remoteActionQueueBudget.maxBytes
                - std::min(d->deferredRemoteActionBytes,
                           d->remoteActionQueueBudget.maxBytes)) {
            routed.state = RemoteActionRouteState::backpressured;
            routed.diagnostic.emplace(CollabText::ErrorCode::deferred_queue_full,
                "deferred remote MAB queue is full; caller retains this envelope");
            return routed;
        }
        auto stored = persistQuarantine(bundle.value(), mabBytes,
                                        *result.diagnostic);
        if (!stored.ok()) {
            routed.state = RemoteActionRouteState::uncertain;
            routed.diagnostic = stored.error();
            return routed;
        }
        d->deferredRemoteActions.push_back({bundle.value().actionId, mabBytes});
        d->deferredRemoteActionBytes += charged;
        routed.state = RemoteActionRouteState::deferred;
        routed.diagnostic = result.diagnostic;
        return routed;
    }

    if (result.state == ActionPublicationState::rejected && result.diagnostic) {
        auto stored = persistQuarantine(bundle.value(), mabBytes, *result.diagnostic);
        if (!stored.ok()) {
            routed.state = RemoteActionRouteState::uncertain;
            routed.diagnostic = stored.error();
            return routed;
        }
        routed.state = RemoteActionRouteState::quarantined;
        routed.diagnostic = result.diagnostic;
        routed.quarantined.push_back(bundle.value().actionId);
        return routed;
    }

    routed.state = result.state == ActionPublicationState::uncertain
        ? RemoteActionRouteState::uncertain : RemoteActionRouteState::rejected;
    routed.diagnostic = result.diagnostic;
    return routed;
}

RemoteActionRouteResult MarkoffDocument::retryDeferredRemoteActions()
{
    using namespace VNext;
    RemoteActionRouteResult routed;
    if (std::this_thread::get_id() != d->ownerThread) {
        routed.state = RemoteActionRouteState::rejected;
        routed.diagnostic.emplace(CollabText::ErrorCode::unsupported_operation,
            "deferred remote action retry must run on the document owner thread");
        return routed;
    }
    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    bool madeProgress = false;
    do {
        madeProgress = false;
        auto pending = d->deferredRemoteActions.begin();
        while (pending != d->deferredRemoteActions.end()) {
            auto pendingBundle = ActionBundleCodec::decode(
                pending->bytes, document, scope.epoch);
            if (!pendingBundle.ok()) {
                routed.state = RemoteActionRouteState::uncertain;
                routed.diagnostic = pendingBundle.error();
                return routed;
            }
            auto retried = acceptRemoteAction(pending->bytes);
            if (retried.state == ActionPublicationState::committed
                && retried.committedRevision) {
                routed.committed.push_back({pending->actionId,
                                            *retried.committedRevision});
                d->deferredRemoteActionBytes -=
                    static_cast<size_t>(pending->bytes.size());
                pending = d->deferredRemoteActions.erase(pending);
                madeProgress = true;
                continue;
            }
            const bool stillDeferred = retried.state == ActionPublicationState::rejected
                && retried.diagnostic
                && (retried.diagnostic->code() == CollabText::ErrorCode::history_unavailable
                    || retried.diagnostic->code() == CollabText::ErrorCode::unknown_stream);
            if (retried.state == ActionPublicationState::rejected && retried.diagnostic
                && !stillDeferred) {
                const auto persistQuarantine = [this, &scope](
                    const ActionBundleCodec::Bundle& rejected,
                    const QByteArray& originalEnvelope,
                    const CollabText::Error& reason) -> CollabText::Result<void> {
                    for (size_t index = 0; index < rejected.operations.size(); ++index) {
                        const auto& operation = rejected.operations[index];
                        QuarantineRecord record;
                        record.document = scope.documentKey.toUtf8().toStdString();
                        record.epoch = scope.epoch;
                        record.stream = operation.stream;
                        record.primitive = operation.primitive == OperationPrimitive::idList
                            ? QuarantinePrimitive::list
                            : operation.primitive == OperationPrimitive::markoffMap
                                ? QuarantinePrimitive::map : QuarantinePrimitive::text;
                        record.actor = operation.producer;
                        record.counter = operation.counter;
                        if (record.primitive == QuarantinePrimitive::map)
                            record.targetBlock = operation.targetBlock;
                        record.payload = operation.payload;
                        record.deliverySequence = rejected.deliverySequence;
                        record.actionId = rejected.actionId;
                        record.originalActionEnvelope = originalEnvelope;
                        record.bundleIndex = uint32_t(index);
                        record.bundleOperationCount = uint32_t(rejected.operations.size());
                        record.reason = reason;
                        auto encoded = QuarantineRecordCodec::encode(record);
                        if (!encoded.ok()) return encoded.error();
                        auto stored = d->lifecycle->storage().storeQuarantinedOperation(
                            encoded.value());
                        if (!stored.ok()) return stored.error();
                    }
                    return {};
                };
                auto stored = persistQuarantine(pendingBundle.value(), pending->bytes,
                                                *retried.diagnostic);
                if (!stored.ok()) {
                    routed.state = RemoteActionRouteState::uncertain;
                    routed.diagnostic = stored.error();
                    return routed;
                }
                routed.quarantined.push_back(pending->actionId);
                d->deferredRemoteActionBytes -=
                    static_cast<size_t>(pending->bytes.size());
                pending = d->deferredRemoteActions.erase(pending);
                madeProgress = true;
                continue;
            }
            if (retried.state == ActionPublicationState::uncertain) {
                routed.state = RemoteActionRouteState::uncertain;
                routed.diagnostic = retried.diagnostic;
                return routed;
            }
            ++pending;
        }
    } while (madeProgress);

    routed.state = d->deferredRemoteActions.empty()
        ? RemoteActionRouteState::committed : RemoteActionRouteState::deferred;
    return routed;
}

CollabText::Result<std::vector<VNext::PendingOutboundAction>>
MarkoffDocument::pendingOutboundActions() const
{
    if (std::this_thread::get_id() != d->ownerThread)
        return CollabText::Error(CollabText::ErrorCode::unsupported_operation,
            "outbound action recovery must run on the document owner thread");
    auto recovery = d->lifecycle->storage().readRecoveryState();
    if (!recovery.ok()) return recovery.error();
    return recovery.value().pendingOutboundActions;
}

CollabText::Result<void> MarkoffDocument::acknowledgeOutboundActionReceipt(
    const QByteArray& actionId, const QByteArray& acceptedRevision)
{
    if (std::this_thread::get_id() != d->ownerThread)
        return CollabText::Error(CollabText::ErrorCode::unsupported_operation,
            "outbound action receipt must be recorded on the document owner thread");
    return d->lifecycle->storage().acknowledgeTransportReceipt(
        actionId, acceptedRevision);
}

CollabText::Result<std::vector<RemoteActionQuarantine>>
MarkoffDocument::quarantinedRemoteActions() const
{
    using namespace VNext;
    if (std::this_thread::get_id() != d->ownerThread)
        return CollabText::Error(CollabText::ErrorCode::unsupported_operation,
            "remote quarantine recovery must run on the document owner thread");
    auto recovery = d->lifecycle->storage().readRecoveryState();
    if (!recovery.ok()) return recovery.error();

    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    std::map<QByteArray, RemoteActionQuarantine> grouped;
    for (const auto& encoded : recovery.value().quarantinedOperations) {
        auto decoded = QuarantineRecordCodec::decode(encoded);
        if (!decoded.ok()) return decoded.error();
        const auto& record = decoded.value();
        if (record.originalActionEnvelope.isEmpty()) continue;
        if (record.document != document || record.epoch != scope.epoch)
            return CollabText::Error(CollabText::ErrorCode::wrong_stream,
                "stored action quarantine belongs to a different document scope");
        auto bundle = ActionBundleCodec::decode(record.originalActionEnvelope,
                                                document, scope.epoch);
        if (!bundle.ok() || bundle.value().actionId != record.actionId)
            return bundle.ok()
                ? CollabText::Error(CollabText::ErrorCode::corrupt_record,
                    "quarantine action ID does not match its original MAB")
                : bundle.error();
        auto prior = grouped.find(record.actionId);
        if (prior != grouped.end()
            && prior->second.originalMab != record.originalActionEnvelope) {
            return CollabText::Error(CollabText::ErrorCode::corrupt_record,
                "quarantine records disagree about one rejected action's MAB");
        }
        grouped[record.actionId] = {record.actionId,
            record.originalActionEnvelope, record.reason};
    }
    std::vector<RemoteActionQuarantine> result;
    for (auto& [actionId, record] : grouped) {
        const auto accepted = std::find_if(recovery.value().acceptedRecords.begin(),
            recovery.value().acceptedRecords.end(), [&](const AcceptedStorageRecord& item) {
                return item.transactionId == actionId
                    && item.format == QByteArrayLiteral("MAB/3")
                    && item.bytes == record.originalMab;
            });
        if (accepted != recovery.value().acceptedRecords.end()) continue;
        if (record.reason.code() != CollabText::ErrorCode::history_unavailable
            && record.reason.code() != CollabText::ErrorCode::unknown_stream)
            result.push_back(std::move(record));
    }
    return result;
}

size_t MarkoffDocument::pendingRemoteActionCount() const noexcept
{
    return d->deferredRemoteActions.size();
}

CollabText::Result<void> MarkoffDocument::setRemoteActionQueueBudget(
    RemoteActionQueueBudget budget)
{
    if (std::this_thread::get_id() != d->ownerThread)
        return CollabText::Error(CollabText::ErrorCode::unsupported_operation,
            "remote action queue limits must change on the document owner thread");
    if (budget.maxActions < d->deferredRemoteActions.size()
        || budget.maxBytes < d->deferredRemoteActionBytes)
        return CollabText::Error(CollabText::ErrorCode::deferred_queue_full,
            "new remote action limits are smaller than the retained queue");
    d->remoteActionQueueBudget = budget;
    return {};
}

VNext::ActionPublicationResult MarkoffDocument::replaceMarkdown(
    const QByteArray& source)
{
    return applyMarkdownSource(source, true);
}

VNext::ActionPublicationResult MarkoffDocument::applyMarkdownSource(
    const QByteArray& source, bool replacement)
{
    using namespace VNext;
    const auto& storageScope = d->lifecycle->storage().scope();
    const std::string document = storageScope.documentKey.toUtf8().toStdString();
    const bool importReady = d->lifecycle->state()
            == ProviderDocumentState::awaitingInitialImport
        || d->lifecycle->state() == ProviderDocumentState::initialImportAuthorized;
    if ((!replacement && !importReady)
        || (replacement && !d->lifecycle->canStartOrdinaryAction()))
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::awaiting_state,
                replacement ? "Markdown replacement requires a ready document"
                            : "initial Markdown import is only valid for an uninitialized document")};

    const QString sourceText = QString::fromUtf8(source);
    if (sourceText.toUtf8() != source)
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::malformed,
                "initial Markdown source must be valid UTF-8")};

    const QByteArray priorAcceptedMarkdown = replacement ? serializeForSave() : QByteArray{};

    SourceSnapshotRecord sourceRecord;
    sourceRecord.document = document;
    sourceRecord.epoch = storageScope.epoch;
    sourceRecord.originalMarkdown = source;
    auto encodedSource = SourceSnapshotCodec::encode(sourceRecord);
    if (!encodedSource.ok())
        return {ActionPublicationState::rejected, std::nullopt, encodedSource.error()};

    const auto extracted = Markoff::Document::extract(sourceText);
    const auto parsed = Markoff::Document::fromMarkdown(extracted.body);
    if (!parsed)
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::malformed,
                "Markdown parser could not create an initial document snapshot")};

    QByteArray actionId;
    if (!replacement
        && d->lifecycle->state() == ProviderDocumentState::awaitingInitialImport) {
        actionId = QUuid::createUuid().toRfc4122();
        auto authorized = d->lifecycle->authorizeInitialImport({actionId, source});
        if (!authorized.ok())
            return {ActionPublicationState::rejected, std::nullopt, authorized.error()};
    }
    auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, recoveryResult.error()};
    const auto recovery = std::move(recoveryResult).value();
    if (!replacement && actionId.isEmpty()) {
        if (!recovery.initialImport
            || recovery.initialImport->sourceMarkdown != source)
            return {ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::pin_conflict,
                    "retry source differs from the authorized initial import")};
        actionId = recovery.initialImport->transactionId;
    }
    if (replacement) actionId = QUuid::createUuid().toRfc4122();
    uint64_t sequence = 1;
    if (replacement) {
        auto next = nextDeliverySequence(recovery, document, storageScope.epoch);
        if (!next.ok())
            return {ActionPublicationState::rejected, std::nullopt, next.error()};
        sequence = next.value();
    }

    auto listResult = d->lifecycle->stagingIdList();
    if (!listResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, listResult.error()};
    auto* list = listResult.value();
    const auto getMap = [&](const std::string& name) {
        return d->lifecycle->stagingMapStream(MapScope{document, name, storageScope.epoch});
    };
    auto kindResult = getMap("kind-tag");
    auto attrsResult = getMap("block-attrs");
    auto frontmatterResult = getMap("frontmatter");
    auto footnotesResult = getMap("footnote-def");
    auto linkRefsResult = getMap("link-ref");
    if (!kindResult.ok()) return {ActionPublicationState::rejected, std::nullopt, kindResult.error()};
    if (!attrsResult.ok()) return {ActionPublicationState::rejected, std::nullopt, attrsResult.error()};
    if (!frontmatterResult.ok()) return {ActionPublicationState::rejected, std::nullopt, frontmatterResult.error()};
    if (!footnotesResult.ok()) return {ActionPublicationState::rejected, std::nullopt, footnotesResult.error()};
    if (!linkRefsResult.ok()) return {ActionPublicationState::rejected, std::nullopt, linkRefsResult.error()};

    const ActionKind actionKind = replacement
        ? ActionKind::replacement : ActionKind::initialImport;
    ActionStager stager(document, storageScope.epoch, sequence, actionId,
                        actionKind);
    struct StagedImportRollback {
        ProviderBackedDocumentState& lifecycle;
        const ActionStager& stager;
        bool armed = true;
        ~StagedImportRollback()
        {
            if (armed && !stager.bundle().operations.empty())
                (void)lifecycle.publishStagedAction(stager, OwnerHistorySnapshot{});
        }
    } rollback{*d->lifecycle, stager};
    stager.setSourceSnapshot(encodedSource.value());
    std::vector<HistoryGroupAssociation> mapGroups;
    const auto stageMap = [&](CollabText::Result<LocalMapOperation> local)
        -> CollabText::Result<void> {
        if (!local.ok()) return local.error();
        auto added = stager.add(local.value());
        if (!added.ok()) return added.error();
        auto association = toHistoryAssociation(local.value());
        if (!association.ok()) return association.error();
        mapGroups.push_back(std::move(association).value());
        return {};
    };
    std::vector<BlockId> replacedBlockIds;
    if (replacement) {
        auto entries = list->visibleEntries();
        if (!entries.ok())
            return {ActionPublicationState::rejected, std::nullopt, entries.error()};
        for (const auto& entry : entries.value()) {
            if (const auto id = fromVNextIdentity(entry.identity))
                replacedBlockIds.push_back(*id);
            auto removed = list->remove(entry.identity);
            if (!removed.ok())
                return {ActionPublicationState::rejected, std::nullopt, removed.error()};
            auto added = stager.add(removed.value());
            if (!added.ok())
                return {ActionPublicationState::rejected, std::nullopt, added.error()};
        }
        const auto clearMap = [&](ReplicatedMapStream* stream)
            -> CollabText::Result<void> {
            const auto values = stream->values();
            for (const auto& value : values) {
                auto removed = stageMap(stream->remove(value.key, value.targetBlock));
                if (!removed.ok()) return removed.error();
            }
            return {};
        };
        for (auto* stream : {kindResult.value(), attrsResult.value(),
                             frontmatterResult.value(), footnotesResult.value(),
                             linkRefsResult.value()}) {
            auto cleared = clearMap(stream);
            if (!cleared.ok())
                return {ActionPublicationState::rejected, std::nullopt, cleared.error()};
        }
    }
    QByteArray rawFrontmatter = extracted.frontmatter.toUtf8();
    if (rawFrontmatter.endsWith('\r')) rawFrontmatter.chop(1);
    if (!rawFrontmatter.isEmpty()) {
        auto result = stageMap(frontmatterResult.value()->set(
            QByteArrayLiteral("raw"), rawFrontmatter));
        if (!result.ok()) return {ActionPublicationState::rejected, std::nullopt, result.error()};
    }
    for (const auto& footnote : extracted.footnotes) {
        auto result = stageMap(footnotesResult.value()->set(
            footnote.label.toUtf8(), footnote.content.toUtf8()));
        if (!result.ok()) return {ActionPublicationState::rejected, std::nullopt, result.error()};
    }

    const QByteArray body = extracted.body.toUtf8();
    const auto blocks = parsed->topLevelBlocks();
    std::optional<BlockIdentity> after;
    std::vector<BlockId> importedBlockIds;
    QSet<QByteArray> importedLinkReferenceLabels;
    size_t blockCount = 0;
    for (const auto& block : blocks) {
        if (block.kind == Markoff::TopLevelBlock::Kind::LinkReferenceDefinition) {
            const auto parsedReference = parseLinkReferenceDefinition(block);
            if (parsedReference) {
                if (importedLinkReferenceLabels.contains(parsedReference->first))
                    continue;
                importedLinkReferenceLabels.insert(parsedReference->first);
                auto value = MarkoffMapValueCodec::encodeLinkRef(parsedReference->second);
                if (!value.ok()) return {ActionPublicationState::rejected, std::nullopt, value.error()};
                auto result = stageMap(linkRefsResult.value()->set(parsedReference->first, value.value()));
                if (!result.ok()) return {ActionPublicationState::rejected, std::nullopt, result.error()};
                continue;
            }
            if (block.linkReferenceLabel.toUcs4().size() <= 999)
                return {ActionPublicationState::rejected, std::nullopt,
                    CollabText::Error(CollabText::ErrorCode::malformed,
                        "Markdown link reference definition is malformed")};
            // CommonMark treats a definition with an overlong label as
            // ordinary paragraph content. Let the regular block path below
            // retain and project its exact source text.
        }

        const auto inserted = list->insertAfter(after, 0);
        if (!inserted.ok()) return {ActionPublicationState::rejected, std::nullopt, inserted.error()};
        auto listOperation = std::move(inserted).value();
        if (!listOperation.insertedBlock)
            return {ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::invalid_identity,
                    "initial list insertion returned no block identity")};
        after = listOperation.insertedBlock;
        if (const auto identity = fromVNextIdentity(*after))
            importedBlockIds.push_back(*identity);
        auto addedList = stager.add(listOperation);
        if (!addedList.ok()) return {ActionPublicationState::rejected, std::nullopt, addedList.error()};

        BlockKind kind = BlockKind::Paragraph;
        using Kind = Markoff::TopLevelBlock::Kind;
        switch (block.kind) {
        case Kind::AtxHeading: case Kind::SetextHeading: kind = BlockKind::Heading; break;
        case Kind::FencedCodeBlock: case Kind::IndentedCodeBlock: kind = BlockKind::CodeBlock; break;
        case Kind::ListItem: kind = BlockKind::ListItem; break;
        case Kind::ThematicBreak: kind = BlockKind::HorizontalRule; break;
        case Kind::HtmlBlock: kind = BlockKind::HtmlBlock; break;
        case Kind::Table: kind = BlockKind::Table; break;
        default: break;
        }
        if (block.blockQuoteDepth > 0 && block.kind == Kind::Paragraph)
            kind = BlockKind::BlockQuote;
        auto encodedKind = MarkoffMapValueCodec::encodeBlockKind(kind);
        if (!encodedKind.ok()) return {ActionPublicationState::rejected, std::nullopt, encodedKind.error()};
        auto kindWrite = stageMap(kindResult.value()->set({}, encodedKind.value(), after));
        if (!kindWrite.ok()) return {ActionPublicationState::rejected, std::nullopt, kindWrite.error()};

        const auto writeAttr = [&](const QByteArray& name, const AttrValue& attr)
            -> CollabText::Result<void> {
            auto encoded = MarkoffMapValueCodec::encodeAttribute(attr);
            if (!encoded.ok()) return encoded.error();
            return stageMap(attrsResult.value()->set(name, encoded.value(), after));
        };
        if (kind == BlockKind::Heading && block.headingLevel > 0) {
            auto result = writeAttr(AttrNames::Level, block.headingLevel);
            if (!result.ok()) return {ActionPublicationState::rejected, std::nullopt, result.error()};
        }
        if (kind == BlockKind::Heading && block.kind == Kind::SetextHeading) {
            auto result = writeAttr(AttrNames::HeadingForm, QStringLiteral("setext"));
            if (!result.ok()) return {ActionPublicationState::rejected, std::nullopt, result.error()};
        }
        if (kind == BlockKind::CodeBlock && !block.codeLanguage.isEmpty()) {
            auto result = writeAttr(AttrNames::InfoString, block.codeLanguage);
            if (!result.ok()) return {ActionPublicationState::rejected, std::nullopt, result.error()};
        }
        if (kind == BlockKind::ListItem) {
            for (const auto& result : {
                writeAttr(AttrNames::IndentLevel, block.indentDepth),
                writeAttr(AttrNames::MarkerStyle, block.markerStyle),
                writeAttr(AttrNames::LooseRun, block.looseRun)}) {
                if (!result.ok()) return {ActionPublicationState::rejected, std::nullopt, result.error()};
            }
            if (block.markerStyle == QStringLiteral("dot")
                || block.markerStyle == QStringLiteral("paren")) {
                auto result = writeAttr(AttrNames::MarkerNumber, block.markerNumber);
                if (!result.ok()) return {ActionPublicationState::rejected, std::nullopt, result.error()};
            }
            if (block.markerStyle == QStringLiteral("task")) {
                auto result = writeAttr(AttrNames::Checked, block.checked);
                if (!result.ok()) return {ActionPublicationState::rejected, std::nullopt, result.error()};
            }
        }
        if (block.blockQuoteDepth > 0) {
            auto depth = writeAttr(AttrNames::BlockQuoteDepth, block.blockQuoteDepth);
            auto run = writeAttr(AttrNames::BlockQuoteRunId, block.blockQuoteRunId);
            if (!depth.ok()) return {ActionPublicationState::rejected, std::nullopt, depth.error()};
            if (!run.ok()) return {ActionPublicationState::rejected, std::nullopt, run.error()};
        }

        const QByteArray content = initialTextForSourceBlock(body, block);

        auto text = d->lifecycle->stagingTextStream(*after);
        if (!text.ok()) return {ActionPublicationState::rejected, std::nullopt, text.error()};
        if (replacement) {
            auto changed = text.value()->replaceUtf8(0, 0, content);
            if (!changed.ok())
                return {ActionPublicationState::rejected, std::nullopt, changed.error()};
            if (!changed.value().bytes.isEmpty()) {
                auto addedText = stager.add(changed.value());
                if (!addedText.ok())
                    return {ActionPublicationState::rejected, std::nullopt, addedText.error()};
            }
        } else {
            auto seeded = text.value()->seedInitialImport(content);
            if (!seeded.ok())
                return {ActionPublicationState::rejected, std::nullopt, seeded.error()};
            if (seeded.value()) {
                auto addedText = stager.add(*seeded.value());
                if (!addedText.ok())
                    return {ActionPublicationState::rejected, std::nullopt, addedText.error()};
            }
        }
        ++blockCount;
    }
    if (blockCount == 0) {
        auto inserted = list->insertAfter(std::nullopt, 0);
        if (!inserted.ok()) return {ActionPublicationState::rejected, std::nullopt, inserted.error()};
        auto listOperation = std::move(inserted).value();
        after = listOperation.insertedBlock;
        auto added = stager.add(listOperation);
        if (!added.ok()) return {ActionPublicationState::rejected, std::nullopt, added.error()};
        auto paragraph = MarkoffMapValueCodec::encodeBlockKind(BlockKind::Paragraph);
        if (!paragraph.ok()) return {ActionPublicationState::rejected, std::nullopt, paragraph.error()};
        auto kindWrite = stageMap(kindResult.value()->set({}, paragraph.value(), after));
        if (!kindWrite.ok()) return {ActionPublicationState::rejected, std::nullopt, kindWrite.error()};
        auto text = d->lifecycle->stagingTextStream(*after);
        if (!text.ok()) return {ActionPublicationState::rejected, std::nullopt, text.error()};
        if (replacement) {
            auto seeded = text.value()->replaceUtf8(0, 0, {});
            if (!seeded.ok())
                return {ActionPublicationState::rejected, std::nullopt, seeded.error()};
        } else {
            auto seeded = text.value()->seedInitialImport({});
            if (!seeded.ok())
                return {ActionPublicationState::rejected, std::nullopt, seeded.error()};
        }
    }

    ActionHistoryEvent event;
    event.actionId = actionId;
    event.kind = actionKind;
    if (replacement) {
        SourceSnapshotRecord priorSource;
        priorSource.document = document;
        priorSource.epoch = storageScope.epoch;
        priorSource.originalMarkdown = priorAcceptedMarkdown;
        auto encodedPriorSource = SourceSnapshotCodec::encode(priorSource);
        if (!encodedPriorSource.ok())
            return {ActionPublicationState::rejected, std::nullopt,
                    encodedPriorSource.error()};
        event.sourceSnapshotBefore = std::move(encodedPriorSource).value();
    }
    for (const auto& operation : stager.bundle().operations) {
        if (operation.primitive == OperationPrimitive::markoffMap)
            continue;
        const CollabText::Crdt::Identity identity{operation.producer, operation.counter};
        event.groups.push_back({operation.stream, operation.primitive, identity, {identity}});
    }
    event.groups.insert(event.groups.end(), mapGroups.begin(), mapGroups.end());

    OwnerHistorySnapshot ownerHistory;
    if (replacement) {
        auto previous = ownerHistoryForAction(recovery, d->lifecycle->storage(),
            document, storageScope.epoch, actionId, event);
        if (!previous.ok())
            return {ActionPublicationState::rejected, std::nullopt, previous.error()};
        ownerHistory = std::move(previous).value();
    } else {
        ownerHistory.document = document;
        ownerHistory.epoch = storageScope.epoch;
        ownerHistory.owner = d->lifecycle->storage().actor();
        ownerHistory.transactionId = actionId;
        ownerHistory.acceptedPrior = recovery.acceptedRevision.token;
        ownerHistory.actionIndex.document = document;
        ownerHistory.actionIndex.epoch = storageScope.epoch;
        ownerHistory.actionIndex.owner = d->lifecycle->storage().actor();
        ownerHistory.actionIndex.acceptedPrior = recovery.acceptedRevision.token;
        ownerHistory.actionIndex.events.push_back(event);
    }
    const auto saveStreamHistory = [&](const LocalHistoryRecord& record) {
        const auto found = std::find_if(ownerHistory.streams.begin(),
            ownerHistory.streams.end(), [&](const LocalHistoryRecord& existing) {
                return existing.scope == record.scope && existing.actor == record.actor;
            });
        if (found == ownerHistory.streams.end()) ownerHistory.streams.push_back(record);
        else found->snapshot = record.snapshot;
    };
    if (!replacement) {
        // The initial import is not an undoable action. Leaving its order
        // groups on the list engine would sit in front of later edits and
        // block retirement of those edits.
        auto cleared = list->restoreLocalHistory({});
        if (!cleared.ok())
            return {ActionPublicationState::rejected, std::nullopt, cleared.error()};
    }
    saveStreamHistory({list->scope(), d->lifecycle->storage().actor(),
                       list->localHistorySnapshot()});
    // Record text history only for streams represented by an operation in
    // this accepted action. Empty imported blocks have no text operation, so
    // their lazily-created stream is not yet part of the replayed prefix and
    // cannot be named by owner-local history until a later edit publishes it.
    for (const auto block : importedBlockIds) {
        const auto identity = toVNextIdentity(block);
        if (!identity) continue;
        const auto route = blockTextStreamName(*identity);
        if (!route.ok())
            return {ActionPublicationState::rejected, std::nullopt, route.error()};
        const bool touched = std::any_of(stager.bundle().operations.cbegin(),
            stager.bundle().operations.cend(), [&](const ActionOperation& operation) {
                return operation.primitive == OperationPrimitive::text
                    && operation.stream == route.value();
            });
        if (!touched) continue;
        auto text = d->lifecycle->stagingTextStream(*identity);
        if (!text.ok()) return {ActionPublicationState::rejected, std::nullopt, text.error()};
        saveStreamHistory({text.value()->scope(), d->lifecycle->storage().actor(),
                           text.value()->localHistorySnapshot()});
    }
    rollback.armed = false;
    const auto publication = d->lifecycle->publishStagedAction(stager, ownerHistory);
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->originalMarkdown = source;
        d->hasOriginalMarkdown = true;
        d->sourcePristine = true;
        rebuildSourceBlocks(*d, importedBlockIds);
        emit documentLoaded();
        emit d2DocumentChanged();
        emit documentChanged();
        QList<BlockId> changedBlocks;
        for (const auto& block : iterateBlocks())
            changedBlocks.push_back(block);
        emit blocksChanged(changedBlocks);
    }
    return publication;
}

VNext::ActionPublicationResult MarkoffDocument::applyBlockEdit(const BlockEdit& edit)
{
    using namespace VNext;
    if (!d->lifecycle->canStartOrdinaryAction()) {
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::awaiting_state,
                              "document lifecycle does not permit an ordinary action")};
    }
    const auto visibleBlocks = iterateBlocks();
    if (std::find(visibleBlocks.begin(), visibleBlocks.end(), edit.blockId)
        == visibleBlocks.end()) {
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::not_found,
                              "text edit targets a block that is not visible")};
    }
    const auto identity = toVNextIdentity(edit.blockId);
    if (!identity) {
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::invalid_identity,
                              "text edit has a null block identity")};
    }

    const auto scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    const uint64_t epoch = scope.epoch;
    auto route = blockTextStreamName(*identity);
    if (!route.ok())
        return {ActionPublicationState::rejected, std::nullopt, route.error()};

    auto streamResult = d->lifecycle->stagingTextStream(*identity);
    if (!streamResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, streamResult.error()};
    ScopedTextStream* stream = streamResult.value();
    const auto local = stream->replaceUtf8(
        edit.withinBlockByteOffset,
        qsizetype(edit.withinBlockByteOffset) + edit.removedBytes,
        edit.insertedUtf8);
    if (!local.ok())
        return {ActionPublicationState::rejected, std::nullopt, local.error()};

    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    const auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok()) {
        ActionStager failed(document, epoch, 1, actionId, ActionKind::edit);
        const auto rolledBack = d->lifecycle->publishStagedAction(
            failed, OwnerHistorySnapshot{});
        emit tentativeBlockReverted(edit.blockId);
        return rolledBack;
    }
    const auto& recovery = recoveryResult.value();
    uint64_t deliverySequence = 1;
    for (const auto& accepted : recovery.acceptedRecords) {
        if (accepted.format != QByteArrayLiteral("MAB/3"))
            continue;
        const auto acceptedBundle = ActionBundleCodec::decode(
            accepted.bytes, document, epoch);
        if (!acceptedBundle.ok()) {
            const auto rolledBack = d->lifecycle->publishStagedAction(
                ActionStager(document, epoch, 1, actionId, ActionKind::edit),
                OwnerHistorySnapshot{});
            emit tentativeBlockReverted(edit.blockId);
            return rolledBack;
        }
        if (acceptedBundle.value().deliverySequence >= deliverySequence) {
            if (acceptedBundle.value().deliverySequence
                == std::numeric_limits<uint64_t>::max()) {
                const auto rolledBack = d->lifecycle->publishStagedAction(
                    ActionStager(document, epoch, 1, actionId, ActionKind::edit),
                    OwnerHistorySnapshot{});
                emit tentativeBlockReverted(edit.blockId);
                return rolledBack;
            }
            deliverySequence = acceptedBundle.value().deliverySequence + 1;
        }
    }
    ActionStager stager(document, epoch, deliverySequence, actionId, ActionKind::edit);
    auto staged = stager.add(local.value());
    if (!staged.ok()) {
        const auto rolledBack = d->lifecycle->publishStagedAction(
            stager, OwnerHistorySnapshot{});
        emit tentativeBlockReverted(edit.blockId);
        return rolledBack;
    }

    OwnerHistorySnapshot ownerHistory;
    if (recovery.localHistory) {
        const auto committedHistoryAction = std::find_if(
            recovery.acceptedRecords.cbegin(), recovery.acceptedRecords.cend(),
            [&](const AcceptedStorageRecord& record) {
                return record.transactionId
                    == recovery.localHistory->committedTransactionId
                    && record.committedRevision
                    == recovery.localHistory->acceptedRevision;
            });
        if (committedHistoryAction == recovery.acceptedRecords.cend()) {
            const auto rolledBack = d->lifecycle->publishStagedAction(
                stager, OwnerHistorySnapshot{});
            emit tentativeBlockReverted(edit.blockId);
            return rolledBack;
        }
        auto decoded = OwnerHistoryCodec::decode(recovery.localHistory->bytes,
            document, epoch, d->lifecycle->storage().actor(),
            recovery.localHistory->committedTransactionId,
            committedHistoryAction->priorRevision.token);
        if (!decoded.ok()) {
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
            emit tentativeBlockReverted(edit.blockId);
            return {ActionPublicationState::rejected, std::nullopt, decoded.error()};
        }
        ownerHistory = std::move(decoded).value();
    } else {
        ownerHistory.document = document;
        ownerHistory.epoch = epoch;
        ownerHistory.owner = d->lifecycle->storage().actor();
        ownerHistory.actionIndex.document = document;
        ownerHistory.actionIndex.epoch = epoch;
        ownerHistory.actionIndex.owner = d->lifecycle->storage().actor();
    }

    ownerHistory.transactionId = actionId;
    ownerHistory.acceptedPrior = recovery.acceptedRevision.token;
    ownerHistory.actionIndex.acceptedPrior = recovery.acceptedRevision.token;
    const auto textHistory = stream->localHistorySnapshot();
    auto historyRecord = std::find_if(ownerHistory.streams.begin(),
        ownerHistory.streams.end(), [&](const LocalHistoryRecord& record) {
            return record.scope == local.value().scope
                && record.actor == d->lifecycle->storage().actor();
        });
    if (historyRecord == ownerHistory.streams.end()) {
        ownerHistory.streams.push_back(
            {local.value().scope, d->lifecycle->storage().actor(), textHistory});
    } else {
        historyRecord->snapshot = textHistory;
    }

    ActionHistoryEvent event;
    event.actionId = actionId;
    event.kind = ActionKind::edit;
    const CollabText::Crdt::Identity operationIdentity{
        local.value().identity.actor, local.value().identity.counter};
    event.groups.push_back({local.value().scope.stream, OperationPrimitive::text,
        operationIdentity, {operationIdentity}});
    ownerHistory.actionIndex.events.push_back(std::move(event));

    emit tentativeBlockChanged(edit.blockId);
    const auto publication = d->lifecycle->publishStagedAction(stager, ownerHistory);
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        ++d->blockEditSequences[edit.blockId];
        emit tentativeBlockCommitted(edit.blockId);
        emit blocksChanged({edit.blockId});
        emit d2DocumentChanged();
        emit documentChanged();
    } else if (publication.state == ActionPublicationState::absent
               || publication.state == ActionPublicationState::rejected) {
        emit tentativeBlockReverted(edit.blockId);
    }
    return publication;
}

VNext::ActionPublicationResult MarkoffDocument::applyBlockEdits(
    const QList<BlockEdit>& requested)
{
    using namespace VNext;
    if (requested.isEmpty()) {
        auto recovery = d->lifecycle->storage().readRecoveryState();
        if (!recovery.ok())
            return {ActionPublicationState::rejected, std::nullopt, recovery.error()};
        return {ActionPublicationState::committed,
            recovery.value().acceptedRevision, std::nullopt};
    }
    if (!d->lifecycle->canStartOrdinaryAction())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::awaiting_state,
                "document lifecycle does not permit an ordinary action")};

    struct PendingEdit { BlockEdit edit; size_t order; };
    QHash<BlockId, size_t> orderOf;
    size_t order = 0;
    for (const auto block : iterateBlocks()) orderOf.insert(block, order++);
    std::vector<PendingEdit> edits;
    edits.reserve(static_cast<size_t>(requested.size()));
    for (const auto& edit : requested) {
        const auto found = orderOf.constFind(edit.blockId);
        if (found == orderOf.cend())
            return {ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::not_found,
                    "block edit targets a block that is not visible")};
        const QByteArray current = blockText(edit.blockId);
        const uint64_t end = uint64_t(edit.withinBlockByteOffset) + edit.removedBytes;
        if (end > uint64_t(current.size()))
            return {ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::off_end,
                    "block edit range lies outside its current text")};
        edits.push_back({edit, *found});
    }
    std::sort(edits.begin(), edits.end(), [](const PendingEdit& left,
                                             const PendingEdit& right) {
        if (left.order != right.order) return left.order < right.order;
        return left.edit.withinBlockByteOffset > right.edit.withinBlockByteOffset;
    });
    for (size_t i = 1; i < edits.size(); ++i) {
        const auto& prior = edits[i - 1].edit;
        const auto& next = edits[i].edit;
        if (prior.blockId == next.blockId
            && (next.withinBlockByteOffset == prior.withinBlockByteOffset
                || uint64_t(next.withinBlockByteOffset) + next.removedBytes
                    > prior.withinBlockByteOffset))
            return {ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::malformed,
                    "block edits overlap or target the same insertion boundary")};
    }

    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, recoveryResult.error()};
    auto recovery = std::move(recoveryResult).value();
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok())
        return {ActionPublicationState::rejected, std::nullopt, sequence.error()};
    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId, ActionKind::edit);
    ActionHistoryEvent event;
    event.actionId = actionId;
    event.kind = ActionKind::edit;
    std::vector<LocalHistoryRecord> histories;
    std::vector<BlockId> touched;
    const auto abort = [&](const CollabText::Error& error) {
        if (!stager.bundle().operations.empty())
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
        for (const auto block : touched) emit tentativeBlockReverted(block);
        return ActionPublicationResult{ActionPublicationState::rejected,
            std::nullopt, error};
    };
    for (const auto& pending : edits) {
        const auto& edit = pending.edit;
        const auto identity = toVNextIdentity(edit.blockId);
        if (!identity)
            return abort(CollabText::Error(CollabText::ErrorCode::invalid_identity,
                "block edit has no structural identity"));
        auto streamResult = d->lifecycle->stagingTextStream(*identity);
        if (!streamResult.ok()) return abort(streamResult.error());
        auto operation = streamResult.value()->replaceUtf8(
            edit.withinBlockByteOffset,
            qsizetype(edit.withinBlockByteOffset) + edit.removedBytes,
            edit.insertedUtf8);
        if (!operation.ok()) return abort(operation.error());
        if (operation.value().bytes.isEmpty()) continue;
        auto staged = stager.add(operation.value());
        if (!staged.ok()) return abort(staged.error());
        const CollabText::Crdt::Identity opId{
            operation.value().identity.actor, operation.value().identity.counter};
        event.groups.push_back({operation.value().scope.stream,
            OperationPrimitive::text, opId, {opId}});
        if (touched.empty() || touched.back() != edit.blockId)
            touched.push_back(edit.blockId);
        const LocalHistoryRecord snapshot{streamResult.value()->scope(),
            d->lifecycle->storage().actor(),
            streamResult.value()->localHistorySnapshot()};
        const auto history = std::find_if(histories.begin(), histories.end(),
            [&](const LocalHistoryRecord& item) {
                return item.scope == snapshot.scope && item.actor == snapshot.actor;
            });
        if (history == histories.end()) histories.push_back(snapshot);
        else history->snapshot = snapshot.snapshot;
    }
    if (stager.bundle().operations.empty()) {
        return {ActionPublicationState::committed,
            recovery.acceptedRevision, std::nullopt};
    }
    auto owner = ownerHistoryForAction(recovery, d->lifecycle->storage(), document,
        scope.epoch, actionId, std::move(event), histories.empty()
            ? std::nullopt : std::optional<LocalHistoryRecord>(histories.front()));
    if (!owner.ok()) return abort(owner.error());
    for (size_t i = 1; i < histories.size(); ++i) {
        const auto& item = histories[i];
        const auto found = std::find_if(owner.value().streams.begin(),
            owner.value().streams.end(), [&](const LocalHistoryRecord& existing) {
                return existing.scope == item.scope && existing.actor == item.actor;
            });
        if (found == owner.value().streams.end()) owner.value().streams.push_back(item);
        else found->snapshot = item.snapshot;
    }
    for (const auto block : touched) emit tentativeBlockChanged(block);
    const auto publication = d->lifecycle->publishStagedAction(stager, owner.value());
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        QList<BlockId> changed;
        for (const auto block : touched) {
            ++d->blockEditSequences[block];
            emit tentativeBlockCommitted(block);
            changed.push_back(block);
        }
        emit blocksChanged(changed);
        emit d2DocumentChanged();
        emit documentChanged();
    } else if (publication.state == ActionPublicationState::absent
               || publication.state == ActionPublicationState::rejected) {
        for (const auto block : touched) emit tentativeBlockReverted(block);
    }
    return publication;
}

VNext::ActionPublicationResult MarkoffDocument::applyBlockMerge(
    const BlockEdit& survivorEdit, const QList<BlockId>& removedBlocks)
{
    using namespace VNext;
    if (removedBlocks.isEmpty()) return applyBlockEdit(survivorEdit);
    if (!d->lifecycle->canStartOrdinaryAction())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::awaiting_state,
                "document lifecycle does not permit an ordinary action")};

    const auto blocks = iterateBlocks();
    const auto survivor = std::find(blocks.begin(), blocks.end(), survivorEdit.blockId);
    if (survivor == blocks.end())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::not_found,
                "merge survivor is not visible")};
    const auto survivorIndex = static_cast<size_t>(std::distance(blocks.begin(), survivor));
    if (survivorIndex + removedBlocks.size() >= blocks.size())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::off_end,
                "merge removes a block outside the visible document")};
    for (size_t i = 0; i < static_cast<size_t>(removedBlocks.size()); ++i) {
        if (blocks[survivorIndex + i + 1] != removedBlocks[static_cast<qsizetype>(i)])
            return {ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::malformed,
                    "merge removals must be contiguous blocks after the survivor")};
    }
    const QByteArray survivorText = blockText(survivorEdit.blockId);
    if (uint64_t(survivorEdit.withinBlockByteOffset) + survivorEdit.removedBytes
        > uint64_t(survivorText.size()))
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::off_end,
                "merge text range lies outside its survivor block")};

    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, recoveryResult.error()};
    auto recovery = std::move(recoveryResult).value();
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok())
        return {ActionPublicationState::rejected, std::nullopt, sequence.error()};

    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId, ActionKind::edit);
    ActionHistoryEvent event;
    event.actionId = actionId;
    event.kind = ActionKind::edit;
    QList<BlockId> touched{survivorEdit.blockId};
    auto textIdentity = toVNextIdentity(survivorEdit.blockId);
    if (!textIdentity)
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::invalid_identity,
                "merge survivor has no structural identity")};
    auto textStream = d->lifecycle->stagingTextStream(*textIdentity);
    if (!textStream.ok())
        return {ActionPublicationState::rejected, std::nullopt, textStream.error()};
    const auto rollback = [&](const CollabText::Error& error) {
        if (!stager.bundle().operations.empty())
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
        for (const auto block : touched) emit tentativeBlockReverted(block);
        return ActionPublicationResult{ActionPublicationState::rejected,
            std::nullopt, error};
    };
    auto textOperation = textStream.value()->replaceUtf8(
        survivorEdit.withinBlockByteOffset,
        qsizetype(survivorEdit.withinBlockByteOffset) + survivorEdit.removedBytes,
        survivorEdit.insertedUtf8);
    if (!textOperation.ok()) return rollback(textOperation.error());
    if (!textOperation.value().bytes.isEmpty()) {
        auto added = stager.add(textOperation.value());
        if (!added.ok()) return rollback(added.error());
        const CollabText::Crdt::Identity identity{
            textOperation.value().identity.actor, textOperation.value().identity.counter};
        event.groups.push_back({textOperation.value().scope.stream,
            OperationPrimitive::text, identity, {identity}});
    }

    auto listResult = d->lifecycle->stagingIdList();
    if (!listResult.ok()) return rollback(listResult.error());
    ScopedIdListStream* list = listResult.value();
    auto kindResult = d->lifecycle->stagingMapStream(
        MapScope{document, "kind-tag", scope.epoch});
    if (!kindResult.ok()) return rollback(kindResult.error());
    ReplicatedMapStream* kindMap = kindResult.value();
    for (const auto block : removedBlocks) {
        const auto identity = toVNextIdentity(block);
        if (!identity) return rollback(CollabText::Error(
            CollabText::ErrorCode::invalid_identity,
            "removed block has no structural identity"));
        auto removed = list->remove(*identity);
        if (!removed.ok()) return rollback(removed.error());
        auto added = stager.add(removed.value());
        if (!added.ok()) return rollback(added.error());
        const auto& operationIdentity = removed.value().identity;
        event.groups.push_back({removed.value().scope.stream,
            OperationPrimitive::idList, operationIdentity, {operationIdentity}});
        auto kindRemoval = kindMap->remove({}, *identity);
        if (!kindRemoval.ok()) return rollback(kindRemoval.error());
        added = stager.add(kindRemoval.value());
        if (!added.ok()) return rollback(added.error());
        auto association = toHistoryAssociation(kindRemoval.value());
        if (!association.ok()) return rollback(association.error());
        event.groups.push_back(std::move(association).value());
        touched.push_back(block);
    }

    const auto textHistory = LocalHistoryRecord{textStream.value()->scope(),
        d->lifecycle->storage().actor(), textStream.value()->localHistorySnapshot()};
    const auto listHistory = LocalHistoryRecord{list->scope(),
        d->lifecycle->storage().actor(), list->localHistorySnapshot()};
    auto owner = ownerHistoryForAction(recovery, d->lifecycle->storage(), document,
        scope.epoch, actionId, std::move(event), textHistory);
    if (!owner.ok()) return rollback(owner.error());
    auto history = std::find_if(owner.value().streams.begin(), owner.value().streams.end(),
        [&](const LocalHistoryRecord& item) {
            return item.scope == listHistory.scope && item.actor == listHistory.actor;
        });
    if (history == owner.value().streams.end()) owner.value().streams.push_back(listHistory);
    else history->snapshot = listHistory.snapshot;
    for (const auto block : touched) emit tentativeBlockChanged(block);
    const auto publication = d->lifecycle->publishStagedAction(stager, owner.value());
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        ++d->blockEditSequences[survivorEdit.blockId];
        ++d->structuralEditSequence;
        for (const auto block : touched) {
            emit tentativeBlockCommitted(block);
            emit blocksChanged({block});
            if (block != survivorEdit.blockId) {
                const auto row = std::find(blocks.begin(), blocks.end(), block);
                if (row != blocks.end())
                    emit blockRemoved(block, static_cast<int>(std::distance(blocks.begin(), row)));
            }
        }
        emit d2DocumentChanged();
        emit documentChanged();
    } else if (publication.state == ActionPublicationState::absent
               || publication.state == ActionPublicationState::rejected) {
        for (const auto block : touched) emit tentativeBlockReverted(block);
    }
    return publication;
}

VNext::ActionPublicationResult MarkoffDocument::applyBlockSplit(
    const BlockEdit& prefixEdit, BlockKind newBlockKind,
    const QByteArray& newBlockPrefix)
{
    using namespace VNext;
    if (!d->lifecycle->canStartOrdinaryAction())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::awaiting_state,
                "document lifecycle does not permit an ordinary action")};
    const auto blocks = iterateBlocks();
    const auto current = std::find(blocks.begin(), blocks.end(), prefixEdit.blockId);
    if (current == blocks.end())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::not_found,
                "split target is not visible")};
    const QByteArray original = blockText(prefixEdit.blockId);
    if (uint64_t(prefixEdit.withinBlockByteOffset) + prefixEdit.removedBytes
            > uint64_t(original.size())
        || !prefixEdit.insertedUtf8.isEmpty())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::malformed,
                "split must remove a suffix without inserting replacement text")};
    const QByteArray suffix = original.mid(prefixEdit.withinBlockByteOffset);
    const auto oldIdentity = toVNextIdentity(prefixEdit.blockId);
    if (!oldIdentity)
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::invalid_identity,
                "split target has no structural identity")};

    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, recoveryResult.error()};
    auto recovery = std::move(recoveryResult).value();
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok())
        return {ActionPublicationState::rejected, std::nullopt, sequence.error()};
    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId, ActionKind::edit);
    ActionHistoryEvent event;
    event.actionId = actionId;
    event.kind = ActionKind::edit;
    QList<BlockId> touched{prefixEdit.blockId};
    const auto rollback = [&](const CollabText::Error& error) {
        if (!stager.bundle().operations.empty())
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
        for (const auto block : touched) emit tentativeBlockReverted(block);
        return ActionPublicationResult{ActionPublicationState::rejected,
            std::nullopt, error};
    };

    auto listResult = d->lifecycle->stagingIdList();
    if (!listResult.ok()) return rollback(listResult.error());
    auto inserted = listResult.value()->insertAfter(*oldIdentity, 0);
    if (!inserted.ok()) return rollback(inserted.error());
    if (!inserted.value().insertedBlock)
        return rollback(CollabText::Error(CollabText::ErrorCode::invalid_identity,
            "split insertion did not return its new block identity"));
    auto listAdded = stager.add(inserted.value());
    if (!listAdded.ok()) return rollback(listAdded.error());
    const auto newIdentity = *inserted.value().insertedBlock;
    const auto newBlock = fromVNextIdentity(newIdentity);
    if (!newBlock)
        return rollback(CollabText::Error(CollabText::ErrorCode::invalid_identity,
            "split insertion returned an invalid block identity"));
    touched.push_back(*newBlock);
    const auto& listOpIdentity = inserted.value().identity;
    event.groups.push_back({inserted.value().scope.stream,
        OperationPrimitive::idList, listOpIdentity, {listOpIdentity}});

    auto kindMapResult = d->lifecycle->stagingMapStream(
        MapScope{document, "kind-tag", scope.epoch});
    if (!kindMapResult.ok()) return rollback(kindMapResult.error());
    auto encodedKind = MarkoffMapValueCodec::encodeBlockKind(newBlockKind);
    if (!encodedKind.ok()) return rollback(encodedKind.error());
    auto kindOperation = kindMapResult.value()->set({}, encodedKind.value(), newIdentity);
    if (!kindOperation.ok()) return rollback(kindOperation.error());
    auto kindAdded = stager.add(kindOperation.value());
    if (!kindAdded.ok()) return rollback(kindAdded.error());
    auto kindAssociation = toHistoryAssociation(kindOperation.value());
    if (!kindAssociation.ok()) return rollback(kindAssociation.error());
    event.groups.push_back(std::move(kindAssociation).value());

    auto oldTextStream = d->lifecycle->stagingTextStream(*oldIdentity);
    if (!oldTextStream.ok()) return rollback(oldTextStream.error());
    auto oldTextOperation = oldTextStream.value()->replaceUtf8(
        prefixEdit.withinBlockByteOffset,
        qsizetype(prefixEdit.withinBlockByteOffset) + prefixEdit.removedBytes, {});
    if (!oldTextOperation.ok()) return rollback(oldTextOperation.error());
    if (!oldTextOperation.value().bytes.isEmpty()) {
        auto added = stager.add(oldTextOperation.value());
        if (!added.ok()) return rollback(added.error());
        const CollabText::Crdt::Identity identity{
            oldTextOperation.value().identity.actor, oldTextOperation.value().identity.counter};
        event.groups.push_back({oldTextOperation.value().scope.stream,
            OperationPrimitive::text, identity, {identity}});
    }
    auto newTextStream = d->lifecycle->stagingTextStream(newIdentity);
    if (!newTextStream.ok()) return rollback(newTextStream.error());
    auto newTextOperation = newTextStream.value()->replaceUtf8(
        0, 0, newBlockPrefix + suffix);
    if (!newTextOperation.ok()) return rollback(newTextOperation.error());
    if (!newTextOperation.value().bytes.isEmpty()) {
        auto added = stager.add(newTextOperation.value());
        if (!added.ok()) return rollback(added.error());
        const CollabText::Crdt::Identity identity{
            newTextOperation.value().identity.actor, newTextOperation.value().identity.counter};
        event.groups.push_back({newTextOperation.value().scope.stream,
            OperationPrimitive::text, identity, {identity}});
    }

    std::vector<LocalHistoryRecord> histories;
    histories.push_back({listResult.value()->scope(), d->lifecycle->storage().actor(),
        listResult.value()->localHistorySnapshot()});
    histories.push_back({oldTextStream.value()->scope(), d->lifecycle->storage().actor(),
        oldTextStream.value()->localHistorySnapshot()});
    histories.push_back({newTextStream.value()->scope(), d->lifecycle->storage().actor(),
        newTextStream.value()->localHistorySnapshot()});
    auto owner = ownerHistoryForAction(recovery, d->lifecycle->storage(), document,
        scope.epoch, actionId, std::move(event), histories.front());
    if (!owner.ok()) return rollback(owner.error());
    for (size_t i = 1; i < histories.size(); ++i) {
        const auto& item = histories[i];
        const auto found = std::find_if(owner.value().streams.begin(), owner.value().streams.end(),
            [&](const LocalHistoryRecord& existing) {
                return existing.scope == item.scope && existing.actor == item.actor;
            });
        if (found == owner.value().streams.end()) owner.value().streams.push_back(item);
        else found->snapshot = item.snapshot;
    }
    for (const auto block : touched) emit tentativeBlockChanged(block);
    const auto publication = d->lifecycle->publishStagedAction(stager, owner.value());
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        ++d->blockEditSequences[prefixEdit.blockId];
        ++d->blockEditSequences[*newBlock];
        ++d->structuralEditSequence;
        for (const auto block : touched) {
            emit tentativeBlockCommitted(block);
            emit blocksChanged({block});
        }
        const auto blocksAfter = iterateBlocks();
        const auto row = std::find(blocksAfter.begin(), blocksAfter.end(), *newBlock);
        if (row != blocksAfter.end())
            emit blockInserted(*newBlock, static_cast<int>(std::distance(blocksAfter.begin(), row)));
        emit d2DocumentChanged();
        emit documentChanged();
    } else if (publication.state == ActionPublicationState::absent
               || publication.state == ActionPublicationState::rejected) {
        for (const auto block : touched) emit tentativeBlockReverted(block);
    }
    return publication;
}

VNext::ActionPublicationResult MarkoffDocument::replaceMatches(
    const QList<SearchHit>& matches, const QString& replacement)
{
    using namespace VNext;
    if (matches.isEmpty()) {
        auto recovery = d->lifecycle->storage().readRecoveryState();
        if (!recovery.ok())
            return {ActionPublicationState::rejected, std::nullopt, recovery.error()};
        return {ActionPublicationState::committed,
            recovery.value().acceptedRevision, std::nullopt};
    }
    if (!d->lifecycle->canStartOrdinaryAction())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::awaiting_state,
                "document lifecycle does not permit an ordinary action")};

    struct Edit {
        BlockId block;
        uint32_t start;
        uint32_t length;
        size_t blockOrder;
    };
    QHash<BlockId, size_t> orderOf;
    size_t order = 0;
    for (const auto block : iterateBlocks()) orderOf.insert(block, order++);
    std::vector<Edit> edits;
    edits.reserve(static_cast<size_t>(matches.size()));
    for (const auto& match : matches) {
        const auto orderIt = orderOf.constFind(match.blockId);
        if (orderIt == orderOf.cend()) continue; // A stale search result.
        const QByteArray text = blockText(match.blockId);
        const uint64_t end = uint64_t(match.matchStart) + match.matchLen;
        if (end > uint64_t(text.size()))
            return {ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::off_end,
                    "replace match lies outside its current block")};
        edits.push_back({match.blockId, match.matchStart, match.matchLen, *orderIt});
    }
    if (edits.empty()) {
        auto recovery = d->lifecycle->storage().readRecoveryState();
        if (!recovery.ok())
            return {ActionPublicationState::rejected, std::nullopt, recovery.error()};
        return {ActionPublicationState::committed,
            recovery.value().acceptedRevision, std::nullopt};
    }
    std::sort(edits.begin(), edits.end(), [](const Edit& left, const Edit& right) {
        if (left.blockOrder != right.blockOrder) return left.blockOrder < right.blockOrder;
        return left.start > right.start;
    });
    for (size_t i = 1; i < edits.size(); ++i) {
        if (edits[i - 1].block == edits[i].block
            && uint64_t(edits[i].start) + edits[i].length > edits[i - 1].start)
            return {ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::malformed,
                    "replace matches overlap")};
    }

    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, recoveryResult.error()};
    auto recovery = std::move(recoveryResult).value();
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok())
        return {ActionPublicationState::rejected, std::nullopt, sequence.error()};
    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId, ActionKind::edit);
    const auto abort = [&](const CollabText::Error& error,
                           const std::vector<BlockId>& touched) {
        if (!stager.bundle().operations.empty())
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
        for (const auto block : touched) emit tentativeBlockReverted(block);
        return ActionPublicationResult{ActionPublicationState::rejected,
            std::nullopt, error};
    };

    ActionHistoryEvent event;
    event.actionId = actionId;
    event.kind = ActionKind::edit;
    std::vector<LocalHistoryRecord> updatedStreams;
    std::vector<BlockId> touched;
    const QByteArray replacementBytes = replacement.toUtf8();
    for (const auto& edit : edits) {
        auto identity = toVNextIdentity(edit.block);
        if (!identity)
            return abort(CollabText::Error(CollabText::ErrorCode::invalid_identity,
                "replace match has a null block identity"), touched);
        auto streamResult = d->lifecycle->stagingTextStream(*identity);
        if (!streamResult.ok()) return abort(streamResult.error(), touched);
        auto* stream = streamResult.value();
        auto operation = stream->replaceUtf8(edit.start,
            qsizetype(edit.start) + edit.length, replacementBytes);
        if (!operation.ok()) return abort(operation.error(), touched);
        if (touched.empty() || touched.back() != edit.block) touched.push_back(edit.block);
        auto staged = stager.add(operation.value());
        if (!staged.ok()) return abort(staged.error(), touched);
        const CollabText::Crdt::Identity opIdentity{
            operation.value().identity.actor, operation.value().identity.counter};
        event.groups.push_back({operation.value().scope.stream,
            OperationPrimitive::text, opIdentity, {opIdentity}});
        auto history = std::find_if(updatedStreams.begin(), updatedStreams.end(),
            [&](const LocalHistoryRecord& item) {
                return item.scope == operation.value().scope
                    && item.actor == d->lifecycle->storage().actor();
            });
        LocalHistoryRecord snapshot{stream->scope(), d->lifecycle->storage().actor(),
            stream->localHistorySnapshot()};
        if (history == updatedStreams.end()) updatedStreams.push_back(std::move(snapshot));
        else history->snapshot = std::move(snapshot.snapshot);
    }

    auto owner = ownerHistoryForAction(recovery, d->lifecycle->storage(), document,
        scope.epoch, actionId, std::move(event),
        updatedStreams.empty() ? std::nullopt
            : std::optional<LocalHistoryRecord>(updatedStreams.front()));
    if (!owner.ok()) return abort(owner.error(), touched);
    for (size_t i = 1; i < updatedStreams.size(); ++i) {
        const auto& item = updatedStreams[i];
        const auto existing = std::find_if(owner.value().streams.begin(),
            owner.value().streams.end(), [&](const LocalHistoryRecord& record) {
                return record.scope == item.scope && record.actor == item.actor;
            });
        if (existing == owner.value().streams.end()) owner.value().streams.push_back(item);
        else existing->snapshot = item.snapshot;
    }
    for (const auto block : touched) emit tentativeBlockChanged(block);
    const auto publication = d->lifecycle->publishStagedAction(stager, owner.value());
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        for (const auto block : touched) {
            ++d->blockEditSequences[block];
            emit tentativeBlockCommitted(block);
        }
        QList<BlockId> changedBlocks;
        for (const auto block : touched) changedBlocks.push_back(block);
        emit blocksChanged(changedBlocks);
        emit d2DocumentChanged();
        emit documentChanged();
    } else if (publication.state == ActionPublicationState::absent
               || publication.state == ActionPublicationState::rejected) {
        for (const auto block : touched) emit tentativeBlockReverted(block);
    }
    return publication;
}

const BlockSerializerRegistry *MarkoffDocument::serializerRegistry() const
{
    return d->serializerRegistry;
}

Session *MarkoffDocument::createSession(const SessionParams& params)
{
    auto *session = new Session(this, params);
    d->sessions.push_back(session);
    emit sessionCreated(session);
    return session;
}

void MarkoffDocument::destroySession(Session *session)
{
    if (!session || !d->sessions.removeOne(session)) return;
    emit sessionDestroyed(session);
    session->deleteLater();
}

QList<Session *> MarkoffDocument::sessions() const
{
    return d->sessions;
}

Session *MarkoffDocument::sessionForParticipant(const QString& participantId) const
{
    for (Session *session : d->sessions)
        if (session && session->participantId() == participantId) return session;
    return nullptr;
}

std::vector<BlockId> MarkoffDocument::iterateBlocks() const
{
    std::vector<BlockId> result;
    const auto *list = d->lifecycle->projection().idList();
    if (!list)
        return result;
    const auto entries = list->visibleEntries();
    if (!entries.ok())
        return result;
    result.reserve(entries.value().size());
    for (const auto& entry : entries.value()) {
        if (const auto block = fromVNextIdentity(entry.identity))
            result.push_back(*block);
    }
    return result;
}

QByteArray MarkoffDocument::blockText(BlockId id) const
{
    const auto identity = toVNextIdentity(id);
    if (!identity)
        return {};
    const auto route = VNext::blockTextStreamName(*identity);
    if (!route.ok())
        return {};
    const auto *stream = d->lifecycle->projection().textStream(route.value());
    return stream ? stream->text() : QByteArray{};
}

void MarkoffDocument::flushPendingD2Changed()
{
    emit d2DocumentChanged();
    emit documentChanged();
}

CollabText::Result<QByteArray> MarkoffDocument::boundaryTokenAt(
    BlockId block, quint32 byteOffset, bool rightAffinity) const
{
    const auto identity = toVNextIdentity(block);
    if (!identity)
        return CollabText::Error(CollabText::ErrorCode::invalid_identity,
            "text boundary requires a structural block identity");
    auto stream = d->lifecycle->stagingTextStream(*identity);
    if (!stream.ok()) return stream.error();
    return stream.value()->boundaryTokenAt(byteOffset,
        rightAffinity ? CollabText::Affinity::right : CollabText::Affinity::left);
}

CollabText::Result<quint32> MarkoffDocument::resolveBoundaryToken(
    BlockId block, const QByteArray& token) const
{
    const auto identity = toVNextIdentity(block);
    if (!identity)
        return CollabText::Error(CollabText::ErrorCode::invalid_identity,
            "text boundary requires a structural block identity");
    auto stream = d->lifecycle->stagingTextStream(*identity);
    if (!stream.ok()) return stream.error();
    auto resolved = stream.value()->resolveBoundary(token);
    if (!resolved.ok()) return resolved.error();
    if (resolved.value().collapsed())
        return CollabText::Error(CollabText::ErrorCode::detached,
            "text boundary detached from its original content");
    return resolved.value().byte_offset;
}

BlockKind MarkoffDocument::blockKind(BlockId id) const
{
    const auto identity = toVNextIdentity(id);
    if (!identity)
        return BlockKind::Paragraph;
    const auto *map = d->lifecycle->projection().mapStream("kind-tag");
    if (!map)
        return BlockKind::Paragraph;
    const auto encoded = map->get({}, *identity);
    if (!encoded)
        return BlockKind::Paragraph;
    const auto decoded = VNext::MarkoffMapValueCodec::decodeBlockKind(*encoded);
    return decoded.ok() ? decoded.value() : BlockKind::Paragraph;
}

QHash<AttrName, AttrValue> MarkoffDocument::blockAttrs(BlockId id) const
{
    QHash<AttrName, AttrValue> result;
    const auto identity = toVNextIdentity(id);
    if (!identity)
        return result;
    const auto *map = d->lifecycle->projection().mapStream("block-attrs");
    if (!map)
        return result;
    for (const auto& entry : map->values()) {
        if (entry.targetBlock != identity)
            continue;
        const auto decoded = VNext::MarkoffMapValueCodec::decodeAttribute(entry.value);
        if (decoded.ok())
            result.insert(entry.key, decoded.value());
    }
    return result;
}

QByteArray MarkoffDocument::listItemDisplayMarker(BlockId id) const
{
    if (blockKind(id) != BlockKind::ListItem) return {};
    const auto attrs = blockAttrs(id);
    const auto value = [&](const AttrName& name) -> const AttrValue* {
        const auto found = attrs.constFind(name);
        return found == attrs.cend() ? nullptr : &found.value();
    };
    int indent = 0;
    if (const auto* attr = value(AttrNames::IndentLevel))
        if (const auto* level = std::get_if<int>(attr)) indent = *level;
    QString style = QStringLiteral("minus");
    if (const auto* attr = value(AttrNames::MarkerStyle))
        if (const auto* markerStyle = std::get_if<QString>(attr)) style = *markerStyle;
    QByteArray marker;
    if (style == QStringLiteral("dot") || style == QStringLiteral("paren")) {
        int number = 1;
        if (const auto* attr = value(AttrNames::MarkerNumber))
            if (const auto* markerNumber = std::get_if<int>(attr)) number = *markerNumber;
        marker = QByteArray::number(number)
            + (style == QStringLiteral("dot") ? "." : ")");
    } else if (style == QStringLiteral("plus")) {
        marker = "+";
    } else if (style == QStringLiteral("star")) {
        marker = "*";
    } else if (style == QStringLiteral("task")) {
        bool checked = false;
        if (const auto* attr = value(AttrNames::Checked))
            if (const auto* state = std::get_if<bool>(attr)) checked = *state;
        marker = checked ? "- [x]" : "- [ ]";
    } else {
        marker = "-";
    }
    return QByteArray(std::max(0, indent) * 3, ' ') + marker + " ";
}

QByteArray MarkoffDocument::blockQuoteDisplayMarker(BlockId id) const
{
    if (blockKind(id) != BlockKind::BlockQuote) return {};
    int depth = 1;
    const auto attrs = blockAttrs(id);
    const auto found = attrs.constFind(AttrNames::BlockQuoteDepth);
    if (found != attrs.cend())
        if (const auto* value = std::get_if<int>(&found.value()))
            depth = std::max(1, *value);
    QByteArray marker;
    for (int i = 0; i < depth; ++i) marker += "> ";
    return marker;
}

VNext::ActionPublicationResult MarkoffDocument::setBlockAttribute(
    BlockId id, const AttrName& name, const AttrValue& value)
{
    using namespace VNext;
    const auto identity = toVNextIdentity(id);
    if (!identity)
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::invalid_identity,
                "attribute target has no structural identity")};
    auto encoded = MarkoffMapValueCodec::encodeAttribute(value);
    if (!encoded.ok())
        return {ActionPublicationState::rejected, std::nullopt, encoded.error()};
    emit tentativeBlockChanged(id);
    const auto publication = publishMapValue(*d->lifecycle, "block-attrs",
        name, identity, encoded.value());
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        emit tentativeBlockCommitted(id);
        emit blocksChanged({id});
        emit d2DocumentChanged();
        emit documentChanged();
    } else if (publication.state == ActionPublicationState::absent
               || publication.state == ActionPublicationState::rejected) {
        emit tentativeBlockReverted(id);
    }
    return publication;
}

VNext::ActionPublicationResult MarkoffDocument::removeBlockAttribute(
    BlockId id, const AttrName& name)
{
    using namespace VNext;
    const auto identity = toVNextIdentity(id);
    if (!identity)
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::invalid_identity,
                "attribute target has no structural identity")};
    emit tentativeBlockChanged(id);
    const auto publication = publishMapValue(*d->lifecycle, "block-attrs",
        name, identity, {}, true);
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        emit tentativeBlockCommitted(id);
        emit blocksChanged({id});
        emit d2DocumentChanged();
        emit documentChanged();
    } else if (publication.state == ActionPublicationState::absent
               || publication.state == ActionPublicationState::rejected) {
        emit tentativeBlockReverted(id);
    }
    return publication;
}

VNext::ActionPublicationResult MarkoffDocument::changeBlockKind(
    BlockId block, BlockKind kind, const QList<AttrName>& attrNames,
    const QList<AttrValue>& attrValues)
{
    using namespace VNext;
    if (attrNames.size() != attrValues.size())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::malformed,
                "kind-change attribute names and values differ in length")};
    std::set<QByteArray> uniqueNames;
    for (const auto& name : attrNames) {
        if (!uniqueNames.insert(name).second)
            return {ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::malformed,
                    "kind-change attributes contain a duplicate name")};
    }
    const auto identity = toVNextIdentity(block);
    const auto visibleBlocks = iterateBlocks();
    if (!identity || std::find(visibleBlocks.begin(), visibleBlocks.end(), block)
            == visibleBlocks.end())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::invalid_identity,
                "kind change requires a visible structural block")};
    if (!d->lifecycle->canStartOrdinaryAction())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::awaiting_state,
                "document lifecycle does not permit an ordinary action")};

    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, recoveryResult.error()};
    auto recovery = std::move(recoveryResult).value();
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok())
        return {ActionPublicationState::rejected, std::nullopt, sequence.error()};
    auto kindStreamResult = d->lifecycle->stagingMapStream(
        MapScope{document, "kind-tag", scope.epoch});
    auto attrsStreamResult = d->lifecycle->stagingMapStream(
        MapScope{document, "block-attrs", scope.epoch});
    if (!kindStreamResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, kindStreamResult.error()};
    if (!attrsStreamResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, attrsStreamResult.error()};

    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId, ActionKind::edit);
    ActionHistoryEvent event;
    event.actionId = actionId;
    event.kind = ActionKind::edit;
    std::vector<HistoryGroupAssociation> groups;
    const auto rollback = [&](const CollabText::Error& error) {
        if (!stager.bundle().operations.empty())
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
        return ActionPublicationResult{ActionPublicationState::rejected,
            std::nullopt, error};
    };
    const auto stage = [&](CollabText::Result<LocalMapOperation> local)
        -> CollabText::Result<void> {
        if (!local.ok()) return local.error();
        auto added = stager.add(local.value());
        if (!added.ok()) return added.error();
        auto association = toHistoryAssociation(local.value());
        if (!association.ok()) return association.error();
        groups.push_back(std::move(association).value());
        return {};
    };
    auto encodedKind = MarkoffMapValueCodec::encodeBlockKind(kind);
    if (!encodedKind.ok()) return rollback(encodedKind.error());
    auto kindWrite = stage(kindStreamResult.value()->set({}, encodedKind.value(), identity));
    if (!kindWrite.ok()) return rollback(kindWrite.error());
    for (int i = 0; i < attrNames.size(); ++i) {
        auto encoded = MarkoffMapValueCodec::encodeAttribute(attrValues[i]);
        if (!encoded.ok()) return rollback(encoded.error());
        auto written = stage(attrsStreamResult.value()->set(
            attrNames[i], encoded.value(), identity));
        if (!written.ok()) return rollback(written.error());
    }
    event.groups = std::move(groups);
    auto owner = ownerHistoryForAction(recovery, d->lifecycle->storage(), document,
        scope.epoch, actionId, std::move(event));
    if (!owner.ok()) return rollback(owner.error());
    emit tentativeBlockChanged(block);
    const auto publication = d->lifecycle->publishStagedAction(stager, owner.value());
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        ++d->structuralEditSequence;
        emit tentativeBlockCommitted(block);
        emit blocksChanged({block});
        emit d2DocumentChanged();
        emit documentChanged();
    } else if (publication.state == ActionPublicationState::absent
               || publication.state == ActionPublicationState::rejected) {
        emit tentativeBlockReverted(block);
    }
    return publication;
}

VNext::ActionPublicationResult MarkoffDocument::swapBlockPayload(
    BlockId first, BlockId second, const QHash<AttrName, AttrValue>& defaults)
{
    using namespace VNext;
    const auto blocks = iterateBlocks();
    if (first == second || std::find(blocks.begin(), blocks.end(), first) == blocks.end()
        || std::find(blocks.begin(), blocks.end(), second) == blocks.end())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::not_found,
                "payload swap requires two distinct visible blocks")};
    if (!d->lifecycle->canStartOrdinaryAction())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::awaiting_state,
                "document lifecycle does not permit an ordinary action")};

    const auto firstIdentity = toVNextIdentity(first);
    const auto secondIdentity = toVNextIdentity(second);
    if (!firstIdentity || !secondIdentity)
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::invalid_identity,
                "payload swap target has no structural identity")};
    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, recoveryResult.error()};
    auto recovery = std::move(recoveryResult).value();
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok())
        return {ActionPublicationState::rejected, std::nullopt, sequence.error()};

    auto firstText = d->lifecycle->stagingTextStream(*firstIdentity);
    auto secondText = d->lifecycle->stagingTextStream(*secondIdentity);
    auto kindMap = d->lifecycle->stagingMapStream(
        MapScope{document, "kind-tag", scope.epoch});
    auto attrsMap = d->lifecycle->stagingMapStream(
        MapScope{document, "block-attrs", scope.epoch});
    if (!firstText.ok()) return {ActionPublicationState::rejected, std::nullopt, firstText.error()};
    if (!secondText.ok()) return {ActionPublicationState::rejected, std::nullopt, secondText.error()};
    if (!kindMap.ok()) return {ActionPublicationState::rejected, std::nullopt, kindMap.error()};
    if (!attrsMap.ok()) return {ActionPublicationState::rejected, std::nullopt, attrsMap.error()};

    const QByteArray firstBytes = blockText(first);
    const QByteArray secondBytes = blockText(second);
    const auto firstAttrs = blockAttrs(first);
    const auto secondAttrs = blockAttrs(second);
    const BlockKind firstKind = blockKind(first);
    const BlockKind secondKind = blockKind(second);
    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId, ActionKind::edit);
    ActionHistoryEvent event;
    event.actionId = actionId;
    event.kind = ActionKind::edit;
    std::vector<BlockId> touched{first, second};
    const auto abort = [&](const CollabText::Error& error) {
        if (!stager.bundle().operations.empty())
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
        for (const auto block : touched) emit tentativeBlockReverted(block);
        return ActionPublicationResult{ActionPublicationState::rejected, std::nullopt, error};
    };
    const auto stageText = [&](CollabText::Result<LocalOperation> operation)
        -> CollabText::Result<void> {
        if (!operation.ok()) return operation.error();
        if (operation.value().bytes.isEmpty()) return {};
        auto added = stager.add(operation.value());
        if (!added.ok()) return added.error();
        const CollabText::Crdt::Identity id{operation.value().identity.actor,
                                            operation.value().identity.counter};
        event.groups.push_back({operation.value().scope.stream,
            OperationPrimitive::text, id, {id}});
        return {};
    };
    auto firstEdit = firstText.value()->replaceUtf8(0, firstBytes.size(), secondBytes);
    auto secondEdit = secondText.value()->replaceUtf8(0, secondBytes.size(), firstBytes);
    auto staged = stageText(std::move(firstEdit));
    if (!staged.ok()) return abort(staged.error());
    staged = stageText(std::move(secondEdit));
    if (!staged.ok()) return abort(staged.error());

    const auto stageMap = [&](const QByteArray& key, const QByteArray& value,
                               const VNext::BlockIdentity& target)
        -> CollabText::Result<void> {
        auto local = attrsMap.value()->set(key, value, target);
        if (!local.ok()) return local.error();
        auto added = stager.add(local.value());
        if (!added.ok()) return added.error();
        auto association = toHistoryAssociation(local.value());
        if (!association.ok()) return association.error();
        event.groups.push_back(std::move(association).value());
        return {};
    };
    auto kindA = MarkoffMapValueCodec::encodeBlockKind(secondKind);
    auto kindB = MarkoffMapValueCodec::encodeBlockKind(firstKind);
    if (!kindA.ok()) return abort(kindA.error());
    if (!kindB.ok()) return abort(kindB.error());
    for (const auto& pair : {std::pair{firstIdentity, kindA.value()},
                             std::pair{secondIdentity, kindB.value()}}) {
        auto local = kindMap.value()->set({}, pair.second, *pair.first);
        if (!local.ok()) return abort(local.error());
        auto added = stager.add(local.value());
        if (!added.ok()) return abort(added.error());
        auto association = toHistoryAssociation(local.value());
        if (!association.ok()) return abort(association.error());
        event.groups.push_back(std::move(association).value());
    }
    std::set<AttrName> names;
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) names.insert(it.key());
    for (auto it = firstAttrs.cbegin(); it != firstAttrs.cend(); ++it) names.insert(it.key());
    for (auto it = secondAttrs.cbegin(); it != secondAttrs.cend(); ++it) names.insert(it.key());
    for (const auto& name : names) {
        const auto fromSecond = secondAttrs.constFind(name);
        const auto fromFirst = firstAttrs.constFind(name);
        const AttrValue valueA = fromSecond == secondAttrs.cend()
            ? defaults.value(name) : fromSecond.value();
        const AttrValue valueB = fromFirst == firstAttrs.cend()
            ? defaults.value(name) : fromFirst.value();
        auto encodedA = MarkoffMapValueCodec::encodeAttribute(valueA);
        auto encodedB = MarkoffMapValueCodec::encodeAttribute(valueB);
        if (!encodedA.ok()) return abort(encodedA.error());
        if (!encodedB.ok()) return abort(encodedB.error());
        auto a = stageMap(name, encodedA.value(), *firstIdentity);
        if (!a.ok()) return abort(a.error());
        auto b = stageMap(name, encodedB.value(), *secondIdentity);
        if (!b.ok()) return abort(b.error());
    }

    std::vector<LocalHistoryRecord> histories{
        {firstText.value()->scope(), d->lifecycle->storage().actor(), firstText.value()->localHistorySnapshot()},
        {secondText.value()->scope(), d->lifecycle->storage().actor(), secondText.value()->localHistorySnapshot()},
    };
    auto owner = ownerHistoryForAction(recovery, d->lifecycle->storage(), document,
        scope.epoch, actionId, std::move(event), histories.front());
    if (!owner.ok()) return abort(owner.error());
    for (size_t i = 1; i < histories.size(); ++i) {
        const auto& item = histories[i];
        const auto found = std::find_if(owner.value().streams.begin(), owner.value().streams.end(),
            [&](const LocalHistoryRecord& current) {
                return current.scope == item.scope && current.actor == item.actor;
            });
        if (found == owner.value().streams.end()) owner.value().streams.push_back(item);
        else found->snapshot = item.snapshot;
    }
    emit tentativeBlockChanged(first);
    emit tentativeBlockChanged(second);
    const auto publication = d->lifecycle->publishStagedAction(stager, owner.value());
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        ++d->blockEditSequences[first];
        ++d->blockEditSequences[second];
        ++d->structuralEditSequence;
        emit tentativeBlockCommitted(first);
        emit tentativeBlockCommitted(second);
        emit blocksChanged({first, second});
        emit d2DocumentChanged();
        emit documentChanged();
    } else if (publication.state == ActionPublicationState::absent
               || publication.state == ActionPublicationState::rejected) {
        emit tentativeBlockReverted(first);
        emit tentativeBlockReverted(second);
    }
    return publication;
}

BlockPasteResult MarkoffDocument::pasteMarkdownAt(
    BlockId target, uint32_t byteOffset, const QByteArray& markdown)
{
    return pasteMarkdownReplacingRange(target, byteOffset, target, byteOffset,
                                       markdown);
}

BlockPasteResult MarkoffDocument::pasteMarkdownReplacingRange(
    BlockId target, uint32_t byteOffset, BlockId endTarget,
    uint32_t endByteOffset, const QByteArray& markdown)
{
    using namespace VNext;
    const BlockPasteResult noop{{ActionPublicationState::rejected, std::nullopt,
        CollabText::Error(CollabText::ErrorCode::malformed, "paste has no parsed blocks")},
        target, byteOffset};
    const bool hasSelection = target != endTarget || byteOffset != endByteOffset;
    if (!d->lifecycle->canStartOrdinaryAction())
        return {{ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::awaiting_state,
                "document lifecycle does not permit an ordinary action")}, target, byteOffset};
    QByteArray cleaned;
    cleaned.reserve(markdown.size());
    for (const char c : markdown) if (c != '\0') cleaned.append(c);
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty() && !hasSelection) {
        const auto recovery = d->lifecycle->storage().readRecoveryState();
        if (!recovery.ok())
            return {{ActionPublicationState::rejected, std::nullopt, recovery.error()}, target, byteOffset};
        return {{ActionPublicationState::committed, recovery.value().acceptedRevision, std::nullopt},
            target, byteOffset};
    }
    const QString source = QString::fromUtf8(cleaned);
    if (source.toUtf8() != cleaned) return noop;
    const auto parsed = Markoff::Document::fromMarkdown(source);
    if (!parsed) return noop;
    auto pasteBlocks = parsed->topLevelBlocks();
    pasteBlocks.erase(std::remove_if(pasteBlocks.begin(), pasteBlocks.end(),
        [](const auto& block) {
            return block.kind == Markoff::TopLevelBlock::Kind::LinkReferenceDefinition;
        }), pasteBlocks.end());
    if (pasteBlocks.empty()) {
        if (!hasSelection) return noop;
        const auto visible = iterateBlocks();
        const auto start = std::find(visible.begin(), visible.end(), target);
        const auto end = std::find(visible.begin(), visible.end(), endTarget);
        if (start == visible.end() || end == visible.end() || start > end)
            return {{ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::not_found,
                    "paste replacement range is not visible")}, target, byteOffset};
        const QByteArray first = blockText(target);
        const QByteArray last = blockText(endTarget);
        if ((target == endTarget && byteOffset > endByteOffset)
            || byteOffset > uint32_t(first.size())
            || endByteOffset > uint32_t(last.size()))
            return {{ActionPublicationState::rejected, std::nullopt,
                CollabText::Error(CollabText::ErrorCode::off_end,
                    "paste replacement range is outside its block")}, target, byteOffset};
        const QByteArray tail = last.mid(endByteOffset);
        VNext::ActionPublicationResult publication;
        if (target == endTarget) {
            publication = applyBlockEdit({target, byteOffset,
                endByteOffset - byteOffset, tail});
        } else {
            QList<BlockId> removed;
            for (auto it = start + 1; it <= end; ++it)
                removed.append(*it);
            publication = applyBlockMerge({target, byteOffset,
                uint32_t(first.size()) - byteOffset, tail}, removed);
        }
        return {publication, target, byteOffset};
    }

    const auto visible = iterateBlocks();
    const auto startIt = std::find(visible.begin(), visible.end(), target);
    const auto endIt = std::find(visible.begin(), visible.end(), endTarget);
    if (startIt == visible.end() || endIt == visible.end() || startIt > endIt)
        return {{ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::not_found,
                "paste target or replacement range is not visible")}, target, byteOffset};
    const QByteArray targetText = blockText(target);
    const QByteArray endText = blockText(endTarget);
    if ((target == endTarget && byteOffset > endByteOffset)
        || byteOffset > quint32(targetText.size())
        || endByteOffset > quint32(endText.size()))
        return {{ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::off_end,
                "paste offset is outside the replacement range")}, target, byteOffset};
    const QByteArray tail = endText.mid(endByteOffset);
    const bool reuseTarget = byteOffset == 0
        && (targetText.isEmpty() || hasSelection);
    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok())
        return {{ActionPublicationState::rejected, std::nullopt, recoveryResult.error()}, target, byteOffset};
    auto recovery = std::move(recoveryResult).value();
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok())
        return {{ActionPublicationState::rejected, std::nullopt, sequence.error()}, target, byteOffset};
    auto listResult = d->lifecycle->stagingIdList();
    auto kindResult = d->lifecycle->stagingMapStream(MapScope{document, "kind-tag", scope.epoch});
    auto attrsResult = d->lifecycle->stagingMapStream(MapScope{document, "block-attrs", scope.epoch});
    if (!listResult.ok()) return {{ActionPublicationState::rejected, std::nullopt, listResult.error()}, target, byteOffset};
    if (!kindResult.ok()) return {{ActionPublicationState::rejected, std::nullopt, kindResult.error()}, target, byteOffset};
    if (!attrsResult.ok()) return {{ActionPublicationState::rejected, std::nullopt, attrsResult.error()}, target, byteOffset};
    auto after = toVNextIdentity(target);
    if (!after)
        return {{ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::invalid_identity,
                "paste target has no structural identity")}, target, byteOffset};

    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId, ActionKind::edit);
    ActionHistoryEvent event;
    event.actionId = actionId;
    event.kind = ActionKind::edit;
    std::vector<BlockId> touched{target};
    std::vector<BlockId> insertedBlocks;
    std::vector<BlockId> removedBlocks;
    std::vector<BlockId> textBlocks;
    std::vector<LocalHistoryRecord> textHistories;
    const auto abort = [&](const CollabText::Error& error) {
        if (!stager.bundle().operations.empty())
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
        for (const auto block : touched) emit tentativeBlockReverted(block);
        return BlockPasteResult{{ActionPublicationState::rejected, std::nullopt, error}, target, byteOffset};
    };
    const auto stageText = [&](BlockId block, const BlockIdentity& identity,
                               uint32_t from, uint32_t to, const QByteArray& replacement)
        -> CollabText::Result<void> {
        auto stream = d->lifecycle->stagingTextStream(identity);
        if (!stream.ok()) return stream.error();
        auto local = stream.value()->replaceUtf8(from, to, replacement);
        if (!local.ok()) return local.error();
        if (!local.value().bytes.isEmpty()) {
            auto added = stager.add(local.value());
            if (!added.ok()) return added.error();
            const CollabText::Crdt::Identity id{local.value().identity.actor,
                                                local.value().identity.counter};
            event.groups.push_back({local.value().scope.stream,
                OperationPrimitive::text, id, {id}});
            textBlocks.push_back(block);
        }
        const LocalHistoryRecord history{stream.value()->scope(),
            d->lifecycle->storage().actor(), stream.value()->localHistorySnapshot()};
        const auto found = std::find_if(textHistories.begin(), textHistories.end(),
            [&](const LocalHistoryRecord& old) {
                return old.scope == history.scope && old.actor == history.actor;
            });
        if (found == textHistories.end()) textHistories.push_back(history);
        else found->snapshot = history.snapshot;
        return {};
    };
    const auto stageMap = [&](const LocalMapOperation& local) -> CollabText::Result<void> {
        auto added = stager.add(local);
        if (!added.ok()) return added.error();
        auto association = toHistoryAssociation(local);
        if (!association.ok()) return association.error();
        event.groups.push_back(std::move(association).value());
        return {};
    };
    const auto writeAttrs = [&](const BlockIdentity& identity,
                                const Markoff::TopLevelBlock& block,
                                BlockKind kind) -> CollabText::Result<void> {
        const auto write = [&](const QByteArray& name, const AttrValue& value)
            -> CollabText::Result<void> {
            auto encoded = MarkoffMapValueCodec::encodeAttribute(value);
            if (!encoded.ok()) return encoded.error();
            auto local = attrsResult.value()->set(name, encoded.value(), identity);
            if (!local.ok()) return local.error();
            return stageMap(local.value());
        };
        if (kind == BlockKind::Heading) {
            auto r = write(AttrNames::Level, qBound(1, block.headingLevel, 6));
            if (!r.ok()) return r.error();
            r = write(AttrNames::HeadingForm,
                block.kind == Markoff::TopLevelBlock::Kind::SetextHeading
                    ? QStringLiteral("setext") : QStringLiteral("atx"));
            if (!r.ok()) return r.error();
        } else if (kind == BlockKind::ListItem) {
            for (const auto& r : {write(AttrNames::IndentLevel, block.indentDepth),
                                  write(AttrNames::MarkerStyle, block.markerStyle),
                                  write(AttrNames::LooseRun, block.looseRun)})
                if (!r.ok()) return r.error();
            if (block.markerStyle == QStringLiteral("dot")
                || block.markerStyle == QStringLiteral("paren")) {
                auto r = write(AttrNames::MarkerNumber, block.markerNumber);
                if (!r.ok()) return r.error();
            }
            if (block.markerStyle == QStringLiteral("task")) {
                auto r = write(AttrNames::Checked, block.checked);
                if (!r.ok()) return r.error();
            }
        } else if (kind == BlockKind::BlockQuote) {
            auto r = write(AttrNames::BlockQuoteDepth, qMax(1, block.blockQuoteDepth));
            if (!r.ok()) return r.error();
            if (block.blockQuoteRunId > 0) {
                r = write(AttrNames::BlockQuoteRunId, block.blockQuoteRunId);
                if (!r.ok()) return r.error();
            }
        } else if (kind == BlockKind::CodeBlock && !block.codeLanguage.isEmpty()) {
            auto r = write(AttrNames::InfoString, block.codeLanguage);
            if (!r.ok()) return r.error();
        }
        return {};
    };

    for (auto it = startIt + 1; it <= endIt; ++it) {
        const BlockId removed = *it;
        const auto identity = toVNextIdentity(removed);
        if (!identity) return abort(CollabText::Error(
            CollabText::ErrorCode::invalid_identity,
            "paste replacement block has no structural identity"));
        auto listRemoval = listResult.value()->remove(*identity);
        if (!listRemoval.ok()) return abort(listRemoval.error());
        auto listAdded = stager.add(listRemoval.value());
        if (!listAdded.ok()) return abort(listAdded.error());
        const auto listId = listRemoval.value().identity;
        event.groups.push_back({listRemoval.value().scope.stream,
            OperationPrimitive::idList, listId, {listId}});
        auto kindRemoval = kindResult.value()->remove({}, *identity);
        if (!kindRemoval.ok()) return abort(kindRemoval.error());
        auto kindStaged = stageMap(kindRemoval.value());
        if (!kindStaged.ok()) return abort(kindStaged.error());
        removedBlocks.push_back(removed);
        touched.push_back(removed);
    }

    if (reuseTarget) {
        auto identity = toVNextIdentity(target);
        if (!identity) return abort(CollabText::Error(
            CollabText::ErrorCode::invalid_identity,
            "paste target has no structural identity"));
        auto clearAttributes = attrsResult.value()->values();
        for (const auto& attr : clearAttributes) {
            if (attr.targetBlock != identity) continue;
            auto removed = attrsResult.value()->remove(attr.key, identity);
            if (!removed.ok()) return abort(removed.error());
            auto staged = stageMap(removed.value());
            if (!staged.ok()) return abort(staged.error());
        }
        auto clearText = stageText(target, *identity, 0,
            quint32(targetText.size()), {});
        if (!clearText.ok()) return abort(clearText.error());
    } else if (byteOffset < quint32(targetText.size())) {
        auto identity = toVNextIdentity(target);
        auto cut = stageText(target, *identity, byteOffset, quint32(targetText.size()), {});
        if (!cut.ok()) return abort(cut.error());
    }
    const QByteArray body = cleaned;
    BlockId lastBlock = target;
    uint32_t caretOffset = 0;
    bool isFirst = true;
    for (const auto& block : pasteBlocks) {
        using Kind = Markoff::TopLevelBlock::Kind;
        BlockKind kind = BlockKind::Paragraph;
        switch (block.kind) {
        case Kind::AtxHeading: case Kind::SetextHeading: kind = BlockKind::Heading; break;
        case Kind::FencedCodeBlock: case Kind::IndentedCodeBlock: kind = BlockKind::CodeBlock; break;
        case Kind::ListItem: kind = BlockKind::ListItem; break;
        case Kind::ThematicBreak: kind = BlockKind::HorizontalRule; break;
        case Kind::HtmlBlock: kind = BlockKind::HtmlBlock; break;
        case Kind::Table: kind = BlockKind::Table; break;
        default: break;
        }
        if (block.blockQuoteDepth > 0 && block.kind == Kind::Paragraph)
            kind = BlockKind::BlockQuote;
        BlockIdentity identity;
        BlockId blockId;
        if (isFirst && reuseTarget) {
            identity = *after;
            blockId = target;
        } else {
            auto inserted = listResult.value()->insertAfter(after, 0);
            if (!inserted.ok()) return abort(inserted.error());
            if (!inserted.value().insertedBlock)
                return abort(CollabText::Error(CollabText::ErrorCode::invalid_identity,
                    "paste insertion returned no block identity"));
            auto local = std::move(inserted).value();
            auto added = stager.add(local);
            if (!added.ok()) return abort(added.error());
            const auto listId = local.identity;
            event.groups.push_back({local.scope.stream, OperationPrimitive::idList,
                                    listId, {listId}});
            identity = *local.insertedBlock;
            const auto converted = fromVNextIdentity(identity);
            if (!converted) return abort(CollabText::Error(
                CollabText::ErrorCode::invalid_identity,
                "paste insertion returned an invalid block identity"));
            blockId = *converted;
            after = identity;
            insertedBlocks.push_back(blockId);
            touched.push_back(blockId);
        }
        auto encodedKind = MarkoffMapValueCodec::encodeBlockKind(kind);
        if (!encodedKind.ok()) return abort(encodedKind.error());
        auto kindLocal = kindResult.value()->set({}, encodedKind.value(), identity);
        if (!kindLocal.ok()) return abort(kindLocal.error());
        auto kindStaged = stageMap(kindLocal.value());
        if (!kindStaged.ok()) return abort(kindStaged.error());
        auto attrs = writeAttrs(identity, block, kind);
        if (!attrs.ok()) return abort(attrs.error());

        QByteArray content = initialTextForSourceBlock(body, block);
        if (kind == BlockKind::CodeBlock && !block.codeText.isEmpty())
            content = block.codeText.toUtf8();
        if (!content.isEmpty()) {
            auto text = stageText(blockId, identity, 0, 0, content);
            if (!text.ok()) return abort(text.error());
        }
        caretOffset = quint32(content.size());
        lastBlock = blockId;
        isFirst = false;
    }
    if (!tail.isEmpty()) {
        const auto identity = toVNextIdentity(lastBlock);
        if (!identity) return abort(CollabText::Error(CollabText::ErrorCode::invalid_identity,
            "paste tail target has no structural identity"));
        auto append = stageText(lastBlock, *identity, caretOffset, caretOffset, tail);
        if (!append.ok()) return abort(append.error());
    }

    auto listHistory = LocalHistoryRecord{listResult.value()->scope(),
        d->lifecycle->storage().actor(), listResult.value()->localHistorySnapshot()};
    auto owner = ownerHistoryForAction(recovery, d->lifecycle->storage(), document,
        scope.epoch, actionId, std::move(event), listHistory);
    if (!owner.ok()) return abort(owner.error());
    for (const auto& history : textHistories) {
        const auto found = std::find_if(owner.value().streams.begin(), owner.value().streams.end(),
            [&](const LocalHistoryRecord& old) {
                return old.scope == history.scope && old.actor == history.actor;
            });
        if (found == owner.value().streams.end()) owner.value().streams.push_back(history);
        else found->snapshot = history.snapshot;
    }
    for (const auto block : touched) emit tentativeBlockChanged(block);
    const auto publication = d->lifecycle->publishStagedAction(stager, owner.value());
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        for (const auto block : textBlocks) ++d->blockEditSequences[block];
        if (!insertedBlocks.empty() || !removedBlocks.empty())
            ++d->structuralEditSequence;
        for (const auto block : touched) {
            emit tentativeBlockCommitted(block);
            emit blocksChanged({block});
        }
        const auto afterBlocks = iterateBlocks();
        for (const auto block : insertedBlocks) {
            const auto row = std::find(afterBlocks.begin(), afterBlocks.end(), block);
            if (row != afterBlocks.end()) emit blockInserted(block,
                static_cast<int>(std::distance(afterBlocks.begin(), row)));
        }
        for (const auto block : removedBlocks) {
            const auto row = std::find(visible.begin(), visible.end(), block);
            if (row != visible.end())
                emit blockRemoved(block,
                    static_cast<int>(std::distance(visible.begin(), row)));
        }
        emit d2DocumentChanged();
        emit documentChanged();
    } else if (publication.state == ActionPublicationState::absent
               || publication.state == ActionPublicationState::rejected) {
        for (const auto block : touched) emit tentativeBlockReverted(block);
    }
    return {publication, lastBlock, caretOffset};
}

VNext::ActionPublicationResult MarkoffDocument::setFrontmatterValue(
    const QByteArray& key, const QByteArray& value)
{
    using namespace VNext;
    const auto publication = publishMapValue(*d->lifecycle, "frontmatter",
        key, std::nullopt, value);
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        emit d2DocumentChanged();
        emit documentChanged();
    }
    return publication;
}

VNext::ActionPublicationResult MarkoffDocument::removeFrontmatterValue(
    const QByteArray& key)
{
    using namespace VNext;
    const auto publication = publishMapValue(*d->lifecycle, "frontmatter",
        key, std::nullopt, {}, true);
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        emit d2DocumentChanged();
        emit documentChanged();
    }
    return publication;
}

VNext::ActionPublicationResult MarkoffDocument::setLinkReference(
    const LinkRefId& id, const LinkRefValue& value)
{
    using namespace VNext;
    auto encoded = MarkoffMapValueCodec::encodeLinkRef(value);
    if (!encoded.ok())
        return {ActionPublicationState::rejected, std::nullopt, encoded.error()};
    const auto publication = publishMapValue(*d->lifecycle, "link-ref",
        id, std::nullopt, encoded.value());
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        emit d2DocumentChanged();
        emit documentChanged();
    }
    return publication;
}

VNext::ActionPublicationResult MarkoffDocument::removeLinkReference(
    const LinkRefId& id)
{
    using namespace VNext;
    const auto publication = publishMapValue(*d->lifecycle, "link-ref", id,
                                              std::nullopt, {}, true);
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        emit d2DocumentChanged();
        emit documentChanged();
    }
    return publication;
}

std::optional<LinkRefValue> MarkoffDocument::linkReference(
    const LinkRefId& id) const
{
    const auto* map = d->lifecycle->projection().mapStream("link-ref");
    if (!map)
        return std::nullopt;
    const auto encoded = map->get(id);
    if (!encoded)
        return std::nullopt;
    const auto decoded = VNext::MarkoffMapValueCodec::decodeLinkRef(*encoded);
    return decoded.ok() ? std::optional<LinkRefValue>(decoded.value()) : std::nullopt;
}

void MarkoffDocument::toggleListItemChecked(BlockId id)
{
    if (blockKind(id) != BlockKind::ListItem)
        return;
    const auto attrs = blockAttrs(id);
    const auto found = attrs.constFind(AttrNames::Checked);
    if (found == attrs.cend())
        return;
    const auto* checked = std::get_if<bool>(&found.value());
    if (!checked)
        return;
    (void)setBlockAttribute(id, AttrNames::Checked, !*checked);
}

VNext::ActionPublicationResult MarkoffDocument::applyStructural(
    const StructuralOp& op)
{
    using namespace VNext;
    const auto& scope = d->lifecycle->storage().scope();
    const std::string document = scope.documentKey.toUtf8().toStdString();
    if (!d->lifecycle->canStartOrdinaryAction())
        return {ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::awaiting_state,
                "document lifecycle does not permit a structural action")};

    auto recoveryResult = d->lifecycle->storage().readRecoveryState();
    if (!recoveryResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, recoveryResult.error()};
    auto recovery = std::move(recoveryResult).value();
    auto sequence = nextDeliverySequence(recovery, document, scope.epoch);
    if (!sequence.ok())
        return {ActionPublicationState::rejected, std::nullopt, sequence.error()};

    const QByteArray actionId = QUuid::createUuid().toRfc4122();
    ActionStager stager(document, scope.epoch, sequence.value(), actionId,
                        ActionKind::edit);
    ActionHistoryEvent event;
    event.kind = ActionKind::edit;
    std::optional<BlockId> changedBlock;
    std::optional<LocalListOperation> listOperation;
    std::optional<LocalMapOperation> mapOperation;
    bool insertedBlock = false;
    std::optional<int> removedRow;

    auto listResult = d->lifecycle->stagingIdList();
    if (!listResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, listResult.error()};
    ScopedIdListStream* list = listResult.value();
    auto mapResult = d->lifecycle->stagingMapStream(
        MapScope{document, "kind-tag", scope.epoch});
    if (!mapResult.ok())
        return {ActionPublicationState::rejected, std::nullopt, mapResult.error()};
    ReplicatedMapStream* kindMap = mapResult.value();

    const auto encodedKind = [](BlockKind kind) {
        return MarkoffMapValueCodec::encodeBlockKind(kind);
    };
    const auto stageMap = [&](CollabText::Result<LocalMapOperation> operation)
        -> CollabText::Result<void> {
        if (!operation.ok())
            return operation.error();
        mapOperation = std::move(operation).value();
        return stager.add(*mapOperation);
    };

    const auto stagedStructural = std::visit([&](const auto& payload)
        -> CollabText::Result<void> {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, StructuralOp::InsertEntry>) {
            std::optional<BlockIdentity> after;
            if (!payload.afterBlockId.isNull()) {
                after = toVNextIdentity(payload.afterBlockId);
                if (!after)
                    return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                        "insert anchor has no structural identity");
            }
            auto inserted = list->insertAfter(after, 0);
            if (!inserted.ok()) return inserted.error();
            listOperation = std::move(inserted).value();
            if (!listOperation->insertedBlock)
                return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                    "insert did not return a full block identity");
            changedBlock = fromVNextIdentity(*listOperation->insertedBlock);
            insertedBlock = true;
            auto stagedList = stager.add(*listOperation);
            if (!stagedList.ok()) return stagedList.error();
            auto value = encodedKind(payload.kind);
            if (!value.ok()) return value.error();
            return stageMap(kindMap->set({}, value.value(),
                                         listOperation->insertedBlock));
        } else if constexpr (std::is_same_v<T, StructuralOp::RemoveEntry>) {
            const auto identity = toVNextIdentity(payload.blockId);
            if (!identity)
                return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                    "removed block has no structural identity");
            const auto priorBlocks = iterateBlocks();
            const auto prior = std::find(priorBlocks.begin(), priorBlocks.end(), payload.blockId);
            if (prior != priorBlocks.end())
                removedRow = static_cast<int>(std::distance(priorBlocks.begin(), prior));
            auto removed = list->remove(*identity);
            if (!removed.ok()) return removed.error();
            listOperation = std::move(removed).value();
            changedBlock = payload.blockId;
            auto stagedList = stager.add(*listOperation);
            if (!stagedList.ok()) return stagedList.error();
            return stageMap(kindMap->remove({}, *identity));
        } else {
            const auto identity = toVNextIdentity(payload.blockId);
            if (!identity)
                return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                    "changed block has no structural identity");
            changedBlock = payload.blockId;
            auto value = encodedKind(payload.newKind);
            if (!value.ok()) return value.error();
            return stageMap(kindMap->set({}, value.value(), identity));
        }
    }, op.payload);

    if (!stagedStructural.ok()) {
        if (!stager.bundle().operations.empty())
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
        return {ActionPublicationState::rejected, std::nullopt,
                stagedStructural.error()};
    }
    if (listOperation) {
        const auto identity = listOperation->identity;
        event.groups.push_back({listOperation->scope.stream,
            OperationPrimitive::idList, identity, {identity}});
    }
    if (mapOperation) {
        auto association = toHistoryAssociation(*mapOperation);
        if (!association.ok()) {
            (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
            return {ActionPublicationState::rejected, std::nullopt, association.error()};
        }
        event.groups.push_back(std::move(association).value());
    }

    std::optional<LocalHistoryRecord> listHistory;
    if (listOperation) {
        listHistory = LocalHistoryRecord{list->scope(),
            d->lifecycle->storage().actor(), list->localHistorySnapshot()};
    }
    auto ownerHistory = ownerHistoryForAction(recovery, d->lifecycle->storage(),
        document, scope.epoch, actionId, std::move(event), std::move(listHistory));
    if (!ownerHistory.ok()) {
        (void)d->lifecycle->publishStagedAction(stager, OwnerHistorySnapshot{});
        return {ActionPublicationState::rejected, std::nullopt, ownerHistory.error()};
    }

    if (changedBlock)
        emit tentativeBlockChanged(*changedBlock);
    const auto publication = d->lifecycle->publishStagedAction(
        stager, ownerHistory.value());
    if (publication.state == ActionPublicationState::committed
        || publication.state == ActionPublicationState::committedHistoryPending) {
        d->sourcePristine = false;
        ++d->structuralEditSequence;
        if (changedBlock) {
            emit tentativeBlockCommitted(*changedBlock);
            emit blocksChanged({*changedBlock});
            if (insertedBlock) {
                const auto currentBlocks = iterateBlocks();
                const auto row = std::find(currentBlocks.begin(), currentBlocks.end(),
                                           *changedBlock);
                if (row != currentBlocks.end())
                    emit blockInserted(*changedBlock,
                        static_cast<int>(std::distance(currentBlocks.begin(), row)));
            } else if (removedRow) {
                emit blockRemoved(*changedBlock, *removedRow);
            }
        }
        emit d2DocumentChanged();
        emit documentChanged();
    } else if (changedBlock
        && (publication.state == ActionPublicationState::absent
            || publication.state == ActionPublicationState::rejected)) {
        emit tentativeBlockReverted(*changedBlock);
    }
    return publication;
}

std::optional<QByteArray> MarkoffDocument::frontmatterValue(const QByteArray& key) const
{
    const auto *map = d->lifecycle->projection().mapStream("frontmatter");
    return map ? map->get(key) : std::nullopt;
}

QByteArray MarkoffDocument::serializeForSave() const
{
    if (d->sourcePristine && d->hasOriginalMarkdown)
        return d->originalMarkdown;

    // A frontmatter-only edit must not canonicalize the Markdown body. In
    // particular, a metadata update beside setext headings or CRLF content
    // should patch only the frontmatter span in the accepted source snapshot.
    if (d->hasOriginalMarkdown && !d->sourceBlockOrder.empty()
        && d->sourceBlocks.size() == qsizetype(d->sourceBlockOrder.size())
        && iterateBlocks() == d->sourceBlockOrder) {
        bool frontmatterOnlyHistory = true;
        bool foundSourceSnapshot = false;
        const auto& scope = d->lifecycle->storage().scope();
        const std::string document = scope.documentKey.toUtf8().toStdString();
        const auto& acceptedRecords = d->lifecycle->recovery().acceptedRecords;
        size_t lastSourceAction = acceptedRecords.size();
        for (size_t i = 0; i < acceptedRecords.size(); ++i) {
            const auto& accepted = acceptedRecords[i];
            if (accepted.format != QByteArrayLiteral("MAB/3")) continue;
            const auto bundle = VNext::ActionBundleCodec::decode(
                accepted.bytes, document, scope.epoch);
            if (!bundle.ok()) { frontmatterOnlyHistory = false; break; }
            if (!bundle.value().sourceSnapshot.isEmpty())
                lastSourceAction = i;
        }
        for (size_t i = 0; frontmatterOnlyHistory && i < acceptedRecords.size(); ++i) {
            if (i < lastSourceAction) continue;
            const auto& accepted = acceptedRecords[i];
            if (accepted.format != QByteArrayLiteral("MAB/3")) continue;
            const auto bundle = VNext::ActionBundleCodec::decode(
                accepted.bytes, document, scope.epoch);
            if (!bundle.ok()) { frontmatterOnlyHistory = false; break; }
            if (!bundle.value().sourceSnapshot.isEmpty()) {
                foundSourceSnapshot = true;
                continue;
            }
            for (const auto& operation : bundle.value().operations) {
                if (operation.primitive != VNext::OperationPrimitive::markoffMap
                    || operation.stream != "markoff/map/v1/frontmatter") {
                    frontmatterOnlyHistory = false;
                    break;
                }
            }
            if (!frontmatterOnlyHistory) break;
        }
        for (BlockId id : d->sourceBlockOrder) {
            const auto source = d->sourceBlocks.constFind(id);
            if (source == d->sourceBlocks.cend()
                || blockText(id) != source->originalText) {
                frontmatterOnlyHistory = false;
                break;
            }
        }
        if (frontmatterOnlyHistory && foundSourceSnapshot) {
            const QString original = QString::fromUtf8(d->originalMarkdown);
            const auto extracted = Markoff::Document::extract(original);
            const auto* frontmatter = d->lifecycle->projection().mapStream("frontmatter");
            QByteArray yaml;
            if (frontmatter) {
                if (const auto raw = frontmatter->get(QByteArrayLiteral("raw")))
                    yaml = *raw;
                for (const auto& entry : frontmatter->values()) {
                    if (entry.targetBlock || entry.key == QByteArrayLiteral("raw"))
                        continue;
                    if (!yaml.isEmpty() && !yaml.endsWith('\n')) yaml.append('\n');
                    yaml += entry.key + QByteArrayLiteral(": ") + entry.value;
                }
            }
            if (yaml == extracted.frontmatter.toUtf8())
                return d->originalMarkdown;

            qsizetype spanEnd = 0;
            if (extracted.frontmatterBlockEnd > 0) {
                spanEnd = original.left(extracted.frontmatterBlockEnd)
                              .toUtf8().size();
            }
            if (spanEnd < 0 || spanEnd > d->originalMarkdown.size())
                frontmatterOnlyHistory = false;
            if (frontmatterOnlyHistory) {
                const QByteArray originalFrontmatter = d->originalMarkdown.left(spanEnd);
                QByteArray eol = originalFrontmatter.contains(QByteArrayLiteral("\r\n"))
                    ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n");
                QByteArray replacement;
                if (!yaml.isEmpty()) {
                    replacement = QByteArrayLiteral("---") + eol + yaml;
                    if (!replacement.endsWith(eol)) replacement += eol;
                    replacement += QByteArrayLiteral("---") + eol;
                }
                QByteArray output = d->originalMarkdown;
                output.replace(0, spanEnd, replacement);
                return output;
            }
        }
    }

    // Text-only actions leave the source's block boundaries and separators
    // authoritative. Replace only blocks whose accepted text differs from
    // their imported text; untouched Markdown remains byte-for-byte intact.
    if (d->hasOriginalMarkdown && !d->sourceBlockOrder.empty()
        && d->sourceBlocks.size() == qsizetype(d->sourceBlockOrder.size())) {
        bool textOnlyHistory = true;
        bool foundSourceSnapshot = false;
        const auto& scope = d->lifecycle->storage().scope();
        const std::string document = scope.documentKey.toUtf8().toStdString();
        const auto& acceptedRecords = d->lifecycle->recovery().acceptedRecords;
        size_t lastSourceAction = acceptedRecords.size();
        for (size_t i = 0; i < acceptedRecords.size(); ++i) {
            const auto& accepted = acceptedRecords[i];
            if (accepted.format != QByteArrayLiteral("MAB/3")) continue;
            const auto bundle = VNext::ActionBundleCodec::decode(
                accepted.bytes, document, scope.epoch);
            if (!bundle.ok()) { textOnlyHistory = false; break; }
            if (!bundle.value().sourceSnapshot.isEmpty())
                lastSourceAction = i;
        }
        for (size_t i = 0; textOnlyHistory && i < acceptedRecords.size(); ++i) {
            if (i < lastSourceAction) continue;
            const auto& accepted = acceptedRecords[i];
            if (accepted.format != QByteArrayLiteral("MAB/3")) continue;
            const auto bundle = VNext::ActionBundleCodec::decode(
                accepted.bytes, document, scope.epoch);
            if (!bundle.ok()) { textOnlyHistory = false; break; }
            if (!bundle.value().sourceSnapshot.isEmpty()) {
                foundSourceSnapshot = true;
                continue;
            }
            for (const auto& operation : bundle.value().operations)
                if (operation.primitive != VNext::OperationPrimitive::text) {
                    textOnlyHistory = false;
                    break;
                }
            if (!textOnlyHistory) break;
        }
        const auto blocks = iterateBlocks();
        if (textOnlyHistory && foundSourceSnapshot
            && blocks == d->sourceBlockOrder) {
            struct Patch { qsizetype start; qsizetype end; QByteArray bytes; };
            std::vector<Patch> patches;
            for (BlockId id : blocks) {
                const auto source = d->sourceBlocks.constFind(id);
                if (source == d->sourceBlocks.cend()) {
                    textOnlyHistory = false;
                    break;
                }
                const auto currentText = blockText(id);
                if (currentText == source->originalText) continue;
                QByteArray replacement = serializeAcceptedBlock(*this, id);
                if (source->end > source->start
                    && source->end <= d->originalMarkdown.size()) {
                    const auto originalSlice = d->originalMarkdown.mid(
                        source->start, source->end - source->start);
                    // Keep a line ending inside the parser's block span in its
                    // original form. Separators outside the span remain untouched.
                    if (originalSlice.endsWith("\r\n")) replacement += "\r\n";
                    else if (originalSlice.endsWith('\n')) replacement += '\n';
                }
                patches.push_back({source->start, source->end, std::move(replacement)});
            }
            if (textOnlyHistory) {
                std::sort(patches.begin(), patches.end(), [](const Patch& left,
                                                              const Patch& right) {
                    return left.start > right.start;
                });
                QByteArray output = d->originalMarkdown;
                qsizetype previousStart = output.size();
                for (const auto& patch : patches) {
                    if (patch.start < 0 || patch.end < patch.start
                        || patch.end > previousStart) {
                        textOnlyHistory = false;
                        break;
                    }
                    output.replace(patch.start, patch.end - patch.start, patch.bytes);
                    previousStart = patch.start;
                }
                if (textOnlyHistory) return output;
            }
        }
    }

    QByteArray output;
    const auto* frontmatter = d->lifecycle->projection().mapStream("frontmatter");
    if (frontmatter) {
        QByteArray yaml;
        if (const auto raw = frontmatter->get(QByteArrayLiteral("raw")))
            yaml = *raw;
        for (const auto& entry : frontmatter->values()) {
            if (entry.targetBlock || entry.key == QByteArrayLiteral("raw"))
                continue;
            if (!yaml.isEmpty() && !yaml.endsWith('\n'))
                yaml.append('\n');
            yaml += entry.key + QByteArrayLiteral(": ") + entry.value;
        }
        if (!yaml.isEmpty()) {
            output += QByteArrayLiteral("---\n") + yaml;
            if (!output.endsWith('\n')) output.append('\n');
            output += QByteArrayLiteral("---\n\n");
        }
    }

    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    const auto blocks = iterateBlocks();
    for (size_t index = 0; index < blocks.size(); ++index) {
        const auto id = blocks[index];
        const auto kind = blockKind(id);
        const auto attrs = blockAttrs(id);
        if (kind == BlockKind::ListItem) {
            const qsizetype itemStart = output.size();
            int indent = 0;
            if (const auto it = attrs.constFind(AttrNames::IndentLevel);
                it != attrs.cend()) {
                if (const auto* value = std::get_if<int>(&it.value()))
                    indent = std::clamp(*value, 0, 64);
            }
            output += QByteArray(indent, ' ');
            QString style = QStringLiteral("minus");
            if (const auto it = attrs.constFind(AttrNames::MarkerStyle);
                it != attrs.cend()) {
                if (const auto* value = std::get_if<QString>(&it.value()))
                    style = *value;
            }
            int number = 1;
            if (const auto it = attrs.constFind(AttrNames::MarkerNumber);
                it != attrs.cend()) {
                if (const auto* value = std::get_if<int>(&it.value()))
                    number = *value;
            }
            if (style == QStringLiteral("dot"))
                output += QByteArray::number(number) + '.';
            else if (style == QStringLiteral("paren"))
                output += QByteArray::number(number) + ')';
            else if (style == QStringLiteral("plus")) output += '+';
            else if (style == QStringLiteral("star")) output += '*';
            else if (style == QStringLiteral("task")) {
                bool checked = false;
                if (const auto it = attrs.constFind(AttrNames::Checked);
                    it != attrs.cend()) {
                    if (const auto* value = std::get_if<bool>(&it.value()))
                        checked = *value;
                }
                output += checked ? QByteArrayLiteral("- [x]")
                                  : QByteArrayLiteral("- [ ]");
            } else output += '-';
            output += ' ';
            if (const auto it = attrs.constFind(AttrNames::BlockQuoteDepth);
                it != attrs.cend()) {
                if (const auto* value = std::get_if<int>(&it.value())) {
                    QByteArray prefix;
                    for (int level = 0; level < std::clamp(*value, 0, 16); ++level)
                        prefix += "> ";
                    if (!prefix.isEmpty())
                        output.insert(itemStart, prefix);
                }
            }
            output += blockText(id);
        } else if (d->serializerRegistry) {
            output += d->serializerRegistry->serialize(kind, attrs, blockText(id));
        } else {
            output += BuiltinBlockSerializerRegistry::instance().get(kind)(
                kind, attrs, blockText(id));
        }
        if (index + 1 < blocks.size()) {
            bool loose = true;
            if (kind == BlockKind::ListItem
                && blockKind(blocks[index + 1]) == BlockKind::ListItem) {
                const auto it = attrs.constFind(AttrNames::LooseRun);
                if (it != attrs.cend()) {
                    if (const auto* value = std::get_if<bool>(&it.value()))
                        loose = *value;
                }
            }
            output += (kind == BlockKind::ListItem
                       && blockKind(blocks[index + 1]) == BlockKind::ListItem
                       && !loose) ? QByteArrayLiteral("\n")
                                  : QByteArrayLiteral("\n\n");
        }
    }
    if (!blocks.empty() && !output.endsWith('\n'))
        output.append('\n');

    const auto* footnotes = d->lifecycle->projection().mapStream("footnote-def");
    if (footnotes) {
        for (const auto& entry : footnotes->values()) {
            if (entry.targetBlock) continue;
            if (!output.isEmpty() && !output.endsWith("\n\n"))
                output += '\n';
            output += QByteArrayLiteral("[^") + entry.key + QByteArrayLiteral("]: ")
                + entry.value + '\n';
        }
    }

    const auto* linkRefs = d->lifecycle->projection().mapStream("link-ref");
    if (linkRefs) {
        for (const auto& entry : linkRefs->values()) {
            if (entry.targetBlock) continue;
            const auto ref = VNext::MarkoffMapValueCodec::decodeLinkRef(entry.value);
            if (!ref.ok()) continue;
            if (ref.value().url.isEmpty() && d->hasOriginalMarkdown) {
                const auto lines = d->originalMarkdown.split('\n');
                const auto original = std::find_if(lines.cbegin(), lines.cend(),
                    [&](const QByteArray& line) { return line.startsWith(entry.key); });
                if (original != lines.cend()) {
                    if (!output.isEmpty() && !output.endsWith("\n\n")) output += '\n';
                    output += *original + '\n';
                    continue;
                }
            }
            if (!output.isEmpty() && !output.endsWith("\n\n")) output += '\n';
            output += '[' + entry.key + QByteArrayLiteral("]: ")
                + ref.value().url.toUtf8();
            if (!ref.value().title.isEmpty())
                output += QByteArrayLiteral(" \"") + ref.value().title.toUtf8()
                    + QByteArrayLiteral("\"");
            output += '\n';
        }
    }
    return output;
}

CollabText::Result<MarkoffDocument::ExportSnapshot>
MarkoffDocument::snapshotForExport() const
{
    const auto& revision = d->lifecycle->recovery().acceptedRevision;
    if (revision.token.isEmpty())
        return CollabText::Error(CollabText::ErrorCode::history_unavailable,
            "document has no accepted revision to bind to an export");
    return ExportSnapshot{serializeForSave(), revision};
}

CollabText::Result<void> MarkoffDocument::acknowledgeExport(
    const ExportSnapshot& snapshot)
{
    if (snapshot.revision != d->lifecycle->recovery().acceptedRevision)
        return CollabText::Error(CollabText::ErrorCode::pin_conflict,
            "export snapshot belongs to a stale accepted revision");
    if (snapshot.markdown != serializeForSave())
        return CollabText::Error(CollabText::ErrorCode::pin_conflict,
            "export bytes differ from the current document projection");
    return d->lifecycle->storage().acknowledgeMarkdownExport(snapshot.revision);
}

bool MarkoffDocument::save(const QString& path)
{
    const auto snapshot = snapshotForExport();
    if (!snapshot.ok())
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(snapshot.value().markdown) != snapshot.value().markdown.size()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
        return false;
    return acknowledgeExport(snapshot.value()).ok();
}

quint64 MarkoffDocument::blockEditSequence(BlockId id) const
{
    return d->blockEditSequences.value(id, 0);
}

quint64 MarkoffDocument::d2EditSequence() const noexcept
{
    quint64 sequence = d->structuralEditSequence;
    for (const auto value : d->blockEditSequences) sequence += value;
    return sequence;
}

quint64 MarkoffDocument::structuralEditSequence() const noexcept
{
    return d->structuralEditSequence;
}

QList<SourceSpan> MarkoffDocument::inlineSpansFor(BlockId id) const
{
    const auto blocks = iterateBlocks();
    if (std::find(blocks.begin(), blocks.end(), id) == blocks.end()) return {};
    const quint64 sequence = blockEditSequence(id);
    const auto cached = d->inlineSpanCache.constFind(id);
    if (cached != d->inlineSpanCache.cend() && cached->first == sequence)
        return cached->second;
    auto spans = Markoff::inlineSpansFor(blockText(id));
    d->inlineSpanCache.insert(id, qMakePair(sequence, spans));
    return spans;
}

QByteArray MarkoffDocument::flatView() const
{
    QByteArray result;
    const auto blocks = iterateBlocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (i != 0)
            result.append("\n\n");
        result.append(blockText(blocks[i]));
    }
    return result;
}

QByteArray MarkoffDocument::widgetFlatView() const
{
    QByteArray result;
    const auto blocks = iterateBlocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (i != 0)
            result.append('\n');
        result.append(blockText(blocks[i]));
    }
    return result;
}

QByteArray MarkoffDocument::reconstructFlatMarkdown(const QJsonArray& blocks)
{
    QByteArray result;
    bool first = true;
    for (const auto& value : blocks) {
        const QJsonObject block = value.toObject();
        if (!first)
            result.append("\n\n");
        first = false;
        const QString kind = block.value(QStringLiteral("kind")).toString();
        const QByteArray text = block.value(QStringLiteral("text")).toString().toUtf8();
        if (kind != QStringLiteral("list-item")) {
            result.append(text);
            continue;
        }
        const QJsonObject attrs = block.value(QStringLiteral("attrs")).toObject();
        const QString style = attrs.value(QStringLiteral("markerStyle"))
                                  .toString(QStringLiteral("minus"));
        const int markerNumber = attrs.value(QStringLiteral("markerNumber")).toInt(1);
        const bool checked = attrs.value(QStringLiteral("checked")).toBool();
        if (style == QStringLiteral("dot")) result.append(QByteArray::number(markerNumber) + '.');
        else if (style == QStringLiteral("paren")) result.append(QByteArray::number(markerNumber) + ')');
        else if (style == QStringLiteral("plus")) result.append('+');
        else if (style == QStringLiteral("star")) result.append('*');
        else if (style == QStringLiteral("task")) result.append(checked ? "- [x]" : "- [ ]");
        else result.append('-');
        result.append(" ");
        result.append(text);
    }
    return result;
}

VNext::ActionPublicationResult MarkoffDocument::applyStructuredPaste(
    quint32 startByte, quint32 endByte, const QJsonArray& blocks)
{
    const auto visible = iterateBlocks();
    if (visible.empty())
        return {VNext::ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::not_found,
                "structured paste requires a visible target block")};
    uint32_t totalBytes = 0;
    for (const auto block : visible)
        totalBytes += static_cast<uint32_t>(blockText(block).size());
    const uint32_t lo = std::min(startByte, endByte);
    const uint32_t hi = std::min(std::max(startByte, endByte), totalBytes);
    const uint32_t start = std::min(lo, totalBytes);
    const bool cursorEdit = start == hi;

    int startIndex = -1;
    int endIndex = -1;
    uint32_t startOffset = 0;
    uint32_t endOffset = 0;
    uint32_t cursor = 0;
    for (size_t i = 0; i < visible.size(); ++i) {
        const uint32_t size = static_cast<uint32_t>(blockText(visible[i]).size());
        const uint32_t blockEnd = cursor + size;
        if (startIndex < 0) {
            if (cursorEdit) {
                if (cursor == start) {
                    startIndex = static_cast<int>(i);
                    startOffset = 0;
                } else if (start < blockEnd) {
                    startIndex = static_cast<int>(i);
                    startOffset = start - cursor;
                }
            } else if (start <= blockEnd) {
                startIndex = static_cast<int>(i);
                startOffset = start - cursor;
            }
        }
        if (!cursorEdit && hi <= blockEnd) {
            endIndex = static_cast<int>(i);
            endOffset = hi - cursor;
            break;
        }
        cursor = blockEnd;
    }
    if (cursorEdit && startIndex >= 0) {
        endIndex = startIndex;
        endOffset = startOffset;
    }
    if (cursorEdit && startIndex < 0) {
        startIndex = static_cast<int>(visible.size() - 1);
        startOffset = static_cast<uint32_t>(blockText(visible.back()).size());
        endIndex = startIndex;
        endOffset = startOffset;
    }
    if (startIndex < 0 || endIndex < startIndex)
        return {VNext::ActionPublicationState::rejected, std::nullopt,
            CollabText::Error(CollabText::ErrorCode::off_end,
                "structured paste range could not be resolved")};

    const QByteArray markdown = reconstructFlatMarkdown(blocks);
    return pasteMarkdownReplacingRange(visible[static_cast<size_t>(startIndex)],
        startOffset, visible[static_cast<size_t>(endIndex)], endOffset,
        markdown).publication;
}

MarkoffDocument::~MarkoffDocument() = default;

} // namespace Markoff

