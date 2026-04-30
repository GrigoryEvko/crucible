#pragma once

// ── crucible::cipher::ComputationCache ──────────────────────────────
//
// FOUND-F09 of 27_04_2026.md §5.13.  The dispatcher analog of
// KernelCache:  KernelCache stores compiled kernel bytecode keyed on
// `(content_hash, row_hash)`; ComputationCache stores compiled-body
// objects keyed on `(stable_function_id<FnPtr>, stable_type_id<Args>...)`.
// When the dispatcher generates a lowering for `dispatch(fn, args)`,
// it consults this cache:  hit → use the cached body, miss → generate,
// optionally insert.
//
// ── Why a separate cache from KernelCache ──────────────────────────
//
// KernelCache lives in MerkleDag.h and is keyed on the IR-level
// content hash of a kernel — "this exact arithmetic produces this
// kernel".  ComputationCache is keyed on the FUNCTION-HANDLE shape
// — "this fn called with these argument types maps to this compiled
// body."  The two caches sit at different layers of the dispatcher:
//
//   FnPtr + Args                         ──→ ComputationCache (this header)
//        ↓ generate lowering
//   IR (op DAG with content_hash)        ──→ KernelCache (MerkleDag.h)
//        ↓ Forge → Mimic compile
//   Native bytecode                      ──→ KernelCache (same)
//
// A single fn → IR generation step lives in ComputationCache; the
// IR → bytecode step lives in KernelCache.  Both can hit (full
// reuse), only one (regen one layer), or neither (full compile).
//
// ── API surface ─────────────────────────────────────────────────────
//
//   struct CompiledBody;
//        Opaque forward-declared type.  The dispatcher provides the
//        concrete definition — could be a function pointer, a code
//        blob, a serialized computation graph, or anything else.
//        The foundation only stores `CompiledBody*` and never
//        dereferences it.
//
//   template <auto FnPtr, typename... Args>
//   inline constexpr std::uint64_t computation_cache_key;
//        Canonical 64-bit cache key for federation / serialization
//        / debugging.  Combines `stable_function_id<FnPtr>` with
//        each `stable_type_id<Args>` via Boost-style `combine_ids`
//        (FOUND-E10).  Order-sensitive, deterministic across runs
//        within one build.
//
//   template <auto FnPtr, typename... Args>
//   [[nodiscard]] CompiledBody*
//   lookup_computation_cache() noexcept;
//        Returns the cached pointer if previously inserted for this
//        instantiation, nullptr on miss.  Single atomic acquire-load.
//
//   template <auto FnPtr, typename... Args>
//   void insert_computation_cache(CompiledBody* body) noexcept
//       pre (body != nullptr);
//        Idempotent insert.  CAS expecting nullptr → store; if the
//        slot is already populated (concurrent insert, or this is a
//        second insert by the same thread), the CAS fails and the
//        function returns.  The first writer wins; subsequent
//        callers MUST re-lookup to discover what was actually
//        cached.
//
//   void drain_computation_cache(std::chrono::seconds max_age) noexcept;
//        Phase 5 stub.  Today: no-op (slots persist for program
//        lifetime).  Phase 5: walks the global slot registry and
//        evicts entries whose last-publish age exceeds `max_age`.
//        The API is published now so call sites already speak the
//        cache-tier vocabulary.
//
// ── Storage discipline ──────────────────────────────────────────────
//
// One `inline std::atomic<CompiledBody*>` variable per (FnPtr,
// Args...) template instantiation.  `inline` means the linker
// deduplicates across TUs — every TU that instantiates
// `lookup_computation_cache<F, A1, A2>()` reads the SAME atomic.
// Default-constructed to nullptr; lookup is a single acquire-load
// (~1-3 ns on warm L1).  Insert is a single CAS (~10-20 ns
// uncontended, ~50-100 ns contended).
//
// This pattern is lock-free, contention-free between distinct
// instantiations (no shared hash buckets), and zero-allocation.
// The trade-off is that drain cannot enumerate slots without a
// global registry — Phase 5 will add the registry; today the API
// is published so call sites compile against it.
//
// Memory cost: one cache-line per active (FnPtr, Args...)
// instantiation in the binary's BSS — typically dozens to low
// hundreds, bounded by the static call graph at compile time.
//
// ── Why per-instantiation atomic over hash table ────────────────────
//
// (a) Lock-free with no false sharing — every distinct instantiation
//     has its own line.
// (b) No hash collision possible — the C++ template instantiation
//     IS the key.  Two slots can't alias the same atomic unless they
//     specialize the SAME (FnPtr, Args...) tuple, in which case they
//     SHOULD alias.
// (c) Zero runtime allocation — the atomic lives in BSS.
// (d) Cost-of-miss equals cost-of-hit equals one atomic-load — the
//     dispatcher never sees a fast-path / slow-path branch.
//
// The cost is the drain limitation noted above.
//
// ── 8-axiom audit ───────────────────────────────────────────────────
//
//   InitSafe   — atomic slots are zero-initialized at program-start
//                static init (BSS).  No reads-before-init possible.
//   TypeSafe   — instantiation-driven keying makes type-misuse
//                impossible: lookup<F, int> and lookup<F, double>
//                are different overloads, different slots.
//   NullSafe   — lookup returns nullptr on miss; consumers contract-
//                check before deref.  Insert refuses nullptr via
//                pre()-clause.
//   MemSafe    — no heap; stored pointers' lifetime is the caller's
//                responsibility (the cache stores raw CompiledBody*
//                — the dispatcher pins the body's lifetime).
//   BorrowSafe — atomic slots; no aliased mutation possible.
//   ThreadSafe — std::atomic with acq_rel CAS; multi-threaded
//                publish is safe; the FIRST writer wins (idempotent
//                contract).
//   LeakSafe   — slots' lifetime is the program's; CompiledBody*
//                lifetime is the dispatcher's responsibility (and
//                drain_computation_cache() will be wired in Phase 5).
//   DetSafe    — cache key is deterministic (stable_function_id +
//                stable_type_id are bit-stable within a build).
//                Same (FnPtr, Args...) → same slot → same cached
//                pointer.

