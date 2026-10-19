// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/CodeBlockProcessor.h>

namespace Markoff::Embed {

/// Display-math backend: jkqtmathtext, the engine already vendored for the
/// canvas leaf. Registers as "math" (plus the common aliases Obsidian notes
/// may use) and returns a rasterised `Image` artifact at the request's DPR —
/// the runtime does not rasterise this one because jkqtmathtext draws directly
/// to a pixmap (with an opaque background, which the request supplies).
class MathBackend final : public Markoff::CodeBlockProcessor {
public:
    QStringList languages() const override
    {
        return {QStringLiteral("math"), QStringLiteral("latex"), QStringLiteral("katex")};
    }

    Markoff::RenderedBlock render(const Markoff::RenderRequest &request) override;
};

/// Strip a math block's delimiters down to bare LaTeX: trims whitespace, then
/// removes a leading/trailing `$$` (display) or `$` (inline) pair. Pure string
/// surgery over the block source — the buffer keeps its delimiters.
QString stripMathDelimiters(const QString &source);

}  // namespace Markoff::Embed
