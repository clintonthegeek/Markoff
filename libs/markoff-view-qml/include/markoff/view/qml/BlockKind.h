// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace Markoff::View::Qml {

/// Block-kind constants. String-keyed (not a closed enum) so that plugin-
/// registered kinds in future phases don't require recompiling this library.
namespace BlockKind {
    extern const QString Paragraph;
    extern const QString Heading;
    extern const QString HorizontalRule;
    extern const QString Image;
    extern const QString CodeBlock;
}

}  // namespace Markoff::View::Qml
