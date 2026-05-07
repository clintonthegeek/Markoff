// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/BlockHitTester.h>

namespace Markoff::Live {

BlockHitTester::BlockHitTester(QObject *parent) : QObject(parent) {}

void BlockHitTester::reportHit(int blockIndex, int qtPos)
{
    m_lastBlockIndex = blockIndex;
    m_lastQtPos      = qtPos;
    Q_EMIT hitReported(blockIndex, qtPos);
}

}  // namespace Markoff::Live
