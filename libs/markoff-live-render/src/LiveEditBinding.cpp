// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveEditBinding.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/Coordinates.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

#include <QQuickTextDocument>
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
}

void LiveEditBinding::setRawTextDocument(QTextDocument *td)
{
    rewireTextDocument(td);
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

    // Guard: model-driven update echo (spec §4.5).
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

    // Pre-edit text for old-coordinate translation: model record's text,
    // which has not been updated since the LAST parse arrival.
    const QByteArray preUtf8 = record.text.toUtf8();

    // Post-edit text from the live QTextDocument.
    const QString postQt = m_listenedDoc->toPlainText();

    // Old block-local byte range:
    const qsizetype oldStartLocal =
        Coordinates::qtPosToByte(preUtf8, qtPos);
    const qsizetype oldEndLocal =
        Coordinates::qtPosToByte(preUtf8, qtPos + charsRemoved);

    // New text slice from the live document, UTF-8.
    const QString addedQt = postQt.mid(qtPos, charsAdded);
    const QByteArray addedUtf8 = addedQt.toUtf8();

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
    // Body in Task 7 (IME composition deferral).
}

}  // namespace Markoff::LiveRender
