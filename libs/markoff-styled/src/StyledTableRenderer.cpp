// SPDX-License-Identifier: GPL-3.0-or-later
#include "StyledTableRenderer.h"

#include <QString>

#include <markoff/core/MarkoffDocument.h>

#include "TableFrame.h"

namespace Markoff::Styled {

bool StyledTableRenderer::isOpaque(Markoff::BlockId id,
                                   Markoff::BlockKind kind) const {
    if (kind != Markoff::BlockKind::Table || !m_doc) return false;
    // Degrade-to-text valve: only claim opacity when the buffer parses as a
    // table. A malformed table block renders as ordinary text instead.
    return parsePipeTable(m_doc->blockText(id)).ok;
}

int StyledTableRenderer::renderOpaque(QTextCursor &at, Markoff::BlockId id) {
    if (!m_doc) return 0;
    ParsedTable t = parsePipeTable(m_doc->blockText(id));
    if (!t.ok) return 0;
    const QString key = QStringLiteral("markoff-table:")
                        + QString::number(id.raw());
    return materializeTable(at, t, key, m_fontScale);
}

}  // namespace Markoff::Styled
