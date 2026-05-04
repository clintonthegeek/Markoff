// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/BlockHitTester.h>
#include <markoff/live-render/LiveSelectionView.h>
#include <markoff/live-render/LiveStructuralKeyHandler.h>
#include <markoff/live-render/UndoCoalescer.h>
#include <markoff/live-render/LiveHoleLayer.h>
#include <markoff/live-render/LiveProxyBlockModel.h>
#include <markoff/live-render/MarkerScrubber.h>

#include <QObject>
#include <memory>
#include <qqmlintegration.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/BlockAnchor.h>

namespace Markoff { class Document; class Session; }

namespace Markoff::LiveRender {

class MARKOFF_LIVE_RENDER_EXPORT LiveListModelBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Markoff::MarkoffDocument *document
               READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Markoff::LiveRender::LiveBlockModel *model
               READ model CONSTANT)
    Q_PROPERTY(Markoff::LiveRender::LiveCursorState *cursorState
               READ cursorState CONSTANT)
    Q_PROPERTY(Markoff::LiveRender::BlockHitTester *hitTester
               READ hitTester CONSTANT)
    Q_PROPERTY(Markoff::LiveRender::LiveSelectionView *selectionView
               READ selectionView CONSTANT)
    Q_PROPERTY(Markoff::LiveRender::LiveStructuralKeyHandler *structuralKeyHandler
               READ structuralKeyHandler CONSTANT)
    Q_PROPERTY(Markoff::LiveRender::UndoCoalescer *undoCoalescer
               READ undoCoalescer CONSTANT)
    Q_PROPERTY(Markoff::LiveRender::LiveHoleLayer *holeLayer
               READ holeLayer NOTIFY holeLayerChanged)
    Q_PROPERTY(Markoff::LiveRender::LiveProxyBlockModel *proxyModel
               READ proxyModel NOTIFY proxyModelChanged)
    Q_PROPERTY(Markoff::LiveRender::MarkerScrubber *markerScrubber
               READ markerScrubber NOTIFY markerScrubberChanged)

public:
    explicit LiveListModelBinding(QObject *parent = nullptr);
    ~LiveListModelBinding() override;

    Markoff::MarkoffDocument *document() const;
    void setDocument(Markoff::MarkoffDocument *doc);

    LiveBlockModel           *model()               const;
    LiveCursorState          *cursorState()         const;
    BlockHitTester           *hitTester()           const;
    LiveSelectionView        *selectionView()       const;
    LiveStructuralKeyHandler *structuralKeyHandler() const;
    UndoCoalescer            *undoCoalescer()       const;
    const BlockKindRegistry  *registry()            const;
    LiveHoleLayer            *holeLayer()           const;
    LiveProxyBlockModel      *proxyModel()          const;
    MarkerScrubber           *markerScrubber()      const;

    /// True for the synchronous duration of `applyOps` while parse
    /// arrival is mutating model rows. LiveEditBinding queries this
    /// inside its `contentsChange` slot to suppress the synchronous
    /// QML-binding echo. Spec §4.5 (surviving setPlainText-echo
    /// suppression cycle guard).
    bool applyingModelUpdate() const;

    /// Commit all pending holes to source in ascending reifyAnchor byte
    /// order. Hosts must call this BEFORE serializing the document
    /// (Ctrl-S / save-to-disk path). After this returns, the layer holds
    /// zero holes and `document.toMarkdown()` equals the on-screen content.
    /// No-op when no doc is attached or no holes exist.
    Q_INVOKABLE void flushPendingHoles();

    /// Marker-paragraph save path (spec §6.4): hosts call this from
    /// their Ctrl-S / save-to-disk handler before serializing bytes.
    /// Routes to `MarkerScrubber::scrubBeforeSave`, which removes any
    /// marker-only paragraphs leaked into source. No-op when no doc is
    /// attached. Replaces `flushPendingHoles` for the marker-paragraph
    /// regime; the v2 hole flush stays in place until Tasks 13/15 retire
    /// the hole-layer code paths.
    Q_INVOKABLE void flushPendingMarkers();

Q_SIGNALS:
    void documentChanged();
    void holeLayerChanged();
    void proxyModelChanged();
    void markerScrubberChanged();

private:
    void onParseUpdated(const Markoff::Document *parsed,
                        quint64 parseSequence,
                        const QList<Markoff::BlockAnchor> &blockAnchors,
                        quint64 parseInputEditSequence);

    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff::LiveRender
