// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// Origin of a content reset on MarkoffDocument. Determines undo-stack
/// behavior on resetContent() — see MarkoffDocument.h for full semantics.
enum class Origin {
    FirstOpen,              ///< empty undo stack, no command pushed
    ExternalReloadClean,    ///< stack cleared (file changed; no pending edits)
    ExternalReloadResolved, ///< stack cleared (post-merge-modal resolution)
    UserRevertToSaved,      ///< pushes one mega edit so Ctrl+Z reverses it
    TestFixture,            ///< stack cleared (test setup)
};

}  // namespace Markoff
