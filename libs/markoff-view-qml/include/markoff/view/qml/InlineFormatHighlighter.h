// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSyntaxHighlighter>
#include <QList>
#include <qqmlintegration.h>

#include <markoff-parser/SourceSpan.h>

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
///       }
///   }
///
/// `source` should be the raw markdown source for this block (not stripped
/// heading text). The highlighter parses it, builds spans, then maps them
/// over the text currently shown in the TextEdit.
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

public:
    explicit InlineFormatHighlighter(QObject *parent = nullptr);

    QQuickTextDocument *quickDocument() const;
    void setQuickDocument(QQuickTextDocument *doc);

    QString source() const;
    void setSource(const QString &src);

Q_SIGNALS:
    void quickDocumentChanged();
    void sourceChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildSpans();

    QQuickTextDocument *m_quickDoc = nullptr;
    QString             m_source;
    QList<SourceSpan>   m_spans;   // cached span list for the current source
};

}  // namespace Markoff::View::Qml
