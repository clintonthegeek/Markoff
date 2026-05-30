// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/OpaqueBlockRenderer.h>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::Styled {

/// Renders BlockKind::Table blocks as native QTextTable frames for the styled
/// view's SourceTextDocumentBinding (read-only phase). A Table block is opaque
/// only when its buffer actually parses as a GFM table — otherwise it degrades
/// to text (the binding falls back to plain rendering).
class StyledTableRenderer : public Markoff::OpaqueBlockRenderer {
public:
    void setMarkoffDocument(Markoff::MarkoffDocument *d) { m_doc = d; }
    void setFontScale(qreal s) { m_fontScale = s; }

    bool isOpaque(Markoff::BlockId id, Markoff::BlockKind kind) const override;
    int  renderOpaque(QTextCursor &at, Markoff::BlockId id) override;

private:
    Markoff::MarkoffDocument *m_doc = nullptr;
    qreal m_fontScale = 1.0;
};

}  // namespace Markoff::Styled
