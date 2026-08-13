// SPDX-License-Identifier: GPL-3.0-only
#pragma once

class QKeyEvent;

namespace Markoff::Canvas::Detail {

/// Is this key event acceptable as printable text input? Ported from
/// QInputControl::isAcceptableInput (Qt upstream reference, plan T2):
/// `event->text().isEmpty() && !ctrl` is not sufficient on its own — it
/// gets ZWJ/format characters, the AltGr exception (QTBUG-35734), and
/// surrogate pairs wrong. See InputPredicate.cpp for the copied logic and
/// its license note.
bool isAcceptableTextInput(const QKeyEvent *event);

}  // namespace Markoff::Canvas::Detail
