// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QUndoCommand>
#include <QString>
#include <markoff/MarkoffCoreExport.h>

namespace Markoff {

class MarkoffDocument;

class MARKOFF_CORE_EXPORT MarkdownDelta : public QUndoCommand {
public:
    MarkdownDelta(MarkoffDocument *doc,
                  qsizetype offset,
                  qsizetype removedLength,
                  QString inserted,
                  QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;
    int  id() const override;
    bool mergeWith(const QUndoCommand *other) override;

    qsizetype      offset() const          { return m_offset; }
    qsizetype      removedLength() const   { return m_removed.size(); }
    const QString &removedText() const     { return m_removed; }
    const QString &insertedText() const    { return m_inserted; }

private:
    MarkoffDocument *m_doc;
    qsizetype m_offset;
    QString   m_removed;   // captured on first redo(), replayed on undo()
    QString   m_inserted;
    qsizetype m_removedLengthHint = 0;  // set in ctor; used only on first redo()
    bool      m_firstRedo = true;
};

} // namespace Markoff
