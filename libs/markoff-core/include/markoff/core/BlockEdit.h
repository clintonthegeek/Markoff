// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/BlockId.h>
#include <QByteArray>
namespace Markoff {
struct BlockEdit {
    BlockId blockId;
    uint32_t withinBlockByteOffset;
    uint32_t removedBytes;
    QByteArray insertedUtf8;
};
}
