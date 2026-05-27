// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/styled/Editor.h>

namespace Markoff::Styled {

Editor::Editor(QWidget *parent) : Markoff::MarkdownView(parent) {}
Editor::~Editor() = default;

}  // namespace Markoff::Styled
