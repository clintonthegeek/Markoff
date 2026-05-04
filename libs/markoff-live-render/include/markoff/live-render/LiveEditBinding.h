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

class MarkerScrubber;

/// Per-delegate edit binding. Translates `QTextDocument::contentsChange`
/// (qtPos, charsRemoved, charsAdded) into a `Markoff::MarkoffEdit` in
/// UTF-8 byte coordinates, calls `MarkoffDocument::applyLocalEdit`, and
/// stamps the row's `lastEditEditSequence` for the freshness rule
/// (spec §4.3, §7.1).
///
/// Cycle guards (spec §4.5):
///   - applyingModelUpdate: skip the contentsChange that fires
///     synchronously when the model updates the delegate's text.
///   - composing: skip during IME preedit; on commit (composing →
///     false) re-sync to the post-commit text.
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
    /// user edit. Replaces the older `text: model.text` direct binding
    /// on TextEdit (which caused infinite-loop content duplication
    /// because delegate creation/recycling fires contentsChange outside
    /// applyingModelUpdate's window).
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

    /// Marker-paragraph focus-out hook (spec §6.4). QML invokes this
    /// from the delegate's `Component.onDestruction` and from the
    /// TextEdit's `onActiveFocusChanged: if (!activeFocus) ...`. If the
    /// delegate's text was the model-pushed marker-only payload and the
    /// user focused in/out without typing, this calls
    /// `MarkerScrubber::scrubOnFocusOut(modelIndex)` to drop the marker
    /// paragraph from source.
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
    // QTextEdit-backed document and wire it via this seam. Production
    // code uses setTextDocument(QQuickTextDocument*) instead.
    friend class ::TstLiveRenderParagraphEdit;

    /// Test-only seam: wire directly to a QTextDocument without going
    /// through QQuickTextDocument. Used by unit tests that cannot
    /// instantiate a QQuickItem/QML scene. Production code must use
    /// setTextDocument(QQuickTextDocument*) instead.
    void setRawTextDocument(QTextDocument *td);

    void rewireTextDocument(QTextDocument *newDoc);
    void flushPendingComposition();
    void pushTextToDocument();

    QPointer<LiveListModelBinding> m_binding;
    QPointer<MarkerScrubber>        m_markerScrubber;  ///< borrowed; owned by LiveListModelBinding
    int                             m_modelIndex = -1;
    QPointer<QQuickTextDocument>    m_textDocument;
    QPointer<QTextDocument>         m_listenedDoc;  // remembered so we can disconnect
    bool                            m_composing = false;
    bool                            m_compositionPendingFlush = false;  // see Task 7
    bool                            m_applyingTextUpdate = false;  // pushTextToDocument re-entrance guard
    bool                            m_pendingMarkerScrub = false; ///< next contentsChange bundles marker scrub
    QString                         m_text;          // bound QML text (mirrors model.text)
    QString                         m_previousText;  // CRDT-coherent snapshot of m_listenedDoc's text
};

}  // namespace Markoff::LiveRender
