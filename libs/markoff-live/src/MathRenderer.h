// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_MATHRENDERER_H
#define MARKOFF_MATHRENDERER_H

#include <QImage>
#include <QString>

namespace Markoff {

/// Renders LaTeX math snippets to QImage via JKQTMathText, with a
/// process-wide cache keyed by (latex, displayMode, fontSize, dpr).
///
/// Used both by the inline-math text object (editor live preview) and
/// the HTML Renderer (reading view, where the result is base64-encoded
/// into a data URI).
class MathRenderer {
public:
    /// Default base font size in points for inline math. Display math is
    /// rendered slightly larger via DefaultDisplayBoost.
    static constexpr qreal DefaultInlineFontSize = 12.0;
    static constexpr qreal DefaultDisplayBoost = 14.0 / 12.0;

    /// Render the given LaTeX source. Returns a null image on parse failure.
    /// `displayMode` selects display vs inline math sizing.
    /// `fontSize` is the base font size in points; pass 0 to use the default.
    /// `dpr` is the target device pixel ratio (for HiDPI screens). Defaults
    /// to 3.0 so the cached glyph has enough pixels to look crisp when
    /// downsampled on regular (1x) displays.
    static QImage render(const QString &latex, bool displayMode,
                          qreal fontSize = 0.0, qreal dpr = 3.0);

    /// PNG-encoded base64 data URI of the rendered image. Empty on failure.
    /// Used by Renderer.cpp to embed math in HTML output.
    static QString renderToDataUri(const QString &latex, bool displayMode,
                                    qreal fontSize = 0.0);

    /// Drop all cached images. Useful for tests or memory pressure.
    static void clearCache();
};

} // namespace Markoff

#endif // MARKOFF_MATHRENDERER_H
