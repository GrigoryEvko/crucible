#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <crucible/Platform.h>
#include <crucible/MerkleDag.h>

namespace crucible {

// Parallel SPSC buffer for tensor metadata.
//
// The TraceRing stores 64B fingerprints per op (~5ns hot path). Full tensor
// metadata (sizes, strides, dtype, device, data_ptr) is too large to inline.
// MetaLog holds this "fat" metadata in a separate SPSC buffer; each ring
// entry's corresponding meta_starts[] slot stores the index into here.
//
// Same SPSC protocol as TraceRing:
//   - Foreground thread writes at head
//   - Background thread reads and advances tail
//   - Overflow → MetaIndex::none() (entry still works for
//     iteration detection; background skips DAG building for that op)
//
// Performance optimizations over naive SPSC:
//   1. Cached tail: producer caches last-seen tail locally. The tail atomic
//      lives on the consumer's cache line; reading it from the producer forces
//      a cross-core cache-line transfer (~20-40ns on multi-socket). Since the
//      buffer is 1M entries deep and almost never full, the cached check passes
//      on the fast path and we never touch the atomic. Only on apparent
//      overflow do we reload the real tail (slow path).
//   2. Bulk memcpy: instead of per-element assignment with per-iteration
//      masking ((h+i) & MASK), we split into contiguous vs wraparound cases.
//      Contiguous (99.99% of calls): single memcpy of n*144 bytes.
//      Wraparound: two memcpys. memcpy of 144B structs compiles to
//      2-3 AVX-512 stores vs scalar field-by-field copy.
//   3. Aligned allocation: 64-byte aligned buffer base for cache-line-
//      friendly access patterns.
//   4. Software prefetch: after each write, prefetch 3 cache lines for the
//      NEXT write position. Hides ~50-100ns DRAM latency behind the caller's
//      other work (TraceRing append, Python dispatch). Each TensorMeta is
//      144B = 3 x 64B cache lines.
//   5. Cache-line layout: head, cached_tail_, and entries pointer share one
//      64-byte cache line (producer-only). tail on a separate line (consumer).
//      Zero false sharing between threads.
struct CRUCIBLE_OWNER MetaLog {
  static constexpr uint32_t CAPACITY = 1 << 20; // 1M entries (~168MB)
  static constexpr uint32_t MASK = CAPACITY - 1;

  // ── Producer cache line ──
  // head, cached_tail_, and entries share one cache line because ALL three
  // are accessed exclusively by the producer thread on the hot path.
  // Keeping them together means a single L1 hit serves the entire fast path.
  //
  // cached_tail_: producer-local copy of tail. Avoids cross-core atomic load
  // on every call. Stale value is conservative — if it says "full" we reload
  // the real tail (slow path), which may have advanced.
  // NOT relaxed: same SPSC publish pattern as TraceRing.
  // head.store(release) publishes TensorMeta entries to consumer.
  alignas(64) std::atomic<uint32_t> head{0};   // 4B — producer writes, consumer reads
  uint32_t cached_tail_ = 0;                    // 4B — producer-only (never touched by consumer)
  TensorMeta* entries = nullptr;                // 8B — producer-only read
  // 48B padding to fill cache line (implicitly provided by alignas on tail)

  // ── Consumer cache line ──
  // tail lives alone on its own cache line to prevent false sharing with
  // the producer's head/cached_tail_/entries. The consumer (background thread)
  // writes tail; the producer only reads it on the rare slow path.
  // NOT relaxed: tail.store(release) signals that consumer finished reading.
  alignas(64) std::atomic<uint32_t> tail{0};   // 4B — consumer writes, producer reads (rare)

  MetaLog() {
    // 64-byte aligned allocation for cache-line-friendly access.
    // std::aligned_alloc requires size to be a multiple of alignment — it is:
    // CAPACITY * 168 is divisible by 8 (168 = 8*21). For aligned_alloc(64,...),
    // ALLOC_BYTES must be a multiple of 64: 1M * 168 = 168MB, check via static_assert.
    static constexpr size_t ALLOC_BYTES = CAPACITY * sizeof(TensorMeta);
    static_assert(ALLOC_BYTES % 64 == 0, "allocation size must be multiple of alignment");
    entries = static_cast<TensorMeta*>(std::aligned_alloc(64, ALLOC_BYTES));
    if (!entries) [[unlikely]] std::abort(); // 168MB alloc failed — unrecoverable
  }

  ~MetaLog() { std::free(entries); }

