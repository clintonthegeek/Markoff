// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/UndoCoalescer.h>

#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::LiveRender {

UndoCoalescer::UndoCoalescer(Markoff::MarkoffDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
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
    m_haveLast         = true;
    m_lastWasPrintable = false;
    m_lastAnchor       = Markoff::BlockAnchor{};
    m_lastTimer.restart();
}

void UndoCoalescer::notifyFocusChanged() { clearLast(); }
void UndoCoalescer::notifyMovement()      { clearLast(); }
void UndoCoalescer::notifyIdleExpired()   { clearLast(); }

void UndoCoalescer::clearLast()
{
    m_haveLast         = false;
    m_lastWasPrintable = false;
    m_lastAnchor       = Markoff::BlockAnchor{};
    m_lastTimer.invalidate();
}

}  // namespace Markoff::LiveRender
