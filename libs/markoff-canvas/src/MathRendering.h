// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QPixmap>
#include <QString>

namespace Markoff::Canvas::Detail {

/// Render `latex` (delimiters already stripped, see stripMathDelimiters()
/// below) via jkqtmathtext to a QPixmap, for a display Math block's
/// caret-outside paint (P5.3, plan's "display math as a painted block").
/// `pixelSize` is the already fontScale-adjusted size (BlockPresentation's
/// convention, same as every other block's font). `foreground` paints the
/// glyphs; `backgroundColor` fills the pixmap's backing rect — jkqtmathtext
/// has no transparent-background draw path (drawIntoPixmap always paints
/// an opaque rect), so the caller must pass the same color the block's own
/// background paints (or the editor background if the block has none), or
/// a visible seam will show around the glyphs. Returns a null QPixmap if
/// `latex` fails to parse — callers fall back to the plain text layout
/// (same fallback shape as CodeHighlighting's "service miss renders plain
/// monospace").
QPixmap renderMathPixmap(const QString &latex, qreal pixelSize,
                         const QColor &foreground, const QColor &backgroundColor);

/// Strip a Math block's raw buffer text down to bare LaTeX: trims
/// surrounding whitespace, then removes a leading/trailing `$$` (display)
/// or single `$` (inline) delimiter pair. Pure string surgery over
/// `blockText()` — same "canvas-local rule" precedent as
/// CodeHighlighting::parseCodeFence (T1: passthrough kinds keep their
/// markers inline in the buffer, so extracting the bare source is a
/// leaf-local scan, not an AST-span query).
QString stripMathDelimiters(const QString &text);

}  // namespace Markoff::Canvas::Detail
