// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveEditBinding.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/Coordinates.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

#include <QQuickTextDocument>
#include <QScopeGuard>
#include <QTextDocument>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcEdit, "markoff.live.edit", QtWarningMsg)

namespace Markoff::LiveRender {

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
    // applyingTextUpdate guard makes onContentsChange treat that as a
    // model-driven update (no applyLocalEdit, no row-seq stamp).
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
    if (m_modelIndex < 0 || m_modelIndex >= m_binding->model()->rowCount())
        return;
    if (!m_listenedDoc)
        return;

    // Snapshot the post-edit text and compute the delta against m_previousText
    // (the CRDT-coherent before-state for THIS contentsChange). m_previousText
    // is updated unconditionally at the end so the cache stays in sync with
    // the live QTextDocument across guard skip-paths too.
    const QString postQt = m_listenedDoc->toPlainText();
    auto _ = qScopeGuard([&]{ m_previousText = postQt; });

    // Guard: pushTextToDocument-driven update echo. Set when LiveEditBinding
    // itself is calling setPlainText to mirror the bound `text` property
    // (initial delegate load, ListView recycling, parse-arrival fresh-row
    // updates routed through the QML text property). Without this guard
    // those non-user document writes would be misread as user edits and
    // pumped back into the CRDT, duplicating content on every parse cycle.
    if (m_applyingTextUpdate) {
        qCDebug(lcEdit) << "skip: applyingTextUpdate";
        return;
    }

    // Guard: model-driven update echo via applyOps (spec §4.5).
    if (m_binding->applyingModelUpdate()) {
        qCDebug(lcEdit) << "skip: applyingModelUpdate";
        return;
    }

    // Guard: IME composition (spec §4.5). Note the touch so the commit
    // pass knows there's pending state to flush.
    if (m_composing) {
        m_compositionPendingFlush = true;
        return;
    }

    auto *doc = m_binding->document();
    auto *model = m_binding->model();
    const auto &record = model->recordAt(m_modelIndex);

    const auto blockRangeOpt = doc->blockByteRange(record.blockAnchor);
    if (!blockRangeOpt) {
        qCWarning(lcEdit) << "blockByteRange failed for row" << m_modelIndex;
        return;
    }
    const quint32 blockStart = blockRangeOpt->first;

    // Pre-edit text for old-coordinate translation: m_previousText (the
    // CRDT-coherent before-state of THIS edit). Using record.text instead
    // would lag behind by any local edits since the last parse arrival,
    // producing scrambled bytes when the user types faster than parses.
    const QByteArray preUtf8 = m_previousText.toUtf8();

    // Old block-local byte range:
    const qsizetype oldStartLocal =
        Coordinates::qtPosToByte(preUtf8, qtPos);
    const qsizetype oldEndLocal =
        Coordinates::qtPosToByte(preUtf8, qtPos + charsRemoved);

    // New text slice. Skip toPlainText work in pure-deletion (no chars added).
    QByteArray addedUtf8;
    if (charsAdded > 0)
        addedUtf8 = postQt.mid(qtPos, charsAdded).toUtf8();

    Markoff::MarkoffEdit edit;
    edit.oldStart = blockStart + static_cast<quint32>(oldStartLocal);
    edit.oldEnd   = blockStart + static_cast<quint32>(oldEndLocal);
    edit.newText  = addedUtf8;

    doc->applyLocalEdit({ edit });

    // Stamp the row for the freshness rule (spec §4.3).
    model->setRowEditSequence(m_modelIndex, doc->editSequence());
}

void LiveEditBinding::flushPendingComposition()
{
    if (!m_compositionPendingFlush) return;
    m_compositionPendingFlush = false;

    if (!m_binding || !m_binding->model() || !m_binding->document() || !m_listenedDoc)
        return;
    if (m_modelIndex < 0 || m_modelIndex >= m_binding->model()->rowCount())
        return;

    auto *doc   = m_binding->document();
    auto *model = m_binding->model();
    const auto &record = model->recordAt(m_modelIndex);

    const auto blockRangeOpt = doc->blockByteRange(record.blockAnchor);
    if (!blockRangeOpt) return;
    const quint32 blockStart = blockRangeOpt->first;
    const quint32 blockEnd   = blockRangeOpt->second;

    const QString postQt = m_listenedDoc->toPlainText();
    const QByteArray postUtf8 = postQt.toUtf8();

    Markoff::MarkoffEdit edit;
    edit.oldStart = blockStart;
    edit.oldEnd   = blockEnd;
    edit.newText  = postUtf8;

    doc->applyLocalEdit({ edit });
    model->setRowEditSequence(m_modelIndex, doc->editSequence());

    // Keep the cache in sync with the post-commit document state so the
    // next non-composing contentsChange computes its byte range against
    // the right pre-edit reference.
    m_previousText = postQt;
}

}  // namespace Markoff::LiveRender
