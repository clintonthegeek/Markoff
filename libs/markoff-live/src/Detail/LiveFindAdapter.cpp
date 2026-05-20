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
}

void LiveFindAdapter::detach()
{
    if (!m_controller) return;
    disconnect(m_controller, nullptr, this, nullptr);
    m_controller = nullptr;
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
