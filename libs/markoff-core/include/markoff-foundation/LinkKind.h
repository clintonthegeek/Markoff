// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QtCore/qmetatype.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

enum class LinkKind {
    Unknown,
    External,    ///< http://, https://, mailto:, etc.
    File,        ///< local file path or file://
    WikiLink,    ///< [[note title]] or [[note#anchor]]
    Tag,         ///< #tag
    Anchor,      ///< in-document #heading-id
};

}  // namespace Markoff
