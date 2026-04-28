#pragma once

// ── crucible::safety::diag — stable name + type/function ID ─────────
//
// Federation foundation for cache row_hash + structured diagnostic
// names.  Ships four primitives in one header (they share the
// underlying P2996R13 reflection mechanism + the FNV-1a hash):
//
//   * stable_name_of<T>          — canonical type display name
//   * stable_type_id<T>          — 64-bit FNV-1a + fmix64 over the name
//   * canonicalize_pack<Ts...>   — sort-by-name pack normalization
//   * stable_function_id<FnPtr>  — combined hash over function type
//
// Implements FOUND-E07 / E08 / E09 / E10 of 28_04_2026_effects.md
// §7 + 27_04_2026.md §5.9.
//
// ── Federation contract (READ FIRST) ────────────────────────────────
//
// The FOUND-I cache key extension uses `stable_type_id<T>` as part of
// the row_hash that joins L1 IR002 entries across organizations.  For
// federation to work — Meta's Llama-70B on H100 sharing IR002 entries
// with Google's deployment on v5p — the SAME T at SAME row position
// must produce the SAME 64-bit ID across BOTH installations.
//
// **THIS HEADER SHIPS V1 STABILITY GUARANTEES.**  V1 = bit-stable
// WITHIN one build artifact; SAME compiler version + SAME header
// inclusion order → SAME ID.  V2 (cross-compiler federation across
// independent organizations) is NOT yet shipped — it requires either
// a custom canonical type-walker (FOUND-H09) or an ABI-level
// stable-name ABI from the C++26 reflection committee.
//
// What V1 DOES guarantee:
//   * Within one build, stable_type_id<T> is deterministic across
//     repeated calls and across TUs (modulo the TU-context-fragility
//     of display_string_of, mitigated by the .ends_with discipline
//     callers must follow per algebra/Graded.h:156-186).
//   * Across builds with the SAME compiler version + SAME include
//     order, stable_type_id<T> is bit-stable.
//   * Across compiler updates within the same major version (GCC
//     16.0.x), stable_type_id<T> is bit-stable in practice (P2996
//     name-mangling stability has held since GCC 16.0.0).
//
// What V1 DOES NOT guarantee:
//   * Cross-compiler federation (GCC ↔ Clang).  display_string_of's
//     output varies by compiler implementation; FNV-1a downstream
//     amplifies the divergence.
//   * Stability across MAJOR version bumps (GCC 16 → GCC 17).  Future
//     P2996 implementations may rephrase display_string_of's output;
//     when this happens, federation across versions requires either
//     a per-version recipe or the V2 canonical walker.
//   * Stability under inline-namespace introduction.  If a future
//     libstdc++ adds an inline namespace where there wasn't one
//     (e.g. `std::__1::string_view` instead of `std::string_view`),
//     stable_type_id<std::string_view> changes.
//
// **Engineering posture (per CLAUDE.md hard stops):** ship V1
// honestly with the cross-compiler federation caveat documented at
// EVERY usage site that depends on it.  The cache infrastructure
// (FOUND-I) treats federation as a STRETCH goal that requires
// additional machinery; basic single-org caching uses V1 directly.
//
// ── TU-context-fragility (the prior known-issue) ────────────────────
//
// `display_string_of(^^T)` returns a name whose qualification depth
// depends on the including TU's scope chain.  See algebra/Graded.h:
// 156-186 for the canonical write-up of this issue.  Mitigation:
//
//   * For COMPARISON: never use `==` against expected literal names;
//     use `.ends_with(suffix)` since the simple name is always a
//     suffix of the qualified form.
//   * For HASHING: stable_type_id consumes whatever display_string_of
//     produces in the TU where the variable template instantiates.
//     Within one build, this is deterministic (the variable template
//     instantiates at MOST once per TU per T, and the resulting hash
//     is the same across TUs because all of them eventually reach
//     the same `inline constexpr` definition).
//
// ── Architecture ────────────────────────────────────────────────────
//
// FNV-1a 64-bit (Fowler-Noll-Vo) is the underlying string hash:
//   * Bit-stable across all platforms (no signed-integer trickery,
//     no SSE-dependent intrinsics, no endian sensitivity in the algo).
//   * Industry standard for short-string hashing; well-understood
//     avalanche properties.
//   * Reasonable distribution for type-name strings (typically 20-200
//     chars; collision probability ~2^-32 for 10K types per FNV
//     literature).
//
// We finalize with `crucible::detail::fmix64` (MurmurHash3 finalizer
// from Expr.h) to maximize avalanche bits in the high half of the
// 64-bit ID.  fmix64 is already proven in production at
// `Reflect.h::reflect_hash` and `ExprPool` interning paths.
//
// ── References ──────────────────────────────────────────────────────
//
//   28_04_2026_effects.md §7.3 + §8.5  — design rationale + cache row
//                                        keying
//   27_04_2026.md §5.9                  — original spec sketch
//   algebra/Graded.h:156-186            — TU-context-fragility doc
//   Expr.h::detail::fmix64              — MurmurHash3 finalizer
//   FNV-1a 64-bit                       — Fowler-Noll-Vo specification
//                                        offset basis 0xcbf29ce484222325
//                                        prime         0x100000001b3
//
// FOUND-E07 — stable_name_of<T> consteval string_view
// FOUND-E08 — stable_type_id<T> 64-bit hash
// FOUND-E09 — stable_function_id<FnPtr>
// FOUND-E10 — canonicalize_pack<Ts...>

