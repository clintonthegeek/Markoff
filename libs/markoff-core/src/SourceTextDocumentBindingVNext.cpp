// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/SourceTextDocumentBinding.h>

#include <markoff/core/OpaqueBlockRenderer.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Detail/FlatBlockResolve.h>
#include <markoff/core/StructuralKeyHandler.h>

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

namespace Markoff {

SourceTextDocumentBinding::SourceTextDocumentBinding(QObject* parent)
    : QObject(parent) {}

SourceTextDocumentBinding::~SourceTextDocumentBinding() = default;

quint32 SourceTextDocumentBinding::qtPosToByteOffset(const QString& text, int qtOffset)
{
    const int limit = std::clamp(qtOffset, 0, static_cast<int>(text.size()));
    return static_cast<quint32>(text.left(limit).toUtf8().size());
}

int SourceTextDocumentBinding::byteOffsetToQtPos(const QByteArray& utf8, quint32 byteOffset)
{
    return QString::fromUtf8(utf8.left(static_cast<qsizetype>(byteOffset))).size();
}

MarkoffDocument* SourceTextDocumentBinding::markoffDocument() const
{
    return m_markoffDocument;
}

void SourceTextDocumentBinding::setMarkoffDocument(MarkoffDocument* doc)
{
    if (m_markoffDocument == doc) return;
    m_savedViewSelection.reset();
    m_markoffDocument = doc;
    Q_EMIT markoffDocumentChanged();
    rebindMarkoffDocumentSubscription();
    rewireQtDocument();
}

Session* SourceTextDocumentBinding::session() const { return m_session; }

void SourceTextDocumentBinding::setSession(Session* session)
{
    if (m_session == session) return;
    m_session = session;
    Q_EMIT sessionChanged();
    rebindSessionSubscription();
}

void SourceTextDocumentBinding::onSessionPrimarySelectionChanged(const Selection&)
{
    syncFromSession();
}

void SourceTextDocumentBinding::syncFromSession()
{
    if (!m_session || !m_markoffDocument || !m_textDocument) return;
    const auto selection = m_session->primarySelection();
    if (!selection.anchorPosition || !selection.activePosition
        || !selection.anchorPosition->isValid()
        || !selection.activePosition->isValid())
        return;

    const auto anchor = m_markoffDocument->resolveBoundaryToken(
        selection.anchorPosition->block,
        selection.anchorPosition->boundaryToken);
    const auto active = m_markoffDocument->resolveBoundaryToken(
        selection.activePosition->block,
        selection.activePosition->boundaryToken);
    const auto blocks = m_markoffDocument->iterateBlocks();
    const auto isVisible = [&blocks](BlockId block) {
        return std::find(blocks.begin(), blocks.end(), block) != blocks.end();
    };
    if (!anchor.ok() || !active.ok()
        || !isVisible(selection.anchorPosition->block)
        || !isVisible(selection.activePosition->block))
        return;
    emitCaret(sepViewPosOf(selection.anchorPosition->block,
                           static_cast<int>(anchor.value())),
              sepViewPosOf(selection.activePosition->block,
                           static_cast<int>(active.value())));
}

QTextDocument* SourceTextDocumentBinding::textDocument() const { return m_textDocument; }

void SourceTextDocumentBinding::setTextDocument(QTextDocument* document)
{
    if (m_textDocument == document) return;
    m_textDocument = document;
    Q_EMIT textDocumentChanged();
    rewireQtDocument();
}

void SourceTextDocumentBinding::setOpaqueRenderer(OpaqueBlockRenderer* renderer)
{
    if (m_opaqueRenderer == renderer) return;
    m_opaqueRenderer = renderer;
    syncQtDocumentFromMarkoff();
}

void SourceTextDocumentBinding::captureViewSelection(int anchorQtPosition,
                                                       int activeQtPosition)
{
    if (m_applyingRemoteEdit || !m_markoffDocument || !m_textDocument) return;
    const QString view = sepViewIndex().view;
    const auto capture = [this, &view](int qtPosition)
        -> std::optional<ScopedTextPosition> {
        const auto viewByte = qtPosToByteOffset(view, qtPosition);
        const auto hit = findBlockAtSepByteCached(viewByte, false);
        if (!hit) return std::nullopt;
        auto token = m_markoffDocument->boundaryTokenAt(
            hit->blockId, hit->byteInBlock, false);
        if (!token.ok()) return std::nullopt;
        return ScopedTextPosition{hit->blockId, std::move(token).value()};
    };
    const auto anchor = capture(anchorQtPosition);
    const auto active = capture(activeQtPosition);
    if (!anchor || !active) {
        m_savedViewSelection.reset();
        return;
    }
    m_savedViewSelection = SavedViewSelection{*anchor, *active};
    if (m_session) {
        auto selection = m_session->primarySelection();
        selection.anchorPosition = *anchor;
        selection.activePosition = *active;
        selection.kind = Selection::Kind::Primary;
        m_session->setPrimarySelection(selection);
    }
}

void SourceTextDocumentBinding::rewireQtDocument()
{
    if (m_textDocument) {
        QObject::disconnect(m_textDocument, &QTextDocument::contentsChange,
                            this, &SourceTextDocumentBinding::onQtContentsChange);
        m_textDocument->setUndoRedoEnabled(false);
        QObject::connect(m_textDocument, &QTextDocument::contentsChange,
                         this, &SourceTextDocumentBinding::onQtContentsChange);
    }
    syncQtDocumentFromMarkoff();
}

void SourceTextDocumentBinding::rebindMarkoffDocumentSubscription()
{
    if (m_subscribedDoc == m_markoffDocument) return;
    if (m_subscribedDoc)
        QObject::disconnect(m_subscribedDoc, &MarkoffDocument::d2DocumentChanged,
                            this, &SourceTextDocumentBinding::onD2DocumentChanged);
    m_subscribedDoc = m_markoffDocument;
    if (m_subscribedDoc)
        QObject::connect(m_subscribedDoc, &MarkoffDocument::d2DocumentChanged,
                         this, &SourceTextDocumentBinding::onD2DocumentChanged);
    syncQtDocumentFromMarkoff();
}

void SourceTextDocumentBinding::rebindSessionSubscription()
{
    if (m_subscribedSession == m_session) return;
    if (m_subscribedSession)
        QObject::disconnect(m_subscribedSession, &Session::primarySelectionChanged,
                            this, &SourceTextDocumentBinding::onSessionPrimarySelectionChanged);
    m_subscribedSession = m_session;
    if (m_subscribedSession)
        QObject::connect(m_subscribedSession, &Session::primarySelectionChanged,
                         this, &SourceTextDocumentBinding::onSessionPrimarySelectionChanged);
    syncFromSession();
}

void SourceTextDocumentBinding::syncQtDocumentFromMarkoff()
{
    if (!m_subscribedDoc || !m_textDocument) return;
    m_applyingRemoteEdit = true;
    m_opaqueSnapshots.clear();
    if (!m_opaqueRenderer) {
        const QString text = QString::fromUtf8(m_subscribedDoc->widgetFlatView());
        if (m_textDocument->toPlainText() != text) m_textDocument->setPlainText(text);
    } else {
        m_textDocument->clear();
        QTextCursor cursor(m_textDocument);
        const auto blocks = m_subscribedDoc->iterateBlocks();
        for (size_t i = 0; i < blocks.size(); ++i) {
            const auto id = blocks[i];
            const auto kind = m_subscribedDoc->blockKind(id);
            if (m_opaqueRenderer->isOpaque(id, kind)) {
                m_opaqueRenderer->renderOpaque(cursor, id);
                cursor.movePosition(QTextCursor::End);
                m_opaqueSnapshots.insert(id, m_subscribedDoc->blockText(id));
            } else {
                if (i) cursor.insertBlock();
                cursor.insertText(QString::fromUtf8(m_subscribedDoc->blockText(id)));
            }
        }
    }

    if (m_savedViewSelection) {
        const auto& saved = *m_savedViewSelection;
        const auto anchor = m_markoffDocument->resolveBoundaryToken(
            saved.anchor.block, saved.anchor.boundaryToken);
        const auto active = m_markoffDocument->resolveBoundaryToken(
            saved.active.block, saved.active.boundaryToken);
        const auto blocks = m_markoffDocument->iterateBlocks();
        const auto isVisible = [&blocks](BlockId block) {
            return std::find(blocks.begin(), blocks.end(), block) != blocks.end();
        };
        if (anchor.ok() && active.ok() && isVisible(saved.anchor.block)
            && isVisible(saved.active.block)) {
            const int anchorPos = sepViewPosOf(saved.anchor.block,
                static_cast<int>(anchor.value()));
            const int activePos = sepViewPosOf(saved.active.block,
                static_cast<int>(active.value()));
            emitCaret(anchorPos, activePos);
        } else {
            // A deleted/detached endpoint is deliberately discarded. Never
            // turn an unavailable token into an apparent block-end position.
            m_savedViewSelection.reset();
        }
    }
    m_applyingRemoteEdit = false;
}

const SourceTextDocumentBinding::SepViewIndex &
SourceTextDocumentBinding::sepViewIndex() const
{
    const quint64 version = m_markoffDocument
        ? m_markoffDocument->d2EditSequence() : 0;
    if (m_sepIndex.valid && m_sepIndex.version == version && m_markoffDocument)
        return m_sepIndex;
    m_sepIndex = SepViewIndex{};
    if (!m_markoffDocument) return m_sepIndex;
    const auto blocks = m_markoffDocument->iterateBlocks();
    m_sepIndex.viewBytes = m_markoffDocument->widgetFlatView();
    m_sepIndex.view = QString::fromUtf8(m_sepIndex.viewBytes);
    m_sepIndex.version = version;
    m_sepIndex.order.assign(blocks.begin(), blocks.end());
    m_sepIndex.byteStart.reserve(blocks.size());
    m_sepIndex.qtStart.reserve(blocks.size());
    m_sepIndex.byteLen.reserve(blocks.size());
    m_sepIndex.qtLen.reserve(blocks.size());
    quint32 byteCursor = 0;
    quint32 qtCursor = 0;
    int notFound = 0;
    for (const auto& id : blocks) {
        const QByteArray text = m_markoffDocument->blockText(id);
        const int qtLen = int(QString::fromUtf8(text).size());
        m_sepIndex.byteStart.push_back(byteCursor);
        m_sepIndex.qtStart.push_back(qtCursor);
        m_sepIndex.byteLen.push_back(static_cast<quint32>(text.size()));
        m_sepIndex.qtLen.push_back(static_cast<quint32>(qtLen));
        byteCursor += static_cast<quint32>(text.size()) + 1;
        qtCursor += static_cast<quint32>(qtLen) + 1;
        notFound += qtLen + 1;
    }
    m_sepIndex.notFoundPos = notFound;
    m_sepIndex.valid = true;
    return m_sepIndex;
}

std::optional<Markoff::Detail::BlockHit>
SourceTextDocumentBinding::findBlockAtSepByteCached(quint32 sepOff,
                                                    bool biasForward) const
{
    const auto& index = sepViewIndex();
    if (!index.valid || index.order.empty()) return std::nullopt;
    const auto& starts = index.byteStart;
    const auto upper = std::upper_bound(starts.begin(), starts.end(), sepOff);
    if (upper == starts.begin()) return std::nullopt;
    const size_t i = static_cast<size_t>(upper - starts.begin()) - 1;
    const quint32 sz = index.byteLen[i];
    const quint32 blkEnd = starts[i] + sz;
    if (sepOff < blkEnd)
        return Markoff::Detail::BlockHit{index.order[i], sepOff - starts[i],
                                         static_cast<int>(i)};
    if (sepOff == blkEnd) {
        if (!biasForward || i + 1 == index.order.size())
            return Markoff::Detail::BlockHit{index.order[i], sz,
                                             static_cast<int>(i)};
        return Markoff::Detail::BlockHit{index.order[i + 1], 0,
                                         static_cast<int>(i + 1)};
    }
    // Separator zone: with the single-'\n' widget separator no integer offset
    // lands strictly between blkEnd and blkEnd+1, so this is defensive only —
    // mirrors the walk in Detail::FlatBlockResolve.
    if (i + 1 < index.order.size()) {
        if (!biasForward)
            return Markoff::Detail::BlockHit{index.order[i], sz,
                                             static_cast<int>(i)};
        return Markoff::Detail::BlockHit{index.order[i + 1], 0,
                                         static_cast<int>(i + 1)};
    }
    return std::nullopt;
}

int SourceTextDocumentBinding::sepViewPosOf(BlockId block, int byteInBlock) const
{
    if (!m_markoffDocument) return 0;
    const auto& index = sepViewIndex();
    if (!index.valid) return 0;
    const auto it = std::find(index.order.begin(), index.order.end(), block);
    if (it == index.order.end()) return index.notFoundPos;
    const size_t i = static_cast<size_t>(it - index.order.begin());
    const QByteArray bytes = index.viewBytes.mid(
        static_cast<qsizetype>(index.byteStart[i]),
        static_cast<qsizetype>(index.byteLen[i]));
    return static_cast<int>(index.qtStart[i]
        + byteOffsetToQtPos(bytes, static_cast<quint32>(byteInBlock)));
}

int SourceTextDocumentBinding::noSepByteToSepViewPos(quint32) const { return 0; }

std::optional<SourceTextDocumentBinding::PendingCaret>
SourceTextDocumentBinding::deleteSepRange(quint32 sepLo, quint32 sepHi)
{
    if (!m_markoffDocument || sepLo >= sepHi) return std::nullopt;
    const auto first = findBlockAtSepByteCached(sepLo, false);
    const auto last = findBlockAtSepByteCached(sepHi, true);
    if (!first || !last) return std::nullopt;
    const auto firstText = m_markoffDocument->blockText(first->blockId);
    const auto removed = [&] {
        if (first->blockId == last->blockId) {
            const BlockEdit edit{first->blockId, first->byteInBlock,
                last->byteInBlock - first->byteInBlock, {}};
            const auto result = m_markoffDocument->applyBlockEdit(edit);
            return result.state == VNext::ActionPublicationState::committed
                || result.state == VNext::ActionPublicationState::committedHistoryPending;
        }
        const auto blocks = m_markoffDocument->iterateBlocks();
        const auto lastText = m_markoffDocument->blockText(last->blockId);
        QList<BlockId> toRemove;
        for (int i = first->blockIndex + 1; i <= last->blockIndex; ++i)
            toRemove.append(blocks[static_cast<size_t>(i)]);
        const BlockEdit edit{first->blockId, first->byteInBlock,
            static_cast<quint32>(firstText.size()) - first->byteInBlock,
            lastText.mid(static_cast<qsizetype>(last->byteInBlock))};
        const auto result = m_markoffDocument->applyBlockMerge(edit, toRemove);
        return result.state == VNext::ActionPublicationState::committed
            || result.state == VNext::ActionPublicationState::committedHistoryPending;
    }();
    if (!removed) return std::nullopt;
    return PendingCaret{first->blockId, static_cast<int>(first->byteInBlock)};
}

void SourceTextDocumentBinding::emitCaret(int start, int active)
{
    Q_EMIT caretResolved(start, active);
}

bool SourceTextDocumentBinding::handleStructuralKey(int key, int modifiers,
                                                     int qtPos, int qtAnchor)
{
    if (!m_markoffDocument || !m_textDocument) return false;
    const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    const bool backspace = key == Qt::Key_Backspace;
    const bool deletion = key == Qt::Key_Delete;
    const bool tab = key == Qt::Key_Tab || key == Qt::Key_Backtab;
    if (!enter && !backspace && !deletion && !tab) return false;

    QString before = sepViewIndex().view;
    int caret = qtPos;
    if (qtPos != qtAnchor) {
        const int lo = std::min(qtPos, qtAnchor);
        const int hi = std::max(qtPos, qtAnchor);
        const auto sepLo = qtPosToByteOffset(before, lo);
        const auto sepHi = qtPosToByteOffset(before, hi);
        m_applyingLocalEdit = true;
        const auto collapsed = deleteSepRange(sepLo, sepHi);
        m_applyingLocalEdit = false;
        if (!collapsed) return false;
        caret = sepViewPosOf(collapsed->block, collapsed->offsetInBlock);
        before = sepViewIndex().view;
    }

    const auto sepPos = qtPosToByteOffset(before, caret);
    const auto hit = findBlockAtSepByteCached(sepPos, false);
    if (!hit) return false;
    auto current = hit->blockId;
    int currentOffset = static_cast<int>(hit->byteInBlock);
    m_applyingLocalEdit = true;
    const auto result = StructuralKeyHandler::handle(*m_markoffDocument, current,
        key, modifiers, hit->byteInBlock);
    m_applyingLocalEdit = false;
    if (!result.handled) {
        if (qtPos != qtAnchor) {
            syncQtDocumentFromMarkoff();
            const int resolved = sepViewPosOf(current, currentOffset);
            emitCaret(resolved, resolved);
            return true;
        }
        return false;
    }
    current = result.caretBlock;
    currentOffset = static_cast<int>(result.caretByteInBlock);
    syncQtDocumentFromMarkoff();
    const int resolved = sepViewPosOf(current, currentOffset);
    emitCaret(resolved, resolved);
    return true;
}

void SourceTextDocumentBinding::onQtContentsChange(int qtPos, int charsRemoved, int charsAdded)
{
    if (m_applyingRemoteEdit || !m_markoffDocument || !m_textDocument) return;
    const QString before = sepViewIndex().view;
    const auto begin = qtPosToByteOffset(before, qtPos);
    const auto end = qtPosToByteOffset(before, qtPos + charsRemoved);
    const QString after = m_textDocument->toPlainText();
    const QByteArray inserted = after.mid(qtPos, charsAdded).toUtf8();
    const auto hitStart = findBlockAtSepByteCached(begin, false);
    const auto hitEnd = charsRemoved == 0 ? hitStart
        : findBlockAtSepByteCached(end, true);
    if (hitStart && hitEnd && hitStart->blockId == hitEnd->blockId
        && !inserted.contains('\n')) {
        const BlockEdit edit{hitStart->blockId, hitStart->byteInBlock,
            hitEnd->byteInBlock - hitStart->byteInBlock, inserted};
        m_applyingLocalEdit = true;
        const auto result = m_markoffDocument->applyBlockEdit(edit);
        m_applyingLocalEdit = false;
        if (result.state == VNext::ActionPublicationState::committed
            || result.state == VNext::ActionPublicationState::committedHistoryPending)
            return;
    }
    // Multiline paste is a structured replacement: the document parser
    // creates the inserted blocks, and one accepted action removes the
    // selected text/blocks, inserts their text/list/kind/attribute state, and
    // restores the original end-block tail on the final pasted block.
    if (inserted.contains('\n') && hitStart && hitEnd) {
        m_applyingLocalEdit = true;
        const auto pasted = m_markoffDocument->pasteMarkdownReplacingRange(
            hitStart->blockId, hitStart->byteInBlock,
            hitEnd->blockId, hitEnd->byteInBlock, inserted);
        m_applyingLocalEdit = false;
        if (pasted.publication.state == VNext::ActionPublicationState::committed
            || pasted.publication.state
                == VNext::ActionPublicationState::committedHistoryPending) {
            syncQtDocumentFromMarkoff();
            const int resolved = sepViewPosOf(pasted.caretBlock,
                static_cast<int>(pasted.caretByteOffset));
            emitCaret(resolved, resolved);
            return;
        }
    }
    // Deleting a separator merges the selected block range as one accepted
    // provider action spanning the survivor text, list and kind-map streams.
    if (hitStart && hitEnd && hitStart->blockIndex < hitEnd->blockIndex
        && inserted.isEmpty()) {
        const auto& blocks = sepViewIndex().order;
        const auto firstText = m_markoffDocument->blockText(hitStart->blockId);
        const auto lastText = m_markoffDocument->blockText(hitEnd->blockId);
        const QByteArray suffix = lastText.mid(static_cast<qsizetype>(hitEnd->byteInBlock));
        const BlockEdit merge{hitStart->blockId, hitStart->byteInBlock,
            static_cast<quint32>(firstText.size()) - hitStart->byteInBlock, suffix};
        QList<BlockId> removed;
        for (int i = hitStart->blockIndex + 1; i <= hitEnd->blockIndex; ++i)
            removed.append(blocks[static_cast<size_t>(i)]);
        m_applyingLocalEdit = true;
        const auto result = m_markoffDocument->applyBlockMerge(merge, removed);
        if (result.state == VNext::ActionPublicationState::committed
            || result.state == VNext::ActionPublicationState::committedHistoryPending) {
            m_applyingLocalEdit = false;
            return;
        }
        m_applyingLocalEdit = false;
    }
    // Until a mixed text/list atomic operation exists, reject splits and
    // edits that could not complete a multi-block merge.
    syncQtDocumentFromMarkoff();
}

void SourceTextDocumentBinding::onD2DocumentChanged()
{
    if (m_applyingLocalEdit || !m_textDocument || !m_subscribedDoc) return;
    syncQtDocumentFromMarkoff();
    if (m_pendingCaret) {
        const int position = sepViewPosOf(m_pendingCaret->block,
                                          m_pendingCaret->offsetInBlock);
        emitCaret(position, position);
        m_pendingCaret.reset();
    }
}

} // namespace Markoff
