// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone reproducer for the CollabText::Crdt::Buffer
// O(replica_id) regression. No Qt, no Markoff dependency — just
// crdt::Buffer.
//
// Build (adjust include path to your tree):
//
//   g++ -std=c++20 -O2 \
//       -I path/to/collabtext/libs/collabtext/src \
//       repro_buffer_replica_id.cpp \
//       -L path/to/collabtext/build/libs/collabtext -lcollabtext \
//       -o repro_buffer_replica_id
//
// Run a sweep (each value should print in well under 1 s if perf
// were independent of replica_id; observed values run from ~50 ms
// at replica_id=1 to ~3.6 s at replica_id=10000):
//
//   for r in 1 100 1000 5000 10000 30000 60000; do
//       ./repro_buffer_replica_id $r
//   done

#include "crdt/Buffer.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char *argv[])
{
    const uint16_t replica_id =
        argc > 1 ? static_cast<uint16_t>(std::atoi(argv[1])) : 1;
    const int n_edits = argc > 2 ? std::atoi(argv[2]) : 400;
    const int chunk_bytes = argc > 3 ? std::atoi(argv[3]) : 180;

    CollabText::Crdt::Buffer buf(replica_id);

    const std::string chunk(static_cast<size_t>(chunk_bytes), 'a');
    uint32_t cursor = 0;

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n_edits; ++i) {
        // Append at end. {pos, pos} = empty range = pure insert.
        buf.apply_local_edit({{cursor, cursor}}, {chunk});
        cursor += static_cast<uint32_t>(chunk.size());
    }
    auto t1 = std::chrono::steady_clock::now();

    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::printf(
        "replica_id=%-6u  edits=%-5d  chunk=%-4dB  total=%6ld ms  "
        "per-edit=%7.3f ms  visible_bytes=%u\n",
        replica_id, n_edits, chunk_bytes, static_cast<long>(ms),
        double(ms) / std::max(1, n_edits),
        buf.visible_length());
    return 0;
}
