// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QRectF>

#include <markoff/styled/MarkoffStyledExport.h>

class QPainter;
class QTextDocument;

namespace Markoff {
class MarkoffDocument;
class Theme;
}

namespace Markoff::Styled {

/// Headless, read-only Markoff renderer. Formats a MarkoffDocument into a
/// caller-owned QTextDocument (T1) and measures/paints it without a widget
/// (T2). No cursor, no editing, no model mutation. Honors
/// `const MarkoffDocument*` so the read-only contract is enforced by the type.
/// Spec: docs/specs/2026-05-29-markoff-styled-document-renderer-design.md.
class MARKOFF_STYLED_EXPORT DocumentRenderer {
public:
    DocumentRenderer();
    ~DocumentRenderer();

    void setTheme(const Markoff::Theme *theme);  ///< non-owning, may be null
    void setFontScale(qreal s);

    // T1 — populate a caller-owned document.
    void renderInto(QTextDocument *target,
                    const Markoff::MarkoffDocument *source) const;
    void renderInto(QTextDocument *target,
                    const QByteArray &markdownUtf8) const;

    // T2 — convenience one-shots (build a transient doc over T1). For per-frame
    // canvas paint, own a QTextDocument, renderInto it once, and paint/measure
    // it directly instead.
    qreal idealHeight(const Markoff::MarkoffDocument *source, qreal width) const;
    void  paint(QPainter *painter, const QRectF &rect,
                const Markoff::MarkoffDocument *source) const;

private:
    const Markoff::Theme *m_theme     = nullptr;
    qreal                 m_fontScale = 1.0;
};

}  // namespace Markoff::Styled
