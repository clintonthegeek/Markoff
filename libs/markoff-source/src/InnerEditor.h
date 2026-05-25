// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>

namespace Markoff::Source {
namespace Detail {

/// Thin QPlainTextEdit subclass that promotes protected geometry accessors
/// to public so Gutter can call them without being a QPlainTextEdit subclass.
class InnerEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit InnerEditor(QWidget *parent = nullptr) : QPlainTextEdit(parent) {}

    // Promote protected QPlainTextEdit/QAbstractScrollArea methods to public.
    using QPlainTextEdit::firstVisibleBlock;
    using QPlainTextEdit::blockBoundingGeometry;
    using QPlainTextEdit::contentOffset;
    using QPlainTextEdit::blockBoundingRect;
    using QAbstractScrollArea::setViewportMargins;
};

} // namespace Detail
} // namespace Markoff::Source
