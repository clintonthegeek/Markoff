// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#ifndef MARKOFF_READING_VAULTRESOURCEPROVIDER_H
#define MARKOFF_READING_VAULTRESOURCEPROVIDER_H

// Forwarding typedef. The real `Markoff::Vault::ResourceProvider` interface
// lives in markoff-core; markoff-reading-internal code and tests keep
// referencing `Markoff::Reading::VaultResourceProvider` unchanged.
//
// Pre-Phase-C this header forwarded to `Corbomite::Core::VaultResourceProvider`
// (Corbomite-owned type under the Phase B stub bridge). Phase C1 retires the
// Corbomite-named stub in favor of the DI seam shape.

#include <markoff/vault/ResourceProvider.h>

namespace Markoff::Reading {

using VaultResourceProvider = Markoff::Vault::ResourceProvider;

} // namespace Markoff::Reading

#endif // MARKOFF_READING_VAULTRESOURCEPROVIDER_H
