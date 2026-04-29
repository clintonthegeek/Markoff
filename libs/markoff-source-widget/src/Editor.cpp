// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/widget/Editor.h>
#include "Gutter.h"

namespace Markoff::Source::Widget {

Editor::Editor(QWidget *parent) : QPlainTextEdit(parent), m_theme(Markoff::Theme::defaultLight()) {}
Editor::~Editor() = default;

Markoff::MarkoffDocument *Editor::document() const { return m_document; }
void Editor::setDocument(Markoff::MarkoffDocument *) { /* TODO Phase C */ }
Markoff::Theme Editor::theme() const { return m_theme; }
void Editor::setTheme(const Markoff::Theme &t) { m_theme = t; emit themeChanged(); }

} // namespace
