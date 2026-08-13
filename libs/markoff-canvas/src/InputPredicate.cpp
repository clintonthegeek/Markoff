// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: GPL-3.0-only
//
// Ported from QInputControl::isAcceptableInput
// (qtbase src/gui/text/qinputcontrol.cpp, ~v6.12). Plan T2's Qt upstream
// reference: this predicate is small, self-contained, and gets edge cases
// (ZWJ/format chars, the Ctrl/Ctrl+Shift rejection with the AltGr
// exception for QTBUG-35734, surrogate pairs, private-use) that a naive
// `!event->text().isEmpty() && !ctrl` check does not. Copying is legally
// permissible (qtbase is LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only;
// Markoff is GPL-3.0-or-later) — this file is pinned to GPL-3.0-only per
// the plan's license rule for copied snippets. The upstream `m_type ==
// TextEdit` tab exception is dropped: the canvas leaf routes Tab through
// StructuralKeyHandler (list indent/outdent), not as literal text.
#include "InputPredicate.h"

#include <QKeyEvent>

namespace Markoff::Canvas::Detail {

bool isAcceptableTextInput(const QKeyEvent *event)
{
    const QString text = event->text();
    if (text.isEmpty())
        return false;

    const QChar c = text.at(0);

    // Formatting characters such as ZWNJ, ZWJ, RLM, etc. This needs to go
    // before the next test, since CTRL+SHIFT is sometimes used to input it
    // on Windows.
    if (c.category() == QChar::Other_Format)
        return true;

    // QTBUG-35734: ignore Ctrl/Ctrl+Shift; accept only AltGr (Alt+Ctrl) on
    // German keyboards.
    if (event->modifiers() == Qt::ControlModifier
        || event->modifiers() == (Qt::ShiftModifier | Qt::ControlModifier)) {
        return false;
    }

    if (c.isPrint())
        return true;

    if (c.category() == QChar::Other_PrivateUse)
        return true;

    if (c.isHighSurrogate() && text.size() > 1 && text.at(1).isLowSurrogate())
        return true;

    return false;
}

}  // namespace Markoff::Canvas::Detail
