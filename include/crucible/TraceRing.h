#pragma once

// Lock-free SPSC ring buffer for op recording.
//
// Foreground writes one Entry per ATen op (~5 ns target). Background drains
// in batches and builds the trace. The ring is pre-allocated (~5.25 MB) and
// never resized; a full ring silently drops the entry (the next iteration
// re-records).
//
// Entry is exactly one 64 B cache line. See Entry below for field layout.
//
// Parallel arrays (indexed by the same ring slot):
//   meta_starts[]     — MetaLog index for tensor metadata (MetaIndex, 256 KB)
//   scope_hashes[]    — module-hierarchy hash from CrucibleContext (512 KB)
//   callsite_hashes[] — Python source-location identity           (512 KB)
//
// cached_tail_ lives on the producer's cache line and holds the last-read
// tail. Stale cache is conservative (reports less free space than reality),
// so the producer only touches the consumer's atomic when the cache shows
// the ring full — roughly once per ~20 k appends at 5 ns/op and a 100 µs
// drain interval.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/rt/Registry.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Tagged.h>

namespace crucible {

// Packed flags in Entry::op_flags — preserves the 64 B cache-line Entry.
//   bit 0   : INFERENCE_MODE — c10::InferenceMode active
//   bit 1   : IS_MUTABLE     — schema.is_mutable() (in-place / out= op)
//   bit 2-3 : TRAINING_PHASE — 0 forward, 1 backward, 2 optimizer, 3 other
//   bit 4   : TORCH_FUNCTION — DispatchKey::Python active
//   bit 5-7 : reserved
namespace op_flag {
inline constexpr uint8_t INFERENCE_MODE = 1 << 0;
inline constexpr uint8_t IS_MUTABLE     = 1 << 1;
inline constexpr uint8_t PHASE_MASK     = 0x3 << 2;
inline constexpr uint8_t PHASE_SHIFT    = 2;
inline constexpr uint8_t TORCH_FUNCTION = 1 << 4;
} // namespace op_flag

enum class TrainingPhase : uint8_t {
  FORWARD   = 0,
  BACKWARD  = 1,
  OPTIMIZER = 2,
  OTHER     = 3,
};

// 2 MB alignment so make_unique lands on a PMD-aligned region and
// MADV_HUGEPAGE doesn't EINVAL (kernel 5.8+).
struct alignas(crucible::rt::kHugePageBytes) CRUCIBLE_OWNER TraceRing {
  // One cache line per op. Layout is load-bearing: bit-reinterpreted by
  // Serialize.h and assumed stable by Vigil / BackgroundThread / TraceLoader.
  //   schema(8) + shape(8) + num_inputs(2) + num_outputs(2)
  //   + num_scalar_args(2) + grad_enabled(1) + op_flags(1)
  //   + scalar_values[5](40) = 64 B
  struct alignas(64) Entry {
    SchemaHash schema_hash;               // op identity
    ShapeHash  shape_hash;                // quick hash of input shapes
    uint16_t   num_inputs       = 0;
    uint16_t   num_outputs      = 0;
    uint16_t   num_scalar_args  = 0;      // count; first 5 stored inline
    bool       grad_enabled     = false;
    uint8_t    op_flags         = 0;      // see op_flag::*
    // Inline scalar args (int64 bit-cast for doubles/bools/enums). 5 slots
    // cover 99.9 % of ops; overflow is counted in num_scalar_args. Zero-init
    // prevents hash instability on padding bytes.
    int64_t    scalar_values[5]{};
  };

  static_assert(sizeof(Entry) == 64, "Entry must be exactly one cache line");
  static_assert(alignof(Entry) == 64);
  CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Entry);

  // ── Vessel-boundary provenance ────────────────────────────────────
  //
  // Every Entry that reaches record_op / dispatch_op / TraceRing::
  // try_append must first be vouched for.  The vouching happens by
  // constructing one of these Tagged-pointer types:
  //
  //   FromPytorchEntryPtr  — raw Entry values built directly from FFI
  //                          input.  Must be validated (Vessel-side)
  //                          before recording.  No dispatch / record
  //                          overload accepts this type.
  //   ValidatedEntryPtr    — either the result of Vessel validation, or
  //                          constructed by internal code that certifies
  //                          the Entry by construction (tests, synthetic
  //                          drivers, replay engines).  Accepted by
  //                          dispatch_op and record_op.
  //
  // Pointer-based Tagged keeps the call site zero-copy: the underlying
  // 64-B Entry stays in the caller's stack/ring, and only an 8-B const*
  // is tagged and passed.  `vouch(e)` is the idiomatic internal wrap.
  using FromPytorchEntryPtr = crucible::safety::Tagged<
      const Entry*, crucible::safety::vessel_trust::FromPytorch>;
  using ValidatedEntryPtr   = crucible::safety::Tagged<
      const Entry*, crucible::safety::vessel_trust::Validated>;


  static constexpr uint32_t CAPACITY = 1u << 16; // 65 536 entries = 4 MB
  static constexpr uint32_t MASK     = CAPACITY - 1;
  static_assert((CAPACITY & MASK) == 0, "CAPACITY must be a power of two");

