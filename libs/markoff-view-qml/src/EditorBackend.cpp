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
        connect(m_session, &Markoff::Session::primarySelectionChanged,
                this, &EditorBackend::onSessionPrimarySelectionChanged);
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

CollabText::Crdt::Anchor EditorBackend::cursorAnchor() const { return m_cursorAnchor; }

void EditorBackend::setCursorAnchor(const CollabText::Crdt::Anchor &a)
{
    if (m_cursorAnchor == a) return;
    m_cursorAnchor = a;

    // Lift to Session as a degenerate (cursor) selection.
    if (m_session && !m_applyingSessionSelection) {
        Markoff::Selection sel;
        sel.anchor = a;
        sel.active = a;
        sel.kind   = Markoff::Selection::Kind::Primary;
        m_session->setPrimarySelection(sel);
    }
    Q_EMIT cursorAnchorChanged();
}

void EditorBackend::onSessionPrimarySelectionChanged(const Markoff::Selection &sel)
{
    // Only update cursorAnchor when selection is degenerate (anchor == active).
    // Non-degenerate selections (T6) are reported via selectionAnchor/selectionActive.
    if (sel.anchor != sel.active) return;

    if (m_cursorAnchor == sel.anchor) return;
    m_cursorAnchor = sel.anchor;
    m_applyingSessionSelection = true;
    Q_EMIT cursorAnchorChanged();
    m_applyingSessionSelection = false;
}

}  // namespace Markoff::View::Qml
