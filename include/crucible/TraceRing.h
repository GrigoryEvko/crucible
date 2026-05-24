#pragma once

// Lock-free SPSC ring buffer for op recording.
//
// Foreground writes one Entry per ATen op. Background drains in batches and
// builds the trace. The ring is pre-allocated (~5.25 MB) and never resized;
// a full ring silently drops the entry (the next iteration re-records).
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
// so the producer only touches the consumer's atomic on the rare path where
// the cached view shows the ring full.
//
// ── API tiers ──────────────────────────────────────────────────────────
//
// Three additive surfaces share the same body:
//   • try_append() / drain()           — base API, untyped return.
//   • try_append_pinned() / drain_pinned() — HotPath<Hot|Warm, T> wrap
//                                            (FOUND-G22).
//   • try_append_pure<CallerRow>() / drain_pure<CallerRow>() — row-typed
//                                            wrap, IsPure<CallerRow>
//                                            constraint (FOUND-I16).
//
// New production call sites SHOULD use the row-typed `_pure` facades.
// They are zero-cost at runtime (thin forwarders, default CallerRow =
// Row<>) and give a compile-time gate that rejects any caller in an
// {IO, Block, Bg, Init, Test, Alloc} context.  The non-pure variants
// remain for ABI compatibility, internal forwarders, and call sites
// that intentionally accept callers from non-Pure contexts.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>
#include <crucible/warden/Registry.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/fixy/Hw.h>                 // FIXY-V-264: grant::hw::cache<Prefetch, L> + which_dim
#include <crucible/fixy/Dim.h>                // FIXY-V-264: dim::DimensionAxis (HwInstruction)

// FIXY-U-031b (S1-TraceRing of #1736): TraceRing spells its safety
// wrappers through fixy::wrap:: / fixy::tags:: , never safety::* — see
// the usages below.  TraceRing is a foundation hot-path header (the
// ~5ns/op recording ring); it deliberately does NOT pull the
// fixy/Wrap.h umbrella.  Mirror the Arena.h (FIXY-U-096y) populate
// precedent: include the NARROW substrate headers TraceRing already
// needs (above) and re-open the fixy namespaces to install the
// using-decls / alias TraceRing references.  fixy/Wrap.h + fixy/Source.h
// re-declare these independently in their own TUs — idempotent (a
// using-decl / namespace-alias naming one entity is not a redeclaration
// error).  This irreducible ::crucible::safety:: re-export plumbing is
// why TraceRing.h stays absent from scripts/fixy-clean-headers.txt.
namespace crucible::fixy::wrap {
using ::crucible::safety::AtomicMonotonic;
using ::crucible::safety::bounded_above;
using ::crucible::safety::FixedArray;
using ::crucible::safety::HotPath;
using ::crucible::safety::HotPathTier_v;
using ::crucible::safety::Monotonic;
using ::crucible::safety::Refined;
using ::crucible::safety::Stale;
using ::crucible::safety::Tagged;
}  // namespace crucible::fixy::wrap
namespace crucible::fixy::tags {
namespace vessel_trust = ::crucible::safety::vessel_trust;
}  // namespace crucible::fixy::tags

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

// ── FIXY-V-264: hardware-axis grant declaration (HwInstruction) ─────
//
// try_append() prefetches the NEXT ring slot into L1d via four
// __builtin_prefetch(addr, /*rw=*/1, /*locality=*/3) calls.  A prefetch
// is a cache-control instruction on the HwInstruction axis (V-251); this
// block pins the (CacheOp, Locality) the kernel emits so a future locality
// edit cannot drift from the declared grant.  `kPrefetchLocality` is the
// SINGLE source of truth — it parameterizes BOTH the grant tag AND the
// builtin's third argument below, so the declaration is load-bearing
// (changing it changes the actual instruction), not documentation.
namespace tracering_hw {

namespace fh  = ::crucible::fixy::hw;
namespace fgh = ::crucible::fixy::grant::hw;

// Temporal-locality hint for the four try_append prefetches: 3 = highest
// reuse (keep in all cache levels), matching the "next slot is touched on
// the very next append" access pattern.  Wired into the builtin calls.
inline constexpr int kPrefetchLocality = 3;

using ActiveCacheGrant = fgh::cache<fh::CacheOp::Prefetch, kPrefetchLocality>;

// Well-formed grant tag routing to the HwInstruction axis (FIXY-V-253).
static_assert(::crucible::fixy::grant::IsGrantTag<ActiveCacheGrant>,
              "FIXY-V-264: the active cache grant must be a well-formed grant tag");
static_assert(::crucible::fixy::grant::which_dim_v<ActiveCacheGrant>
                  == ::crucible::fixy::dim::DimensionAxis::HwInstruction,
              "FIXY-V-264: grant::hw::cache routes to the HwInstruction axis");

// Locality consistency — the declared hint MUST be a valid prefetch
// temporal-locality level [0, 3].  __builtin_prefetch's third argument
// has the same domain; a value outside it is undefined.
static_assert(kPrefetchLocality >= 0 && kPrefetchLocality <= 3,
              "FIXY-V-264: prefetch locality must be in [0, 3] — the "
              "__builtin_prefetch third-argument domain");

}  // namespace tracering_hw