  // ── Cross-thread atomics on isolated cache lines ────────────────────
  // head and tail on one shared line would ping-pong MESI on every
  // producer/consumer write (~40 ns each). alignas(64) separates them.

  // Producer-owned. release on store publishes the entry data written before
  // it; consumer pairs with acquire to observe that data before reading.
  alignas(64) std::atomic<uint64_t> head{0};

  // Consumer-owned. release on store publishes "reader finished with these
  // slots"; producer pairs with acquire to avoid overwriting live data.
  alignas(64) std::atomic<uint64_t> tail{0};

  // Producer-local shadow of tail. Never read by the consumer. Own cache
  // line to avoid sharing with head (each line owned by exactly one thread).
  // Invariant: cached_tail_ <= tail.load(). Stale is conservative — it
  // under-reports free space, forcing a real tail reload; never
  // over-reports.  Wrapped in Monotonic to enforce "only advances"
  // structurally — the consumer only increments tail, so each
  // tail.load(acquire) observes a value ≥ the previous observation;
  // any reverse move would be an acquire-semantics bug worth catching.
  alignas(64) crucible::safety::Monotonic<uint64_t> cached_tail_{0};

  // 4 MB contiguous ring plus the three parallel arrays.
  alignas(64) Entry         entries[CAPACITY]{};
  MetaIndex                 meta_starts[CAPACITY]{};      // MetaIndex::none() default
  ScopeHash                 scope_hashes[CAPACITY]{};     // 0 = no scope
  CallsiteHash              callsite_hashes[CAPACITY]{};  // 0 = no callsite

  TraceRing() noexcept {
    crucible::rt::register_hot_region(this, sizeof(*this),
        /*huge=*/true, "TraceRing");
  }
  ~TraceRing() { crucible::rt::unregister_hot_region(this); }
  TraceRing(const TraceRing&)            = delete("SPSC ring is pinned to a producer/consumer thread pair");
  TraceRing& operator=(const TraceRing&) = delete("SPSC ring is pinned to a producer/consumer thread pair");
  TraceRing(TraceRing&&)                 = delete("SPSC ring is pinned to a producer/consumer thread pair");
  TraceRing& operator=(TraceRing&&)      = delete("SPSC ring is pinned to a producer/consumer thread pair");

