// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

namespace Markoff::Source::Widget {

class Editor;

class Gutter : public QWidget {
    Q_OBJECT
public:
    explicit Gutter(Editor *editor);

protected:
    void paintEvent(QPaintEvent *) override;
    QSize sizeHint() const override;

private:
    Editor *m_editor = nullptr;
};

} // namespace
