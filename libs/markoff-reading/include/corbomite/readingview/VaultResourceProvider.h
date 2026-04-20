// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_VAULTRESOURCEPROVIDER_H
#define CORBOMITE_READINGVIEW_VAULTRESOURCEPROVIDER_H

// Forwarding typedef: the VaultResourceProvider interface was promoted to
// libs/core/ during Cluster J Phase 1 so embed renderers, Markoff and
// ReadingView can share a single contract. ReadingView-scoped code keeps
// referencing `Corbomite::ReadingView::VaultResourceProvider` unchanged.

#include "corbomite/core/VaultResourceProvider.h"

namespace Corbomite::ReadingView {

using VaultResourceProvider = Corbomite::Core::VaultResourceProvider;

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_VAULTRESOURCEPROVIDER_H