// 2 MB alignment so make_unique lands on a PMD-aligned region and
// MADV_HUGEPAGE doesn't EINVAL (kernel 5.8+).
struct alignas(crucible::warden::kHugePageBytes) CRUCIBLE_OWNER TraceRing {
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
    //
    // #1057 WRAP-TraceRing-5: the raw `int64_t[5]` is wrapped in
    // fixy::wrap::FixedArray<int64_t, 5> so callers cannot index past the
    // structural bound without dropping to .data() + raw arithmetic.
    // sizeof(FixedArray<int64_t, 5>) == sizeof(int64_t[5]) — Entry stays
    // exactly one 64-B cache line; ABI / serialize layout unchanged.
    ::crucible::fixy::wrap::FixedArray<int64_t, 5> scalar_values{};

    // ── Scalar type accessors ──────────────────────────────────────
    //
    // Extract/install the 2-bit type tag for slot `i` (0..4).
    // Packed across scalar_types byte (slots 0..3) and op_flags bits
    // 6-7 (slot 4).  Hot path: both reads compile to a shift+mask.
    // CONTRACT-128 cite: `i < 5` migrates from anonymous bare-< to
    // named `decide::in_range<uint32_t>(i, 0u, 4u)`.  Predicate references
    // a parameter only (no `this->` member), so vanilla P2900 `pre()` is
    // safe at consteval — the consteval-bypass family applies only to
    // foldable bodies whose predicate touches `this->` members through
    // the implicit object parameter.  Cite is the soundness witness:
    // the scalar-args inline buffer is a 5-element array (slots 0..4
    // aliased across `scalar_types[3:0]` + `op_flags[7:6]`), grep'able
    // via `decide::in_range` for any future scalar-buffer resize.
    [[nodiscard, gnu::pure]] ScalarType2 get_scalar_type(uint32_t i) const noexcept
        pre (::crucible::decide::in_range<uint32_t>(i, 0u, 4u))
    {
      if (i < 4) {
        return static_cast<ScalarType2>((scalar_types >> (i * 2)) & 0x3);
      }
      return static_cast<ScalarType2>(
          (op_flags & op_flag::SCALAR4_TYPE_MASK) >> op_flag::SCALAR4_TYPE_SHIFT);
    }

    void set_scalar_type(uint32_t i, ScalarType2 t) noexcept
        pre (::crucible::decide::in_range<uint32_t>(i, 0u, 4u))
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
  using FromPytorchEntryPtr = crucible::fixy::wrap::Tagged<
      const Entry*, crucible::fixy::tags::vessel_trust::FromPytorch>;
  using ValidatedEntryPtr   = crucible::fixy::wrap::Tagged<
      const Entry*, crucible::fixy::tags::vessel_trust::Validated>;


  static constexpr uint32_t CAPACITY = 1u << 16; // 65 536 entries = 4 MB
  static constexpr uint32_t MASK     = CAPACITY - 1;
  static_assert((CAPACITY & MASK) == 0, "CAPACITY must be a power of two");

  // ── Cross-thread atomics on isolated cache lines ────────────────────
  // head and tail on one shared line would ping-pong MESI on every
  // producer/consumer write. alignas(64) separates them onto distinct
  // cache lines, each owned by exactly one thread.

  // Producer-owned.  AtomicMonotonic lifts the SPSC monotonicity
  // invariant to the type level: producer publishes via advance(release),
  // consumer reads via get(acquire), reset under quiescence via
  // reset_under_quiescence.  Hot-path cost is identical to the prior
  // raw atomic — peek_relaxed compiles to a plain MOV; advance's pre()
  // collapses to [[assume]] under hot-TU contract semantics, body is
  // store(release).
  alignas(64) crucible::fixy::wrap::AtomicMonotonic<uint64_t> head{0};