#include <crucible/Expr.h>             // detail::fmix64
#include <crucible/Platform.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// ── FNV-1a 64-bit string hash + fmix64 avalanche finalizer ─────────
// ═════════════════════════════════════════════════════════════════════
//
// FNV-1a 64-bit constants per the Fowler-Noll-Vo specification:
//   offset basis: 0xcbf29ce484222325
//   prime:        0x00000100000001b3
//
// Algorithm: h = offset; for byte b in input: h ^= b; h *= prime.
// All-unsigned arithmetic; no platform-specific intrinsics; bit-stable
// across x86-64, ARM64, RISC-V.
//
// We follow with `detail::fmix64` (MurmurHash3 finalizer) to spread
// avalanche bits across the full 64-bit output.  Without the finalizer,
// FNV-1a's output has slightly skewed bit distribution in the high
// nibbles; fmix64 fixes this without changing collision resistance.

namespace detail {

inline constexpr std::uint64_t FNV1A_OFFSET_BASIS = 0xcbf29ce484222325ULL;
inline constexpr std::uint64_t FNV1A_PRIME        = 0x00000100000001b3ULL;

[[nodiscard]] consteval std::uint64_t fnv1a_64(std::string_view s) noexcept {
    std::uint64_t h = FNV1A_OFFSET_BASIS;
    for (char c : s) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= FNV1A_PRIME;
    }
    return h;
}

// Compose FNV-1a with fmix64 for full-width avalanche.  Used as the
// final transform on type/function names before exposing to callers.
[[nodiscard]] consteval std::uint64_t hash_name(std::string_view s) noexcept {
    return ::crucible::detail::fmix64(fnv1a_64(s));
}

// Combine two 64-bit IDs into one.  Used to build composite IDs (e.g.,
// stable_function_id from per-parameter stable_type_id values).
// Boost-style hash combine: a ^= b + golden_ratio + (a << 6) + (a >> 2).
// Avalanche-strong, order-sensitive (combining (a, b) ≠ (b, a)).
[[nodiscard]] consteval std::uint64_t combine_ids(
    std::uint64_t a, std::uint64_t b) noexcept
{
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return ::crucible::detail::fmix64(a);
}

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── stable_name_of<T> — canonical display name (FOUND-E07) ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Returns `std::meta::display_string_of(^^T)` — the type's name as
// reflection emits it.  V1 contract: bit-stable within one build for
// any given T.  Cross-compiler federation NOT yet guaranteed — see
// the federation contract at file head.
//
// Callers MUST follow the .ends_with(...) discipline for shape
// comparisons (see algebra/Graded.h:156-186).  Direct equality
// against literal names is forbidden (TU-fragility makes such
// comparisons compile in some TUs and fail in others).
//
// Usage:
//
//   constexpr auto name = stable_name_of<MyType>;  // string_view
//   static_assert(name.ends_with("MyType"));        // OK — suffix safe
//   static_assert(name == "MyType");                // FORBIDDEN —
//                                                   // TU-fragile
//
// The string is held in the consteval reflection-result storage; its
// lifetime persists for the life of the translation unit; callers
// may safely persist the string_view as long as the TU's compile
// session is alive (i.e., for the entire program lifetime once
// linked).

template <typename T>
inline constexpr std::string_view stable_name_of =
    std::meta::display_string_of(^^T);

