// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/ReplaceController.h>

#include <QLoggingCategory>

#include <markoff/MarkoffDocument.h>
#include <markoff/SearchAdapter.h>

namespace {
Q_LOGGING_CATEGORY(lc, "markoff.core.replace")
}

namespace Markoff {

ReplaceController::ReplaceController(MarkoffDocument *doc,
                                     SearchAdapter *adapter,
                                     QObject *parent)
    : SearchController(doc, adapter, parent)
{}

void ReplaceController::replaceCurrent(const QString &with)
{
    if (!m_adapter->supportsReplace()) {
        qCWarning(lc) << "replaceCurrent refused: adapter is read-only";
        return;
    }
    if (m_current < 0 || m_current >= m_matches.size()) return;
    const TextSpan s = m_matches[m_current];
    m_doc->replace(s.offset, s.length, with);
}

int ReplaceController::replaceAll(const QString &with)
{
    if (!m_adapter->supportsReplace()) {
        qCWarning(lc) << "replaceAll refused: adapter is read-only";
        return 0;
    }
    if (m_matches.isEmpty()) return 0;

    QVector<TextSpan> spans = m_matches;
    const int count = spans.size();

    m_doc->beginTransaction();
    for (int i = count - 1; i >= 0; --i) {
        const TextSpan s = spans[i];
        m_doc->replace(s.offset, s.length, with);
    }
    m_doc->endTransaction();
    return count;
}

}  // namespace Markoff
