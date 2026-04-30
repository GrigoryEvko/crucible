#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <crucible/Platform.h>
#include <crucible/MerkleDag.h>
#include <crucible/rt/Registry.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Mutation.h>

namespace crucible {

// Parallel SPSC buffer for tensor metadata.
//
// The TraceRing stores 64B fingerprints per op on the hot path. Full tensor
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
// Structural properties (orthogonal to any specific timing):
//   1. Cached tail: producer caches last-seen tail locally. The tail atomic
//      lives on the consumer's cache line; reading it from the producer
//      forces a cross-core cache-line transfer. Since the buffer is 1M
//      entries deep and almost never full, the cached check passes on the
//      fast path and we never touch the atomic. Only on apparent overflow
//      do we reload the real tail (slow path).
//   2. Bulk memcpy: instead of per-element assignment with per-iteration
//      masking ((h+i) & MASK), we split into contiguous vs wraparound cases.
//      Contiguous (the overwhelming majority of calls): single memcpy of
//      n*144 bytes. Wraparound: two memcpys. memcpy of 144B structs
//      compiles to a small number of vector stores instead of scalar
//      field-by-field copy.
//   3. Aligned allocation: 64-byte aligned buffer base for cache-line-
//      friendly access patterns.
//   4. Software prefetch: after each write, prefetch 3 cache lines for the
//      NEXT write position. Each TensorMeta is 144B = 3 × 64B cache lines.
//   5. Cache-line layout: head, cached_tail_, and entries pointer share one
//      64-byte cache line (producer-only). tail on a separate line
//      (consumer). Zero false sharing between threads.
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
  //
  // Memory ordering (same SPSC pattern as TraceRing):
  //   - Producer reads its OWN head with memory_order_relaxed. Single-thread
  //     coherence orders a thread's loads of its own atomic; no cross-thread
  //     sync is needed. On ARM this saves LDAR (one cycle) vs LDR; on x86 TSO
  //     it's identical codegen.
  //   - Producer stores head with memory_order_release. This publishes the
  //     preceding entries[] memcpy to any consumer that acquires head — either
  //     directly via size() or transitively via TraceRing.head (which is
  //     released AFTER MetaLog.head in program order, so its acquire observes
  //     MetaLog.entries).
  // Producer-owned.  AtomicMonotonic surfaces the SPSC publish
  // discipline: peek_relaxed for own-side read, advance for release-
  // store with monotonicity contract, get for cross-thread acquire,
  // reset_under_quiescence for the rare quiescent reset.  Hot-path
  // cost identical to the prior raw atomic.
  alignas(64) crucible::safety::AtomicMonotonic<uint32_t> head{0};   // 4B
  // Producer-only shadow of the consumer's tail.  Monotonic enforces
  // "only advances" at the type level — the consumer only releases tail
  // forward, so each acquire-load observes ≥ the prior observation; any
  // reverse motion would be an acquire-semantics bug worth catching.
  crucible::safety::Monotonic<uint32_t> cached_tail_{0};  // 4B
  TensorMeta* entries = nullptr;                // 8B — producer-only read
  // 48B padding to fill cache line (implicitly provided by alignas on tail)

  // ── Consumer cache line ──
  // tail lives alone on its own cache line to prevent false sharing with
  // the producer's head/cached_tail_/entries.  Consumer-owned: bg thread
  // publishes via advance(release); producer reads via get(acquire) on
  // the rare full-path.
  alignas(64) crucible::safety::AtomicMonotonic<uint32_t> tail{0};   // 4B

  MetaLog() {
    // PMD alignment: kernel 5.8+ EINVALs MADV_HUGEPAGE on non-2-MB addrs.
    static constexpr size_t RAW_BYTES   = CAPACITY * sizeof(TensorMeta);
    static constexpr size_t ALLOC_BYTES = crucible::rt::round_up_huge(RAW_BYTES);
    entries = static_cast<TensorMeta*>(
        std::aligned_alloc(crucible::rt::kHugePageBytes, ALLOC_BYTES));
    if (!entries) [[unlikely]] std::abort();
    crucible::rt::register_hot_region(entries, ALLOC_BYTES,
        /*huge=*/true, "MetaLog.entries");
  }

  ~MetaLog() {
    crucible::rt::unregister_hot_region(entries);
    std::free(entries);
  }

