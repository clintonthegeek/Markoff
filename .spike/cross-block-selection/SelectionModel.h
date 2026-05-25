#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <qqmlintegration.h>

class SelectionModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int anchorBlock  READ anchorBlock  NOTIFY selectionChanged)
    Q_PROPERTY(int anchorOffset READ anchorOffset NOTIFY selectionChanged)
    Q_PROPERTY(int activeBlock  READ activeBlock  NOTIFY selectionChanged)
    Q_PROPERTY(int activeOffset READ activeOffset NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)

public:
    explicit SelectionModel(QObject *parent = nullptr);

    int anchorBlock()  const { return m_anchorBlock; }
    int anchorOffset() const { return m_anchorOffset; }
    int activeBlock()  const { return m_activeBlock; }
    int activeOffset() const { return m_activeOffset; }

    /// True iff a non-empty selection exists.
    bool hasSelection() const;

    /// Begin selection at the given (block, offset). Sets both anchor and
    /// active to that point, producing a zero-length selection.
    Q_INVOKABLE void begin(int block, int offset);

    /// Extend the active end of selection to (block, offset). Anchor stays.
    Q_INVOKABLE void extend(int block, int offset);

    /// Clear selection.
    Q_INVOKABLE void clear();

    /// Returns the (start, end) range to highlight in the given block,
    /// or QPoint(-1, -1) if no selection touches this block. Both coordinates
    /// are text positions inside the block.
    Q_INVOKABLE QPoint rangeForBlock(int blockIndex) const;

    /// Concatenate selected text given the per-block source strings.
    /// Caller passes the text of every block (the spike has a fixed list).
    Q_INVOKABLE QString collectSelectedText(const QStringList &blockTexts) const;

    /// Copy the current selection's text to the system clipboard.
    Q_INVOKABLE void copySelectionToClipboard(const QStringList &blockTexts) const;

Q_SIGNALS:
    void selectionChanged();

private:
    /// Returns (firstBlock, firstOffset, lastBlock, lastOffset) — the
    /// directionally-normalized form, where first is at-or-before last.
    void normalized(int &fb, int &fo, int &lb, int &lo) const;

    int m_anchorBlock  = -1;
    int m_anchorOffset = -1;
    int m_activeBlock  = -1;
    int m_activeOffset = -1;
};
