// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffFoundationExport.h>
#include <markoff-foundation/Session.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT CommandFacade : public QObject {
    Q_OBJECT
    Q_PROPERTY(Markoff::MarkoffDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Markoff::Session *session READ session WRITE setSession NOTIFY sessionChanged)
public:
    explicit CommandFacade(QObject *parent = nullptr);
    ~CommandFacade() override;

    MarkoffDocument *document() const;
    void             setDocument(MarkoffDocument *);

    Session *session() const;
    void     setSession(Session *);

    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void toggleStrikethrough();
    Q_INVOKABLE void toggleInlineCode();
    Q_INVOKABLE void setHeading(int level);
    Q_INVOKABLE void toggleCheckbox();
    Q_INVOKABLE void blockQuote();
    Q_INVOKABLE void insertTable(int rows, int cols, bool hasHeader = true);
    Q_INVOKABLE void insertLink(const QString &linkText, const QString &target);
    Q_INVOKABLE void insertImage(const QString &alt, const QString &target);
    Q_INVOKABLE void insertHorizontalRule();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

Q_SIGNALS:
    void documentChanged();
    void sessionChanged();

private:
    MarkoffDocument *m_doc = nullptr;
    Session         *m_sess = nullptr;
};

}  // namespace Markoff
