#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

#include <crucible/Platform.h>
#include <crucible/Types.h>

namespace crucible {

// Lock-free SPSC ring buffer for op recording.
//
// Foreground thread writes one entry per ATen op (~5ns).
// Background thread drains in batches and builds the trace.
//
// Entry is exactly 64 bytes (one cache line). Layout:
//   schema_hash(8) + shape_hash(8) + num_inputs(2) + num_outputs(2)
//   + num_scalar_args(2) + grad_enabled(1) + inference_mode(1)
//   + scalar_values[5](40) = 64 bytes
//
// Parallel arrays alongside entries[]:
//   meta_starts[] — MetaLog index for tensor metadata (MetaIndex, 256KB)
//   scope_hashes[] — module hierarchy hash from CrucibleContext (uint64_t, 512KB)
//   callsite_hashes[] — Python callsite identity (uint64_t, 512KB)
//
// The ring is pre-allocated (~5.25MB total) and never resized.
// If the background thread falls behind, entries are silently
// dropped — the next iteration will re-record everything.
struct TraceRing {
  struct alignas(64) Entry {
    SchemaHash schema_hash;              // 8B — op identity
    ShapeHash shape_hash;                // 8B — quick hash of input shapes
    uint16_t num_inputs = 0;             // 2B
    uint16_t num_outputs = 0;            // 2B
    uint16_t num_scalar_args = 0;        // 2B
    bool grad_enabled = false;           // 1B
    bool inference_mode = false;         // 1B
    // Scalar argument values (int64_t bitcast for doubles/bools/enums).
    // 5 slots cover 99.9% of ops. Overflow counted in num_scalar_args.
    int64_t scalar_values[5]{};          // 40B — zero-init prevents hash instability
  };

  static_assert(sizeof(Entry) == 64, "Entry must be exactly one cache line");

  static constexpr uint32_t CAPACITY = 1 << 16; // 65536 entries = 4MB
  static constexpr uint32_t MASK = CAPACITY - 1;

  // Producer state (foreground thread writes, consumer reads).
  alignas(64) std::atomic<uint64_t> head{0};

  // Consumer state (consumer reads/writes, producer reads for fullness).
  // Must be atomic: producer reads in try_append, consumer writes in drain.
  alignas(64) std::atomic<uint64_t> tail{0};

  // The ring buffer itself (4MB contiguous, cache-line aligned).
  alignas(64) Entry entries[CAPACITY];

  // Parallel array: MetaLog start index for each ring entry.
  // Written by foreground alongside entries[], read by background.
  // MetaIndex::none() means no metadata (zero-tensor op or MetaLog overflow).
  MetaIndex meta_starts[CAPACITY];

  // Parallel array: module scope hash for each ring entry.
  // Default (0) means no scope (op at top level, outside any nn.Module).
  ScopeHash scope_hashes[CAPACITY];

  // Parallel array: Python callsite hash for each ring entry.
  // Identifies the Python source location (file:func:line) that triggered
  // this op. Default (0) means no callsite captured (e.g., pure C++).
  CallsiteHash callsite_hashes[CAPACITY];

  // ── Producer (foreground): ~5 ns, never blocks ──
  //
  // Returns true if the entry was written, false if the ring is full.
  // A full ring means the background thread is behind — we silently
  // drop the entry. The next iteration will re-record everything.
  // meta_start: MetaLog index for this entry's tensor metadata.
  //   MetaIndex::none() if op has no tensors or MetaLog is full.
  // scope_hash: current module hierarchy hash from CrucibleContext.
  // callsite_hash: Python source location identity.
  TraceRing() = default;
  TraceRing(const TraceRing&) = delete("SPSC ring is pinned to producer/consumer thread pair");
  TraceRing& operator=(const TraceRing&) = delete("SPSC ring is pinned to producer/consumer thread pair");
  TraceRing(TraceRing&&) = delete("SPSC ring is pinned to producer/consumer thread pair");
  TraceRing& operator=(TraceRing&&) = delete("SPSC ring is pinned to producer/consumer thread pair");

  [[nodiscard]] CRUCIBLE_INLINE bool try_append(
      const Entry& e,
      MetaIndex meta_start = MetaIndex::none(),
      ScopeHash scope_hash = {},
      CallsiteHash callsite_hash = {}) {
    uint64_t h = head.load(std::memory_order_relaxed);
    // Stale tail read is safe: worst case we think there's less space
    // than there actually is (conservative).
    uint64_t t = tail.load(std::memory_order_relaxed);
    if (h - t >= CAPACITY) [[unlikely]] {
      return false;
    }
    entries[h & MASK] = e;
    meta_starts[h & MASK] = meta_start;
    scope_hashes[h & MASK] = scope_hash;
    callsite_hashes[h & MASK] = callsite_hash;
    head.store(h + 1, std::memory_order_release);
    return true;
  }

  // ── Consumer (background): drain all available entries ──
  //
  // Copies up to max_count entries into `out` and their corresponding
  // parallel arrays into output buffers (any may be null).
  // Returns the number of entries actually drained.
  [[nodiscard]] uint32_t drain(Entry* out, uint32_t max_count,
                 MetaIndex* out_meta_starts = nullptr,
                 ScopeHash* out_scope_hashes = nullptr,
                 CallsiteHash* out_callsite_hashes = nullptr) {
    uint64_t h = head.load(std::memory_order_acquire);
    uint64_t t = tail.load(std::memory_order_relaxed);
    uint32_t available = static_cast<uint32_t>(h - t);
    uint32_t count = std::min(available, max_count);
    for (uint32_t i = 0; i < count; i++) {
      out[i] = entries[(t + i) & MASK];
      if (out_meta_starts) {
        out_meta_starts[i] = meta_starts[(t + i) & MASK];
      }
      if (out_scope_hashes) {
        out_scope_hashes[i] = scope_hashes[(t + i) & MASK];
      }
      if (out_callsite_hashes) {
        out_callsite_hashes[i] = callsite_hashes[(t + i) & MASK];
      }
    }
    tail.store(t + count, std::memory_order_relaxed);
    return count;
  }

  // Number of entries currently in the ring (approximate — racy).
  [[nodiscard]] uint32_t size() const {
    return static_cast<uint32_t>(
        head.load(std::memory_order_relaxed) -
        tail.load(std::memory_order_relaxed));
  }

  void reset() {
    head.store(0, std::memory_order_relaxed);
    tail.store(0, std::memory_order_relaxed);
  }
};

} // namespace crucible
