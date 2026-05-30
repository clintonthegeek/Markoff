// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointer>
#include <QTextEdit>

#include <markoff/core/LinkService.h>
#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/Theme.h>
#include <markoff/styled/MarkoffStyledExport.h>

namespace Markoff {
class SourceTextDocumentBinding;
class DefaultLinkService;
}

namespace Markoff::Styled {

class StyleApplier;
class DocHighlighter;
class LinkInteraction;
class StructuralTextEdit;

class MARKOFF_STYLED_EXPORT Editor : public Markoff::MarkdownView {
    Q_OBJECT
    Q_PROPERTY(Markoff::Session *session
               READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(Markoff::Theme theme
               READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(Markoff::LinkService *linkService
               READ linkService WRITE setLinkService NOTIFY linkServiceChanged)
    Q_PROPERTY(QString fromContext
               READ fromContext WRITE setFromContext NOTIFY fromContextChanged)
    Q_PROPERTY(qreal fontScale
               READ fontScale WRITE setFontScale NOTIFY fontScaleChanged)
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    // MarkdownView contract
    void                       setDocument(Markoff::MarkoffDocument *doc) override;
    Markoff::CursorPos         cursorPosition() const override;
    void                       setCursorPosition(Markoff::CursorPos pos) override;
    float                      scrollPositionVisualLine() const override;
    void                       setScrollPositionVisualLine(float pos) override;
    void                       setReadOnly(bool ro) override;
    bool                       isReadOnly() const override;
    bool                       hasCursor()  const override { return true; }
    bool                       hasEditing() const override { return !isReadOnly(); }

    // Session
    Markoff::Session *session() const;
    void              setSession(Markoff::Session *);

    // Theme
    Markoff::Theme theme() const;
    void           setTheme(const Markoff::Theme &);

    // Link service (lazy DefaultLinkService when nullptr)
    Markoff::LinkService *linkService() const;
    void                  setLinkService(Markoff::LinkService *);

    // Wikilink resolution context (forwarded to LinkService)
    QString fromContext() const;
    void    setFromContext(const QString &);

    // Font scale (1.0 = default; Ctrl+/- multiplies block-format font sizes)
    qreal fontScale() const;
    void  setFontScale(qreal);

    // Accessor for tests + internal helpers
    QTextEdit *textEdit() const;

    /// Test-only accessor: number of hash-skipped blocks during the
    /// most recent StyleApplier restyle pass.
    quint64 styleApplierHashSkips() const;

Q_SIGNALS:
    void sessionChanged();
    void themeChanged();
    void linkServiceChanged();
    void fromContextChanged();
    void fontScaleChanged();

private:
    StructuralTextEdit                     *m_editor       = nullptr;
    QPointer<Markoff::Session>              m_session;
    Markoff::SourceTextDocumentBinding     *m_binding      = nullptr;
    StyleApplier                           *m_styleApplier = nullptr;
    DocHighlighter                         *m_highlighter  = nullptr;
    LinkInteraction                        *m_linkInteract = nullptr;
    Markoff::Theme                          m_theme;
    Markoff::LinkService                   *m_linkService  = nullptr;
    mutable Markoff::DefaultLinkService    *m_defaultLink  = nullptr;
    QString                                 m_fromContext;
    qreal                                   m_fontScale    = 1.0;
    QMetaObject::Connection                 m_d2ScrollCaptureCon;
};

}  // namespace Markoff::Styled
