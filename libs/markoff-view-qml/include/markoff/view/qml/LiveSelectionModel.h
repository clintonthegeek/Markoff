// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <qqmlintegration.h>

namespace Markoff::View::Qml {

/// Cross-block selection state. Mirrors the spike's SelectionModel verbatim
/// (see docs/specs/2026-04-29-cross-block-selection-spike-findings.md §4).
///
/// Contract for QML consumers:
///   - rangeForBlock returns QPoint(-1,-1) for unselected blocks.
///   - For selected blocks the y component MAY equal INT32_MAX as a
///     "to end of block" sentinel. CONSUMERS MUST CLAMP via
///     min(y, textEdit.length) before calling TextEdit.select(start, end),
///     because TextEdit.select silently no-ops if end > text.length.
class LiveSelectionModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int  anchorBlock  READ anchorBlock  NOTIFY selectionChanged)
    Q_PROPERTY(int  anchorOffset READ anchorOffset NOTIFY selectionChanged)
    Q_PROPERTY(int  activeBlock  READ activeBlock  NOTIFY selectionChanged)
    Q_PROPERTY(int  activeOffset READ activeOffset NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)

public:
    explicit LiveSelectionModel(QObject *parent = nullptr);

    int anchorBlock()  const { return m_anchorBlock; }
    int anchorOffset() const { return m_anchorOffset; }
    int activeBlock()  const { return m_activeBlock; }
    int activeOffset() const { return m_activeOffset; }
    bool hasSelection() const;

    Q_INVOKABLE void begin(int block, int offset);
    Q_INVOKABLE void extend(int block, int offset);
    Q_INVOKABLE void clear();

    Q_INVOKABLE QPoint rangeForBlock(int blockIndex) const;
    Q_INVOKABLE QString collectSelectedText(const QStringList &blockTexts) const;
    Q_INVOKABLE void copySelectionToClipboard(const QStringList &blockTexts) const;

Q_SIGNALS:
    void selectionChanged();

private:
    void normalized(int &fb, int &fo, int &lb, int &lo) const;

    int m_anchorBlock  = -1;
    int m_anchorOffset = -1;
    int m_activeBlock  = -1;
    int m_activeOffset = -1;
};

}  // namespace Markoff::View::Qml
