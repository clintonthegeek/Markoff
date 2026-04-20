// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QTextDocument;

namespace Markoff {

class Document;                          // markoff-parser

/// Canonical markdown source + undo + cached parse. Views attach via
/// MarkdownView::setDocument(). In Phase A the leaf widgets still
/// own their own content; attaching a MarkoffDocument stores the
/// pointer but doesn't yet bind text. Phase C flips that.
class MarkoffDocument : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MarkoffDocument)
public:
    explicit MarkoffDocument(QObject *parent = nullptr);
    ~MarkoffDocument() override;

    QString plainText() const;
    void setPlainText(const QString &text);

    QTextDocument *textDocument() const;

    void replace(int sourceOffset, int removeLen, const QString &insert);
    void insert(int sourceOffset, const QString &text);
    void remove(int sourceOffset, int len);

    void beginTransaction();
    void endTransaction();

    void setCoalescingIdleMs(int ms);
    int coalescingIdleMs() const;

    /// Synchronous parse cache. Async worker lands in Phase C.
    const Document *parsed() const;
    bool parseIsPending() const;

Q_SIGNALS:
    void contentsChanged();
    void parseUpdated(const Document *);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff
