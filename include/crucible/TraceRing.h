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
//   bit 5   : GRAD_ENABLED   — GradMode::is_enabled() (folded from its
//                              own byte to free a slot for scalar types)
//   bit 6-7 : SCALAR4_TYPE   — 2 bits encoding the type of the 5th
//                              inline scalar (slot index 4).  Paired
//                              with Entry::scalar_types byte which
//                              holds types for slots 0..3.
namespace op_flag {
inline constexpr uint8_t INFERENCE_MODE = 1 << 0;
inline constexpr uint8_t IS_MUTABLE     = 1 << 1;
inline constexpr uint8_t PHASE_MASK     = 0x3 << 2;
inline constexpr uint8_t PHASE_SHIFT    = 2;
inline constexpr uint8_t TORCH_FUNCTION = 1 << 4;
inline constexpr uint8_t GRAD_ENABLED   = 1 << 5;
inline constexpr uint8_t SCALAR4_TYPE_MASK  = 0x3 << 6;
inline constexpr uint8_t SCALAR4_TYPE_SHIFT = 6;
} // namespace op_flag

enum class TrainingPhase : uint8_t {
  FORWARD   = 0,
  BACKWARD  = 1,
  OPTIMIZER = 2,
  OTHER     = 3,
};

// Type tag for an inline scalar_values[i] slot.  Two bits per slot,
// packed into Entry::scalar_types (for slots 0..3) and op_flag bits
// 6-7 (for slot 4).  The type is stored so that scalar_values can
// be interpreted back to its original type — without this tag, a
// float 1.5 bit-cast to int64 (0x3FF8000000000000) is identical to
// the integer 0x3FF8000000000000, and callers that read the scalar
// cannot distinguish them.
//
// Content-hashing ignores the type tag (hashes raw bit pattern), so
// this is purely for semantic readback at Python-callsite rewriting
// and debugging paths.
enum class ScalarType2 : uint8_t {
  INT   = 0,   // int64_t — default, covers all integer types via sign-ext
  FLOAT = 1,   // double  — bit_cast via std::bit_cast<double>(v)
  BOOL  = 2,   // 0 or 1 — canonicalized at record time
  ENUM  = 3,   // underlying integer of an enum class (e.g. ScalarType, Layout)
};

// 2 MB alignment so make_unique lands on a PMD-aligned region and
// MADV_HUGEPAGE doesn't EINVAL (kernel 5.8+).
struct alignas(crucible::rt::kHugePageBytes) CRUCIBLE_OWNER TraceRing {
  // One cache line per op. Layout is load-bearing: bit-reinterpreted by
  // Serialize.h and assumed stable by Vigil / BackgroundThread / TraceLoader.
  //   schema(8) + shape(8) + num_inputs(2) + num_outputs(2)
  //   + num_scalar_args(2) + scalar_types(1) + op_flags(1)
  //   + scalar_values[5](40) = 64 B
  //
  // Layout change (v7→v8): grad_enabled's dedicated byte is folded
  // into op_flags bit 5.  The freed byte becomes `scalar_types`:
  // two-bit type tag for each of the first 4 inline slots.  The 5th
  // slot's type tag lives in op_flags bits 6-7.  Net: same 64B, new
  // capability to distinguish int / float / bool / enum scalars
  // without ambiguity from bit-cast aliasing.
  struct alignas(64) Entry {
    SchemaHash schema_hash;               // op identity
    ShapeHash  shape_hash;                // quick hash of input shapes
    uint16_t   num_inputs       = 0;
    uint16_t   num_outputs      = 0;
    uint16_t   num_scalar_args  = 0;      // count; first 5 stored inline
    uint8_t    scalar_types     = 0;      // 2 bits × 4 slots: types for slots 0..3
                                          // slot 4's type lives in op_flags[6-7]
    uint8_t    op_flags         = 0;      // see op_flag::*
    // Inline scalar args (int64 bit-cast for doubles/bools/enums).
    // 5 slots cover 99.9 % of ops; overflow is counted in num_scalar_args.
    // Zero-init prevents hash instability on padding bytes.
    // Use the get_scalar_type(i) / set_scalar_type(i, t) helpers rather
    // than reading scalar_values[i] blind — the type tag disambiguates
    // float 1.5 (bit_cast(0x3FF8000000000000)) from int 0x3FF8000000000000.
    int64_t    scalar_values[5]{};

