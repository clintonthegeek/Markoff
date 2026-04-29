// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

namespace Markoff::Source::Widget {

class Editor;

class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(Editor *editor, QWidget *parent = nullptr);

Q_SIGNALS:
    void closed();

private:
    Editor *m_editor = nullptr;
};

} // namespace
