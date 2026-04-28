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
MarkoffDocument::applyLocalEdit(const QList<MarkoffEdit> &)
{
    // Stub - filled in Task 10.
    return CollabText::Crdt::EditOperation{};
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::undo()
{
    return std::nullopt;
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::redo()
{
    return std::nullopt;
}

int MarkoffDocument::undoDepth() const { return 0; }
bool MarkoffDocument::coalesceLastUndo() { return false; }

void MarkoffDocument::applyRemoteOps(
    const std::vector<CollabText::Crdt::Operation> &)
{
    // Filled in Task 13.
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