  MetaLog(const MetaLog&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
  MetaLog& operator=(const MetaLog&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
  MetaLog(MetaLog&&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
  MetaLog& operator=(MetaLog&&) = delete("SPSC buffer is pinned to producer/consumer thread pair");

  // ── Producer (foreground): append n consecutive TensorMetas ──
  //
  // Returns the start index, or MetaIndex::none() if the buffer is full.
  //
  // Hot path shape (typical n=3, no wraparound, buffer not full):
  //   - head relaxed load (same cache line as cached_tail_)
  //   - cached_tail_ check (same cache line, in L1d)
  //   - memcpy(n * 144 B) to prefetched destination
  //   - prefetch next 3 cache lines for the following append
  //   - head release store
  // Foreground thread only (SPSC producer).
  // Safe by protocol: only one thread writes head + entries[head..head+n].
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] CRUCIBLE_INLINE MetaIndex try_append(const TensorMeta* metas, uint32_t n)
      CRUCIBLE_NO_THREAD_SAFETY
      pre (n == 0 || metas != nullptr)
  {
    if (n == 0) [[unlikely]] return MetaIndex::none();

    // peek_relaxed: producer reads its own head — no cross-thread sync needed.
    // Same pattern as TraceRing::try_append.
    const uint32_t h = head.peek_relaxed();

    // Fast path: check against cached (possibly stale) tail.
    // Stale tail is conservative — if it says "not full", it's guaranteed
    // correct because the real tail only advances (consumer frees space).
    if (h - cached_tail_.get() + n > CAPACITY) [[unlikely]] {
      // Slow path: get() is acquire — pair with consumer's advance(release).
      cached_tail_.advance(tail.get());
      if (h - cached_tail_.get() + n > CAPACITY) [[unlikely]] {
        return MetaIndex::none();
      }
    }

    // Compute masked start position in the circular buffer.
    uint32_t start_pos = h & MASK;
    // MASK = CAPACITY - 1 (power-of-two CAPACITY).  Tell the optimizer
    // so the memcpy destinations drop redundant bound checks.
    [[assume(start_pos < CAPACITY)]];
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

    // advance: publish the entries[] memcpy (above) to the consumer.
    // pre(load(acquire) < h+n) collapses to [[assume]] under hot-TU
    // contract semantics; body is the same store(release) as before.
    // n > 0 guaranteed by the early return at the top.
    head.advance(h + n);
    return MetaIndex{h};
  }

  // ═══════════════════════════════════════════════════════════════
  // FOUND-G22: HotPath-pinned producer surface
  // ═══════════════════════════════════════════════════════════════
  //
  // try_append is the foreground per-op TensorMeta append site —
  // declared Hot per HotPath.h docblock.  The body satisfies the
  // hot-path contract (SPSC ring + memcpy + acquire/release atomics
  // + prefetch; no alloc, no syscall, no block).
  //
  // try_append_pinned() declares this fact at the type level so a
  // consumer of the returned MetaIndex who declares
  // `requires HotPath::satisfies<Hot>` gets compile-time
  // confirmation that the value originated from a hot-path-safe
  // producer.  ADDITIVE: existing try_append() callers stay
  // unchanged.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] CRUCIBLE_INLINE
  crucible::safety::HotPath<crucible::safety::HotPathTier_v::Hot, MetaIndex>
  try_append_pinned(const TensorMeta* metas, uint32_t n)
      CRUCIBLE_NO_THREAD_SAFETY
      pre (n == 0 || metas != nullptr)
  {
    return crucible::safety::HotPath<crucible::safety::HotPathTier_v::Hot, MetaIndex>{
        try_append(metas, n)};
  }

  // Background thread only (SPSC consumer): read meta at absolute index.
  //
  // TypeSafe: the primary overload takes a MetaIndex strong ID.  The
  // uint32_t overload exists ONLY for the single bench site that needs
  // raw arithmetic over absolute positions (first_meta + offset), where
  // wrapping each sum in MetaIndex would obscure the intent without
  // adding safety — the sum isn't any stronger a MetaIndex than a
  // uint32_t is.  Everywhere else: pass MetaIndex.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] const TensorMeta& at(MetaIndex idx) const CRUCIBLE_LIFETIMEBOUND
      CRUCIBLE_NO_THREAD_SAFETY
      pre (idx.is_valid())
  {
    return entries[idx.raw() & MASK];
  }

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

  // Background thread only (SPSC consumer): advance tail past consumed
  // entries.  AtomicMonotonic::advance enforces monotonicity at the
  // type level — caller cannot accidentally regress tail.
  void advance_tail(uint32_t new_tail) CRUCIBLE_NO_THREAD_SAFETY {
    tail.advance(new_tail);
  }

  // Approximate count — deliberately racy (diagnostic only).
  [[nodiscard]] uint32_t size() const CRUCIBLE_NO_THREAD_SAFETY {
    return head.get() - tail.get();
  }

  // Only when both threads are quiescent (join/stop).
  // reset_under_quiescence bypasses AtomicMonotonic's monotonicity
  // contract — caller must have already joined producer + consumer.
  void reset() CRUCIBLE_NO_THREAD_SAFETY
      post (head.get() == 0)
      post (tail.get() == 0)
      post (cached_tail_.get() == 0)
  {
    head.reset_under_quiescence();
    tail.reset_under_quiescence();
    // Rewind to 0 would violate Monotonic's pre() if done via advance().
    // Safe here because both threads are quiescent; reconstruct in place.
    cached_tail_ = crucible::safety::Monotonic<uint32_t>{0};
  }
};

} // namespace crucible