    // ── Scalar type accessors ──────────────────────────────────────
    //
    // Extract/install the 2-bit type tag for slot `i` (0..4).
    // Packed across scalar_types byte (slots 0..3) and op_flags bits
    // 6-7 (slot 4).  Hot path: both reads compile to a shift+mask.
    [[nodiscard, gnu::pure]] ScalarType2 get_scalar_type(uint32_t i) const noexcept
        pre (i < 5)
    {
      if (i < 4) {
        return static_cast<ScalarType2>((scalar_types >> (i * 2)) & 0x3);
      }
      return static_cast<ScalarType2>(
          (op_flags & op_flag::SCALAR4_TYPE_MASK) >> op_flag::SCALAR4_TYPE_SHIFT);
    }

    void set_scalar_type(uint32_t i, ScalarType2 t) noexcept
        pre (i < 5)
    {
      const uint8_t bits = static_cast<uint8_t>(t) & 0x3;
      if (i < 4) {
        const uint8_t shift = static_cast<uint8_t>(i * 2);
        scalar_types = static_cast<uint8_t>(
            (scalar_types & ~(uint8_t{0x3} << shift)) | (bits << shift));
      } else {
        op_flags = static_cast<uint8_t>(
            (op_flags & ~op_flag::SCALAR4_TYPE_MASK)
            | (bits << op_flag::SCALAR4_TYPE_SHIFT));
      }
    }

    // Convenience: grad_enabled now lives as op_flags bit 5 rather
    // than its own byte.  Keep the legacy setter/getter so existing
    // call sites compile unchanged.  Once callers migrate to op_flag
    // bit access directly, these can be deprecated.
    [[nodiscard, gnu::pure]] bool grad_enabled() const noexcept {
      return (op_flags & op_flag::GRAD_ENABLED) != 0;
    }
    void set_grad_enabled(bool v) noexcept {
      if (v) op_flags = static_cast<uint8_t>(op_flags |  op_flag::GRAD_ENABLED);
      else   op_flags = static_cast<uint8_t>(op_flags & static_cast<uint8_t>(~op_flag::GRAD_ENABLED));
    }
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

  // Producer-owned.  AtomicMonotonic lifts the SPSC monotonicity
  // invariant to the type level: producer publishes via advance(release),
  // consumer reads via get(acquire), reset under quiescence via
  // reset_under_quiescence.  Hot-path cost is identical to the prior
  // raw atomic — peek_relaxed compiles to a plain MOV; advance's pre()
  // collapses to [[assume]] under hot-TU contract semantics, body is
  // store(release).
  alignas(64) crucible::safety::AtomicMonotonic<uint64_t> head{0};

