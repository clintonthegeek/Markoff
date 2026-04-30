// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>
#include <atomic>
#include <chrono>

namespace Markoff::Render {

/// Read absolute steady_clock nanoseconds from process boot.
inline quint64 nowNs() noexcept {
    using namespace std::chrono;
    return static_cast<quint64>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

/// Opt-in per-iteration timestamp taps for render-tier benchmarking. The
/// foundation populates the four absolute steady_clock timestamps below when
/// an instance is installed via `MarkoffDocument::setRenderPhaseTaps()`.
///
/// Bench-only — production callers leave the pointer null and pay zero (one
/// branch per scope, no atomic store).
///
/// Cross-thread visibility: `tWorkerEntryNs` and `tWorkerEmitNs` are written
/// on the parse-pool worker thread. `tMainSlotEntryNs` and `tModelDoneNs` are
/// written on the document's owner thread. Stores use release semantics; the
/// bench reads after `frameSwapped`, which is itself preceded by a queued
/// signal delivery, so any read after the next frame is safe.
struct RenderPhaseTaps {
    std::atomic<quint64> tWorkerEntryNs{0};
    std::atomic<quint64> tWorkerEmitNs{0};
    std::atomic<quint64> tMainSlotEntryNs{0};
    std::atomic<quint64> tModelDoneNs{0};

    void reset() noexcept {
        tWorkerEntryNs.store(0, std::memory_order_relaxed);
        tWorkerEmitNs.store(0, std::memory_order_relaxed);
        tMainSlotEntryNs.store(0, std::memory_order_relaxed);
        tModelDoneNs.store(0, std::memory_order_relaxed);
    }
};

}  // namespace Markoff::Render