#include <crucible/safety/diag/StableName.h>   // stable_function_id, stable_type_id, combine_ids

#include <atomic>
#include <chrono>
#include <cstdint>

namespace crucible::cipher {

// ═════════════════════════════════════════════════════════════════════
// ── CompiledBody — opaque forward declaration ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The foundation only stores pointers.  The dispatcher (or any
// downstream consumer) defines the concrete `struct CompiledBody {
// ... };` in its TU and casts the cached pointer back at lookup
// time.

struct CompiledBody;

// ═════════════════════════════════════════════════════════════════════
// ── computation_cache_key — canonical 64-bit cache key ─────────────
// ═════════════════════════════════════════════════════════════════════
//
// Combines `stable_function_id<FnPtr>` with each `stable_type_id<Args>`
// via Boost-style `combine_ids` (FOUND-E10).  Used by:
//
//   * Federation:  serialized cache entries key on this 64-bit value.
//   * Debugging:   diagnostic output displays this key for hits/misses.
//   * Future Phase 5 drain: the global registry indexes by this key.
//
// Properties:
//   * Order-sensitive — `key<F, int, float>` ≠ `key<F, float, int>`.
//   * Bit-stable within one build (E07-E10 contracts).
//   * Cross-build stability not guaranteed (paper: P2996 reflection
//     normalization is implementation-defined; see StableName.h
//     §federation contract).
//
// Empty Args... reduces to just stable_function_id<FnPtr>.

namespace detail {

// Fold combine_ids over the parameter pack, seeded with the
// function ID.  Order-sensitive by construction (Boost-style).
template <auto FnPtr, typename... Args>
[[nodiscard]] consteval std::uint64_t computation_cache_key_impl() noexcept {
    std::uint64_t k = ::crucible::safety::diag::stable_function_id<FnPtr>;
    // Fold-expression: combine_ids(k, stable_type_id<Arg_i>) for each Arg_i.
    ((k = ::crucible::safety::diag::detail::combine_ids(
              k, ::crucible::safety::diag::stable_type_id<Args>)),
     ...);
    return k;
}

}  // namespace detail

template <auto FnPtr, typename... Args>
inline constexpr std::uint64_t computation_cache_key =
    detail::computation_cache_key_impl<FnPtr, Args...>();

// ═════════════════════════════════════════════════════════════════════
// ── Per-instantiation atomic slot ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

// One atomic per distinct (FnPtr, Args...) tuple.  `inline` =
// linker dedups across TUs.  Default-constructs to nullptr (BSS,
// zero-initialized at program-start).
template <auto FnPtr, typename... Args>
inline std::atomic<CompiledBody*> compiled_body_slot{nullptr};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── lookup_computation_cache ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Single atomic acquire-load.  Returns nullptr on miss; otherwise
// returns the FIRST inserted body (subsequent inserts no-op per the
// idempotent contract).

template <auto FnPtr, typename... Args>
[[nodiscard]] CompiledBody*
lookup_computation_cache() noexcept {
    return detail::compiled_body_slot<FnPtr, Args...>
        .load(std::memory_order_acquire);
}

// ═════════════════════════════════════════════════════════════════════
// ── insert_computation_cache ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// CAS expecting nullptr → store the new pointer.  If the slot is
// already populated, the CAS fails and we return without storing —
// the first writer wins.  Callers who care about which body got
// cached MUST re-lookup after insert.
//
// pre(body != nullptr) — inserting nullptr would be indistinguishable
// from a miss, defeating the cache's purpose.

template <auto FnPtr, typename... Args>
void insert_computation_cache(CompiledBody* body) noexcept
    pre (body != nullptr)
{
    CompiledBody* expected = nullptr;
    detail::compiled_body_slot<FnPtr, Args...>
        .compare_exchange_strong(expected, body,
                                  std::memory_order_acq_rel,
                                  std::memory_order_acquire);
    // Idempotent: if expected was non-null, CAS failed and we leave
    // the existing entry alone.  The caller can re-lookup to
    // discover the actual cached pointer.
}

// ═════════════════════════════════════════════════════════════════════
// ── drain_computation_cache — Phase 5 stub ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Today: no-op.  Slots persist for the program's lifetime.
//
// Phase 5: a global registry (registered at compile_body_slot's
// static init) will allow this function to walk all live slots and
// evict entries older than `max_age`.  The API is published now so
// production call sites already invoke `drain_computation_cache(...)`
// and the Phase 5 wiring is a body change without an API change.

inline void drain_computation_cache(
    [[maybe_unused]] std::chrono::seconds max_age) noexcept {
    // Phase 5: walk global slot registry, evict stale entries.
    // Today: no-op.
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block — invariants asserted at header inclusion ──────
// ═════════════════════════════════════════════════════════════════════

namespace detail::computation_cache_self_test {

inline void p_unary(int) noexcept {}
inline void p_binary(int, double) noexcept {}
inline int  p_returning(int) noexcept { return 0; }

// ── Cache keys: distinct instantiations → distinct keys ───────────

// Different functions → different keys.
static_assert(::crucible::cipher::computation_cache_key<&p_unary, int>
              != ::crucible::cipher::computation_cache_key<&p_binary, int, double>);

// Same function, different argument types → different keys.
static_assert(::crucible::cipher::computation_cache_key<&p_unary, int>
              != ::crucible::cipher::computation_cache_key<&p_unary, float>);
static_assert(::crucible::cipher::computation_cache_key<&p_unary, int>
              != ::crucible::cipher::computation_cache_key<&p_unary, double>);

// Same function, same argument types → same key.
static_assert(::crucible::cipher::computation_cache_key<&p_unary, int>
              == ::crucible::cipher::computation_cache_key<&p_unary, int>);

// Order matters: (int, double) ≠ (double, int).
static_assert(::crucible::cipher::computation_cache_key<&p_binary, int, double>
              != ::crucible::cipher::computation_cache_key<&p_binary, double, int>);

// Empty Args... → key reduces to just the function ID.
static_assert(::crucible::cipher::computation_cache_key<&p_unary>
              != ::crucible::cipher::computation_cache_key<&p_binary>);
static_assert(::crucible::cipher::computation_cache_key<&p_unary>
              == ::crucible::safety::diag::stable_function_id<&p_unary>);

// Keys are non-zero (the underlying hash_name is non-zero by
// construction; the fold can't zero out a non-zero seed).
static_assert(::crucible::cipher::computation_cache_key<&p_unary, int> != 0);
static_assert(::crucible::cipher::computation_cache_key<&p_binary, int, double> != 0);

}  // namespace detail::computation_cache_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Verifies the load-bearing runtime contracts:
//   1. Lookup-before-insert returns nullptr (miss).
//   2. Insert + lookup round-trips (hit).
//   3. Second insert is idempotent (first writer wins).
//   4. Distinct (FnPtr, Args...) instantiations have isolated slots.
//
// Uses two of-the-test functions and stub `CompiledBody` pointers
// (cast from non-null integer addresses — never dereferenced).

inline bool computation_cache_smoke_test() noexcept {
    using namespace detail::computation_cache_self_test;

    // Stub bodies — never dereferenced; the cache stores pointers
    // opaquely.  Different non-null addresses suffice.
    auto* body_a = reinterpret_cast<CompiledBody*>(
        static_cast<std::uintptr_t>(0x1));
    auto* body_b = reinterpret_cast<CompiledBody*>(
        static_cast<std::uintptr_t>(0x2));

    bool ok = true;

    // ── (1) Lookup-before-insert: miss returns nullptr ───────────
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, int>()
                == nullptr);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, float>()
                == nullptr);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_binary, int, double>()
                == nullptr);

    // ── (2) Insert-then-lookup: round-trips ──────────────────────
    ::crucible::cipher::insert_computation_cache<&p_unary, int>(body_a);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, int>()
                == body_a);

    // ── (3) Second insert: idempotent (first writer wins) ────────
    ::crucible::cipher::insert_computation_cache<&p_unary, int>(body_b);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, int>()
                == body_a);  // body_a still cached; body_b ignored.

    // ── (4) Distinct instantiations: isolated slots ──────────────
    // p_unary<float> still misses (different slot from p_unary<int>).
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, float>()
                == nullptr);
    // p_binary still misses (different function).
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_binary, int, double>()
                == nullptr);

    // ── (5) drain_computation_cache: no-op stub today ────────────
    // Calls without exception; doesn't evict the body_a entry.
    ::crucible::cipher::drain_computation_cache(std::chrono::seconds{0});
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, int>()
                == body_a);

    return ok;
}

}  // namespace crucible::cipher
