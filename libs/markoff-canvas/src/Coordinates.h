// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace Markoff::Canvas::Detail::Coordinates {

/// UTF-8 byte offset -> QChar index, within a single block's text. The ONE
/// conversion helper the leaf is allowed at the layout boundary (spec §4,
/// C4): always call this against `doc.blockText(id)`, never against a
/// layout's own text() — BlockLayoutCache substitutes '\n' with
/// QChar::LineSeparator, which is 1 QChar but NOT 1 byte, so round-tripping
/// through the layout string is wrong by 2 bytes per preceding newline
/// (spec §9, T1 finding). The substitution is 1 QChar -> 1 QChar, so a
/// QChar index computed here is valid for both the raw block text and the
/// layout text.
qsizetype byteToQtPos(const QByteArray &utf8, qsizetype byteOffset);

/// QChar index -> UTF-8 byte offset, within a single block's text. Inverse
/// of byteToQtPos(); see its doc comment for the layout-text trap.
qsizetype qtPosToByte(const QByteArray &utf8, qsizetype qtPos);

}  // namespace Markoff::Canvas::Detail::Coordinates
