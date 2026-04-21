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
#include <QTimer>

namespace Markoff {

struct MarkoffDocument::Private {
    std::unique_ptr<CanonicalBuffer> buffer;
    std::unique_ptr<Document>        parsedDoc;   // markoff-parser — fwd-declared above
    std::unique_ptr<QUndoStack>      undoStack;
    ParsePool *pool = nullptr;
    bool       ownsPool = false;
    int        coalescingIdleMs = 150;
    bool       m_parsePending = false;
    QTimer    *poolDebounce = nullptr;
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

    // Pool fallback: if none provided, construct an owned default.
    if (pool) {
        d->pool = pool;
        d->ownsPool = false;
    } else {
        d->pool = new ParsePool(this);
        d->ownsPool = true;
    }

    // No Qt parent on undoStack: unique_ptr has sole ownership.
    d->undoStack = std::make_unique<QUndoStack>();

    // Wire jobCompleted → parseUpdated.
    connect(d->pool, &ParsePool::jobCompleted, this,
            [this](MarkoffDocument *sender, Document *parsed) {
        if (sender != this) return;
        d->parsedDoc.reset(parsed);
        d->m_parsePending = false;
        emit parseUpdated(d->parsedDoc.get());
    });

    // Pool debounce timer: QObject child of this, so destroyed with us.
    d->poolDebounce = new QTimer(this);
    d->poolDebounce->setSingleShot(true);
    d->poolDebounce->setInterval(d->coalescingIdleMs);
    connect(d->poolDebounce, &QTimer::timeout, this, [this]() {
        if (!d->pool) return;
        d->m_parsePending = true;
        d->pool->postJob(this, d->buffer->toMarkdown());
    });
}

MarkoffDocument::~MarkoffDocument() {
    // Invariant per Phase C3 spec §4.7: cancel any in-flight parse for this doc
    // before the ParsePool's queued lambdas can fire with a dangling sender.
    if (d->pool) {
        d->pool->cancelJobsFor(this);
    }
    // d->undoStack dies first (unique_ptr order in the struct), then d->parsedDoc,
    // then d->buffer. QObject base dtor disconnects all connections targeting
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
    const qsizetype oldLen = d->buffer->length();

    switch (origin) {
    case Origin::FirstOpen:
        // Stack is already empty (fresh document). Just reset the buffer and
        // signal a ranged replacement (not a pure insertion).
        d->buffer->reset(newContent);
        emit contentsChanged(0, oldLen, newContent.size());
        break;

    case Origin::ExternalReloadClean:
    case Origin::ExternalReloadResolved:
    case Origin::TestFixture:
        d->buffer->reset(newContent);
        d->undoStack->clear();
        emit documentReloaded();
        break;

    case Origin::UserRevertToSaved:
        // Push a MarkdownDelta so the user can Ctrl+Z to reverse the revert.
        // The command's redo() calls applyCanonicalDelta which does the actual
        // buffer mutation + emits contentsChanged + schedules the pool post.
        d->undoStack->push(new MarkdownDelta(this, 0, oldLen, newContent));
        // NOTE: no direct buffer->reset here; applyCanonicalDelta handles it.
        return; // schedulePoolPost() called inside applyCanonicalDelta
    }

    schedulePoolPost();
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
void MarkoffDocument::setCoalescingIdleMs(int ms) {
    d->coalescingIdleMs = ms;
    if (d->poolDebounce) {
        d->poolDebounce->setInterval(ms);
    }
}

int  MarkoffDocument::coalescingIdleMs() const    { return d->coalescingIdleMs; }

// ---- Package-private helpers ----
QString MarkoffDocument::canonicalSubstring(qsizetype offset, qsizetype len) const {
    return d->buffer->substring(offset, len);
}

void MarkoffDocument::applyCanonicalDelta(qsizetype offset, qsizetype removedLength,
                                          const QString &inserted) {
    d->buffer->applyDelta(offset, removedLength, inserted);
    emit contentsChanged(offset, removedLength, inserted.size());
    schedulePoolPost();
}

void MarkoffDocument::releaseAnchorHandle(quint64 handle) {
    d->buffer->releaseAnchor(handle);
}

// ---- Private helpers ----
void MarkoffDocument::schedulePoolPost() {
    if (!d->poolDebounce) return;
    d->poolDebounce->start();  // restart timer; coalesces bursts
}

} // namespace Markoff
