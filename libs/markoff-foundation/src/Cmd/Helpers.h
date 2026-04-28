// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <utility>

namespace Markoff {
class MarkoffDocument;
struct Selection;
namespace Cmd::Detail {

/// Returns (startByte, endByte) of the selection in OLD-text coords,
/// resolved against the document's current Buffer. start <= end.
std::pair<quint32, quint32>
    selectionByteRange(const MarkoffDocument *doc, const Selection &sel);

}}  // namespace Markoff::Cmd::Detail
