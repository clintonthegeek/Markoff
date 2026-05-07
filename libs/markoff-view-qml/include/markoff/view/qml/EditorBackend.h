// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQmlIntegration>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>
#include <markoff-foundation/TextAnchor.h>
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
    Q_PROPERTY(Markoff::TextAnchor cursorAnchor
               READ cursorAnchor
               WRITE setCursorAnchor
               NOTIFY cursorAnchorChanged)
    Q_PROPERTY(Markoff::TextAnchor selectionAnchor
               READ selectionAnchor
               WRITE setSelectionAnchor
               NOTIFY selectionAnchorChanged)
    Q_PROPERTY(Markoff::TextAnchor selectionActive
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

    Markoff::TextAnchor cursorAnchor() const;
    void setCursorAnchor(const Markoff::TextAnchor &);

    Markoff::TextAnchor selectionAnchor() const;
    void setSelectionAnchor(const Markoff::TextAnchor &);

    Markoff::TextAnchor selectionActive() const;
    void setSelectionActive(const Markoff::TextAnchor &);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE QString copySelectionAsMarkdown() const;

Q_SIGNALS:
    void documentChanged();
    void sessionChanged();
    void themeChanged();
    void cursorAnchorChanged();
    void selectionAnchorChanged();
    void selectionActiveChanged();

private Q_SLOTS:
    void onSessionPrimarySelectionChanged(const Markoff::Selection &);

private:
    void pushSelectionToSession();

    Markoff::MarkoffDocument *m_document = nullptr;
    Markoff::Session         *m_session  = nullptr;
    Markoff::Theme            m_theme    = Markoff::Theme::defaultLight();
    Markoff::TextAnchor       m_cursorAnchor;
    Markoff::TextAnchor       m_selectionAnchor;
    Markoff::TextAnchor       m_selectionActive;
    bool                      m_applyingSessionSelection = false;
};

}  // namespace Markoff::View::Qml

Q_DECLARE_METATYPE(Markoff::TextAnchor)