  // Consumer-owned.  Same discipline: consumer publishes via advance,
  // producer reads via get for cross-thread acquire on the full-ring
  // path; consumer reads its own tail via peek_relaxed.
  alignas(64) crucible::safety::AtomicMonotonic<uint64_t> tail{0};

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
  [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE bool try_append(
      const Entry& e,
      MetaIndex    meta_start    = MetaIndex::none(),
      ScopeHash    scope_hash    = {},
      CallsiteHash callsite_hash = {}) noexcept CRUCIBLE_NO_THREAD_SAFETY {
    // peek_relaxed: producer reads its own head — no cross-thread sync needed.
    const uint64_t h = head.peek_relaxed();

    // Fast path uses the producer-local cached_tail_ copy — no atomic load.
    // Stale cache is conservative (under-reports free space).
    if (h - cached_tail_.get() >= CAPACITY) [[unlikely]] {
      // get() on tail is acquire — pair with consumer's release in
      // advance() to observe that the slots we're about to overwrite
      // have been fully read.
      cached_tail_.advance(tail.get());
      if (h - cached_tail_.get() >= CAPACITY) [[unlikely]] return false;
    }

    const uint32_t slot = static_cast<uint32_t>(h) & MASK;
    // MASK = CAPACITY - 1 and CAPACITY is a power of two, so slot ∈ [0, CAPACITY).
    // Tell the optimizer so the indexed stores below drop bounds-style checks.
    [[assume(slot < CAPACITY)]];
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

    // advance: publish the entry data (stored above) to the consumer; the
    // consumer's get() acquire load of head will observe all four writes.
    // pre(load(acquire) < h+1) collapses to [[assume]] under hot-TU
    // contract semantics; body is the same store(release) as before.
    head.advance(h + 1);
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
    // get(): acquire on head — pair with producer's release in advance()
    // to observe entry writes.
    const uint64_t h = head.get();
    // peek_relaxed: consumer reads its own tail — no cross-thread sync needed.
    const uint64_t t = tail.peek_relaxed();

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

    // advance: publish "slots [t, t+count) are free"; producer's get()
    // acquire load of tail on the full-ring path observes this before
    // overwriting.  count > 0 guaranteed above ⟹ t+count > t ⟹ pre OK.
    tail.advance(t + count);
    return count;
  }

  // ── Consumer (background): bulk SPSC drain with REQUIRED outputs ────
  //
  // Like drain() but ALL 4 output buffers are mandatory (contract-
  // checked).  Trade-off: caller commits to receiving all four
  // parallel arrays — the implementation loses the per-array null
  // branches that drain() carries (3 fewer conditional jumps per
  // call), and downstream SIMD batch processors (SIMD-12) get a
  // tight contract that every pop populates every array.
  //
  // Determinism: produces ENTRIES in identical FIFO order to N
  // successive single-pop drain(out, 1, ..., ..., ...) calls.  The
  // SPSC head/tail mechanism is the same; this method just amortizes
  // the head acquire-load and the tail release-store across max_count
  // entries.
  //
  // SPSC-safe: consumer is the sole writer of tail and sole reader
  // of entries[tail..head].  Clang thread-safety suppressed.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard, gnu::hot]] uint32_t try_pop_batch(
      Entry*        out_entries,
      MetaIndex*    out_meta_starts,
      ScopeHash*    out_scope_hashes,
      CallsiteHash* out_callsite_hashes,
      uint32_t      max_count) noexcept CRUCIBLE_NO_THREAD_SAFETY
      pre (max_count == 0 ||
           (out_entries != nullptr &&
            out_meta_starts != nullptr &&
            out_scope_hashes != nullptr &&
            out_callsite_hashes != nullptr))
  {
    // get(): acquire on head — pair with producer's release in advance().
    const uint64_t h = head.get();
    // peek_relaxed: consumer reads its own tail — no cross-thread sync needed.
    const uint64_t t = tail.peek_relaxed();

    const uint32_t available = static_cast<uint32_t>(h - t);
    const uint32_t count     = std::min(available, max_count);
    if (count == 0) [[unlikely]] return 0;

    // Wrap split: at most two contiguous runs inside entries[].
    const uint32_t start  = static_cast<uint32_t>(t) & MASK;
    const uint32_t first  = std::min(count, CAPACITY - start);
    const uint32_t second = count - first;

    // Unconditional memcpys — the all-required precondition lets us
    // skip per-array null checks (vs drain()'s 3 branches per array
    // per segment = up to 6 branches per call).  Cleaner SIMD store-
    // forwarding pattern.
    std::memcpy(out_entries,         &entries[start],         first * sizeof(Entry));
    std::memcpy(out_meta_starts,     &meta_starts[start],     first * sizeof(MetaIndex));
    std::memcpy(out_scope_hashes,    &scope_hashes[start],    first * sizeof(ScopeHash));
    std::memcpy(out_callsite_hashes, &callsite_hashes[start], first * sizeof(CallsiteHash));

    if (second > 0) [[unlikely]] {
      std::memcpy(out_entries         + first, &entries[0],         second * sizeof(Entry));
      std::memcpy(out_meta_starts     + first, &meta_starts[0],     second * sizeof(MetaIndex));
      std::memcpy(out_scope_hashes    + first, &scope_hashes[0],    second * sizeof(ScopeHash));
      std::memcpy(out_callsite_hashes + first, &callsite_hashes[0], second * sizeof(CallsiteHash));
    }

    // advance: publish "slots [t, t+count) are free"; count > 0 above
    // guarantees the monotonicity precondition holds.
    tail.advance(t + count);
    return count;
  }

  // Approximate count — racy by design (diagnostic only). pure: depends on
  // memory (atomics) but has no side effects — optimizer may CSE adjacent
  // calls, which is fine for a diagnostic. Invariant: head >= tail always.
  [[nodiscard, gnu::pure]] uint32_t size() const noexcept CRUCIBLE_NO_THREAD_SAFETY {
    return static_cast<uint32_t>(head.get() - tail.get());
  }

  // Monotonic high-water mark of producer commits. Vigil::flush snapshots
  // this before waiting for the background thread to catch up.
  // get(): acquire — synchronizes with producer's release in try_append so
  // the returned bound implies visibility of all prior entries.
  [[nodiscard, gnu::pure]] uint64_t total_produced() const noexcept CRUCIBLE_NO_THREAD_SAFETY {
    return head.get();
  }

  // Only valid when both threads are quiescent (join/stop).
  // reset_under_quiescence bypasses AtomicMonotonic's monotonicity
  // contract — caller must have already joined producer + consumer
  // before calling.  Same precondition the prior implementation
  // documented; now type-system surfaced via the explicit method name.
  void reset() noexcept CRUCIBLE_NO_THREAD_SAFETY
      post (head.get() == 0)
      post (tail.get() == 0)
      post (cached_tail_.get() == 0)
  {
    head.reset_under_quiescence();
    tail.reset_under_quiescence();
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
