// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include <QPointer>
#include <QTextEdit>

#include <markoff/core/EditorContext.h>
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
class StyledTableRenderer;

namespace Detail { class StyledFindAdapter; }

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
    QRect                      caretRect() const override;

    // Find (frame-aware highlights via Detail::StyledFindAdapter)
    void attachFindController(Markoff::FindController *fc) override;
    void detachFindController() override;

    // Format verbs (MarkdownView contract v2 §5) — thin wrappers over
    // Markoff::FormatOps in widgetFlatView coordinates, guarded against
    // table frames (see the frame-guard comment in Editor.cpp).
    void toggleBold() override;
    void toggleItalic() override;
    void toggleStrikethrough() override;
    void toggleInlineCode() override;
    void insertLink() override;
    void setHeadingLevel(int level) override;

    // Session
    Markoff::Session *session() const;
    void              setSession(Markoff::Session *);

    // Theme
    Markoff::Theme theme() const override;
    void           setTheme(const Markoff::Theme &) override;

    // Link service (lazy DefaultLinkService when nullptr)
    Markoff::LinkService *linkService() const;
    void                  setLinkService(Markoff::LinkService *);

    // Wikilink resolution context (forwarded to LinkService)
    QString fromContext() const;
    void    setFromContext(const QString &);

    // Font scale (1.0 = default; Ctrl+/- multiplies block-format font sizes).
    // The base MarkdownView stores + emits; this override forwards to
    // StyleApplier and StyledTableRenderer (the only styled-leaf consumers).
    // No local copy: base fontScale() is the single authority (INVARIANTS §3).
    void  setFontScale(qreal) override;

    // Accessor for tests + internal helpers
    QTextEdit *textEdit() const;

    /// Test-only accessor: number of hash-skipped blocks during the
    /// most recent StyleApplier restyle pass.
    quint64 styleApplierHashSkips() const;

Q_SIGNALS:
    void sessionChanged();
    void linkServiceChanged();
    void fromContextChanged();

private:
    /// Recompute the EditorContext from the current caret position and emit
    /// contextChanged if the context has changed (change-gated, spec §7).
    void recomputeContext();

    StructuralTextEdit                     *m_editor       = nullptr;
    QPointer<Markoff::Session>              m_session;
    Markoff::SourceTextDocumentBinding     *m_binding      = nullptr;
    StyleApplier                           *m_styleApplier = nullptr;
    DocHighlighter                         *m_highlighter  = nullptr;
    Detail::StyledFindAdapter              *m_findAdapter  = nullptr;
    LinkInteraction                        *m_linkInteract = nullptr;
    // Non-QObject; owned via unique_ptr (Editor's dtor is out-of-line in the
    // .cpp where StyledTableRenderer is a complete type).
    std::unique_ptr<StyledTableRenderer>    m_tableRenderer;
    Markoff::Theme                          m_theme;
    Markoff::LinkService                   *m_linkService  = nullptr;
    mutable Markoff::DefaultLinkService    *m_defaultLink  = nullptr;
    QString                                 m_fromContext;
    QMetaObject::Connection                 m_d2ScrollCaptureCon;
    QMetaObject::Connection                 m_contextCursorCon;
    QMetaObject::Connection                 m_contextD2Con;
    Markoff::EditorContext                  m_lastContext;
};

}  // namespace Markoff::Styled
