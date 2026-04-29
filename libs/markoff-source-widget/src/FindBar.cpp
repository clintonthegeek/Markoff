// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/widget/FindBar.h>
#include <markoff/source/widget/Editor.h>

namespace Markoff::Source::Widget {

FindBar::FindBar(Editor *editor, QWidget *parent) : QWidget(parent), m_editor(editor) {}

} // namespace
