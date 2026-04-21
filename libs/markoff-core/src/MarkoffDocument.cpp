// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/MarkoffDocument.h>
#include <markoff/CanonicalBuffer.h>
#include <markoff/CursorPosition.h>
#include <markoff/ParsePool.h>
#include <markoff/MarkdownDelta.h>

// Bring in InMemoryCanonicalBuffer for default construction.
#include "InMemoryCanonicalBuffer.h"

// Full definition of markoff-parser Document required for unique_ptr<Document> in Private.
#include <markoff-parser/Document.h>

#include <QUndoStack>

namespace Markoff {

struct MarkoffDocument::Private {
    std::unique_ptr<CanonicalBuffer> buffer;
    std::unique_ptr<Document>        parsedDoc;   // markoff-parser — fwd-declared above
    std::unique_ptr<QUndoStack>      undoStack;
    ParsePool *pool = nullptr;
    bool       ownsPool = false;   // Task 7: set to true when default pool is constructed
    int        coalescingIdleMs = 150;
    bool       m_parsePending = false;
    // Task 7: poolDebounce QTimer lives here.
};

MarkoffDocument::MarkoffDocument(QObject *parent)
    : MarkoffDocument(std::make_unique<InMemoryCanonicalBuffer>(), nullptr, parent) {
}

MarkoffDocument::MarkoffDocument(std::unique_ptr<CanonicalBuffer> buffer,
                                 ParsePool *pool,
                                 QObject *parent)
    : QObject(parent), d(std::make_unique<Private>()) {
    d->buffer = std::move(buffer);
    if (!d->buffer) {
        d->buffer = std::make_unique<InMemoryCanonicalBuffer>();
    }
    d->pool = pool;        // Task 7: if null, construct a DefaultParsePool here and set ownsPool = true
    d->undoStack = std::make_unique<QUndoStack>(this);
}

MarkoffDocument::~MarkoffDocument() {
    // Invariant per Phase C3 spec §4.7: cancel any in-flight parse for this doc
    // before the ParsePool's queued lambdas can fire with a dangling sender.
    if (d->pool) {
        d->pool->cancelJobsFor(this);
    }
    // d->undoStack dies first (unique_ptr order in the struct), then d->buffer,
    // then d->parsedDoc. QObject base dtor disconnects all connections targeting
    // this — safe against any concurrent jobCompleted emissions.
}

// ---- Reads ----
const QString &MarkoffDocument::toMarkdown() const {
    return d->buffer->toMarkdown();
}

qsizetype MarkoffDocument::length() const {
    return d->buffer->length();
}

QString MarkoffDocument::substring(qsizetype offset, qsizetype len) const {
    return d->buffer->substring(offset, len);
}

const Document *MarkoffDocument::parsedDocument() const {
    return d->parsedDoc.get();
}

bool MarkoffDocument::parseIsPending() const {
    return d->m_parsePending;
}

// ---- Writes ----
QUndoStack *MarkoffDocument::undoStack() const {
    return d->undoStack.get();
}

void MarkoffDocument::resetContent(const QString &newContent, Origin origin) {
    // Task 7 fills in the full Origin-branch logic.
    // For Task 6, a minimal impl lets the tests pass for FirstOpen:
    d->buffer->reset(newContent);
    if (origin == Origin::FirstOpen) {
        emit contentsChanged(0, 0, newContent.size());
    } else {
        d->undoStack->clear();
        emit documentReloaded();
    }
    // Task 7: schedulePoolPost() here.
}

// ---- Anchors ----
CursorPosition MarkoffDocument::trackCursor(qsizetype offset, CursorBias bias) {
    const quint64 h = d->buffer->createAnchor(offset, bias);
    return CursorPosition(this, h);
}

qsizetype MarkoffDocument::resolveCursor(const CursorPosition &cp) const {
    if (!cp.isValid()) return -1;
    // CursorPosition is an opaque handle; we reach into it via friendship.
    // MarkoffDocument is declared as friend in CursorPosition, so m_handle is accessible.
    return d->buffer->resolveAnchor(cp.m_handle);
}

// ---- Coalescing ----
void MarkoffDocument::setCoalescingIdleMs(int ms) { d->coalescingIdleMs = ms; }
int  MarkoffDocument::coalescingIdleMs() const    { return d->coalescingIdleMs; }

// ---- Package-private helpers ----
QString MarkoffDocument::canonicalSubstring(qsizetype offset, qsizetype len) const {
    return d->buffer->substring(offset, len);
}

void MarkoffDocument::applyCanonicalDelta(qsizetype offset, qsizetype removedLength,
                                          const QString &inserted) {
    // Task 7 fills in: buffer->applyDelta + emit contentsChanged + schedulePoolPost.
    // For Task 6, a minimal impl lets MarkdownDelta::redo/undo be testable at Task 7.
    d->buffer->applyDelta(offset, removedLength, inserted);
    emit contentsChanged(offset, removedLength, inserted.size());
    // Task 7: schedulePoolPost()
}

void MarkoffDocument::releaseAnchorHandle(quint64 handle) {
    d->buffer->releaseAnchor(handle);
}

} // namespace Markoff
