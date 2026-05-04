// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QChar>
#include <QString>

namespace Markoff::LiveRender {

/// The marker character used by the marker-paragraph design
/// (`docs/specs/2026-05-03-marker-paragraph-design.md` §3).
/// U+200B ZERO WIDTH SPACE — invisible in every renderer; produces a
/// real paragraph block when present in source.
constexpr QChar       kMarkerChar    = QChar(0x200B);

/// UTF-8 encoding of `kMarkerChar`. Used when emitting `MarkoffEdit`s
/// (which carry `QByteArray newText` in UTF-8 byte coordinates).
constexpr const char *kMarkerUtf8    = "\xE2\x80\x8B";

/// Length of `kMarkerUtf8` in bytes. Compile-time constant for use in
/// `MarkoffEdit::oldEnd - oldStart` arithmetic.
constexpr int         kMarkerUtf8Len = 3;

}  // namespace Markoff::LiveRender
