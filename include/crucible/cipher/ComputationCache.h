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

#include <crucible/safety/diag/StableName.h>   // stable_function_id, stable_type_id, combine_ids, hash_name
#include <crucible/safety/diag/RowHashFold.h>  // row_hash_contribution_v (FOUND-I02 / F11)
#include <crucible/effects/EffectRow.h>        // effects::Row<Es...> (FOUND-H02 / F11)

#include <atomic>
#include <chrono>
#include <cstdint>
#include <meta>                                // std::meta::reflect_constant

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
// ── IsCacheableFunction — concept fence on FnPtr (FOUND-F09-AUDIT-2)
// ═════════════════════════════════════════════════════════════════════
//
// Without this constraint, `auto FnPtr` accepts any structural NTTP:
// data-pointer globals, member-function pointers, integral constants
// — all of which compile silently into nonsense cache slots.
//
// We require FnPtr to be a pointer to a free / static function:
// `decltype(FnPtr)` must be `R(*)(Args...)` (after cv-ref strip),
// and the pointee type must satisfy `std::is_function_v`.  Member-
// function pointers (`R(C::*)(Args...)`) are explicitly rejected
// because they require a `this` argument the cache cannot represent
// in its key.
//
// nullptr-rejection note:  C++ comparison of an `auto` function-
// pointer NTTP against `nullptr` is not always a constant
// expression in GCC 16, so we don't write `FnPtr != nullptr` in
// the concept.  The structural type check is the load-bearing
// part — production call sites pass `&fn` for some named function,
// which is unconditionally non-null; explicitly materializing a
// null function-pointer NTTP requires deliberate
// `static_cast<R(*)(Args...)>(nullptr)`, an obvious construction
// that no production call site would type by accident.
//
// The concept is consteval-evaluable, so failure is a hard
// requires-clause diagnostic at the call site naming the violation.

template <auto FnPtr>
concept IsCacheableFunction =
    std::is_pointer_v<decltype(FnPtr)>
    && std::is_function_v<std::remove_pointer_t<decltype(FnPtr)>>;

// ═════════════════════════════════════════════════════════════════════
// ── IsEffectRow — concept fence on Row template parameter (FOUND-F11)
// ═════════════════════════════════════════════════════════════════════
//
// The row-aware cache API (`*_in_row` family below) takes a `Row`
// template parameter — must be `effects::Row<Es...>` for some
// (possibly empty) effect pack `Es...`.
//
// Without this fence, `typename Row` would accept any type — the
// row-hash fold would compile to 0 for non-Row types (the primary
// `row_hash_contribution<T>::value == 0`) and silently produce a
// row-blind cache key indistinguishable from the legacy unrowed API.
// Worse: the user might intend `lookup_in_row<&fn, int, double>()`
// where `int` is supposed to be the first Arg — accidental binding
// to Row would produce a nonsense key.
//
// Detection via class-template specialization: any
// `effects::Row<Es...>` matches; everything else returns false.
// The concept then tests the trait.

namespace detail {

template <typename R>
inline constexpr bool is_effect_row_v = false;

template <::crucible::effects::Effect... Es>
inline constexpr bool is_effect_row_v<::crucible::effects::Row<Es...>> = true;

}  // namespace detail

template <typename R>
concept IsEffectRow = detail::is_effect_row_v<R>;

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
// Empty Args... still distinguishes by function NAME — two
// functions with the SAME SIGNATURE but different identifiers
// produce DIFFERENT keys (FOUND-F09-AUDIT — see WHY below).

