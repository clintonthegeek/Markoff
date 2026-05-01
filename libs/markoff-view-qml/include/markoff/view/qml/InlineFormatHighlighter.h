// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSyntaxHighlighter>
#include <QList>
#include <qqmlintegration.h>

#include <markoff-parser/SourceSpan.h>

#include <markoff/view/qml/LiveProjectionLayer.h>
#include <markoff/view/qml/ProjectionItem.h>

class QQuickTextDocument;
Q_DECLARE_OPAQUE_POINTER(QQuickTextDocument *)

namespace Markoff::View::Qml {

/// Highlights inline markdown formatting (bold, italic, code, strikethrough,
/// links, highlights) inside a single-block TextEdit. Attaches to the
/// QTextDocument exposed by QQuickTextDocument.
///
/// QML usage:
///   TextEdit {
///       id: edit
///       textFormat: TextEdit.PlainText
///       InlineFormatHighlighter {
///           document: edit.textDocument
///           source: model.text   // or model.source for raw markdown
///           projectionLayer: binding.projectionLayer
///           blockIndex: index
///       }
///   }
///
/// `source` should be the raw markdown source for this block (not stripped
/// heading text). The highlighter parses it, builds spans, then maps them
/// over the text currently shown in the TextEdit.
///
/// Stage-3 of the projection-layer plan rewires this highlighter as a
/// *producer* of `InlinePrediction`s into a `LiveProjectionLayer`. The
/// speculative open-delimiter scan publishes predictions to the layer keyed
/// by `blockIndex` instead of painting directly. During `highlightBlock` the
/// highlighter reads the predictions back from the layer for its row and
/// applies the formats. The layer's `onParseUpdated` clears all inline
/// predictions wholesale on each parse return; the next `setSource` call
/// (driven by the next keystroke / model update) republishes whatever is
/// still applicable. When no layer is wired (e.g. unit tests), the
/// highlighter falls back to an internal prediction list and behaviour is
/// identical to pre-Stage-3.
class InlineFormatHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document
               READ quickDocument
               WRITE setQuickDocument
               NOTIFY quickDocumentChanged)
    Q_PROPERTY(QString source
               READ source
               WRITE setSource
               NOTIFY sourceChanged)
    Q_PROPERTY(Markoff::View::Qml::LiveProjectionLayer *projectionLayer
               READ projectionLayer
               WRITE setProjectionLayer
               NOTIFY projectionLayerChanged)
    Q_PROPERTY(int blockIndex
               READ blockIndex
               WRITE setBlockIndex
               NOTIFY blockIndexChanged)

public:
    explicit InlineFormatHighlighter(QObject *parent = nullptr);

    QQuickTextDocument *quickDocument() const;
    void setQuickDocument(QQuickTextDocument *doc);

    QString source() const;
    void setSource(const QString &src);

    LiveProjectionLayer *projectionLayer() const;
    void setProjectionLayer(LiveProjectionLayer *layer);

    int blockIndex() const;
    void setBlockIndex(int idx);

Q_SIGNALS:
    void quickDocumentChanged();
    void sourceChanged();
    void projectionLayerChanged();
    void blockIndexChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildSpans();
    /// Source-scan for unclosed inline delimiters. Produces InlinePrediction
    /// values for the current row and (a) registers them with the layer if
    /// one is wired, (b) caches them in `m_fallbackPredictions` when no
    /// layer is wired so standalone tests still work.
    void publishInlinePredictions(const QString &source);

    /// Read predictions back for this row. Returns layer-registered ones if
    /// a layer is wired, else the internal fallback set.
    QList<InlinePrediction> currentPredictions() const;

    QQuickTextDocument *m_quickDoc = nullptr;
    QString             m_source;
    QList<SourceSpan>   m_spans;   // cached span list for the current source

    LiveProjectionLayer *m_layer = nullptr;
    int                  m_blockIndex = -1;
    /// Internal prediction cache for the no-layer (test) path. Always
    /// populated by `publishInlinePredictions` regardless of layer state so
    /// `currentPredictions()` has a fallback if the layer cleared its
    /// registry between source-scan and highlightBlock.
    QList<InlinePrediction> m_fallbackPredictions;
};

}  // namespace Markoff::View::Qml