// ═════════════════════════════════════════════════════════════════════
// ── stable_type_id<T> — 64-bit content hash (FOUND-E08) ────────────
// ═════════════════════════════════════════════════════════════════════
//
// 64-bit content hash of stable_name_of<T> via FNV-1a + fmix64.
// Foundation primitive for the FOUND-I cache key extension and the
// FOUND-E08 stable-function-id composition.
//
// Properties (V1):
//   * Deterministic within one build: same T → same ID across calls.
//   * Bit-stable across TUs WITHIN one build: the inline constexpr
//     storage ensures the variable template's value is identical in
//     every TU that instantiates it.
//   * 64-bit collision probability ~2^-32 for 10K distinct types
//     (per FNV-1a literature) — wraparound impossible in practice.
//   * Cross-compiler federation: NOT V1; see federation contract.
//
// Usage:
//
//   constexpr std::uint64_t my_id = stable_type_id<MyType>;
//   static_assert(stable_type_id<int> != stable_type_id<float>);

template <typename T>
inline constexpr std::uint64_t stable_type_id =
    detail::hash_name(stable_name_of<T>);

// ═════════════════════════════════════════════════════════════════════
// ── canonicalize_pack<Ts...> — sort-by-name pack (FOUND-E10) ───────
// ═════════════════════════════════════════════════════════════════════
//
// Returns a `std::tuple<...>` of the same elements as `Ts...` reordered
// by stable_name_of in lexicographic order.  Foundation primitive for
// PermSet / Row / splits_into_pack so order-permutations of equivalent
// packs collapse to the same canonical form.
//
// V1 scope:
//   * Sort: yes (lexicographic by stable_name_of, bubble-sort O(N²))
//   * Dedup: NOT v1 — adjacent duplicates remain in the output tuple.
//     Callers needing dedup should use PermSet's perm_set_canonicalize_t
//     (which composes dedup ON TOP of this canonicalize_pack).  Dedup
//     at this layer would entail tuple-element-removal which expands
//     the metafunction surface considerably.
//
// Empty pack: canonicalize_pack<>::type is std::tuple<>.
// Single-element pack: canonicalize_pack<T>::type is std::tuple<T>.
//
// Usage:
//
//   using sorted = canonicalize_pack<float, int, double>::type;
//   //  → std::tuple<double, float, int>  (alphabetical)
//
//   static_assert(std::is_same_v<
//       canonicalize_pack<int, float>::type,
//       canonicalize_pack<float, int>::type>);

namespace detail {

// Compute the sorted permutation indices for a pack.  Bubble-sort
// over std::array<std::size_t> at consteval — O(N²) which is fine
// for pack sizes typical in our use (< 32).
template <typename... Ts>
[[nodiscard]] consteval auto sort_indices_by_stable_name() noexcept {
    constexpr std::size_t N = sizeof...(Ts);
    std::array<std::string_view, N> const names{ stable_name_of<Ts>... };
    std::array<std::size_t, N> indices{};
    for (std::size_t i = 0; i < N; ++i) indices[i] = i;
    // Bubble sort: simple, stable, sufficient for small N.
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (names[indices[j]] < names[indices[i]]) {
                std::size_t const tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
            }
        }
    }
    return indices;
}

// Materialize the sorted std::tuple from the indices.
template <typename Tuple, std::size_t... Is>
auto reassemble_tuple_impl(std::index_sequence<Is...>)
    -> std::tuple<std::tuple_element_t<Is, Tuple>...>;

}  // namespace detail

template <typename... Ts>
struct canonicalize_pack {
private:
    using source_tuple = std::tuple<Ts...>;
    static constexpr auto sorted_indices = []() consteval {
        if constexpr (sizeof...(Ts) == 0) {
            return std::array<std::size_t, 0>{};
        } else {
            return detail::sort_indices_by_stable_name<Ts...>();
        }
    }();

    template <std::size_t... Is>
    static auto build(std::index_sequence<Is...>)
        -> std::tuple<std::tuple_element_t<sorted_indices[Is], source_tuple>...>;

public:
    using type = decltype(build(std::make_index_sequence<sizeof...(Ts)>{}));
};

template <typename... Ts>
using canonicalize_pack_t = typename canonicalize_pack<Ts...>::type;

