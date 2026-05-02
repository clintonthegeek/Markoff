// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Session.h>

#include <markoff-parser/Document.h>

#include <algorithm>

#include "MarkoffDocumentPrivate.h"
#include "AnchorConversion.h"
#include "BlockAnchorComputation.h"

namespace Markoff {

MarkoffDocument::MarkoffDocument(quint16 replicaId, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(replicaId))
{
    // Runtime registration so QList<BlockAnchor> survives any cross-thread
    // QueuedConnection slot. Q_DECLARE_METATYPE on its own is compile-time
    // only; queued connections need this.
    qRegisterMetaType<Markoff::BlockAnchor>("Markoff::BlockAnchor");
    qRegisterMetaType<QList<Markoff::BlockAnchor>>("QList<Markoff::BlockAnchor>");

    QObject::connect(&d->parsePool, &Markoff::Parse::Detail::ParsePool::parseReady,
                     this, [this](const Markoff::Document *p) {
                         d->latestParse.reset(p);
                         ++d->parseSequence;

                         // Compute BlockAnchors against the CURRENT CRDT buffer (main-thread,
                         // synchronous). Anchors are derived from the parser's tree-sitter
                         // top-level-block enumeration (`Markoff::Document::topLevelBlocks()`),
                         // matching what view-qml's BlockWalker consumes — so anchors[i] and
                         // the view's records[i] always describe the same block. The parse
                         // itself reflects the buffer at parse-schedule time; if intervening
                         // edits moved a block's first-byte char, that block's BlockAnchor
                         // for one parse cycle identifies a slightly different character —
                         // the next parse cycle delivers a corrected anchor. See spec §3.
                         auto bundle = Markoff::Detail::computeBlockAnchors(*this, p);
                         d->latestBlockAnchors = std::move(bundle.anchors);
                         d->latestBlockRanges  = std::move(bundle.ranges);

                         Q_EMIT parseUpdated(p, d->parseSequence, d->latestBlockAnchors);
                     });
}

MarkoffDocument::~MarkoffDocument() = default;

QByteArray MarkoffDocument::toMarkdownUtf8() const
{
    const std::string s = d->buffer.text();
    return QByteArray::fromStdString(s);
}

QString MarkoffDocument::toMarkdown() const
{
    return QString::fromUtf8(toMarkdownUtf8());
}

quint32 MarkoffDocument::visibleLength() const
{
    return d->buffer.visible_length();
}

const Markoff::Document *MarkoffDocument::parsedDocument() const
{
    return d->latestParse.get();
}

bool MarkoffDocument::parseIsPending() const
{
    return d->parsePool.isPending();
}

quint16 MarkoffDocument::replicaId() const
{
    return d->replicaId;
}

CollabText::Crdt::Global MarkoffDocument::version() const
{
    return d->buffer.version();
}

quint64 MarkoffDocument::editSequence() const noexcept
{
    return d->editSequence;
}

quint64 MarkoffDocument::parseSequence() const noexcept
{
    return d->parseSequence;
}

// applyLocalEdit, undo/redo, applyRemoteOps, resetContent, anchorAt,
// resolveAnchor, sessions, collectGarbage, compact, coalescing setters
// are filled in subsequent tasks (10-23).

CollabText::Crdt::Operation
MarkoffDocument::applyLocalEdit(const QList<MarkoffEdit> &edits)
{
    ++d->editSequence;

    // Snapshot version so we can compute the resulting TextEdits afterwards.
    const CollabText::Crdt::Global oldVersion = d->buffer.version();

    // Translate QList<MarkoffEdit> to the std::vector<pair> + std::vector<string>
    // pair shape that Buffer::apply_local_edit expects.
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    std::vector<std::string> newTexts;
    ranges.reserve(static_cast<size_t>(edits.size()));
    newTexts.reserve(static_cast<size_t>(edits.size()));
    for (const MarkoffEdit &e : edits) {
        ranges.emplace_back(e.oldStart, e.oldEnd);
        newTexts.emplace_back(e.newText.constData(),
                              static_cast<size_t>(e.newText.size()));
    }

    const CollabText::Crdt::Operation op = d->buffer.apply_local_edit(ranges, newTexts);

    // Compute resulting visible-text edits from the buffer's diff API.
    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }

