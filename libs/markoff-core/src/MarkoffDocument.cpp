// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/MarkoffDocument.h>

#include <QTextCursor>
#include <QTextDocument>

#include <markoff-parser/Document.h>

namespace Markoff {

struct MarkoffDocument::Private {
    QTextDocument *textDoc = nullptr;
    int coalescingIdleMs = 500;
    bool parseDirty = true;
    std::unique_ptr<Document> cachedParse;
    int transactionDepth = 0;
};

MarkoffDocument::MarkoffDocument(QObject *parent)
    : QObject(parent), d(new Private)
{
    d->textDoc = new QTextDocument(this);
    connect(d->textDoc, &QTextDocument::contentsChanged,
            this, [this] {
                d->parseDirty = true;
                d->cachedParse.reset();
                Q_EMIT contentsChanged();
            });
}

MarkoffDocument::~MarkoffDocument() { delete d; }

QString MarkoffDocument::plainText() const
{
    return d->textDoc->toPlainText();
}

void MarkoffDocument::setPlainText(const QString &text)
{
    d->textDoc->setPlainText(text);
    // setPlainText emits contentsChanged, which invalidates the cache.
}

QTextDocument *MarkoffDocument::textDocument() const
{
    return d->textDoc;
}

void MarkoffDocument::replace(int sourceOffset, int removeLen,
                              const QString &insert)
{
    QTextCursor c(d->textDoc);
    // Wrap in edit block when outside a transaction so Qt does not
    // auto-merge this with adjacent edits into a single undo step.
    const bool standalone = (d->transactionDepth == 0);
    if (standalone) c.beginEditBlock();
    c.setPosition(sourceOffset);
    if (removeLen > 0) {
        c.setPosition(sourceOffset + removeLen, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    }
    if (!insert.isEmpty()) {
        c.insertText(insert);
    }
    if (standalone) c.endEditBlock();
}

void MarkoffDocument::insert(int sourceOffset, const QString &text)
{
    if (text.isEmpty()) return;
    QTextCursor c(d->textDoc);
    // Wrap in edit block when outside a transaction so Qt does not
    // auto-merge this with adjacent edits into a single undo step.
    const bool standalone = (d->transactionDepth == 0);
    if (standalone) c.beginEditBlock();
    c.setPosition(sourceOffset);
    c.insertText(text);
    if (standalone) c.endEditBlock();
}

void MarkoffDocument::remove(int sourceOffset, int len)
{
    if (len <= 0) return;
    QTextCursor c(d->textDoc);
    // Wrap in edit block when outside a transaction so Qt does not
    // auto-merge this with adjacent edits into a single undo step.
    const bool standalone = (d->transactionDepth == 0);
    if (standalone) c.beginEditBlock();
    c.setPosition(sourceOffset);
    c.setPosition(sourceOffset + len, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    if (standalone) c.endEditBlock();
}

void MarkoffDocument::beginTransaction()
{
    if (d->transactionDepth++ == 0) {
        QTextCursor c(d->textDoc);
        c.beginEditBlock();
        // The cursor goes out of scope here; endEditBlock() pairs
        // through the document's own tracking. We use a separate
        // cursor at endTransaction() to close the same block —
        // QTextDocument nests editBlock calls on any cursor tied to it.
    }
}

void MarkoffDocument::endTransaction()
{
    if (--d->transactionDepth == 0) {
        QTextCursor c(d->textDoc);
        c.endEditBlock();
    }
}

void MarkoffDocument::setCoalescingIdleMs(int ms)
{
    d->coalescingIdleMs = ms;
}

int MarkoffDocument::coalescingIdleMs() const
{
    return d->coalescingIdleMs;
}

const Document *MarkoffDocument::parsed() const
{
    if (!d->parseDirty && d->cachedParse) {
        return d->cachedParse.get();
    }
    // Sync parse for Phase A. Async worker arrives in Phase C.
    d->cachedParse = Document::fromMarkdown(d->textDoc->toPlainText());
    d->parseDirty = false;
    // Emit while the caller is inside parsed() — OK, Qt signals are
    // direct by default for same-thread receivers.
    Q_EMIT const_cast<MarkoffDocument *>(this)->parseUpdated(
        d->cachedParse.get());
    return d->cachedParse.get();
}

bool MarkoffDocument::parseIsPending() const
{
    // Sync parse only in Phase A — never pending.
    Q_UNUSED(d);
    return false;
}

}  // namespace Markoff