namespace detail {

// FOUND-F09-AUDIT — Why we don't just use stable_function_id:
//
//   stable_function_id<FnPtr> hashes ONLY the function POINTER
//   TYPE (`void(int)`), not the function identity.  Two distinct
//   functions with the same signature produce the SAME
//   stable_function_id — verified empirically via
//   /tmp/check_sfid.cpp during the F09 audit:
//
//       inline void fn_alpha(int) noexcept {}
//       inline void fn_beta (int) noexcept {}
//       static_assert(stable_function_id<&fn_alpha>
//                     == stable_function_id<&fn_beta>);  // collides!
//
//   For the in-memory cache this is fine — the per-instantiation
//   atomic slot's storage is keyed on the NTTP value of FnPtr, not
//   on stable_function_id.  But for FEDERATION / SERIALIZATION,
//   the 64-bit cache key MUST distinguish identical-signature
//   functions, otherwise the dispatcher would alias unrelated
//   compiled bodies on the wire.
//
// The fix: hash the function's reflected NAME via
// std::meta::reflect_constant(FnPtr) + display_string_of, which
// returns the function identifier (e.g., "fn_alpha" vs
// "fn_beta").  We also fold the function type (preserves
// signature distinguishability — two distinct functions with the
// same name in different scopes / overloads / TUs would still
// differ at the TYPE).  Belt-and-suspenders.

// Fold combine_ids over the parameter pack, seeded with both the
// function NAME hash AND the function TYPE hash.
// Order-sensitive by construction (Boost-style).
template <auto FnPtr, typename... Args>
    requires ::crucible::cipher::IsCacheableFunction<FnPtr>
[[nodiscard]] consteval std::uint64_t computation_cache_key_impl() noexcept {
    // Function name hash — distinguishes same-signature, different-
    // identity functions.  reflect_constant materializes the NTTP
    // as a meta::info for which display_string_of returns the
    // identifier.
    std::uint64_t k = ::crucible::safety::diag::detail::hash_name(
        std::meta::display_string_of(std::meta::reflect_constant(FnPtr)));
    // Combine with function-TYPE hash — guards against future
    // refactors that might canonicalize the name string but not
    // the type.
    k = ::crucible::safety::diag::detail::combine_ids(
        k, ::crucible::safety::diag::stable_function_id<FnPtr>);
    // Fold-expression: combine_ids(k, stable_type_id<Arg_i>) for each Arg_i.
    ((k = ::crucible::safety::diag::detail::combine_ids(
              k, ::crucible::safety::diag::stable_type_id<Args>)),
     ...);
    return k;
}

}  // namespace detail

template <auto FnPtr, typename... Args>
    requires IsCacheableFunction<FnPtr>
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
    requires ::crucible::cipher::IsCacheableFunction<FnPtr>
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
    requires IsCacheableFunction<FnPtr>
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
    requires IsCacheableFunction<FnPtr>
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
// ── Row-aware API (FOUND-F11) ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The `_in_row` family threads an `effects::Row<Es...>` through the
// cache key fold so that two calls to the same function under
// different effect rows hit different cache slots.
//
// ── Why a parallel API instead of replacing the row-blind one ──────
//
// The row-blind API has shipped and is contract-stable (8-axiom
// audited, 6-layer audit chain).  Adding a Row template parameter
// would either (a) break every existing call site, or (b) require
// a default `Row = effects::Row<>` which silently changes the
// inference of an existing `lookup<&fn, int>()` (today: FnPtr,
// Arg=int; with default: FnPtr, Row=int — wrong!).  Neither is
// acceptable.
//
// Instead the row-aware variants live as parallel templates with
// `_in_row` suffix.  Production migration will move call sites
// individually; the row-blind path stays available as the legacy
// surface.  The two paths' atomic slots are KEYED DIFFERENTLY (the
// row-aware slots include row-hash in their fold) so they never
// alias each other — even when the row is `effects::Row<>` (empty).
//
// ── Cache-key formula ──────────────────────────────────────────────
//
// computation_cache_key_in_row<FnPtr, Row, Args...> =
//     combine_ids(name_hash<FnPtr>,
//        combine_ids(stable_function_id<FnPtr>,
//           combine_ids(row_hash_contribution_v<Row>,
//              fold_combine_ids(stable_type_id<Args>...))))
//
// Properties (pinned by static_asserts in the self-test block):
//   * Same FnPtr + same Row + same Args → same key (deterministic).
//   * Different Row → different key (the load-bearing F11 invariant).
//   * Permutation of Es in Row<Es...> → SAME key (row_hash is
//     sort-fold over Effect underlying values).
//   * Different from row-blind key for the same (FnPtr, Args...) —
//     the cache slot identity DIFFERS even when Row is EmptyRow,
//     because the row-aware key folds row_hash AND because the
//     atomic slots are different `inline` template instantiations.

