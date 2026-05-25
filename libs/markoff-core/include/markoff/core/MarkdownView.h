// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
#include <markoff/core/CursorPos.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {
class MarkoffDocument;

class MARKOFF_CORE_EXPORT MarkdownView : public QWidget {
    Q_OBJECT
public:
    explicit MarkdownView(QWidget *parent = nullptr);
    ~MarkdownView() override;

    virtual void setDocument(MarkoffDocument *doc);
    virtual MarkoffDocument *document() const;

    virtual CursorPos cursorPosition() const;
    virtual void setCursorPosition(CursorPos);

    virtual float scrollPositionVisualLine() const;
    virtual void  setScrollPositionVisualLine(float);

    virtual void setReadOnly(bool ro);
    virtual bool isReadOnly() const;

    virtual bool hasCursor()  const { return false; }
    virtual bool hasEditing() const { return false; }

signals:
    void documentChanged(Markoff::MarkoffDocument *doc);
    void cursorPositionChanged(int line, int column);
    void scrollPositionChanged(float pos);

private:
    MarkoffDocument *m_document = nullptr;
    bool m_readOnly = false;
};
} // namespace Markoff
