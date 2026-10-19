// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/RenderedBlock.h>

namespace Markoff {

/// Version of the renderer contract. A runtime refuses a backend built against
/// a different version instead of calling through a mismatched vtable. Bump on
/// any non-additive change to RenderRequest/RenderedBlock/this interface.
inline constexpr int kEmbedContractVersion = 1;

/// What a backend needs to render one block. Everything a renderer may vary on
/// is here: source, available width, DPI scale, and the resolved colours (a
/// backend never reads the Theme — the front-end resolves it, so a backend
/// stays free of core view types and its cache key is explicit).
struct MARKOFF_CORE_EXPORT RenderRequest {
    QString language;        ///< fenced info string, or a synth key ("math")
    QString source;          ///< block content, fences/delimiters excluded
    QSizeF  availableWidth{-1, -1};  ///< content column in DIP; -1 = unbounded
    qreal   devicePixelRatio = 1.0;
    qreal   fontScale = 1.0;
    QColor  foreground;
    QColor  background;
    QString themeKey;        ///< stable hash of the colours above (front-end set)
};

/// A backend: one embedded language family. `render()` may run on a worker
/// thread, must be re-entrant, and must not touch a QObject, a document, or a
/// view. Returning `Kind::Error`/`Empty` is a normal miss, not an exception.
class MARKOFF_CORE_EXPORT CodeBlockProcessor {
public:
    virtual ~CodeBlockProcessor() = default;

    /// Languages this backend answers for, lower-case, without fences.
    /// Aliases are allowed ("mermaid", "mmd").
    virtual QStringList languages() const = 0;

    /// Contract the backend was built against (default: this header's).
    virtual int contractVersion() const { return kEmbedContractVersion; }

    /// First registered language. Kept so consumers written against the older
    /// single-language form keep compiling (Corbomite's registration facade
    /// verifies its language argument against this); new code uses
    /// languages().
    QString language() const { return languages().value(0); }

    virtual RenderedBlock render(const RenderRequest &request) = 0;
};

}  // namespace Markoff
