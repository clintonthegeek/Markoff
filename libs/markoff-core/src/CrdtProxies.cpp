// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/CrdtProxies.h>

namespace Markoff {

BufferProxy::BufferProxy(BlockId blockId, QObject *parent)
    : QObject(parent), m_blockId(blockId) {}

void BufferProxy::notifyChanged()
{
    ++m_editSequence;
    Q_EMIT inlineSpansChanged();
}

IdListProxy::IdListProxy(QObject *parent) : QObject(parent) {}

void IdListProxy::notifyChanged()
{
    ++m_editSequence;
    Q_EMIT structureChanged();
}

SiblingMapProxy::SiblingMapProxy(QObject *parent) : QObject(parent) {}

void SiblingMapProxy::notifyChanged()
{
    ++m_editSequence;
    Q_EMIT mapChanged();
}

}  // namespace Markoff
