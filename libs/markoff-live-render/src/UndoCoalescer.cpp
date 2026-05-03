// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/UndoCoalescer.h>
#include <markoff/live-render/Cursor.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/LiveHoleLayer.h>

#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::LiveRender {

UndoCoalescer::UndoCoalescer(Markoff::MarkoffDocument *document,
                             LiveCursorState          *cursorState,
                             LiveHoleLayer            *holeLayer,
                             QObject                  *parent)
    : QObject(parent)
    , m_document(document)
    , m_cursorState(cursorState)
    , m_holeLayer(holeLayer)
{}

bool UndoCoalescer::recordPrintable(const Markoff::BlockAnchor &anchor)
{
    bool didCoalesce = false;
    if (m_haveLast
        && m_lastWasPrintable
        && m_lastAnchor == anchor
        && m_lastTimer.isValid()
        && m_lastTimer.elapsed() < kIdleThresholdMs)
    {
        if (m_document) {
            didCoalesce = m_document->coalesceLastUndo();
        }
    }
    m_haveLast         = true;
    m_lastWasPrintable = true;
    m_lastAnchor       = anchor;
    m_lastTimer.restart();
    return didCoalesce;
}

void UndoCoalescer::recordStructural()
{
    // Structural edit is its own undo unit; it BREAKS the printable chain.
    m_haveLast         = true;
    m_lastWasPrintable = false;
    m_lastAnchor       = Markoff::BlockAnchor{};
    m_lastTimer.restart();
}

void UndoCoalescer::recordOther()
{
    // Identical body to recordStructural for now; kept separate so R6+ can
    // diverge (per-kind telemetry, IME-specific behavior) without touching
    // call sites.
    m_haveLast         = true;
    m_lastWasPrintable = false;
    m_lastAnchor       = Markoff::BlockAnchor{};
    m_lastTimer.restart();
}

void UndoCoalescer::notifyFocusChanged() { clearLast(); }
void UndoCoalescer::notifyMovement()      { clearLast(); }
void UndoCoalescer::notifyIdleExpired()   { clearLast(); }

void UndoCoalescer::undo()
{
    if (m_cursorState && m_holeLayer) {
        const Cursor c = m_cursorState->cursor();
        if (const auto *tc = std::get_if<TextCaret>(&c)) {
            if (isHoleBlockId(tc->block)) {
                const quint64 holeId = holeIdOf(tc->block);
                if (m_holeLayer->undoBlockHole(holeId)) return;
                // Empty-buffer-empty-stack: drop the hole.
                m_holeLayer->abandonBlockHole(holeId);
                return;
            }
        }
    }
    if (m_document) m_document->undo();
}

void UndoCoalescer::redo()
{
    if (m_cursorState && m_holeLayer) {
        const Cursor c = m_cursorState->cursor();
        if (const auto *tc = std::get_if<TextCaret>(&c)) {
            if (isHoleBlockId(tc->block)) {
                m_holeLayer->redoBlockHole(holeIdOf(tc->block));
                return;
            }
        }
    }
    if (m_document) m_document->redo();
}

void UndoCoalescer::clearLast()
{
    m_haveLast         = false;
    m_lastWasPrintable = false;
    m_lastAnchor       = Markoff::BlockAnchor{};
    m_lastTimer.invalidate();
}

}  // namespace Markoff::LiveRender