  // Consumer-owned.  Same discipline: consumer publishes via advance,
  // producer reads via get for cross-thread acquire on the full-ring
  // path; consumer reads its own tail via peek_relaxed.
  alignas(64) crucible::fixy::wrap::AtomicMonotonic<uint64_t> tail{0};

  // Consumer-owned hot cursor. The consumer is the only reader/writer, so
  // drain paths use this plain value and publish the new value to tail only
  // after the copied slots are complete. That removes a useless relaxed
  // atomic load from every drain while leaving the producer-visible tail
  // publication unchanged.
  uint64_t consumer_tail_ = 0;

  // Producer-local shadow of tail. Never read by the consumer. Own cache
  // line to avoid sharing with head (each line owned by exactly one thread).
  // Invariant: cached_tail_ <= tail.load(). Stale is conservative — it
  // under-reports free space, forcing a real tail reload; never
  // over-reports.  Wrapped in Monotonic to enforce "only advances"
  // structurally — the consumer only increments tail, so each
  // tail.load(acquire) observes a value ≥ the previous observation;
  // any reverse move would be an acquire-semantics bug worth catching.
  alignas(64) crucible::fixy::wrap::Monotonic<uint64_t> cached_tail_{0};

  // 4 MB contiguous ring plus the three parallel arrays.
  alignas(64) Entry         entries[CAPACITY]{};
  MetaIndex                 meta_starts[CAPACITY]{};      // MetaIndex::none() default
  ScopeHash                 scope_hashes[CAPACITY]{};     // 0 = no scope
  CallsiteHash              callsite_hashes[CAPACITY]{};  // 0 = no callsite

  TraceRing() noexcept {
    crucible::warden::register_hot_region(this, sizeof(*this),
        /*huge=*/true, "TraceRing");
  }
  ~TraceRing() { crucible::warden::unregister_hot_region(this); }
  TraceRing(const TraceRing&)            = delete("SPSC ring is pinned to a producer/consumer thread pair");
  TraceRing& operator=(const TraceRing&) = delete("SPSC ring is pinned to a producer/consumer thread pair");
  TraceRing(TraceRing&&)                 = delete("SPSC ring is pinned to a producer/consumer thread pair");
  TraceRing& operator=(TraceRing&&)      = delete("SPSC ring is pinned to a producer/consumer thread pair");