  MetaLog(const MetaLog&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
  MetaLog& operator=(const MetaLog&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
  MetaLog(MetaLog&&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
  MetaLog& operator=(MetaLog&&) = delete("SPSC buffer is pinned to producer/consumer thread pair");

  // ── Producer (foreground): append n consecutive TensorMetas ──
  //
  // Returns the start index, or MetaIndex::none() if the buffer is full.
  //
  // Hot path cost breakdown (typical n=3, no wraparound, buffer not full):
  //   - head relaxed load: ~1ns (same cache line as cached_tail_)
  //   - cached_tail_ check: ~0ns (same cache line, always in L1)
  //   - memcpy(3 * 144 = 432B): ~10ns (to prefetched cache lines)
  //   - prefetch next 3 lines: ~1 cycle each (non-blocking)
  //   - head release store: ~1ns
  //   Total: ~12ns for 1 meta, ~33ns for 3 metas (measured)
  //   Baseline: memcpy alone to advancing dst = 8.6ns (1 meta) / 10ns (3 metas)
  // Foreground thread only (SPSC producer).
  // Safe by protocol: only one thread writes head + entries[head..head+n].
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] CRUCIBLE_INLINE MetaIndex try_append(const TensorMeta* metas, uint32_t n)
      CRUCIBLE_NO_THREAD_SAFETY {
    if (n == 0) [[unlikely]] return MetaIndex::none();

    uint32_t h = head.load(std::memory_order_acquire);

    // Fast path: check against cached (possibly stale) tail.
    // Stale tail is conservative — if it says "not full", it's guaranteed
    // correct because the real tail only advances (consumer frees space).
    if (h - cached_tail_ + n > CAPACITY) [[unlikely]] {
      // Slow path: reload actual tail from consumer's cache line.
      cached_tail_ = tail.load(std::memory_order_acquire);
      if (h - cached_tail_ + n > CAPACITY) [[unlikely]] {
        return MetaIndex::none();
      }
    }

    // Compute masked start position in the circular buffer.
    uint32_t start_pos = h & MASK;
    uint32_t end_pos = start_pos + n;

    if (end_pos <= CAPACITY) [[likely]] {
      // Contiguous region: single memcpy (99.99% of calls).
      // memcpy is vectorized by the compiler to AVX-512/AVX2 stores.
      std::memcpy(&entries[start_pos], metas, n * sizeof(TensorMeta));
    } else {
      // Wraparound: two memcpys (extremely rare with 1M capacity).
      uint32_t first_chunk = CAPACITY - start_pos;
      std::memcpy(&entries[start_pos], metas, first_chunk * sizeof(TensorMeta));
      std::memcpy(&entries[0], metas + first_chunk, (n - first_chunk) * sizeof(TensorMeta));
    }

    // Prefetch cache lines for the NEXT write position BEFORE publishing
    // head. The prefetch is non-blocking and doesn't affect the memcpy's
    // store visibility — it just starts bringing cache lines for the
    // next call's write destination into L1. Issuing it before the release
    // store gives it maximum time to complete before the next call.
    //
    // Each TensorMeta = 144B = 3 cache lines (at 64B/line, offsets 0/64/128).
    // We prefetch the first entry's 3 lines. For n>1, the hardware
    // prefetcher typically handles the sequential continuation.
    {
      uint32_t next_pos = (h + n) & MASK;
      const char* next_ptr = reinterpret_cast<const char*>(&entries[next_pos]);
      __builtin_prefetch(next_ptr,       1 /*write*/, 3 /*high locality*/);
      __builtin_prefetch(next_ptr + 64,  1 /*write*/, 3 /*high locality*/);
      __builtin_prefetch(next_ptr + 128, 1 /*write*/, 3 /*high locality*/);
    }

    head.store(h + n, std::memory_order_release);
    return MetaIndex{h};
  }

  // Background thread only (SPSC consumer): read meta at absolute index.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] const TensorMeta& at(uint32_t idx) const CRUCIBLE_LIFETIMEBOUND
      CRUCIBLE_NO_THREAD_SAFETY {
    return entries[idx & MASK];
  }

  // ── Consumer (background): zero-copy contiguous span access ──
  //
  // Returns a direct pointer into the buffer if the range [start, start+count)
  // doesn't wrap around the circular boundary. Returns nullptr if it wraps
  // (caller must fall back to per-element at() copies).
  //
  // 99.99% of calls succeed (1M capacity, typical iteration ~1500 metas).
  // Saves ~144B × count memcpy per op when successful.
  // Background thread only (SPSC consumer): zero-copy span into buffer.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] TensorMeta* try_contiguous(uint32_t start, uint32_t count) const
      CRUCIBLE_LIFETIMEBOUND CRUCIBLE_NO_THREAD_SAFETY {
    if (count == 0) [[unlikely]] return nullptr;
    uint32_t start_pos = start & MASK;
    if (start_pos + count <= CAPACITY) [[likely]]
      return &entries[start_pos];
    return nullptr; // wraps — caller must copy
  }

  // Background thread only (SPSC consumer): advance tail past consumed entries.
  void advance_tail(uint32_t new_tail) CRUCIBLE_NO_THREAD_SAFETY {
    tail.store(new_tail, std::memory_order_release);
  }

  // Approximate count — deliberately racy (diagnostic only).
  [[nodiscard]] uint32_t size() const CRUCIBLE_NO_THREAD_SAFETY {
    return head.load(std::memory_order_acquire) -
           tail.load(std::memory_order_acquire);
  }

  // Only when both threads are quiescent (join/stop).
  void reset() CRUCIBLE_NO_THREAD_SAFETY {
    head.store(0, std::memory_order_release);
    tail.store(0, std::memory_order_release);
    cached_tail_ = 0;
  }
};

} // namespace crucible
