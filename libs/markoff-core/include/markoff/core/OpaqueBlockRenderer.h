// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QTextFormat>

#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>

class QTextCursor;

namespace Markoff {

/// Frame-format property carrying the source `BlockId.raw()` of an opaque frame
/// (e.g. a QTextTable). The reverse path reads it to match an existing frame to
/// its model block. Qt has no `comment` on QTextTableFormat; we use a custom
/// UserProperty string instead.
inline constexpr int OpaqueBlockKeyProperty = QTextFormat::UserProperty + 17;

/// View-supplied hook letting SourceTextDocumentBinding render selected blocks
/// as opaque document objects (e.g. a QTextTable frame) instead of flat text.
/// The binding stays view-agnostic: it knows only "this block is opaque" and
/// "(re)build it here". Registering a renderer switches the binding's reverse
/// path to per-block reconciliation; with no renderer the binding keeps its
/// original whole-document text diff (markoff-source is unaffected).
///
/// Spec: docs/superpowers/specs/2026-05-30-styled-table-rendering-design.md §3.
class OpaqueBlockRenderer {
public:
    virtual ~OpaqueBlockRenderer() = default;

    /// True if `id` (of kind `kind`) should be rendered as an opaque object.
    virtual bool isOpaque(BlockId id, BlockKind kind) const = 0;

    /// (Re)build the opaque representation for `id`. The binding has already
    /// positioned `at` at the block's insertion point (and, on re-render,
    /// removed the block's previous document region). The callee inserts its
    /// object and leaves `at` at the region end. Returns the number of
    /// QTextDocument characters the inserted representation occupies.
    virtual int renderOpaque(QTextCursor &at, BlockId id) = 0;
};

}  // namespace Markoff
