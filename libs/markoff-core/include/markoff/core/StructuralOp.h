// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>
#include <variant>
namespace Markoff {
struct StructuralOp {
    struct InsertEntry { BlockId afterBlockId; BlockKind kind; };
    struct RemoveEntry { BlockId blockId; };
    struct ChangeKind  { BlockId blockId; BlockKind newKind; };
    std::variant<InsertEntry, RemoveEntry, ChangeKind> payload;
};
}