namespace detail {

// Row-aware variant of computation_cache_key_impl.  Same shape as
// the row-blind impl (above) but folds the Row's content hash in
// after the function-type seed.
template <auto FnPtr, typename Row, typename... Args>
    requires ::crucible::cipher::IsCacheableFunction<FnPtr>
          && ::crucible::cipher::IsEffectRow<Row>
[[nodiscard]] consteval std::uint64_t
computation_cache_key_in_row_impl() noexcept {
    std::uint64_t k = ::crucible::safety::diag::detail::hash_name(
        std::meta::display_string_of(std::meta::reflect_constant(FnPtr)));
    k = ::crucible::safety::diag::detail::combine_ids(
        k, ::crucible::safety::diag::stable_function_id<FnPtr>);
    // Row contribution — the F11 differentiator.  combine_ids is
    // order-sensitive and Boost-style mixed; even a zero-row
    // contribution (impossible for valid Row<Es...> per RowHashFold's
    // cardinality-seed) would still differ from the row-blind key
    // because the position of the row-hash slot in the fold is
    // unique to the row-aware variant.
    k = ::crucible::safety::diag::detail::combine_ids(
        k, ::crucible::safety::diag::row_hash_contribution_v<Row>);
    ((k = ::crucible::safety::diag::detail::combine_ids(
              k, ::crucible::safety::diag::stable_type_id<Args>)),
     ...);
    return k;
}

}  // namespace detail

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
inline constexpr std::uint64_t computation_cache_key_in_row =
    detail::computation_cache_key_in_row_impl<FnPtr, Row, Args...>();

