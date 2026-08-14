// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

namespace Markoff::Canvas::Detail {

/// Parsed shape of a `BlockKind::Image` block's buffer (P5.4): either a
/// standard markdown image `![alt](src "title")` or an Obsidian-style
/// block embed `![[target]]` / `![[target|alias]]`. `KindInference` maps
/// BOTH forms to `BlockKind::Image` (its own "starts with `![`" rule, no
/// further distinction — `KindInference.cpp`'s Image case), so this leaf
/// has to reparse the buffer itself to tell them apart. Pure string scan,
/// same "canvas-local rule" precedent as `CodeHighlighting::parseCodeFence`
/// — the parser's `SourceSpan::isImage` flag doesn't distinguish embed vs
/// standard-image form either (`SourceSpan.h` has no `isEmbed` flag).
struct ImageBlockInfo {
    bool isEmbed = false;
    /// The image `src`, or the embed `target` (a vault-relative path).
    /// Empty if the buffer didn't actually parse as either form.
    QString target;
    /// Image alt text, or an embed's `|alias` half. Empty if none given.
    QString altOrAlias;
};

/// Returns a default (`isEmbed=false`, empty `target`) for a buffer that
/// does not start with `"!["` at all — should not happen for a real
/// `BlockKind::Image` block, but defensive like `parseCodeFence`.
ImageBlockInfo parseImageBlock(const QByteArray &blockText);

}  // namespace Markoff::Canvas::Detail
