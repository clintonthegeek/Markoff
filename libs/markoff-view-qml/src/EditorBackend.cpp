// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <markoff/view/qml/EditorBackend.h>

#include <markoff-foundation/TextAnchor.h>

namespace Markoff::View::Qml {

namespace {
/// Inline conversion at the EditorBackend↔Selection seam. T10/T11 changed
/// Selection.anchor / .active to TextAnchor; EditorBackend's m_selectionAnchor
/// and m_cursorAnchor remain Crdt::Anchor until T12 retypes them. These
/// helpers bridge the two without leaking the foundation-internal
/// AnchorConversion.h into view-qml.
inline Markoff::TextAnchor toTextAnchor(const CollabText::Crdt::Anchor &a) noexcept
{
    return Markoff::TextAnchor{
        a.replica_id,
        a.char_value,
        (a.bias == CollabText::Crdt::Bias::Right) ? quint8(1) : quint8(0)
    };
}

inline CollabText::Crdt::Anchor toCrdtAnchor(const Markoff::TextAnchor &t) noexcept
{
    using CollabText::Crdt::Bias;
    return CollabText::Crdt::Anchor{
        t.replicaId,
        t.charValue,
        t.bias == 1 ? Bias::Right : Bias::Left
    };
}
}  // namespace

EditorBackend::EditorBackend(QObject *parent) : QObject(parent) {}
EditorBackend::~EditorBackend() = default;

Markoff::MarkoffDocument *EditorBackend::document() const { return m_document; }

void EditorBackend::setDocument(Markoff::MarkoffDocument *doc)
{
    if (m_document == doc) return;

    // Disconnect parseUpdated relay from old document before replacing.
    if (m_document) {
        QObject::disconnect(m_document, &Markoff::MarkoffDocument::parseUpdated,
                            this, nullptr);
    }

    // Clean up existing session before swapping document pointer.
    if (m_session && m_document) {
        m_document->destroySession(m_session);
        m_session = nullptr;
        Q_EMIT sessionChanged();
    }

    m_document = doc;
    Q_EMIT documentChanged();

    // Create session for new document and connect relay signals.
    if (m_document) {
        QObject::connect(m_document, &Markoff::MarkoffDocument::parseUpdated,
                         this, &EditorBackend::parseUpdatedAt);
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

    // Cursor set => collapse selection to degenerate (anchor = active = a).
    bool selectionAnchorMoved = (m_selectionAnchor != a);
    bool selectionActiveMoved = (m_selectionActive != a);
    m_selectionAnchor = a;
    m_selectionActive = a;

    if (m_session && !m_applyingSessionSelection) {
        Markoff::Selection sel;
        sel.anchor = toTextAnchor(a);
        sel.active = toTextAnchor(a);
        sel.kind   = Markoff::Selection::Kind::Primary;
        m_session->setPrimarySelection(sel);
    }

    Q_EMIT cursorAnchorChanged();
    if (selectionAnchorMoved) Q_EMIT selectionAnchorChanged();
    if (selectionActiveMoved) Q_EMIT selectionActiveChanged();
}

CollabText::Crdt::Anchor EditorBackend::selectionAnchor() const { return m_selectionAnchor; }

void EditorBackend::setSelectionAnchor(const CollabText::Crdt::Anchor &a)
{
    if (m_selectionAnchor == a) return;
    m_selectionAnchor = a;
    pushSelectionToSession();
    Q_EMIT selectionAnchorChanged();
}

CollabText::Crdt::Anchor EditorBackend::selectionActive() const { return m_selectionActive; }

void EditorBackend::setSelectionActive(const CollabText::Crdt::Anchor &a)
{
    if (m_selectionActive == a) return;
    m_selectionActive = a;
    pushSelectionToSession();
    Q_EMIT selectionActiveChanged();
}

void EditorBackend::pushSelectionToSession()
{
    if (!m_session || m_applyingSessionSelection) return;
    Markoff::Selection sel;
    sel.anchor = toTextAnchor(m_selectionAnchor);
    sel.active = toTextAnchor(m_selectionActive);
    sel.kind   = Markoff::Selection::Kind::Primary;
    m_session->setPrimarySelection(sel);
}

void EditorBackend::undo()
{
    if (m_document) m_document->undo();
}

void EditorBackend::redo()
{
    if (m_document) m_document->redo();
}

QString EditorBackend::copySelectionAsMarkdown() const
{
    if (!m_document) return QString();

    const quint32 anchorOff = m_document->resolveAnchor(m_selectionAnchor);
    const quint32 activeOff = m_document->resolveAnchor(m_selectionActive);
    const quint32 lo = std::min(anchorOff, activeOff);
    const quint32 hi = std::max(anchorOff, activeOff);

    if (lo == hi) return QString();  // empty / degenerate selection

    const QByteArray src = m_document->toMarkdownUtf8();
    return QString::fromUtf8(src.mid(static_cast<int>(lo),
                                     static_cast<int>(hi - lo)));
}

void EditorBackend::onSessionPrimarySelectionChanged(const Markoff::Selection &sel)
{
    m_applyingSessionSelection = true;

    // selectionAnchor + selectionActive always reflect the session selection's two ends.
    const CollabText::Crdt::Anchor selAnchor = toCrdtAnchor(sel.anchor);
    const CollabText::Crdt::Anchor selActive = toCrdtAnchor(sel.active);
    if (m_selectionAnchor != selAnchor) {
        m_selectionAnchor = selAnchor;
        Q_EMIT selectionAnchorChanged();
    }
    if (m_selectionActive != selActive) {
        m_selectionActive = selActive;
        Q_EMIT selectionActiveChanged();
    }

    // cursorAnchor is the "active" end (where the cursor is). For a degenerate
    // selection (anchor == active), it's just the cursor position.
    if (m_cursorAnchor != selActive) {
        m_cursorAnchor = selActive;
        Q_EMIT cursorAnchorChanged();
    }

    m_applyingSessionSelection = false;
}

}  // namespace Markoff::View::Qml
