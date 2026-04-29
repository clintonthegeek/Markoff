// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQmlIntegration>

#include <crdt/Anchor.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>
#include <markoff-foundation/Theme.h>

namespace Markoff::View::Qml {

class EditorBackend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Markoff::MarkoffDocument *document
               READ document
               WRITE setDocument
               NOTIFY documentChanged)
    Q_PROPERTY(Markoff::Session *session
               READ session
               NOTIFY sessionChanged)
    Q_PROPERTY(Markoff::Theme theme
               READ theme
               WRITE setTheme
               NOTIFY themeChanged)
    Q_PROPERTY(CollabText::Crdt::Anchor cursorAnchor
               READ cursorAnchor
               WRITE setCursorAnchor
               NOTIFY cursorAnchorChanged)
    Q_PROPERTY(CollabText::Crdt::Anchor selectionAnchor
               READ selectionAnchor
               WRITE setSelectionAnchor
               NOTIFY selectionAnchorChanged)
    Q_PROPERTY(CollabText::Crdt::Anchor selectionActive
               READ selectionActive
               WRITE setSelectionActive
               NOTIFY selectionActiveChanged)
public:
    explicit EditorBackend(QObject *parent = nullptr);
    ~EditorBackend() override;

    Markoff::MarkoffDocument *document() const;
    void                       setDocument(Markoff::MarkoffDocument *);

    Markoff::Session *session() const;

    Markoff::Theme theme() const;
    void           setTheme(const Markoff::Theme &);

    CollabText::Crdt::Anchor cursorAnchor() const;
    void setCursorAnchor(const CollabText::Crdt::Anchor &);

    CollabText::Crdt::Anchor selectionAnchor() const;
    void setSelectionAnchor(const CollabText::Crdt::Anchor &);

    CollabText::Crdt::Anchor selectionActive() const;
    void setSelectionActive(const CollabText::Crdt::Anchor &);

Q_SIGNALS:
    void documentChanged();
    void sessionChanged();
    void themeChanged();
    void cursorAnchorChanged();
    void selectionAnchorChanged();
    void selectionActiveChanged();
    void parseUpdatedAt(const Markoff::Document *parsed, CollabText::Crdt::Global atVersion);

private Q_SLOTS:
    void onSessionPrimarySelectionChanged(const Markoff::Selection &);

private:
    void pushSelectionToSession();

    Markoff::MarkoffDocument *m_document = nullptr;
    Markoff::Session         *m_session  = nullptr;
    Markoff::Theme            m_theme    = Markoff::Theme::defaultLight();
    CollabText::Crdt::Anchor  m_cursorAnchor;
    CollabText::Crdt::Anchor  m_selectionAnchor;
    CollabText::Crdt::Anchor  m_selectionActive;
    bool                      m_applyingSessionSelection = false;
};

}  // namespace Markoff::View::Qml

Q_DECLARE_METATYPE(CollabText::Crdt::Anchor)
Q_DECLARE_OPAQUE_POINTER(const Markoff::Document *)
