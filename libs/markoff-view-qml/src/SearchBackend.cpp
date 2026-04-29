// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/SearchBackend.h>

namespace Markoff::View::Qml {

SearchBackend::SearchBackend(QObject *parent) : QObject(parent) {}
SearchBackend::~SearchBackend() = default;

EditorBackend *SearchBackend::editorBackend() const { return m_editorBackend; }

void SearchBackend::setEditorBackend(EditorBackend *eb)
{
    if (m_editorBackend == eb) return;
    m_editorBackend = eb;
    Q_EMIT editorBackendChanged();
}

QString SearchBackend::needle() const { return m_needle; }

void SearchBackend::setNeedle(const QString &n)
{
    if (m_needle == n) return;
    m_needle = n;
    Q_EMIT needleChanged();
}

int SearchBackend::flags() const { return m_flags; }

void SearchBackend::setFlags(int f)
{
    if (m_flags == f) return;
    m_flags = f;
    Q_EMIT flagsChanged();
}

int SearchBackend::matchCount() const { return m_matchCount; }

int SearchBackend::findAll()
{
    if (!m_editorBackend) return 0;
    Markoff::MarkoffDocument *doc = m_editorBackend->document();
    Markoff::Session *sess = m_editorBackend->session();
    if (!doc || !sess) return 0;

    const auto findFlags = static_cast<Markoff::SearchEngine::FindFlags>(m_flags);
    const int n = m_engine.findAll(doc, sess, m_needle, findFlags);
    if (n != m_matchCount) {
        m_matchCount = n;
        Q_EMIT matchCountChanged();
    }
    return n;
}

bool SearchBackend::findNext()
{
    if (!m_editorBackend) return false;
    Markoff::MarkoffDocument *doc = m_editorBackend->document();
    Markoff::Session *sess = m_editorBackend->session();
    if (!doc || !sess) return false;
    return m_engine.findNext(doc, sess);
}

bool SearchBackend::findPrevious()
{
    if (!m_editorBackend) return false;
    Markoff::MarkoffDocument *doc = m_editorBackend->document();
    Markoff::Session *sess = m_editorBackend->session();
    if (!doc || !sess) return false;
    return m_engine.findPrevious(doc, sess);
}

void SearchBackend::clear()
{
    if (!m_editorBackend) return;
    Markoff::Session *sess = m_editorBackend->session();
    if (!sess) return;
    m_engine.clearMatches(sess);
    if (m_matchCount != 0) {
        m_matchCount = 0;
        Q_EMIT matchCountChanged();
    }
}

}  // namespace Markoff::View::Qml
