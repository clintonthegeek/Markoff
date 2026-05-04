// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/BlockId.h>
#include <markoff-foundation/BlockKind.h>
#include <variant>
namespace Markoff {
struct StructuralOp {
    struct InsertEntry { BlockId afterBlockId; BlockKind kind; };
    struct RemoveEntry { BlockId blockId; };
    struct ChangeKind  { BlockId blockId; BlockKind newKind; };
    std::variant<InsertEntry, RemoveEntry, ChangeKind> payload;
};
}
