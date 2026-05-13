#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <crucible/Platform.h>
#include <crucible/ir001/MerkleDag.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>
#include <crucible/warden/Registry.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/HugePageBuffer.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Refined.h>

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
// ── API tiers ──────────────────────────────────────────────────────────
//
// Three additive surfaces share the same body:
//   • try_append()                         — base API, untyped return.
//   • try_append_pinned()                  — HotPath<Hot, MetaIndex> wrap
//                                            (FOUND-G22).
//   • try_append_pure<CallerRow>()         — row-typed wrap, IsPure
//                                            constraint (FOUND-I17).
//
// New production call sites SHOULD use the row-typed `_pure` facade.
// It is zero-cost at runtime (thin forwarder, default CallerRow =
// Row<>) and gives a compile-time gate that rejects any caller in a
// non-Pure context (IO / Block / Bg / Init / Test / Alloc).  The
// non-pure variants remain for ABI compatibility, internal forwarders,
// and call sites that intentionally accept callers from non-Pure
// contexts.
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
//      n*sizeof(TensorMeta) bytes. Wraparound: two memcpys. memcpy of
//      compact TensorMeta structs
//      compiles to a small number of vector stores instead of scalar
//      field-by-field copy.
//   3. Aligned allocation: 64-byte aligned buffer base for cache-line-
//      friendly access patterns.
//   4. Software prefetch: after each write, prefetch 3 cache lines for the
//      NEXT write position. Each TensorMeta currently spans 3 × 64B cache
//      lines.
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
  // #944 WRAP-MetaLog-1: the hugepage-aligned `TensorMeta[CAPACITY]`
  // backing was raw `aligned_alloc + register_hot_region + free` —
  // producer-side ctor / dtor pair manually balanced a malloc with a
  // free, with no type-system protection against a leaked-on-throw or
  // double-free regression.  HugePageBuffer<TensorMeta> wraps the
  // alloc lifetime in move-only RAII; `entries` is now a raw pointer
  // projection cached from `entries_buffer_.data()` so the hot path
  // (entries[h & MASK]) keeps its single load with no indirection.
  crucible::safety::HugePageBuffer<TensorMeta> entries_buffer_;  // owner
  TensorMeta* entries = nullptr;                // 8B — producer-only read; aliases entries_buffer_.data()
  // padding to fill cache line (implicitly provided by alignas on tail)

  // ── Consumer cache line ──
  // tail lives alone on its own cache line to prevent false sharing with
  // the producer's head/cached_tail_/entries.  Consumer-owned: bg thread
  // publishes via advance(release); producer reads via get(acquire) on
  // the rare full-path.
  alignas(64) crucible::safety::AtomicMonotonic<uint32_t> tail{0};   // 4B

  MetaLog()
      : entries_buffer_{crucible::safety::HugePageBuffer<TensorMeta>::allocate(CAPACITY)},
        entries{entries_buffer_.data()} {
    // PMD alignment: HugePageBuffer::allocate uses the same kHugePageBytes
    // alignment + round_up_huge byte count, so MADV_HUGEPAGE in the
    // hot-region registration (kernel 5.8+ rejects on non-2-MB addrs)
    // works identically to the prior raw aligned_alloc path.
    crucible::warden::register_hot_region(entries, entries_buffer_.bytes(),
        /*huge=*/true, "MetaLog.entries");
  }

  ~MetaLog() {
    if (entries != nullptr) {
      crucible::warden::unregister_hot_region(entries);
    }
    // entries_buffer_ dtor (HugePageBuffer move-only RAII) frees storage.
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
      // CONTRACT-104: bound check discharges through decide::in_range
      // (closed-interval [0, CAPACITY], unsigned natural lower bound).
      // Pure parameter ref + static constexpr CAPACITY — not subject to
      // the GCC 16.1.1 consteval-bypass that forced CONTRACT-100..103
      // to migrate to in-body CRUCIBLE_PRE; P2900 pre() is sufficient
      // here.  The cite is for grep-discipline consistency: a single
      // hardening change to the in_range predicate propagates to every
      // try_append entry point.
      pre (::crucible::decide::in_range<std::uint32_t>(
          n, std::uint32_t{0}, CAPACITY))
      pre (::crucible::decide::valid_span(n, metas))
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
    // Each TensorMeta spans 3 cache lines at the current 168B layout.
    // We prefetch the first entry's 3 lines. For n>1, the hardware
    // prefetcher typically handles the sequential continuation.
    {
      uint32_t next_pos = (h + n) & MASK;
      // §III-clean cast cascade: TensorMeta* → void* → char*.  Used purely for
      // byte-offset arithmetic into the prefetch builtin (which itself takes
      // const void*); no actual char-array lifetime is started.
      const char* next_ptr = static_cast<const char*>(
          static_cast<const void*>(&entries[next_pos]));
      __builtin_prefetch(next_ptr,       1 /*write*/, 3 /*high locality*/);
      __builtin_prefetch(next_ptr + 64,  1 /*write*/, 3 /*high locality*/);
      __builtin_prefetch(next_ptr + 128, 1 /*write*/, 3 /*high locality*/);
    }

    // advance: publish the entries[] memcpy (above) to the consumer.
    // pre(load(acquire) < h+n) collapses to [[assume]] under hot-TU
    // contract semantics; body is the same store(release) as before.
    // n > 0 guaranteed by the early return at the top.
    head.advance(h + n);
    const MetaIndex result{h};
    // CONTRACT-104-POST: result-shape contract — the returned
    // MetaIndex is either MetaIndex::none() (n==0 or table-full
    // early returns above) OR an in-bounds index `h` that was the
    // pre-advance head value.  Since `h = head.peek_relaxed()` and
    // the SPSC ring's invariant maintains `0 <= head < 2^64`, but
    // entries[] is indexed by `h & MASK` where MASK = CAPACITY-1,
    // the consumer always finds the appended block at
    // `entries[result.raw() & MASK]`.  Catches a future refactor
    // that returns `MetaIndex{h + n}` (post-advance) by mistake,
    // which would skip the just-appended block on read.  Routes
    // through CRUCIBLE_POST for grep-discipline consistency.
    CRUCIBLE_POST(result, !result.is_valid() || result.raw() == h);
    return result;
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
      pre (::crucible::decide::valid_span(n, metas))
  {
    return crucible::safety::HotPath<crucible::safety::HotPathTier_v::Hot, MetaIndex>{
        try_append(metas, n)};
  }

  // ═══════════════════════════════════════════════════════════════
  // FOUND-I17: row-typed facade — pins try_append as Pure
  // ═══════════════════════════════════════════════════════════════
  //
  // try_append is a hot-path memory-only operation: it does NOT do
  // I/O, blocking, or allocation; it is not init/test/bg context.
  // In F* effect terms, it is `Pure` (the bottom of the effect-row
  // lattice — no atoms in the row).  This facade pins that fact at
  // the type level via an `IsPure<CallerRow>` constraint.
  //
  // Caller-row contract: the CallerRow template argument MUST satisfy
  // `Subrow<CallerRow, Row<>>`, i.e., the caller's row must be empty.
  // A caller in any of the following contexts is REJECTED:
  //   • Row<Effect::IO>     — IO context cannot append to MetaLog
  //   • Row<Effect::Block>  — blocking context cannot append
  //   • Row<Effect::Bg>     — bg consumer-side cannot append
  //   • Row<Effect::Init>   — init-time code cannot append (hot path)
  //   • Row<Effect::Test>   — test-only code cannot append in prod
  //   • Row<Effect::Alloc>  — allocating context cannot append
  //
  // The facade is ADDITIVE: existing try_append() / try_append_pinned()
  // callers stay unchanged.  Production hot-path callers can migrate
  // by replacing `try_append(...)` with `try_append_pure<>(...)` to
  // gain the compile-time check.
  //
  // Implementation: thin forwarder to try_append; zero runtime cost
  // (one inlined branchless tail-call under -O3).  The IsPure
  // constraint is checked at substitution time, not at runtime.
  //
  // Default template argument is `Row<>` so `try_append_pure(...)`
  // (no template-arg) is equivalent to `try_append_pure<Row<>>(...)`
  // — the most common case at production hot-path call sites.
  template <typename CallerRow = ::crucible::effects::Row<>>
      requires ::crucible::effects::IsPure<CallerRow>
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] CRUCIBLE_INLINE MetaIndex
  try_append_pure(const TensorMeta* metas, uint32_t n)
      CRUCIBLE_NO_THREAD_SAFETY
      pre (::crucible::decide::valid_span(n, metas))
  {
    return try_append(metas, n);
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
  // Saves sizeof(TensorMeta) × count memcpy per op when successful.
  // Background thread only (SPSC consumer): zero-copy span into buffer.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] TensorMeta* try_contiguous(uint32_t start, uint32_t count) const
      CRUCIBLE_LIFETIMEBOUND CRUCIBLE_NO_THREAD_SAFETY {
    if (count == 0) [[unlikely]] return nullptr;
    uint32_t const start_pos = start & MASK;
    TensorMeta* const result = (start_pos + count <= CAPACITY)
        ? &entries[start_pos]
        : nullptr;
    // CONTRACT-MetaLog-TryContiguous-POST: result-shape post (CRUCIBLE_POST
    // taxonomy class 2).  Three cases unified:
    //   1. count == 0          ⇒ early return nullptr above (excluded
    //                              from this path).
    //   2. start_pos+count > CAP ⇒ wraps; result == nullptr.
    //   3. start_pos+count ≤ CAP ⇒ result == &entries[start_pos].
    // The unified post captures: "either the bg consumer must copy
    // (nullptr return — wrap detected) OR the returned pointer aliases
    // the buffer's contiguous slice starting at start & MASK".
    // Catches a refactor that returns a pointer to the wrong slot
    // (e.g. forgetting the MASK, or off-by-one) which would silently
    // propagate bad memory to the bg DAG-build path.  Sibling
    // discipline: same result-shape framing as CONTRACT-108-POST
    // (ReplayEngine output_ptr / input_ptr — `result == nullptr ||
    // sid.is_valid()`).  Routes through CRUCIBLE_POST for the GCC
    // 16.1.1 consteval-bypass foldable-body class — `result` is a
    // local but the predicate references `entries` (a class member),
    // exactly the bypass-vulnerable shape.
    //
    // `entries` is a `TensorMeta*` pointer member (line 105), not a
    // C array — in this const method the pointer is const but the
    // pointee remains non-const, so `&entries[start_pos]` is plain
    // `TensorMeta*` (no const_cast required).
    CRUCIBLE_POST(result, result == nullptr || result == &entries[start_pos]);
    return result;
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

// ── Validated append-count carrier (#945 WRAP-MetaLog-2) ─────────────
//
// `try_append` accepts an `n` parameter — the number of TensorMeta
// records to append from the caller's buffer.  Any `n > CAPACITY`
// is structurally non-sensical: the buffer holds at most CAPACITY
// records total, so a single call asking for more can never succeed
// (every fast-path retry would observe the same overflow against the
// real tail and return MetaIndex::none() forever).  Without a
// type-level gate, the only protection was the runtime check at the
// top of try_append's body — fine for graceful failure but mute about
// the structural bound at the type system.
//
// `ValidMetaAppendCount` is the typed witness.  Production callers
// who hold an `n` from a trusted source can construct a
// `ValidMetaAppendCount{n}` once at the boundary; downstream code
// that takes a `ValidMetaAppendCount` parameter inherits the bound
// without re-checking.  The Refined ctor's pre clause
// `bounded_above<MetaLog::CAPACITY>(v)` (i.e. v ≤ CAPACITY) makes
// any out-of-bound construction a non-constant expression in
// constexpr context (P1494R5 → ill-formed) and aborts via the
// project contract handler at runtime.
//
// `try_append` itself carries
// `pre (decide::in_range<uint32_t>(n, 0, CAPACITY))`
// (CONTRACT-104) so both the existing untyped surface and the typed
// widening factory enforce the same structural bound through the
// named predicate; future hardening propagates to every site.  Defense-in-depth: existing
// `if (h - cached_tail + n > CAPACITY)` runtime guard plus the new
// type-level witness fire BEFORE any memcpy or head publish.
//
// Regime-1 EBO collapse keeps the wrapper zero-cost:
// `sizeof(ValidMetaAppendCount) == sizeof(uint32_t) == 4`.
using ValidMetaAppendCount = ::crucible::safety::Refined<
    ::crucible::safety::bounded_above<MetaLog::CAPACITY>, uint32_t>;

// Widening factory for `ValidMetaAppendCount → uint32_t` at production
// hot-path call sites.  `gnu::const` documents that the result depends
// only on the argument and has no side effects; the optimizer can CSE
// / DCE the call freely under -O3.
[[nodiscard, gnu::const]] inline constexpr
uint32_t make_meta_append_count(ValidMetaAppendCount raw) noexcept {
    return raw.value();
}

} // namespace crucible
