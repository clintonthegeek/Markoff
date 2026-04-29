// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/EditorBackend.h>

namespace Markoff::View::Qml {

EditorBackend::EditorBackend(QObject *parent) : QObject(parent) {}
EditorBackend::~EditorBackend() = default;

Markoff::MarkoffDocument *EditorBackend::document() const { return m_document; }

void EditorBackend::setDocument(Markoff::MarkoffDocument *doc)
{
    if (m_document == doc) return;

    // Clean up existing session before swapping document pointer.
    if (m_session && m_document) {
        m_document->destroySession(m_session);
        m_session = nullptr;
        Q_EMIT sessionChanged();
    }

    m_document = doc;
    Q_EMIT documentChanged();

    // Create session for new document.
    if (m_document) {
        m_session = m_document->createSession();
        Q_EMIT sessionChanged();
    }
}

Markoff::Session *EditorBackend::session() const { return m_session; }

Markoff::Theme EditorBackend::theme() const { return m_theme; }

void EditorBackend::setTheme(const Markoff::Theme &t)
{
    // Theme::operator== is not defined; emit unconditionally on every setTheme call.
    m_theme = t;
    Q_EMIT themeChanged();
}

}  // namespace Markoff::View::Qml
