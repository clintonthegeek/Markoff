// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {
class MarkoffDocument;

/// Result of a structural-key decision. `handled == false` means the key
/// was not a structural operation for this block/caret and the caller
/// should fall through to native editing. When handled, `caretBlock` +
/// `caretByteInBlock` declare where the caret should land after the model
/// settles (within-block UTF-8 byte offset).
struct StructuralResult {
    bool     handled = false;
    BlockId  caretBlock;
    uint32_t caretByteInBlock = 0;
};

/// Pure, view-agnostic structural-key dispatcher. Decides what a structural
/// key (Enter/Backspace/Delete/Tab) means for `block` given the caret byte
/// offset within it, applies the corresponding `Cmd::*` mutations to `doc`,
/// and returns the intended caret. Assumes an empty selection — the caller
/// (the binding) collapses any selection before calling.
class MARKOFF_CORE_EXPORT StructuralKeyHandler {
public:
    /// `key`/`modifiers` are Qt::Key / Qt::KeyboardModifiers as ints (kept
    /// as ints to avoid a Qt-namespace include in this header).
    static StructuralResult handle(MarkoffDocument &doc, BlockId block,
                                   int key, int modifiers,
                                   uint32_t caretByteInBlock);
};

}  // namespace Markoff