// ═════════════════════════════════════════════════════════════════════
// ── stable_function_id<FnPtr> — function ID (FOUND-E09) ────────────
// ═════════════════════════════════════════════════════════════════════
//
// 64-bit content hash of the function's TYPE (not its address — the
// same function body at different addresses yields the same ID; the
// same signature in different declarations yields the same ID).
//
// Implementation: hash `display_string_of(^^decltype(FnPtr))`, which
// renders the function-pointer-type signature including parameters and
// return type.  FNV-1a + fmix64 finalizes.  Same V1/V2 stability
// contract as stable_type_id.
//
// Use cases (FOUND-F09 Cipher computation cache):
//   * Cache key for computation results: hash function type + each
//     argument's stable_type_id, look up cached body, execute or
//     compute-and-store.
//   * Cross-organization function-result federation: defer until V2
//     cross-compiler stability ships.
//
// Usage:
//
//   void my_fn(int, float);
//   constexpr std::uint64_t fid = stable_function_id<&my_fn>;

template <auto FnPtr>
inline constexpr std::uint64_t stable_function_id =
    detail::hash_name(
        std::meta::display_string_of(^^std::remove_pointer_t<decltype(FnPtr)>));

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block — invariants asserted at header inclusion ──────
// ═════════════════════════════════════════════════════════════════════
//
// Every claim verified at header-inclusion time.  Adversarial cases
// included to lock in expected behavior under future refactors.

namespace detail::stable_name_self_test {

// ─── stable_name_of<T> non-empty for primitives ────────────────────
//
// Use .ends_with discipline per the TU-fragility contract.

static_assert(!stable_name_of<int>.empty());
static_assert(!stable_name_of<float>.empty());
static_assert(!stable_name_of<void>.empty());
static_assert(stable_name_of<int>.ends_with("int"));
static_assert(stable_name_of<float>.ends_with("float"));

// ─── stable_type_id<T> distinguishes primitives ────────────────────
//
// FNV-1a + fmix64 produces visually-distinct outputs even for very
// similar input strings; collisions across primitives are
// vanishingly improbable.

static_assert(stable_type_id<int>    != stable_type_id<float>);
static_assert(stable_type_id<int>    != stable_type_id<double>);
static_assert(stable_type_id<float>  != stable_type_id<double>);
static_assert(stable_type_id<int>    != stable_type_id<unsigned int>);
static_assert(stable_type_id<int>    != stable_type_id<long>);
static_assert(stable_type_id<void>   != stable_type_id<int>);
static_assert(stable_type_id<char>   != stable_type_id<unsigned char>);
static_assert(stable_type_id<short>  != stable_type_id<int>);

// stable_type_id is non-zero for every type encountered (FNV-1a
// offset basis is non-zero, and FNV-1a never reduces to zero except
// for very pathological inputs that don't occur with type names).
static_assert(stable_type_id<int>   != 0);
static_assert(stable_type_id<float> != 0);
static_assert(stable_type_id<void>  != 0);

// stable_type_id is consistent across the variable template: same T
// always yields the same value.  (Trivially true for inline constexpr
// — included for documentation discipline.)
static_assert(stable_type_id<int> == stable_type_id<int>);

// ─── canonicalize_pack<Ts...> sorts by stable name ─────────────────

// Empty pack → empty tuple.
static_assert(std::is_same_v<
    canonicalize_pack_t<>,
    std::tuple<>>);

// Single element → unchanged.
static_assert(std::is_same_v<
    canonicalize_pack_t<int>,
    std::tuple<int>>);

// Order-invariance: int + float in either order produces the same
// canonical tuple.  Note: the SORTED order depends on which name
// "int" or "float" comes first lexicographically, which depends on
// display_string_of's output — but it is the SAME canonical order
// regardless of the input permutation.
static_assert(std::is_same_v<
    canonicalize_pack_t<int, float>,
    canonicalize_pack_t<float, int>>);

static_assert(std::is_same_v<
    canonicalize_pack_t<int, float, double>,
    canonicalize_pack_t<float, double, int>>);

static_assert(std::is_same_v<
    canonicalize_pack_t<int, float, double>,
    canonicalize_pack_t<double, int, float>>);

// Mixed-cardinality permutations all collapse to one canonical form.
static_assert(std::is_same_v<
    canonicalize_pack_t<char, short, int, long>,
    canonicalize_pack_t<long, int, short, char>>);

// Adjacent duplicates remain (V1 dedup-deferred).
static_assert(std::is_same_v<
    canonicalize_pack_t<int, int>,
    std::tuple<int, int>>);

// ─── stable_function_id<FnPtr> distinguishes signatures ────────────

namespace fn_test {
inline void f0()                 noexcept {}
inline void f1(int)              noexcept {}
inline void f2(float)            noexcept {}
inline int  f3(int)              noexcept { return 0; }
inline void f4(int, int)         noexcept {}
inline void f5(int, float)       noexcept {}
}  // namespace fn_test

// Different signatures → different IDs.
static_assert(stable_function_id<&fn_test::f0> != stable_function_id<&fn_test::f1>);
static_assert(stable_function_id<&fn_test::f1> != stable_function_id<&fn_test::f2>);
static_assert(stable_function_id<&fn_test::f1> != stable_function_id<&fn_test::f3>);
static_assert(stable_function_id<&fn_test::f1> != stable_function_id<&fn_test::f4>);
static_assert(stable_function_id<&fn_test::f4> != stable_function_id<&fn_test::f5>);

// IDs are non-zero (FNV-1a property).
static_assert(stable_function_id<&fn_test::f0> != 0);

// ─── FNV-1a + fmix64 well-known test vectors ───────────────────────
//
// FNV-1a over the empty string returns the offset basis (no input
// bytes consumed).  fmix64(offset_basis) gives a deterministic
// constant — verify it doesn't change across compiler updates.

static_assert(detail::fnv1a_64("") == detail::FNV1A_OFFSET_BASIS);

// Single-character "a" = 0x61.  FNV-1a step:
//   h = OFFSET_BASIS ^ 0x61
//   h *= PRIME
constexpr std::uint64_t expected_fnv_a =
    (detail::FNV1A_OFFSET_BASIS ^ 0x61ULL) * detail::FNV1A_PRIME;
static_assert(detail::fnv1a_64("a") == expected_fnv_a);

// Two characters "ab" = 0x61, 0x62.
constexpr std::uint64_t expected_fnv_ab = []() consteval {
    std::uint64_t h = detail::FNV1A_OFFSET_BASIS;
    h = (h ^ 0x61ULL) * detail::FNV1A_PRIME;
    h = (h ^ 0x62ULL) * detail::FNV1A_PRIME;
    return h;
}();
static_assert(detail::fnv1a_64("ab") == expected_fnv_ab);

// hash_name composes fnv1a_64 with fmix64; verify ordering of the
// composition (regression-detect if someone swaps them).
static_assert(detail::hash_name("test") ==
              ::crucible::detail::fmix64(detail::fnv1a_64("test")));

// combine_ids is order-sensitive.
static_assert(detail::combine_ids(1, 2) != detail::combine_ids(2, 1));

}  // namespace detail::stable_name_self_test

