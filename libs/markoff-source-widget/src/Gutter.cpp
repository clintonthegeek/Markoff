// SPDX-License-Identifier: GPL-3.0-or-later
#include "Gutter.h"
#include <markoff/source/widget/Editor.h>

namespace Markoff::Source::Widget {

Gutter::Gutter(Editor *editor) : QWidget(editor->viewport()), m_editor(editor) {}
void Gutter::paintEvent(QPaintEvent *) { /* TODO Phase D */ }
QSize Gutter::sizeHint() const { return QSize(40, 0); }

} // namespace
