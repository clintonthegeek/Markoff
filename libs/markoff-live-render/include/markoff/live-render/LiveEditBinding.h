// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>

#include <markoff/live-render/LiveListModelBinding.h>

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <qqmlintegration.h>

// Note: header includes the full types for `LiveListModelBinding` and
// `QQuickTextDocument` because `QPointer<T>` member fields require T to
// be complete (Qt6 `QtPrivate::is_complete` static-assert). A bare
// forward declaration is rejected by the compiler at the point the
// member is declared, not by MOC.

// Forward declarations for the test-only friend grants below.
class TstLiveRenderParagraphEdit;

namespace Markoff::LiveRender {

/// Per-delegate edit binding. Translates `QTextDocument::contentsChange`
/// (qtPos, charsRemoved, charsAdded) into a D2 per-block buffer edit via
/// `MarkoffDocument::d2ApplyBufferEdit`, using within-block byte coordinates.
///
/// Cycle guards:
///   - applyingTextUpdate: skip the contentsChange that fires synchronously
///     when LiveEditBinding itself calls setPlainText to mirror the bound
///     `text` property (initial delegate load, ListView recycling, model
///     updates). Without this guard those non-user writes would be misread
///     as user edits and pumped back into the CRDT, duplicating content.
///   - composing: skip during IME preedit; on commit (composing → false)
///     re-sync to the post-commit text via flushPendingComposition().
class MARKOFF_LIVE_RENDER_EXPORT LiveEditBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Markoff::LiveRender::LiveListModelBinding *binding
               READ binding WRITE setBinding NOTIFY bindingChanged)
    Q_PROPERTY(int modelIndex
               READ modelIndex WRITE setModelIndex NOTIFY modelIndexChanged)
    Q_PROPERTY(QQuickTextDocument *textDocument
               READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
    Q_PROPERTY(bool composing
               READ composing WRITE setComposing NOTIFY composingChanged)
    /// The text that should be displayed in the wired QTextDocument.
    /// Bind this to `model.text` in QML — LiveEditBinding will push the
    /// value into the QTextDocument under the applyingTextUpdate guard
    /// so the resulting contentsChange echo is NOT misinterpreted as a
    /// user edit.
    Q_PROPERTY(QString text
               READ text WRITE setText NOTIFY textChanged)

public:
    explicit LiveEditBinding(QObject *parent = nullptr);
    ~LiveEditBinding() override;

    LiveListModelBinding *binding() const;
    void setBinding(LiveListModelBinding *b);

    int  modelIndex() const;
    void setModelIndex(int row);

    QQuickTextDocument *textDocument() const;
    void setTextDocument(QQuickTextDocument *td);

    bool composing() const;
    void setComposing(bool c);

    QString text() const;
    void    setText(const QString &t);

    /// Focus-lost hook. QML may invoke this from the delegate's
    /// `Component.onDestruction` and from the TextEdit's
    /// `onActiveFocusChanged`. In D2 this is a no-op (marker paragraph
    /// design is retired); the method is kept for QML API stability.
    Q_INVOKABLE void onFocusLost();

Q_SIGNALS:
    void bindingChanged();
    void modelIndexChanged();
    void textDocumentChanged();
    void composingChanged();
    void textChanged();

private Q_SLOTS:
    void onContentsChange(int qtPos, int charsRemoved, int charsAdded);

private:
    // Grant test-only access to setRawTextDocument. The Qt 6.11 quirk
    // (QQuickTextDocument(nullptr) crashes; a bare QTextDocument doesn't
    // fire the 3-arg contentsChange) means unit tests must use a
    // QTextEdit-backed document and wire it via this seam.
    friend class ::TstLiveRenderParagraphEdit;

    /// Test-only seam: wire directly to a QTextDocument without going
    /// through QQuickTextDocument. Used by unit tests that cannot
    /// instantiate a QQuickItem/QML scene.
    void setRawTextDocument(QTextDocument *td);

    void rewireTextDocument(QTextDocument *newDoc);
    void flushPendingComposition();
    void pushTextToDocument();

    QPointer<LiveListModelBinding> m_binding;
    int                            m_modelIndex = -1;
    QPointer<QQuickTextDocument>   m_textDocument;
    QPointer<QTextDocument>        m_listenedDoc;  // remembered so we can disconnect
    bool                           m_composing = false;
    bool                           m_compositionPendingFlush = false;
    bool                           m_applyingTextUpdate = false;  // pushTextToDocument re-entrance guard
    QString                        m_text;          // bound QML text (mirrors model.text)
    QString                        m_previousText;  // CRDT-coherent snapshot of m_listenedDoc's text
};

}  // namespace Markoff::LiveRender