  // ── Producer (foreground): ~3-5 ns, never blocks ────────────────────
  // Returns true on append, false if the ring is full (entry is dropped;
  // next iteration re-records). SPSC-safe: the producer is the sole writer
  // of head and entries[head]; we suppress Clang's thread-safety analysis.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard, gnu::hot]] CRUCIBLE_INLINE bool try_append(
      const Entry& e,
      MetaIndex    meta_start    = MetaIndex::none(),
      ScopeHash    scope_hash    = {},
      CallsiteHash callsite_hash = {}) noexcept CRUCIBLE_NO_THREAD_SAFETY {
    // relaxed: producer reads its own head — no cross-thread sync needed.
    const uint64_t h = head.load(std::memory_order_relaxed);

    // Fast path uses the producer-local cached_tail_ copy — no atomic load.
    // Stale cache is conservative (under-reports free space).
    if (h - cached_tail_.get() >= CAPACITY) [[unlikely]] {
      // acquire: pair with consumer's release store to observe that the
      // slots we're about to overwrite have been fully read.
      cached_tail_.advance(tail.load(std::memory_order_acquire));
      if (h - cached_tail_.get() >= CAPACITY) [[unlikely]] return false;
    }

    const uint32_t slot = static_cast<uint32_t>(h) & MASK;
    entries[slot]         = e;
    meta_starts[slot]     = meta_start;
    scope_hashes[slot]    = scope_hash;
    callsite_hashes[slot] = callsite_hash;

    // Prefetch the NEXT slot into L1d (write intent, T0 locality) so the
    // following try_append finds its destination cache-hot. Fires now,
    // completes during the caller's post-append work (Python dispatch, etc.).
    {
      const uint32_t next_slot = (slot + 1u) & MASK;
      __builtin_prefetch(&entries[next_slot],         1, 3);
      __builtin_prefetch(&meta_starts[next_slot],     1, 3);
      __builtin_prefetch(&scope_hashes[next_slot],    1, 3);
      __builtin_prefetch(&callsite_hashes[next_slot], 1, 3);
    }

    // release: publish the entry data (stored above) to the consumer; the
    // consumer's acquire load of head will observe all four writes.
    head.store(h + 1, std::memory_order_release);
    return true;
  }

  // ── Consumer (background): drain up to max_count entries ────────────
  // Copies into `out` and the three optional parallel-array buffers (any
  // may be null). Returns the number of entries drained. Uses memcpy for
  // contiguous runs to exploit SIMD store forwarding.
  // SPSC-safe: consumer is the sole writer of tail and sole reader of
  // entries[tail..head]. Clang thread-safety suppressed.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard, gnu::hot]] uint32_t drain(
      Entry*        out,
      uint32_t      max_count,
      MetaIndex*    out_meta_starts     = nullptr,
      ScopeHash*    out_scope_hashes    = nullptr,
      CallsiteHash* out_callsite_hashes = nullptr) noexcept CRUCIBLE_NO_THREAD_SAFETY
      pre (max_count == 0 || out != nullptr)
  {
    // acquire: pair with producer's release store — observe entry writes.
    const uint64_t h = head.load(std::memory_order_acquire);
    // relaxed: consumer reads its own tail — no cross-thread sync needed.
    const uint64_t t = tail.load(std::memory_order_relaxed);

    const uint32_t available = static_cast<uint32_t>(h - t);
    const uint32_t count     = std::min(available, max_count);
    if (count == 0) [[unlikely]] return 0;

    // Wrap split: at most two contiguous runs inside entries[].
    const uint32_t start  = static_cast<uint32_t>(t) & MASK;
    const uint32_t first  = std::min(count, CAPACITY - start);
    const uint32_t second = count - first;

    std::memcpy(out, &entries[start], first * sizeof(Entry));
    if (out_meta_starts)
      std::memcpy(out_meta_starts,     &meta_starts[start],     first * sizeof(MetaIndex));
    if (out_scope_hashes)
      std::memcpy(out_scope_hashes,    &scope_hashes[start],    first * sizeof(ScopeHash));
    if (out_callsite_hashes)
      std::memcpy(out_callsite_hashes, &callsite_hashes[start], first * sizeof(CallsiteHash));

    if (second > 0) [[unlikely]] {
      std::memcpy(out + first, &entries[0], second * sizeof(Entry));
      if (out_meta_starts)
        std::memcpy(out_meta_starts     + first, &meta_starts[0],     second * sizeof(MetaIndex));
      if (out_scope_hashes)
        std::memcpy(out_scope_hashes    + first, &scope_hashes[0],    second * sizeof(ScopeHash));
      if (out_callsite_hashes)
        std::memcpy(out_callsite_hashes + first, &callsite_hashes[0], second * sizeof(CallsiteHash));
    }

    // release: publish "slots [t, t+count) are free"; producer's acquire
    // load of tail on the full-ring path observes this before overwriting.
    tail.store(t + count, std::memory_order_release);
    return count;
  }

  // Approximate count — racy by design (diagnostic only). pure: depends on
  // memory (atomics) but has no side effects — optimizer may CSE adjacent
  // calls, which is fine for a diagnostic. Invariant: head >= tail always.
  [[nodiscard, gnu::pure]] uint32_t size() const noexcept CRUCIBLE_NO_THREAD_SAFETY {
    return static_cast<uint32_t>(
        head.load(std::memory_order_acquire) -
        tail.load(std::memory_order_acquire));
  }

  // Monotonic high-water mark of producer commits. Vigil::flush snapshots
  // this before waiting for the background thread to catch up.
  // acquire: synchronizes with producer's release in try_append so the
  // returned bound implies visibility of all prior entries.
  [[nodiscard, gnu::pure]] uint64_t total_produced() const noexcept CRUCIBLE_NO_THREAD_SAFETY {
    return head.load(std::memory_order_acquire);
  }

  // Only valid when both threads are quiescent (join/stop).
  void reset() noexcept CRUCIBLE_NO_THREAD_SAFETY {
    head.store(0, std::memory_order_release);
    tail.store(0, std::memory_order_release);
    // cached_tail_ goes "backward" to 0, which would fire Monotonic's
    // pre() if we advanced.  Valid here because both threads are
    // quiescent — reconstruct the Monotonic in place to reset cleanly.
    cached_tail_ = crucible::safety::Monotonic<uint64_t>{0};
  }
};

// Expected footprint ~5.25 MB. Bounds catch accidental layout bloat.
static_assert(sizeof(TraceRing) >= (5u * 1024u * 1024u) &&
              sizeof(TraceRing) <= (6u * 1024u * 1024u),
              "TraceRing footprint outside 5-6 MB envelope — layout changed");

// ── vouch(): internal-certification factory for ValidatedEntryPtr ──
//
// Construct a ValidatedEntryPtr from a known-good Entry.  Intended for
// tests, synthetic drivers, and internal recording paths that build
// their Entry by hand and certify it by construction (the op-flags
// come from c10::InferenceMode / TrainingPhase querying, inputs/outputs
// come from the op schema, scalars are packed from the ATen stack).
//
// FFI callers MUST NOT use vouch() to escape validation — FFI goes
// through validate_entry() (Vessel-side) which returns the same
// ValidatedEntryPtr after running the actual checks.  Audit for
// `vouch(` outside of test/ or src/: any such occurrence is a review
// concern.
[[nodiscard]] CRUCIBLE_INLINE
TraceRing::ValidatedEntryPtr vouch(const TraceRing::Entry& e CRUCIBLE_LIFETIMEBOUND) noexcept {
    return TraceRing::ValidatedEntryPtr{&e};
}

} // namespace crucible