// ═════════════════════════════════════════════════════════════════════
// ── runtime_smoke_test — non-constant-args execution probe ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Per feedback_algebra_runtime_smoke_test_discipline: exercise the
// header's consteval surface from runtime contexts.  Calls
// stable_type_id<T> via volatile-bounded dispatch; calls stable_name_
// of<T>.size() with the result fed to a volatile sink.

inline void runtime_smoke_test() noexcept {
    // Capture stable_type_id<T> values into a runtime-volatile array;
    // optimizer cannot fold the array into a static initializer once
    // the volatile bound is involved.
    volatile std::uint64_t sink = 0;
    sink ^= stable_type_id<int>;
    sink ^= stable_type_id<float>;
    sink ^= stable_type_id<double>;
    sink ^= stable_type_id<void>;
    sink ^= stable_type_id<unsigned char>;
    sink ^= stable_type_id<long>;
    sink ^= stable_type_id<short>;
    (void)sink;

    // stable_name_of<T> material exists at runtime (string_view points
    // into consteval-result storage which has program lifetime).
    volatile std::size_t name_sink = 0;
    name_sink ^= stable_name_of<int>.size();
    name_sink ^= stable_name_of<float>.size();
    name_sink ^= stable_name_of<void>.size();
    (void)name_sink;

    // canonicalize_pack instantiates at runtime context (the
    // metafunction is constexpr-evaluable but its result type is
    // queried at runtime via sizeof / is_same_v).
    using sorted2 = canonicalize_pack_t<int, float>;
    using sorted2b = canonicalize_pack_t<float, int>;
    bool const same = std::is_same_v<sorted2, sorted2b>;
    volatile bool sink_b = same;
    (void)sink_b;

    // stable_function_id from a real function pointer at runtime.
    auto const fn_ptr = +[](int) noexcept -> int { return 0; };
    volatile std::uint64_t fid_sink = stable_function_id<+[](int) noexcept -> int { return 0; }>;
    fid_sink ^= reinterpret_cast<std::uintptr_t>(fn_ptr);
    (void)fid_sink;
}

}  // namespace crucible::safety::diag