    if (!resultingEdits.isEmpty()) {
        Q_EMIT contentsChanged(resultingEdits);
        d->parsePool.schedule(toMarkdownUtf8());
    }

    return op;
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::undo()
{
    const CollabText::Crdt::Global oldVersion = d->buffer.version();
    auto op = d->buffer.undo();
    if (!op.has_value())
        return std::nullopt;

    ++d->editSequence;

    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }
    if (!resultingEdits.isEmpty()) {
        Q_EMIT contentsChanged(resultingEdits);
        d->parsePool.schedule(toMarkdownUtf8());
    }

    return op;
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::redo()
{
    const CollabText::Crdt::Global oldVersion = d->buffer.version();
    auto op = d->buffer.redo();
    if (!op.has_value())
        return std::nullopt;

    ++d->editSequence;

    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }
    if (!resultingEdits.isEmpty()) {
        Q_EMIT contentsChanged(resultingEdits);
        d->parsePool.schedule(toMarkdownUtf8());
    }

    return op;
}

int MarkoffDocument::undoDepth() const
{
    return static_cast<int>(d->buffer.undo_depth());
}

bool MarkoffDocument::coalesceLastUndo()
{
    return d->buffer.coalesce_last_undo();
}

void MarkoffDocument::applyRemoteOps(
    const std::vector<CollabText::Crdt::Operation> &ops)
{
    if (ops.empty())
        return;

    ++d->editSequence;

    const CollabText::Crdt::Global oldVersion = d->buffer.version();
    d->buffer.apply_ops(ops);

    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }

    if (!resultingEdits.isEmpty()) {
        Q_EMIT contentsChanged(resultingEdits);
        d->parsePool.schedule(toMarkdownUtf8());
    }
}

void MarkoffDocument::resetContent(const QByteArray &newContent, Origin origin)
{
    ++d->editSequence;

    switch (origin) {
    case Origin::FirstOpen:
    case Origin::ExternalReloadClean:
    case Origin::ExternalReloadResolved:
    case Origin::TestFixture: {
        d->buffer = CollabText::Crdt::Buffer(d->replicaId);
        if (!newContent.isEmpty()) {
            std::vector<std::pair<uint32_t, uint32_t>> ranges{ {0, 0} };
            std::vector<std::string> texts{
                std::string(newContent.constData(),
                            static_cast<size_t>(newContent.size())) };
            d->buffer.apply_local_edit(ranges, texts);
            // Clear the undo stack so this seed is not undoable.
            // Buffer doesn't expose a clear-undo API directly, but
            // set_max_undo_depth(0) trims the stack on shrink.  Reset
            // back to the default afterwards so subsequent local edits
            // can still be undone.
            const auto savedDepth = d->buffer.max_undo_depth();
            d->buffer.set_max_undo_depth(0);
            d->buffer.set_max_undo_depth(savedDepth);
        }
        break;
    }
    case Origin::UserRevertToSaved: {
        // Push one mega-edit that replaces the entire buffer with newContent.
        // Undo reverses the revert.
        const auto curLen = d->buffer.visible_length();
        std::vector<std::pair<uint32_t, uint32_t>> ranges{ {0u, curLen} };
        std::vector<std::string> texts{
            std::string(newContent.constData(),
                        static_cast<size_t>(newContent.size())) };
        d->buffer.apply_local_edit(ranges, texts);
        break;
    }
    }
    d->parsePool.scheduleReset(toMarkdownUtf8());
    Q_EMIT documentReloaded();
}

CollabText::Crdt::Anchor
MarkoffDocument::anchorAt(quint32 byteOffset, CollabText::Crdt::Bias bias) const
{
    return d->buffer.anchor_at(byteOffset, bias);
}

