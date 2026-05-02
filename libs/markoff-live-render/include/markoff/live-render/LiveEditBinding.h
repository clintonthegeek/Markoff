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

namespace Markoff::LiveRender {

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

public:
    explicit LiveEditBinding(QObject *parent = nullptr);
    ~LiveEditBinding() override;

    LiveListModelBinding *binding() const;
    void setBinding(LiveListModelBinding *b);

    int  modelIndex() const;
    void setModelIndex(int row);

    QQuickTextDocument *textDocument() const;
    void setTextDocument(QQuickTextDocument *td);

    /// Test-only accessor: wire directly to a QTextDocument without
    /// going through QQuickTextDocument. Used by unit tests that cannot
    /// instantiate a QQuickItem/QML scene. Production code uses
    /// setTextDocument(QQuickTextDocument*) instead.
    void setRawTextDocument(QTextDocument *td);

    bool composing() const;
    void setComposing(bool c);

Q_SIGNALS:
    void bindingChanged();
    void modelIndexChanged();
    void textDocumentChanged();
    void composingChanged();

private Q_SLOTS:
    void onContentsChange(int qtPos, int charsRemoved, int charsAdded);

private:
    void rewireTextDocument(QTextDocument *newDoc);
    void flushPendingComposition();

    QPointer<LiveListModelBinding> m_binding;
    int                             m_modelIndex = -1;
    QPointer<QQuickTextDocument>    m_textDocument;
    QPointer<QTextDocument>         m_listenedDoc;  // remembered so we can disconnect
    bool                            m_composing = false;
    bool                            m_compositionPendingFlush = false;  // see Task 7
};

}  // namespace Markoff::LiveRender