  // ── Producer (foreground): never blocks ─────────────────────────────
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
      // Locality is the FIXY-V-264-declared tracering_hw::kPrefetchLocality
      // (write intent, T0) — the grant tag and these calls share one source.
      __builtin_prefetch(&entries[next_slot],         1, tracering_hw::kPrefetchLocality);
      __builtin_prefetch(&meta_starts[next_slot],     1, tracering_hw::kPrefetchLocality);
      __builtin_prefetch(&scope_hashes[next_slot],    1, tracering_hw::kPrefetchLocality);
      __builtin_prefetch(&callsite_hashes[next_slot], 1, tracering_hw::kPrefetchLocality);
    }

    // advance: publish the entry data (stored above) to the consumer; the
    // consumer's get() acquire load of head will observe all four writes.
    // pre(load(acquire) < h+1) collapses to [[assume]] under hot-TU
    // contract semantics; body is the same store(release) as before.
    head.advance(h + 1);
    return true;
  }

  // ═══════════════════════════════════════════════════════════════
  // FOUND-G22: HotPath-pinned producer surface
  // ═══════════════════════════════════════════════════════════════
  //
  // try_append is the foreground per-op recording site — the
  // canonical Hot-path operation per HotPath.h docblock.  Per
  // CRUCIBLE.md §L4, this is shape-budgeted at ~5 ns per call and
  // absolutely forbids any class of heavy operation (alloc, syscall,
  // block, futex).  The body satisfies that contract by construction
  // (SPSC ring + acquire/release atomics + _mm_pause spin only;
  // see §IX latency hierarchy).
  //
  // try_append_pinned() declares this fact at the type level so
  // any consumer of the boolean return who declares
  // `requires HotPath::satisfies<Hot>` gets compile-time confirmation
  // that the value originated from a hot-path-safe producer.  A
  // refactor that, e.g., adds a printf or std::format to the body
  // would have to weaken the wrapper's tier from Hot to Cold —
  // and every downstream Hot-fenced consumer rejects the call.
  //
  // ADDITIVE: existing try_append() callers stay unchanged.  Only
  // new sites that participate in the row-typed dispatch network
  // call try_append_pinned().
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE
  crucible::fixy::wrap::HotPath<crucible::fixy::wrap::HotPathTier_v::Hot, bool>
  try_append_pinned(
      const Entry& e,
      MetaIndex    meta_start    = MetaIndex::none(),
      ScopeHash    scope_hash    = {},
      CallsiteHash callsite_hash = {}) noexcept CRUCIBLE_NO_THREAD_SAFETY {
    return crucible::fixy::wrap::HotPath<crucible::fixy::wrap::HotPathTier_v::Hot, bool>{
        try_append(e, meta_start, scope_hash, callsite_hash)};
  }

  // ═══════════════════════════════════════════════════════════════
  // FOUND-I16: row-typed facade — pins try_append as Pure
  // ═══════════════════════════════════════════════════════════════
  //
  // Sibling of MetaLog::try_append_pure (FOUND-I17).  try_append is
  // a hot-path memory-only operation: SPSC ring slot store +
  // acquire/release atomic publish + producer-local cache update.
  // No I/O, no blocking, no allocation, no init/test/bg context.
  // In F* effect terms, it is `Pure` (the bottom of the OsUniverse
  // effect-row lattice — empty row).  This facade pins that fact at
  // the type level via an `IsPure<CallerRow>` constraint.
  //
  // Caller-row contract: the CallerRow template argument MUST satisfy
  // `Subrow<CallerRow, Row<>>`, i.e., the caller's row must be empty.
  // A caller in any of the following contexts is REJECTED at compile
  // time:
  //   • Row<Effect::Alloc> — allocating context cannot append
  //   • Row<Effect::IO>    — IO context cannot append
  //   • Row<Effect::Block> — blocking context cannot append
  //   • Row<Effect::Bg>    — bg consumer-side cannot append
  //   • Row<Effect::Init>  — init-time code cannot append (hot path)
  //   • Row<Effect::Test>  — test-only code cannot append in prod
  //   • Any non-trivial row containing one or more atoms
  //
  // The facade is ADDITIVE: existing try_append() / try_append_pinned()
  // callers stay unchanged.  Production hot-path callers can migrate
  // by replacing `try_append(...)` with `try_append_pure<>(...)` to
  // gain the compile-time check.  Default template argument is `Row<>`
  // so `try_append_pure(...)` (no template-arg) is equivalent to
  // `try_append_pure<Row<>>(...)` — the most common case at production
  // hot-path call sites.
  //
  // Implementation: thin forwarder to try_append; zero runtime cost
  // (one inlined branchless tail-call under -O3).  The IsPure
  // constraint is checked at substitution time, not at runtime.
  template <typename CallerRow = ::crucible::effects::Row<>>
      requires ::crucible::effects::IsPure<CallerRow>
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard, gnu::hot, gnu::flatten]] CRUCIBLE_INLINE bool try_append_pure(
      const Entry& e,
      MetaIndex    meta_start    = MetaIndex::none(),
      ScopeHash    scope_hash    = {},
      CallsiteHash callsite_hash = {}) noexcept CRUCIBLE_NO_THREAD_SAFETY {
    return try_append(e, meta_start, scope_hash, callsite_hash);
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
      // CONTRACT-104: bound check discharges through decide::in_range
      // (closed-interval [0, CAPACITY], unsigned natural lower bound).
      // Pure parameter ref + static constexpr CAPACITY — not subject to
      // the GCC 16.1.1 consteval-bypass that forced CONTRACT-100..103
      // to migrate to in-body CRUCIBLE_PRE; P2900 pre() is sufficient
      // here.  The cite is for grep-discipline consistency: a single
      // hardening change to the in_range predicate propagates to every
      // drain entry point.
      pre (::crucible::decide::in_range<std::uint32_t>(
          max_count, std::uint32_t{0}, CAPACITY))
      pre (::crucible::decide::valid_span(max_count, out))
  {
    if (max_count == 0) [[unlikely]] return 0;

    // get(): acquire on head — pair with producer's release in advance()
    // to observe entry writes.
    const uint64_t h = head.get();
    const uint64_t t = consumer_tail_;

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
    const uint64_t next_tail = t + count;
    consumer_tail_ = next_tail;
    tail.advance(next_tail);
    // CONTRACT-104-POST: result-bound contract — the count returned
    // is at most max_count.  The available-clamp `std::min(available,
    // max_count)` above guarantees this; the post catches a future
    // refactor that drops the clamp (e.g., returns `available`
    // directly when callers seem to "always pass max_count >=
    // available") which would silently overrun the caller's `out`
    // buffer.  Routes through CRUCIBLE_POST because P2900 `post (r:...)`
    // referencing the parameter is bypass-resistant but the cite is
    // grep-discipline consistent with the in-body pre on this file.
    // Early returns of 0 above trivially satisfy `0 <= max_count`.
    CRUCIBLE_POST(count, count <= max_count);
    return count;
  }

  // ── FOUND-G22: HotPath-pinned bg drain ─────────────────────────────
  //
  // drain() runs on the BackgroundThread and is allowed to do alloc,
  // memcpy of large buffers, and other Warm-tier operations (per
  // HotPath.h docblock: "BackgroundThread::drain — declared Warm").
  // Wrapping the count-returned in HotPath<Warm, ...> declares the
  // Warm tier at the type level so a consumer that requires Hot-tier
  // (e.g., a fg dispatch path mistakenly wired to a bg counter) is
  // rejected at compile time.
  //
  // ADDITIVE: existing drain() callers stay unchanged.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard, gnu::hot]]
  crucible::fixy::wrap::HotPath<crucible::fixy::wrap::HotPathTier_v::Warm, uint32_t>
  drain_pinned(
      Entry*        out,
      uint32_t      max_count,
      MetaIndex*    out_meta_starts     = nullptr,
      ScopeHash*    out_scope_hashes    = nullptr,
      CallsiteHash* out_callsite_hashes = nullptr) noexcept CRUCIBLE_NO_THREAD_SAFETY
      pre (::crucible::decide::in_range<std::uint32_t>(
          max_count, std::uint32_t{0}, CAPACITY))
      pre (::crucible::decide::valid_span(max_count, out))
  {
    return crucible::fixy::wrap::HotPath<crucible::fixy::wrap::HotPathTier_v::Warm, uint32_t>{
        drain(out, max_count, out_meta_starts, out_scope_hashes, out_callsite_hashes)};
  }

  // ═══════════════════════════════════════════════════════════════
  // FOUND-I16: row-typed facade — pins drain as Pure
  // ═══════════════════════════════════════════════════════════════
  //
  // Sibling of try_append_pure on the consumer side.  drain reads
  // produced entries into a caller-supplied buffer + advances the
  // tail pointer with a release atomic.  Functionally pure at the
  // C++ level: no I/O, no blocking, no allocation, no syscalls.
  // The bg thread that *owns* the consumer side is itself in a Bg
  // context, but each drain() call is a pure memory-only operation
  // and can be invoked by any caller that declares Pure (the wrap-
  // ping bg thread declares Bg via context structs, not via this
  // call's row).
  //
  // Caller-row contract: identical to try_append_pure — the row
  // must be empty.  This catches the bug where, e.g., an init-time
  // cleanup path (Init context) or a fallback eager-execution path
  // (Block context) inadvertently calls drain on the SPSC ring; the
  // row mismatch rejects the call before it can corrupt the consumer-
  // side tail.
  //
  // Implementation: thin forwarder to drain; zero runtime cost; the
  // IsPure constraint is checked at substitution time.
  template <typename CallerRow = ::crucible::effects::Row<>>
      requires ::crucible::effects::IsPure<CallerRow>
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard, gnu::hot]] uint32_t drain_pure(
      Entry*        out,
      uint32_t      max_count,
      MetaIndex*    out_meta_starts     = nullptr,
      ScopeHash*    out_scope_hashes    = nullptr,
      CallsiteHash* out_callsite_hashes = nullptr) noexcept CRUCIBLE_NO_THREAD_SAFETY
      pre (::crucible::decide::in_range<std::uint32_t>(
          max_count, std::uint32_t{0}, CAPACITY))
      pre (::crucible::decide::valid_span(max_count, out))
  {
    return drain(out, max_count, out_meta_starts, out_scope_hashes, out_callsite_hashes);
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
      pre (::crucible::decide::in_range<std::uint32_t>(
          max_count, std::uint32_t{0}, CAPACITY))
      pre (max_count == 0 ||
           (out_entries != nullptr &&
            out_meta_starts != nullptr &&
            out_scope_hashes != nullptr &&
            out_callsite_hashes != nullptr))
  {
    if (max_count == 0) [[unlikely]] return 0;

    // get(): acquire on head — pair with producer's release in advance().
    const uint64_t h = head.get();
    const uint64_t t = consumer_tail_;

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
    const uint64_t next_tail = t + count;
    consumer_tail_ = next_tail;
    tail.advance(next_tail);
    // CONTRACT-104-POST: result-bound contract — same shape as
    // drain() above; try_pop_batch's available-clamp guarantees
    // count <= max_count.  Sibling cite to drain() — both the
    // optional-output and all-required forms ship the post.
    CRUCIBLE_POST(count, count <= max_count);
    return count;
  }

  // Approximate count — racy by design (diagnostic only). pure: depends on
  // memory (atomics) but has no side effects — optimizer may CSE adjacent
  // calls, which is fine for a diagnostic. Invariant: head >= tail always.
  // size() reads head/tail at different instants under concurrent
  // producer/consumer advance (CRUCIBLE_NO_THREAD_SAFETY) — the
  // Stale<uint32_t> return type-documents the race, symmetric to
  // MetaLog::size() (WRAP-TraceRing, S2b of #1736).  at_infinity =
  // "unknown lag"; callers .peek() to acknowledge the unsynchronized snapshot.
  [[nodiscard, gnu::pure]] crucible::fixy::wrap::Stale<uint32_t> size() const noexcept CRUCIBLE_NO_THREAD_SAFETY {
    return crucible::fixy::wrap::Stale<uint32_t>::at_infinity(
        static_cast<uint32_t>(head.get() - tail.get()));
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
      post (consumer_tail_ == 0)
      post (cached_tail_.get() == 0)
  {
    head.reset_under_quiescence();
    tail.reset_under_quiescence();
    consumer_tail_ = 0;
    // FIXY-FOUND-114: use the named reset_under_quiescence on
    // non-atomic Monotonic for symmetry with the atomic variants
    // above.  cached_tail_ goes "backward" to 0, which advance()
    // would reject; reset_under_quiescence bypasses the contract
    // explicitly, making the bypass site grep-discoverable.
    cached_tail_.reset_under_quiescence();
  }
};

// Expected footprint ~5.25 MB. Bounds catch accidental layout bloat.
static_assert(sizeof(TraceRing) >= (5u * 1024u * 1024u) &&
              sizeof(TraceRing) <= (6u * 1024u * 1024u),
              "TraceRing footprint outside 5-6 MB envelope — layout changed");

// ── Validated drain max-count carrier (#1055 WRAP-TraceRing-3) ──────
//
// `max_count` parameter on drain / drain_pinned / drain_pure /
// try_pop_batch is structurally bounded by TraceRing::CAPACITY (1<<16
// = 65 536 entries).  Asking the consumer to drain more than the ring
// can hold is meaningless — the inner `std::min(available, max_count)`
// silently clamps it — but the silent clamp masks two real call-site
// bugs:
//
//   1. Caller derived `max_count` from a uint32_t loaded from disk /
//      env / FFI without bounds-checking; UINT32_MAX slips through and
//      becomes a no-op clamp at the inner surface, with no diagnostic.
//
//   2. Caller's `out` buffer was sized for some smaller K but a
//      mistake in size-arithmetic produced max_count > CAPACITY; even
//      though the inner clamp prevents memcpy overrun (available is
//      bounded by CAPACITY), the caller's expectation of "max_count
//      slots written" is silently violated by the available-clamp
//      regardless.
//
// `pre (decide::in_range<uint32_t>(max_count, 0, CAPACITY))` on every
// drain entry point (CONTRACT-104) makes the boundary explicit at
// enforce semantic and routes the bound-check through the named
// predicate so future hardening propagates to every site; the typed
// alias gives future callers a witness they can carry across
// boundaries instead of re-validating the bare uint32_t at every layer.
//
// In constexpr context (constant evaluation) the Refined ctor's pre
// clause `bounded_above<CAPACITY>(v)` (v ≤ CAPACITY) makes a
// violating construction non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Cost claim: regime-1 EBO collapse — Refined<bounded_above<N>,
// uint32_t> erases to a bare uint32_t in -O3 codegen.  The
// gnu::const factory lifts to a single register move.
using ValidDrainCount = ::crucible::fixy::wrap::Refined<
    ::crucible::fixy::wrap::bounded_above<TraceRing::CAPACITY>, uint32_t>;

[[nodiscard, gnu::const]] inline constexpr
uint32_t make_drain_count(ValidDrainCount raw) noexcept {
    return raw.value();
}

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
