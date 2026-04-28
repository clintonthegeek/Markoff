// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/MarkoffDocument.h>

#include "MarkoffDocumentPrivate.h"

namespace Markoff {

MarkoffDocument::MarkoffDocument(quint16 replicaId, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(replicaId))
{
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
    // Filled in Task 19 (ParsePool integration).
    return nullptr;
}

bool MarkoffDocument::parseIsPending() const
{
    // Filled in Task 19.
    return false;
}

quint16 MarkoffDocument::replicaId() const
{
    return d->replicaId;
}

CollabText::Crdt::Global MarkoffDocument::version() const
{
    return d->buffer.version();
}

// applyLocalEdit, undo/redo, applyRemoteOps, resetContent, anchorAt,
// resolveAnchor, sessions, collectGarbage, compact, coalescing setters
// are filled in subsequent tasks (10-23).

CollabText::Crdt::Operation
MarkoffDocument::applyLocalEdit(const QList<MarkoffEdit> &edits)
{
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

    if (!resultingEdits.isEmpty())
        Q_EMIT contentsChanged(resultingEdits);

    return op;
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::undo()
{
    const CollabText::Crdt::Global oldVersion = d->buffer.version();
    auto op = d->buffer.undo();
    if (!op.has_value())
        return std::nullopt;

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
    if (!resultingEdits.isEmpty())
        Q_EMIT contentsChanged(resultingEdits);

    return op;
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::redo()
{
    const CollabText::Crdt::Global oldVersion = d->buffer.version();
    auto op = d->buffer.redo();
    if (!op.has_value())
        return std::nullopt;

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
    if (!resultingEdits.isEmpty())
        Q_EMIT contentsChanged(resultingEdits);

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

    if (!resultingEdits.isEmpty())
        Q_EMIT contentsChanged(resultingEdits);
}

void MarkoffDocument::resetContent(const QByteArray &, Origin)
{
    // Filled in Task 16.
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

// Sessions - filled in Task 23.
Session *MarkoffDocument::createSession(const SessionParams &) { return nullptr; }
void MarkoffDocument::destroySession(Session *) {}
QList<Session *> MarkoffDocument::sessions() const { return {}; }
Session *MarkoffDocument::sessionForParticipant(const QString &) const
{
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

void MarkoffDocument::setCoalescingIdleMs(int ms) { d->coalescingIdleMs = ms; }
int  MarkoffDocument::coalescingIdleMs() const { return d->coalescingIdleMs; }

}  // namespace Markoff
