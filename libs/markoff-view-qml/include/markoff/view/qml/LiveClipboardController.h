// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <qqmlintegration.h>
#include <markoff/view/qml/LiveSelectionView.h>
#include <markoff/view/qml/LiveBlockModel.h>

namespace Markoff::View::Qml {

class LiveClipboardController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(LiveSelectionView *selectionModel READ selectionModel
               WRITE setSelectionModel NOTIFY selectionModelChanged)
    Q_PROPERTY(LiveBlockModel *blockModel READ blockModel
               WRITE setBlockModel NOTIFY blockModelChanged)

public:
    explicit LiveClipboardController(QObject *parent = nullptr);

    LiveSelectionView *selectionModel() const;
    void setSelectionModel(LiveSelectionView *m);

    LiveBlockModel *blockModel() const;
    void setBlockModel(LiveBlockModel *m);

    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();

Q_SIGNALS:
    void selectionModelChanged();
    void blockModelChanged();

private:
    QStringList collectBlockTexts() const;

    LiveSelectionView *m_selectionModel = nullptr;
    LiveBlockModel    *m_blockModel     = nullptr;
};

}  // namespace Markoff::View::Qml
