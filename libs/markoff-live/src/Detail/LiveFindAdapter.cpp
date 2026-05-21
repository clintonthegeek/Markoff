// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveFindAdapter.h"

#include <markoff/live/BlockRecord.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>

namespace Markoff::Live::Detail {

LiveFindAdapter::LiveFindAdapter(LiveBlockModel *model,
                                 LiveCursorState *cursorState,
                                 QObject *parent)
    : QObject(parent), m_model(model), m_cursorState(cursorState)
{}

LiveFindAdapter::~LiveFindAdapter() = default;

void LiveFindAdapter::attach(Markoff::FindController *fc)
{
    if (m_controller == fc) return;
    if (m_controller) detach();
    m_controller = fc;
    if (!m_controller) return;
    connect(m_controller, &Markoff::FindController::navigationRequested,
            this, &LiveFindAdapter::onNavigationRequested);
    connect(m_controller, &Markoff::FindController::matchesChanged,
            this, &LiveFindAdapter::onMatchesChanged);
    connect(m_controller, &Markoff::FindController::currentMatchChanged,
            this, &LiveFindAdapter::onCurrentMatchChanged);
    // If the controller already has matches (e.g. needle set before attach),
    // push them through.
    rebuildAndPushSpans();
}

void LiveFindAdapter::detach()
{
    if (!m_controller) return;
    disconnect(m_controller, nullptr, this, nullptr);
    m_controller = nullptr;
    // Clear all previously-pushed spans on the model.
    if (m_model) {
        for (auto it = m_lastPushed.constBegin(); it != m_lastPushed.constEnd(); ++it) {
            m_model->setFindSpans(it.key(), {});
        }
    }
    m_lastPushed.clear();
}

void LiveFindAdapter::onMatchesChanged()
{
    rebuildAndPushSpans();
}

void LiveFindAdapter::onCurrentMatchChanged()
{
    rebuildAndPushSpans();
}

void LiveFindAdapter::rebuildAndPushSpans()
{
    // FALSIFIABILITY STUB — should cause tst_live_find_adapter to fail.
    // Reverted by the next commit. Per invariant 4.
    return;
    if (!m_model) return;
    QHash<Markoff::BlockAnchor, QList<Markoff::Live::FindSpan>> nextByBlock;
    if (m_controller) {
        const QList<Markoff::FindController::Match> &matches = m_controller->matches();
        const int currentIdx = m_controller->currentMatchIndex();
        for (int i = 0; i < matches.size(); ++i) {
            const auto &m = matches[i];
            Markoff::Live::FindSpan span{
                /*byteOffset*/ m.byteOffset,
                /*byteLength*/ m.byteLength,
                /*isCurrent*/  (i == currentIdx)
            };
            nextByBlock[m.block].append(span);
        }
    }
    // Push to model for every block whose list changed, plus blocks that
    // previously had matches and no longer do (drop to empty list).
    QSet<Markoff::BlockAnchor> touched;
    for (auto it = nextByBlock.constBegin(); it != nextByBlock.constEnd(); ++it)
        touched.insert(it.key());
    for (auto it = m_lastPushed.constBegin(); it != m_lastPushed.constEnd(); ++it)
        touched.insert(it.key());
    for (const Markoff::BlockAnchor &anchor : std::as_const(touched)) {
        const auto &newSpans = nextByBlock.value(anchor);
        const auto &oldSpans = m_lastPushed.value(anchor);
        if (newSpans != oldSpans) {
            m_model->setFindSpans(anchor, newSpans);
        }
    }
    m_lastPushed = std::move(nextByBlock);
}

void LiveFindAdapter::onNavigationRequested(Markoff::FindController::Match m)
{
    if (!m_cursorState) return;
    const int qtPos = resolveByteToQtPos(m.block, m.byteOffset);
    m_cursorState->setCaretWithoutFocus(m.block, qtPos);
    // Scroll-into-view: deferred. The QML-side LiveView reacts to
    // cursorChanged for visible-row tracking under existing wiring;
    // explicit positionViewAtIndex is a future enhancement once we
    // have a consumer driving the controller.
}

int LiveFindAdapter::resolveByteToQtPos(const Markoff::BlockAnchor &block,
                                       quint32 byteOffset) const
{
    if (!m_model) return 0;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const auto &rec = m_model->recordAt(r);
        if (rec.blockAnchor != block) continue;
        const QByteArray utf8 = rec.text.toUtf8();
        const QByteArray prefix = utf8.left(static_cast<int>(byteOffset));
        return QString::fromUtf8(prefix).size();
    }
    return 0;
}

}  // namespace Markoff::Live::Detail
