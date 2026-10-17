// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Markoff::VNext::Testing {

enum class ReferenceProviderFaultPoint {
    none,
    afterAtomicRename,
    reservationBeforeAtomicRename,
    appendBeforeAtomicRename,
    fileSync,
    directorySync,
    historyBeforeAtomicRename,
    prepareBeforeAtomicRename,
    appendAfterAtomicRename,
    appendAfterCommit,
};

/// Inject one failure at a durable reference-provider commit boundary.
/// This declaration lives in the implementation tree and is not installed.
void failNextReferenceProviderCommitAt(ReferenceProviderFaultPoint point) noexcept;
/// Fail the matching commit after allowing `matchingCommitsToSkip` commits
/// through the same boundary.
void failReferenceProviderCommitAfter(ReferenceProviderFaultPoint point,
                                      unsigned matchingCommitsToSkip) noexcept;

/// The next action that must record a history retirement is rejected before
/// commit. An action that retires nothing does not consume this fault.
void failNextHistoryRetirement() noexcept;
[[nodiscard]] bool consumeHistoryRetirementFailure() noexcept;

} // namespace Markoff::VNext::Testing