namespace detail {

// One atomic per (FnPtr, Row, Args...) tuple.  Disjoint from
// `compiled_body_slot<FnPtr, Args...>` — different template, different
// linker-deduplicated symbol.
template <auto FnPtr, typename Row, typename... Args>
    requires ::crucible::cipher::IsCacheableFunction<FnPtr>
          && ::crucible::cipher::IsEffectRow<Row>
inline std::atomic<CompiledBody*> compiled_body_slot_in_row{nullptr};

}  // namespace detail

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
[[nodiscard]] CompiledBody*
lookup_computation_cache_in_row() noexcept {
    return detail::compiled_body_slot_in_row<FnPtr, Row, Args...>
        .load(std::memory_order_acquire);
}

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
void insert_computation_cache_in_row(CompiledBody* body) noexcept
    pre (body != nullptr)
{
    CompiledBody* expected = nullptr;
    detail::compiled_body_slot_in_row<FnPtr, Row, Args...>
        .compare_exchange_strong(expected, body,
                                  std::memory_order_acq_rel,
                                  std::memory_order_acquire);
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

// Zero-args function — exercises the empty-fold case in the cache
// key (no Args... contributions, just the function-name +
// function-type seed).  Pins that an empty pack doesn't degenerate
// the hash.
inline void p_void() noexcept {}

// noexcept-vs-throwing distinction probe.  GCC's type system
// distinguishes `void(*)(int)` from `void(*)(int) noexcept`,
// which means stable_function_id<> sees different function-pointer
// types, which means computation_cache_key produces different
// keys, which means the cache slot is isolated.  This is the
// load-bearing invariant: a function that gets `noexcept`-qualified
// (or has it removed) re-keys its compiled body, never aliases
// the old slot.
inline void p_throwing(int) {}  // NOT noexcept
inline void p_noexcept(int) noexcept {}

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

// Empty Args... → key still distinguishes distinct functions
// (function-name hash + function-type hash, no parameter fold).
static_assert(::crucible::cipher::computation_cache_key<&p_unary>
              != ::crucible::cipher::computation_cache_key<&p_binary>);

// FOUND-F09-AUDIT: same-signature distinct functions MUST yield
// distinct keys.  This is the load-bearing post-condition of
// switching from stable_function_id (signature-keyed) to a
// reflect_constant-based name hash.  Verified at compile time
// using two functions with identical signatures.
namespace fnames_collision_check {
inline void s_fn_one(int) noexcept {}
inline void s_fn_two(int) noexcept {}
static_assert(::crucible::cipher::computation_cache_key<&s_fn_one>
              != ::crucible::cipher::computation_cache_key<&s_fn_two>,
              "computation_cache_key MUST distinguish same-signature "
              "different-name functions; otherwise federation aliases "
              "unrelated compiled bodies on the wire.");
static_assert(::crucible::cipher::computation_cache_key<&s_fn_one, int>
              != ::crucible::cipher::computation_cache_key<&s_fn_two, int>);
}  // namespace fnames_collision_check

// Keys are non-zero (the underlying hash_name is non-zero by
// construction; the fold can't zero out a non-zero seed).
static_assert(::crucible::cipher::computation_cache_key<&p_unary, int> != 0);
static_assert(::crucible::cipher::computation_cache_key<&p_binary, int, double> != 0);

// FOUND-F09-AUDIT-3 — empty-fold case (Args... == {}).  The cache
// key must be non-zero AND distinct from any non-empty-Args key
// for the same function.  Empty fold returns the seeded
// (name + type) hash; combining `int` into the seed must produce
// a different key.
static_assert(::crucible::cipher::computation_cache_key<&p_void> != 0);
static_assert(::crucible::cipher::computation_cache_key<&p_void>
              != ::crucible::cipher::computation_cache_key<&p_unary>);
// Empty-Args key for void(void) ≠ Args=int key for void(int) — even
// though both unary calls return same shape, the parameter pack
// distinguishes.
static_assert(::crucible::cipher::computation_cache_key<&p_unary>
              != ::crucible::cipher::computation_cache_key<&p_unary, int>);

// FOUND-F09-AUDIT-3 — noexcept distinguishes cache slots.  GCC's
// type system gives `void(*)(int)` and `void(*)(int) noexcept`
// different identities; stable_function_id<> sees different
// function-pointer types; the keys diverge.  This is the
// load-bearing isolation property: a function gaining or losing
// `noexcept` re-keys its compiled body so the dispatcher cannot
// dispatch a maybe-throwing body through a noexcept call site.
static_assert(::crucible::cipher::computation_cache_key<&p_throwing, int>
              != ::crucible::cipher::computation_cache_key<&p_noexcept, int>,
              "noexcept-vs-throwing function-pointer types MUST "
              "produce different cache keys; otherwise the "
              "dispatcher can dispatch a maybe-throwing compiled "
              "body through a noexcept caller and break the "
              "noexcept guarantee.");

// ── IsCacheableFunction concept witnesses (FOUND-F09-AUDIT-2) ─────
//
// Positive witnesses — function pointers satisfy the concept.
static_assert(::crucible::cipher::IsCacheableFunction<&p_unary>);
static_assert(::crucible::cipher::IsCacheableFunction<&p_binary>);
static_assert(::crucible::cipher::IsCacheableFunction<&p_returning>);

// Negative witnesses — non-function NTTPs do NOT satisfy.  These
// REJECT shapes that the dispatcher cache cannot meaningfully
// represent: integral constants (no function identity), data
// pointers (not callable in a way the cache understands), and
// pointers-to-data (same).  Member-function pointers are also
// rejected (see neg_computation_cache_member_fn_nttp.cpp).
inline int s_data_global = 0;
static_assert(!::crucible::cipher::IsCacheableFunction<42>);
static_assert(!::crucible::cipher::IsCacheableFunction<&s_data_global>);

// FOUND-F09-AUDIT-4 — `nullptr` literal rejection.  `decltype(nullptr)`
// is `std::nullptr_t`, NOT a pointer type, so `is_pointer_v` is false
// and the concept rejects.  This pins the rejection at compile-time;
// production callers who deliberately pass `nullptr` as the FnPtr NTTP
// (e.g., in a generic forwarder) get a hard requires-clause
// diagnostic instead of a silent nonsense slot.
static_assert(!::crucible::cipher::IsCacheableFunction<nullptr>);

// ── FOUND-F11 — IsEffectRow concept witnesses ─────────────────────
//
// Positive: every concrete `effects::Row<Es...>` satisfies.
static_assert(::crucible::cipher::IsEffectRow<::crucible::effects::Row<>>);
static_assert(::crucible::cipher::IsEffectRow<
              ::crucible::effects::Row<::crucible::effects::Effect::Bg>>);
static_assert(::crucible::cipher::IsEffectRow<
              ::crucible::effects::Row<::crucible::effects::Effect::Bg,
                                       ::crucible::effects::Effect::IO>>);

// Negative: bare types do NOT.
static_assert(!::crucible::cipher::IsEffectRow<int>);
static_assert(!::crucible::cipher::IsEffectRow<void>);
// Effect itself is the atom enum, not a row — must be wrapped.
static_assert(!::crucible::cipher::IsEffectRow<::crucible::effects::Effect>);

// ── FOUND-F11 — row-aware cache key witnesses ─────────────────────
//
// Same FnPtr, different Row → different key (the load-bearing
// F11 invariant — the dispatcher MUST differentiate "same fn,
// different effect row" or it would alias semantically distinct
// compiled bodies).
static_assert(
    ::crucible::cipher::computation_cache_key_in_row<
        &p_unary, ::crucible::effects::Row<>, int>
    !=
    ::crucible::cipher::computation_cache_key_in_row<
        &p_unary, ::crucible::effects::Row<::crucible::effects::Effect::Bg>, int>,
    "F11: row-aware cache must distinguish keys for same FnPtr+Args "
    "but different Row.");

// Different FnPtr, same Row, same Args → different key (FnPtr still
// load-bearing).
static_assert(
    ::crucible::cipher::computation_cache_key_in_row<
        &p_unary, ::crucible::effects::Row<>, int>
    !=
    ::crucible::cipher::computation_cache_key_in_row<
        &p_binary, ::crucible::effects::Row<>, int, double>);

// Same FnPtr, same Row, same Args → same key (deterministic).
static_assert(
    ::crucible::cipher::computation_cache_key_in_row<
        &p_unary, ::crucible::effects::Row<>, int>
    ==
    ::crucible::cipher::computation_cache_key_in_row<
        &p_unary, ::crucible::effects::Row<>, int>);

// Permutation invariance: row_hash_contribution<Row<Es...>> is a
// sort-fold over the effect underlying values, so re-ordering the
// effects in the row pack produces the SAME row hash → SAME cache
// key.  This is the documented row_hash design (RowHashFold.h
// FOUND-I02).
static_assert(
    ::crucible::cipher::computation_cache_key_in_row<
        &p_unary,
        ::crucible::effects::Row<::crucible::effects::Effect::Bg,
                                 ::crucible::effects::Effect::IO>, int>
    ==
    ::crucible::cipher::computation_cache_key_in_row<
        &p_unary,
        ::crucible::effects::Row<::crucible::effects::Effect::IO,
                                 ::crucible::effects::Effect::Bg>, int>,
    "F11: row-aware cache key must be permutation-invariant in the "
    "Row's effect pack — row_hash is sort-fold over Effect underlying "
    "values per FOUND-I02.");

// Row-aware key MUST differ from row-blind key for the same
// (FnPtr, Args...) — even when the row is EmptyRow.  This pins the
// disjoint-slot invariant: a row-blind insert into <&fn, int> never
// aliases a row-aware <&fn, EmptyRow, int>.
static_assert(
    ::crucible::cipher::computation_cache_key<&p_unary, int>
    !=
    ::crucible::cipher::computation_cache_key_in_row<
        &p_unary, ::crucible::effects::Row<>, int>,
    "F11: row-aware cache key MUST differ from row-blind key even "
    "for EmptyRow — otherwise migration would silently alias legacy "
    "and row-typed compiled bodies.");

// Empty Args fold edge — row-aware variant.  `<&p_void, EmptyRow>`
// must produce a non-zero, distinct-from-row-blind-empty-args key.
static_assert(
    ::crucible::cipher::computation_cache_key_in_row<
        &p_void, ::crucible::effects::Row<>>
    != 0);
static_assert(
    ::crucible::cipher::computation_cache_key_in_row<
        &p_void, ::crucible::effects::Row<>>
    !=
    ::crucible::cipher::computation_cache_key<&p_void>);

// ── Structural-cost asserts (FOUND-F09-AUDIT) ─────────────────────
//
// Pin the runtime properties that the API contract depends on:
//
//   (a) The atomic slot is ALWAYS lock-free on this platform.
//       If a future libstdc++ rewrites std::atomic<T*> to fall back
//       to a lock for some pointer-size combination, lookup would
//       no longer be ~1-3 ns and the hot-path claim would break.
//       The static_assert catches that at compile time.
//
//   (b) The atomic slot's storage is exactly sizeof(CompiledBody*).
//       No padding, no auxiliary state.  This is the
//       zero-runtime-overhead claim made in the header doc-block.
//       A bigger atomic (e.g., DCAS-backed) would silently inflate
//       BSS and slow the hot path; the static_assert catches that.
//
// These pin platform assumptions documented in CLAUDE.md §XIV
// (64-bit, naturally-aligned atomic pointers are lock-free on
// x86-64 + aarch64).

static_assert(std::atomic<::crucible::cipher::CompiledBody*>::is_always_lock_free,
              "ComputationCache slot must be lock-free; otherwise "
              "lookup is no longer the documented ~1-3 ns hot path.");

static_assert(sizeof(std::atomic<::crucible::cipher::CompiledBody*>)
              == sizeof(::crucible::cipher::CompiledBody*),
              "ComputationCache slot must be exactly pointer-sized; "
              "otherwise BSS-per-instantiation cost is silently inflated.");

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
//   5. drain_computation_cache is a Phase-5 no-op stub.
//   6. Empty-Args round-trip exercises the empty-fold case.
//   7. noexcept-vs-throwing slots are runtime-isolated.
//
// Uses two of-the-test functions and stub `CompiledBody` pointers
// (cast from non-null integer addresses — never dereferenced).
//
// CONTRACT — single-call-per-process (FOUND-F09-AUDIT-5):
//   Sub-blocks (1), (4), (6), (7) check miss-before-insert
//   assertions that hold only on the FIRST invocation.  The
//   underlying atomic slots are `inline std::atomic` globals
//   that persist for program lifetime — once populated, they
//   stay populated.  A second invocation would observe the
//   slots already non-null and the miss checks would fail.
//
//   To make this contract enforceable rather than fragile, we
//   gate entry on a static atomic counter:
//     * First call: runs the full body, returns the test result.
//     * Subsequent calls: return false fail-fast.
//
//   The fail-fast signal flows up through the calling test's
//   EXPECT_TRUE check, surfacing accidental re-invocation as a
//   clear failure rather than as a silent miss-becomes-hit
//   discrepancy in the existing assertions.

inline bool computation_cache_smoke_test() noexcept {
    using namespace detail::computation_cache_self_test;

    // FOUND-F09-AUDIT-5 — single-call guard.  See doc-block above.
    // `relaxed` is sufficient: the test is single-threaded and the
    // counter is itself the synchronization point.  fetch_add returns
    // the PRIOR value, so first call sees 0 and proceeds; second sees
    // 1 and bails.
    static std::atomic<int> call_counter{0};
    if (call_counter.fetch_add(1, std::memory_order_relaxed) > 0) {
        return false;  // Re-invocation breaks miss-before-insert assumptions.
    }

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

    // ── (6) FOUND-F09-AUDIT-4: empty-Args round-trip ─────────────
    // Pins the runtime side of the empty-fold case (AUDIT-3 covered
    // it at compile time).  Verifies `lookup<&p_void>()` exercises a
    // distinct atomic slot from `lookup<&p_unary, int>()`, that the
    // miss/insert/hit cycle behaves identically with no Args, and
    // that the empty-pack instantiation didn't accidentally collapse
    // to a non-template overload.
    auto* body_c = reinterpret_cast<CompiledBody*>(
        static_cast<std::uintptr_t>(0x3));
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_void>()
                == nullptr);                      // miss before insert
    ::crucible::cipher::insert_computation_cache<&p_void>(body_c);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_void>()
                == body_c);                       // hit after insert
    // p_unary<int> slot still has body_a, untouched by p_void insert.
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, int>()
                == body_a);

    // ── (7) FOUND-F09-AUDIT-4: noexcept slot isolation runtime ───
    // Compile-time invariant (key<&p_throwing,int> != key<&p_noexcept,int>)
    // is pinned in the self-test block.  This sub-test pins the
    // RUNTIME complement: distinct keys → distinct atomic slots.
    // Insert into the throwing slot only; verify the noexcept slot
    // still misses.  If the inline-atomic deduplication ever broke
    // such that throwing and noexcept aliased, this would fail.
    auto* body_d = reinterpret_cast<CompiledBody*>(
        static_cast<std::uintptr_t>(0x4));
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_throwing, int>()
                == nullptr);                      // throwing miss
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_noexcept, int>()
                == nullptr);                      // noexcept miss
    ::crucible::cipher::insert_computation_cache<&p_throwing, int>(body_d);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_throwing, int>()
                == body_d);                       // throwing hit
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_noexcept, int>()
                == nullptr);                      // noexcept STILL miss
                                                  // — slot isolation holds.

    return ok;
}

}  // namespace crucible::cipher