quint32 MarkoffDocument::resolveAnchor(const CollabText::Crdt::Anchor &a) const
{
    return d->buffer.resolve_anchor(a);
}

TextAnchor MarkoffDocument::textAnchorAt(quint32 byteOffset, bool rightBias) const
{
    using CollabText::Crdt::Bias;
    return Detail::toTextAnchor(
        anchorAt(byteOffset, rightBias ? Bias::Right : Bias::Left));
}

quint32 MarkoffDocument::resolveTextAnchor(const TextAnchor &t) const
{
    return resolveAnchor(Detail::toCrdtAnchor(t));
}

std::optional<BlockAnchor> MarkoffDocument::blockAnchorAt(int blockIndex) const
{
    if (blockIndex < 0) return std::nullopt;
    if (blockIndex >= d->latestBlockAnchors.size()) return std::nullopt;
    return d->latestBlockAnchors.at(blockIndex);
}

std::optional<std::pair<quint32, quint32>>
MarkoffDocument::blockByteRange(const BlockAnchor &b) const
{
    for (int i = 0; i < d->latestBlockAnchors.size(); ++i) {
        if (d->latestBlockAnchors[i] == b) {
            const auto &r = d->latestBlockRanges[i];
            return std::make_pair(r.startByte, r.endByte);
        }
    }
    return std::nullopt;
}

std::optional<BlockAnchor> MarkoffDocument::blockAt(const TextAnchor &t) const
{
    const quint32 byte = resolveTextAnchor(t);
    for (int i = 0; i < d->latestBlockRanges.size(); ++i) {
        const auto &r = d->latestBlockRanges[i];
        if (byte >= r.startByte && byte < r.endByte) {
            return d->latestBlockAnchors[i];
        }
    }
    return std::nullopt;
}

int MarkoffDocument::offsetInBlock(const BlockAnchor &b, const TextAnchor &t) const
{
    const auto rng = blockByteRange(b);
    if (!rng.has_value()) return 0;
    const quint32 byte = resolveTextAnchor(t);
    if (byte <= rng->first)  return 0;
    if (byte >= rng->second) return static_cast<int>(rng->second - rng->first);
    return static_cast<int>(byte - rng->first);
}

TextAnchor MarkoffDocument::textAnchorAt(const BlockAnchor &b,
                                         int offset,
                                         bool rightBias) const
{
    const auto rng = blockByteRange(b);
    if (!rng.has_value()) return TextAnchor{};
    const int clamped = std::max(0, std::min(offset,
        static_cast<int>(rng->second - rng->first)));
    return textAnchorAt(rng->first + static_cast<quint32>(clamped), rightBias);
}

Session *MarkoffDocument::createSession(const SessionParams &params)
{
    auto *s = new Session(this, params);
    d->sessions.append(s);
    Q_EMIT sessionCreated(s);
    return s;
}

void MarkoffDocument::destroySession(Session *s)
{
    if (!s) return;
    if (!d->sessions.removeOne(s)) return;
    Q_EMIT sessionDestroyed(s);
    s->deleteLater();
}

QList<Session *> MarkoffDocument::sessions() const
{
    return d->sessions;
}

Session *MarkoffDocument::sessionForParticipant(const QString &participantId) const
{
    for (Session *s : d->sessions)
        if (s->participantId() == participantId) return s;
    return nullptr;
}

qsizetype MarkoffDocument::collectGarbage()
{
    return static_cast<qsizetype>(d->buffer.collect_garbage());
}

qsizetype MarkoffDocument::compact(const CollabText::Crdt::Global &watermark)
{
    return static_cast<qsizetype>(d->buffer.compact(watermark));
}

void MarkoffDocument::setRenderPhaseTaps(Markoff::Render::RenderPhaseTaps *taps) noexcept
{
    d->parsePool.setRenderPhaseTaps(taps);
}

}  // namespace Markoff
