// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_MERMAIDRENDERER_H
#define CORBOMITE_READINGVIEW_MERMAIDRENDERER_H

#include <QByteArray>
#include <QString>

namespace Corbomite::ReadingView {

/// Invoke the `mmdr` Rust FFI to render a mermaid diagram to SVG. Empty on
/// failure. Cross-reference: `libs/core/src/MarkdownRenderer.cpp` —
/// `renderMermaidToDataUri`.
class MermaidRenderer
{
public:
    static QByteArray renderSvg(const QString &mermaidText);
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_MERMAIDRENDERER_H
