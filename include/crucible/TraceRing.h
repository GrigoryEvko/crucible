#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <crucible/Platform.h>
#include <crucible/Types.h>

namespace crucible {

// Lock-free SPSC ring buffer for op recording.
//
// Foreground thread writes one entry per ATen op (~5ns target).
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
//
// Performance optimization: cached_tail_ avoids reading the consumer's
// atomic tail on every try_append(). The producer caches the last-read
// tail and only re-reads the atomic when the cache shows the ring full.
// Since drain() only advances tail forward, a stale cache is conservative
// (shows less space than actually available). This eliminates cross-core
// cache-line traffic on the common path: ~20,000 appends between drains
// at 5ns/op and 100us drain interval.
struct CRUCIBLE_OWNER TraceRing {
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
  CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Entry);

  static constexpr uint32_t CAPACITY = 1 << 16; // 65536 entries = 4MB
  static constexpr uint32_t MASK = CAPACITY - 1;

  // ── Cache-line-isolated atomics ──
  // head and tail MUST be on separate cache lines to prevent false sharing.
  // Producer writes head, consumer writes tail — if they shared a cache
  // line, every write would bounce the line between cores (~40ns penalty).

  // Producer state (foreground thread writes, consumer reads).
  alignas(64) std::atomic<uint64_t> head{0};

  // Consumer state (consumer reads/writes, producer reads for fullness).
  // Must be atomic: producer reads in try_append, consumer writes in drain.
  alignas(64) std::atomic<uint64_t> tail{0};

  // Producer-local cache of tail. Lives on the producer's cache line,
  // never written by the consumer. Avoids atomic load of tail on the
  // fast path (~20,000 appends avoid cross-core read per drain cycle).
  // Same cache line as head would cause false sharing, so we give it
  // its own line. Pad to 64B to prevent sharing with entries[].
  alignas(64) uint64_t cached_tail_ = 0;

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

  // ── Producer (foreground): ~3-5 ns, never blocks ──
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

  // Foreground thread only (SPSC producer).
  // Safe by protocol: only one thread writes head + entries[head].
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] CRUCIBLE_INLINE bool try_append(
      const Entry& e,
      MetaIndex meta_start = MetaIndex::none(),
      ScopeHash scope_hash = {},
      CallsiteHash callsite_hash = {}) CRUCIBLE_NO_THREAD_SAFETY {
    uint64_t h = head.load(std::memory_order_acquire);
    // Fast path: check against cached tail (producer-local, no atomic load).
    // Stale cached tail is conservative — shows less space than reality.
    if (h - cached_tail_ >= CAPACITY) [[unlikely]] {
      // Slow path: re-read the real tail from the consumer's atomic.
      // This crosses cache lines but happens at most once per drain cycle
      // (every ~20,000 appends at 5ns/op with 100us drain interval).
      cached_tail_ = tail.load(std::memory_order_acquire);
      if (h - cached_tail_ >= CAPACITY) [[unlikely]] {
        return false;
      }
    }
    uint32_t slot = static_cast<uint32_t>(h) & MASK;
    entries[slot] = e;
    meta_starts[slot] = meta_start;
    scope_hashes[slot] = scope_hash;
    callsite_hashes[slot] = callsite_hash;

    // Prefetch the NEXT write slot into L1d before publishing head.
    // The next try_append() will find its destination cache-hot.
    // Without this, each write hits cold cache lines in the 4MB ring,
    // costing ~100+ cycles for DRAM→L1d. The prefetch fires now and
    // completes during the caller's other work (Python dispatch, etc).
    {
      uint32_t next_slot = (slot + 1) & MASK;
      __builtin_prefetch(&entries[next_slot], 1 /*write*/, 3 /*L1*/);
      // Parallel arrays: each in a separate memory region, prefetch all.
      __builtin_prefetch(&meta_starts[next_slot], 1, 3);
      __builtin_prefetch(&scope_hashes[next_slot], 1, 3);
      __builtin_prefetch(&callsite_hashes[next_slot], 1, 3);
    }

    head.store(h + 1, std::memory_order_release);
    return true;
  }

  // ── Consumer (background): drain all available entries ──
  //
  // Copies up to max_count entries into `out` and their corresponding
  // parallel arrays into output buffers (any may be null).
  // Returns the number of entries actually drained.
  // Uses memcpy for contiguous runs to exploit hardware prefetch and
  // SIMD store forwarding.
  // Background thread only (SPSC consumer).
  // Safe by protocol: only one thread writes tail + reads entries[tail..head].
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] uint32_t drain(Entry* out, uint32_t max_count,
                 MetaIndex* out_meta_starts = nullptr,
                 ScopeHash* out_scope_hashes = nullptr,
                 CallsiteHash* out_callsite_hashes = nullptr) CRUCIBLE_NO_THREAD_SAFETY {
    uint64_t h = head.load(std::memory_order_acquire);
    uint64_t t = tail.load(std::memory_order_acquire);
    uint32_t available = static_cast<uint32_t>(h - t);
    uint32_t count = std::min(available, max_count);
    if (count == 0) [[unlikely]] {
      return 0;
    }

    // Split into at most two contiguous runs (wrap-around at CAPACITY).
    uint32_t start = static_cast<uint32_t>(t) & MASK;
    uint32_t first = std::min(count, CAPACITY - start);
    uint32_t second = count - first;

    // First contiguous run: [start, start + first).
    std::memcpy(out, &entries[start], first * sizeof(Entry));
    if (out_meta_starts)
      std::memcpy(out_meta_starts, &meta_starts[start], first * sizeof(MetaIndex));
    if (out_scope_hashes)
      std::memcpy(out_scope_hashes, &scope_hashes[start], first * sizeof(ScopeHash));
    if (out_callsite_hashes)
      std::memcpy(out_callsite_hashes, &callsite_hashes[start], first * sizeof(CallsiteHash));

    // Second contiguous run (wrap-around): [0, second).
    if (second > 0) [[unlikely]] {
      std::memcpy(out + first, &entries[0], second * sizeof(Entry));
      if (out_meta_starts)
        std::memcpy(out_meta_starts + first, &meta_starts[0], second * sizeof(MetaIndex));
      if (out_scope_hashes)
        std::memcpy(out_scope_hashes + first, &scope_hashes[0], second * sizeof(ScopeHash));
      if (out_callsite_hashes)
        std::memcpy(out_callsite_hashes + first, &callsite_hashes[0], second * sizeof(CallsiteHash));
    }

    tail.store(t + count, std::memory_order_release);
    return count;
  }

  // Approximate count — deliberately racy (diagnostic only).
  [[nodiscard]] uint32_t size() const CRUCIBLE_NO_THREAD_SAFETY {
    return static_cast<uint32_t>(
        head.load(std::memory_order_acquire) -
        tail.load(std::memory_order_acquire));
  }

  // Total entries ever produced (monotonic).  Acquire: synchronizes with
  // producer's release store in try_append().  Used by Vigil::flush() to
  // snapshot the high-water mark before waiting for full processing.
  [[nodiscard]] uint64_t total_produced() const CRUCIBLE_NO_THREAD_SAFETY {
    return head.load(std::memory_order_acquire);
  }

  // Only when both threads are quiescent (join/stop).
  void reset() CRUCIBLE_NO_THREAD_SAFETY {
    head.store(0, std::memory_order_release);
    tail.store(0, std::memory_order_release);
    cached_tail_ = 0;
  }
};

} // namespace crucible
