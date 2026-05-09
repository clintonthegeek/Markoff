// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveEditBinding.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/Coordinates.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>

#include <QDebug>
#include <QQuickTextDocument>
#include <QScopeGuard>
#include <QTextDocument>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcEdit, "markoff.live.edit", QtWarningMsg)

namespace Markoff::Live {

LiveEditBinding::LiveEditBinding(QObject *parent) : QObject(parent) {}
LiveEditBinding::~LiveEditBinding() = default;

LiveListModelBinding *LiveEditBinding::binding() const { return m_binding.data(); }
void LiveEditBinding::setBinding(LiveListModelBinding *b)
{
    if (m_binding == b) return;
    m_binding = b;
    Q_EMIT bindingChanged();
}

int LiveEditBinding::modelIndex() const { return m_modelIndex; }
void LiveEditBinding::setModelIndex(int row)
{
    if (m_modelIndex == row) return;
    m_modelIndex = row;
    Q_EMIT modelIndexChanged();
}

QQuickTextDocument *LiveEditBinding::textDocument() const { return m_textDocument.data(); }
void LiveEditBinding::setTextDocument(QQuickTextDocument *td)
{
    if (m_textDocument == td) return;
    m_textDocument = td;
    rewireTextDocument(td ? td->textDocument() : nullptr);
    Q_EMIT textDocumentChanged();
    // The text property may have been bound before the document was
    // available; flush any pending value into the now-wired document.
    pushTextToDocument();
}

void LiveEditBinding::setRawTextDocument(QTextDocument *td)
{
    rewireTextDocument(td);
    pushTextToDocument();
}

QString LiveEditBinding::text() const { return m_text; }
void LiveEditBinding::setText(const QString &t)
{
    if (m_text == t) return;
    m_text = t;
    Q_EMIT textChanged();
    pushTextToDocument();
}

void LiveEditBinding::pushTextToDocument()
{
    if (!m_listenedDoc) return;
    if (m_listenedDoc->toPlainText() == m_text) return;
    // Setting the document text fires contentsChange synchronously; the
    // applyingTextUpdate guard makes onContentsChange skip it.
    m_applyingTextUpdate = true;
    auto _ = qScopeGuard([this]{ m_applyingTextUpdate = false; });
    m_listenedDoc->setPlainText(m_text);
    m_previousText = m_text;
}

bool LiveEditBinding::composing() const { return m_composing; }
void LiveEditBinding::setComposing(bool c)
{
    if (m_composing == c) return;
    const bool wasComposing = m_composing;
    m_composing = c;
    Q_EMIT composingChanged();
    if (wasComposing && !c)
        flushPendingComposition();
}

void LiveEditBinding::rewireTextDocument(QTextDocument *newDoc)
{
    if (m_listenedDoc) {
        QObject::disconnect(m_listenedDoc.data(), &QTextDocument::contentsChange,
                            this, &LiveEditBinding::onContentsChange);
    }
    m_listenedDoc = newDoc;
    if (m_listenedDoc) {
        QObject::connect(m_listenedDoc.data(), &QTextDocument::contentsChange,
                         this, &LiveEditBinding::onContentsChange);
        m_previousText = m_listenedDoc->toPlainText();
    } else {
        m_previousText.clear();
    }
}

void LiveEditBinding::onContentsChange(int qtPos, int charsRemoved, int charsAdded)
{
    if (!m_binding || !m_binding->model() || !m_binding->document())
        return;
    if (m_modelIndex < 0)
        return;
    if (!m_listenedDoc)
        return;

    qCDebug(lcEdit) << "onContentsChange"
                    << "modelIndex=" << m_modelIndex
                    << "qtPos=" << qtPos
                    << "removed=" << charsRemoved
                    << "added=" << charsAdded
                    << "applyingTextUpdate=" << m_applyingTextUpdate
                    << "composing=" << m_composing;

    const QString postQt = m_listenedDoc->toPlainText();
    auto _ = qScopeGuard([&]{ m_previousText = postQt; });

    // Guard: pushTextToDocument-driven update echo. Prevents non-user
    // writes (initial delegate load, model updates) from being pumped back
    // into the CRDT.
    if (m_applyingTextUpdate) {
        qCDebug(lcEdit) << "skip: applyingTextUpdate";
        return;
    }

    // Guard: IME composition. Defer until commit.
    if (m_composing) {
        m_compositionPendingFlush = true;
        return;
    }

    // Inner-row guard.
    if (m_modelIndex >= m_binding->model()->rowCount())
        return;

    auto *doc   = m_binding->document();
    auto *model = m_binding->model();
    const auto &record = model->recordAt(m_modelIndex);

    // In D2, each block has its own buffer. The within-block byte offset
    // is the only coordinate needed — no absolute document offset required.
    const QByteArray preUtf8 = m_previousText.toUtf8();
    const uint32_t byteOff = static_cast<uint32_t>(
        Coordinates::qtPosToByte(preUtf8, qtPos));
    const uint32_t removedBytes = static_cast<uint32_t>(
        Coordinates::qtPosToByte(preUtf8, qtPos + charsRemoved)) - byteOff;

    QByteArray inserted;
    if (charsAdded > 0)
        inserted = postQt.mid(qtPos, charsAdded).toUtf8();

    // Apply via D2 per-block API.
    auto &undoLog = doc->d2UndoLog();
    UndoLog::Transaction t(undoLog);
    doc->d2ApplyBufferEdit(record.blockAnchor, byteOff, removedBytes, inserted, t);

    // Flush the queued d2DocumentChanged synchronously so the span-update
    // cascade (model → delegate.spans binding → InlineHighlighter::setInlineSpans
    // → rehighlight) lands inside the QTextDocument::contentsChange emission
    // chain, before the QSyntaxHighlighter subscriber's highlightBlock runs.
    // Without this, the highlighter formats the post-edit text with the
    // pre-edit span offsets — inline delimiters at offsets after the
    // insertion point appear visible for one paint frame before the
    // debounced d2-changed timer fires and corrects them.
    doc->flushPendingD2Changed();
}

void LiveEditBinding::onFocusLost()
{
    // Marker-paragraph design is retired in D2; this is a no-op kept for
    // QML API stability.
}

void LiveEditBinding::flushPendingComposition()
{
    if (!m_compositionPendingFlush) return;
    m_compositionPendingFlush = false;

    if (!m_binding || !m_binding->model() || !m_binding->document() || !m_listenedDoc)
        return;
    if (m_modelIndex < 0)
        return;

    // Inner-row guard.
    if (m_modelIndex >= m_binding->model()->rowCount())
        return;

    auto *doc   = m_binding->document();
    auto *model = m_binding->model();
    const auto &record = model->recordAt(m_modelIndex);

    // For IME composition flush: replace the entire block content with the
    // post-composition text. Use the pre-composition text (m_previousText)
    // to compute what the block currently holds, and the post-composition
    // text from the QTextDocument.
    const QByteArray preUtf8  = m_previousText.toUtf8();
    const QByteArray postUtf8 = m_listenedDoc->toPlainText().toUtf8();

    const uint32_t removedBytes = static_cast<uint32_t>(preUtf8.size());

    auto &undoLog = doc->d2UndoLog();
    UndoLog::Transaction t(undoLog);
    doc->d2ApplyBufferEdit(record.blockAnchor, 0, removedBytes, postUtf8, t);

    // Keep the cache in sync with the post-commit state.
    m_previousText = m_listenedDoc->toPlainText();
}

}  // namespace Markoff::Live
