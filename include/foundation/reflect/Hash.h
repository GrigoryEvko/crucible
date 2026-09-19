// SPDX-License-Identifier: Apache-2.0
//
// These ids key a cache that installations share, so the stability they
// carry is the whole contract, and it is narrow. The same T yields the
// same id within one build, across the translation units of that build,
// and across rebuilds with the same compiler version and the same
// include order.
//
// The id is not stable across compilers, nor across a compiler major
// version. Each implementation phrases a reflected name its own way, and
// the hash amplifies any difference. Wrapping an existing type in a new
// inline namespace moves the id the same way. Two installations that
// share these ids must first agree on the toolchain, or one silently
// reads another's entry as its own.
//
// A reflected name is also qualified to a depth that follows the scope
// chain of the including translation unit. Compare such a name with
// ends_with against the simple name, never with == against a literal.
// The simple name is always a suffix of the qualified form, so equality
// compiles in one translation unit and fails in the next. Hashing is
// unaffected: the variable template resolves to one inline constexpr
// definition that every translation unit shares.
//
// Old spelling: include/crucible/safety/diag/StableName.h; fmix64 and
// combine_ids were in include/crucible/Expr.h.

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace foundation::reflect {

// The one mixing primitive in the tree.  Accumulator folds do not call it
// directly — they call `combine_ids(state, input)` just below, which is this
// function behind one displacement step.  The displacement is load-bearing,
// and the last paragraph says why.
//
// This is the MurmurHash3 finalizer.  Each step is invertible modulo 2^64 —
// an xor-shift is its own inverse family, and a multiply by an odd constant
// is invertible — so fmix64 is a PERMUTATION of the 64-bit words.  That one
// property is why it is the only mixer here:
//
//   * A permutation has no absorbing value.  There is no `x` for which
//     `fmix64(state ^ x)` forgets `state`, so no single input can erase the
//     prefix of a fold.  Measured: zero collisions over 400,000 consecutive
//     inputs, as a bijection must give.
//   * It is total.  Every 64-bit input maps to a distinct 64-bit output, so
//     a content hash of 0 — the KernelCache empty-slot sentinel, and a value
//     make_region's own postcondition refuses — can only arise from the one
//     accumulator state that maps to it, never from a degenerate input.
//
// It replaced `wymix(a, b) = lo(a*b) ^ hi(a*b)`, deleted 2026-09-15, which
// was none of those things.  The 128-bit product folded down to 64 bits is
// lossy by construction and had two absorbing values for its second operand:
// `wymix(a, 0)` is 0 and `wymix(a, ~0)` is ~0, for every `a`.  Both were
// reachable from real tensor metadata — a uint8 tensor on CPU:0 packs to
// zero, and ScalarType::Undefined is int8_t(-1), which sign-extends to all
// ones — so two structurally different regions hashed identically, on the
// value that keys the compiler's cache.  Three call sites had already
// patched around the zero case locally with three different spellings, each
// commenting that wymix collapses on it; the per-op tensor fold, the one
// that matters most, never got the treatment.
//
// wymix bought nothing for that.  Its own comment claimed it avalanched
// better than a shift-and-xor chain; measured against this tree's other
// fmix64-based combiner it avalanches identically, 31.93 against 32.04
// flipped output bits per input bit flipped, both at the ideal 32 of 64.
//
// Two reasons a fold calls combine_ids rather than fmix64 on a bare xor.
//
// A single `fmix64(a ^ b)` is xor-symmetric, so it cannot on its own tell
// operand order.  Order sensitivity comes from the CHAIN: the accumulator
// has already been through fmix64 and the input has not, so the two are not
// interchangeable across steps.  Do not flatten a fold into one xor.
//
// And `fmix64(0)` is 0.  A permutation still has exactly one preimage of
// zero, and for the bare form that preimage is `input == accumulator` — a
// coincidence that ordinary data reaches, because a seed and a schema hash
// can be built from the same constant.  It happened on the first run of
// this change: test_cipher_commit_lifetime mints a region whose schema hash
// is `1 * 0x9E3779B97F4A7C15`, which is the fold's own seed, so the first
// step produced `fmix64(0)` and the content hash came out 0 — the
// KernelCache empty-slot sentinel, caught by make_region's postcondition.
// combine_ids displaces the input by the golden ratio and mixes the
// accumulator's own bits before the xor, so the zero preimage stops
// coinciding with `input == accumulator`.  Measured: 200,000 of 200,000
// self-mixes give 0 through the bare form, 0 of 200,000 through combine_ids.
constexpr uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

// The accumulator step every fold in the tree spells.  Boost-style combine
// — a golden-ratio salt and two shifts of the accumulator — finalized by
// fmix64 above.  It is order-sensitive: combining a with b differs from
// combining b with a, and callers that fold a sequence rely on that.
//
// Read the fmix64 block above for why a fold calls this rather than
// fmix64 on a bare xor.
//
// constexpr and not consteval because one body has to serve both the
// compile-time fold and a runtime check that re-derives the same value.
// A second copy of this body under any other name is a drift surface:
// changing the salt, the mix or the finalizer would leave that copy stale
// and change the shared key while every assertion against it still passed.
// scripts/check-no-combine-ids-duplicate.sh is the gate on that.
[[nodiscard]] constexpr uint64_t combine_ids(uint64_t a, uint64_t b) noexcept {
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return fmix64(a);
}

// The two constants are the ones the 64-bit FNV-1a specification fixes.
// The algorithm is unsigned arithmetic over bytes with no intrinsic and
// no endian dependence, so the digest is identical on every platform.

namespace detail {

inline constexpr std::uint64_t FNV1A_OFFSET_BASIS = 0xcbf29ce484222325ULL;
inline constexpr std::uint64_t FNV1A_PRIME = 0x00000100000001b3ULL;

[[nodiscard]] consteval std::uint64_t fnv1a_64(std::string_view s) noexcept {
    std::uint64_t h = FNV1A_OFFSET_BASIS;
    for (char c : s) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= FNV1A_PRIME;
    }
    return h;
}

// FNV-1a alone leaves the high bits weakly mixed, so the finalizer runs
// over the digest before any caller sees it.
[[nodiscard]] consteval std::uint64_t hash_name(std::string_view s) noexcept { return fmix64(fnv1a_64(s)); }

}  // namespace detail

// The string lives in consteval result storage, which outlives every
// caller, so holding the view for the life of the program is safe.

template <typename T>
inline constexpr std::string_view stable_name_of = std::meta::display_string_of(^^T);

template <typename T>
inline constexpr std::uint64_t stable_type_id = detail::hash_name(stable_name_of<T>);

// Sorting by name collapses two packs that differ only in order to one
// type. It does not deduplicate: a repeated element stays repeated.

namespace detail {

// The sort is quadratic. Packs reaching here hold a few dozen elements
// at most, and the whole sort runs at compile time.
template <typename... Ts>
[[nodiscard]] consteval auto sort_indices_by_stable_name() noexcept {
    constexpr std::size_t N = sizeof...(Ts);
    std::array<std::string_view, N> const names{stable_name_of<Ts>...};
    std::array<std::size_t, N> indices{};
    for (std::size_t i = 0; i < N; ++i)
        indices[i] = i;
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

template <typename Tuple, std::size_t... Is>
auto reassemble_tuple_impl(std::index_sequence<Is...>) -> std::tuple<std::tuple_element_t<Is, Tuple>...>;

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

// This hashes the function type, never the address. Two distinct
// functions that share a signature therefore share one id, and one
// function reached through different declarations keeps a single id.

template <auto FnPtr>
inline constexpr std::uint64_t stable_function_id =
    detail::hash_name(std::meta::display_string_of(^^std::remove_pointer_t<decltype(FnPtr)>));

namespace detail::stable_name_self_test {

static_assert(!stable_name_of<int>.empty());
static_assert(!stable_name_of<float>.empty());
static_assert(!stable_name_of<void>.empty());
static_assert(stable_name_of<int>.ends_with("int"));
static_assert(stable_name_of<float>.ends_with("float"));

static_assert(stable_type_id<int> != stable_type_id<float>);
static_assert(stable_type_id<int> != stable_type_id<double>);
static_assert(stable_type_id<float> != stable_type_id<double>);
static_assert(stable_type_id<int> != stable_type_id<unsigned int>);
static_assert(stable_type_id<int> != stable_type_id<long>);
static_assert(stable_type_id<void> != stable_type_id<int>);
static_assert(stable_type_id<char> != stable_type_id<unsigned char>);
static_assert(stable_type_id<short> != stable_type_id<int>);

static_assert(stable_type_id<int> != 0);
static_assert(stable_type_id<float> != 0);
static_assert(stable_type_id<void> != 0);

static_assert(stable_type_id<int> == stable_type_id<int>);

// These literals pin the ids the current toolchain produces. A shift in
// how a name is printed moves every downstream hash silently, so one of
// these fails first and names the type that moved. Treat a failure as a
// decision, not a bug: confirm the shift breaks nobody who reads a shared
// cache, then refresh the pins deliberately. A compiler major-version
// roll is expected to fail them.

static_assert(stable_type_id<int> == 0x038bf5d93760ba14ULL);
static_assert(stable_type_id<unsigned int> == 0x3e40352bf14d5e8cULL);
static_assert(stable_type_id<float> == 0xaac94173610ce8ebULL);
static_assert(stable_type_id<double> == 0x5a427827acb3b7f4ULL);
static_assert(stable_type_id<void> == 0x7095b61429cf52a0ULL);
static_assert(stable_type_id<char> == 0x24810aa534fd4e53ULL);
static_assert(stable_type_id<unsigned char> == 0xeb532a1cd85a3221ULL);
static_assert(stable_type_id<signed char> == 0xe668b88a72723d2eULL);
static_assert(stable_type_id<short> == 0x76a26fe7af41346dULL);
static_assert(stable_type_id<long> == 0xb398537731c4a05dULL);
static_assert(stable_type_id<long long> == 0x8e73a318de406be0ULL);
static_assert(stable_type_id<unsigned long long> == 0xcb9dc82adf69491aULL);
static_assert(stable_type_id<bool> == 0xc7dfd75159543180ULL);

static_assert(std::is_same_v<canonicalize_pack_t<>, std::tuple<>>);

static_assert(std::is_same_v<canonicalize_pack_t<int>, std::tuple<int>>);

// Which name sorts first depends on how reflection prints it. The
// canonical order is the same for every permutation of one pack, which
// is the only property callers rely on.
static_assert(std::is_same_v<canonicalize_pack_t<int, float>, canonicalize_pack_t<float, int>>);

static_assert(std::is_same_v<canonicalize_pack_t<int, float, double>, canonicalize_pack_t<float, double, int>>);

static_assert(std::is_same_v<canonicalize_pack_t<int, float, double>, canonicalize_pack_t<double, int, float>>);

static_assert(std::is_same_v<canonicalize_pack_t<char, short, int, long>, canonicalize_pack_t<long, int, short, char>>);

static_assert(std::is_same_v<canonicalize_pack_t<int, int>, std::tuple<int, int>>);

namespace fn_test {
inline void f0() noexcept {}
inline void f1(int) noexcept {}
inline void f2(float) noexcept {}
inline int f3(int) noexcept { return 0; }
inline void f4(int, int) noexcept {}
inline void f5(int, float) noexcept {}
}  // namespace fn_test

static_assert(stable_function_id<&fn_test::f0> != stable_function_id<&fn_test::f1>);
static_assert(stable_function_id<&fn_test::f1> != stable_function_id<&fn_test::f2>);
static_assert(stable_function_id<&fn_test::f1> != stable_function_id<&fn_test::f3>);
static_assert(stable_function_id<&fn_test::f1> != stable_function_id<&fn_test::f4>);
static_assert(stable_function_id<&fn_test::f4> != stable_function_id<&fn_test::f5>);

static_assert(stable_function_id<&fn_test::f0> != 0);

// The empty string consumes no bytes, so the digest is the offset basis.
static_assert(detail::fnv1a_64("") == detail::FNV1A_OFFSET_BASIS);

constexpr std::uint64_t expected_fnv_a = (detail::FNV1A_OFFSET_BASIS ^ 0x61ULL) * detail::FNV1A_PRIME;
static_assert(detail::fnv1a_64("a") == expected_fnv_a);

constexpr std::uint64_t expected_fnv_ab = []() consteval {
    std::uint64_t h = detail::FNV1A_OFFSET_BASIS;
    h = (h ^ 0x61ULL) * detail::FNV1A_PRIME;
    h = (h ^ 0x62ULL) * detail::FNV1A_PRIME;
    return h;
}();
static_assert(detail::fnv1a_64("ab") == expected_fnv_ab);

// Swapping the two stages produces different digests, so the order of
// composition is pinned here.
static_assert(detail::hash_name("test") == fmix64(detail::fnv1a_64("test")));

static_assert(combine_ids(1, 2) != combine_ids(2, 1));

}  // namespace detail::stable_name_self_test

// A static_assert can be discharged by the constant folder without the
// consteval body running as written. Driving the same surface from a
// runtime context, through volatile sinks the optimizer cannot fold,
// keeps that path honest.

inline void runtime_smoke_test_stable_name() noexcept {
    volatile std::uint64_t sink = 0;
    sink ^= stable_type_id<int>;
    sink ^= stable_type_id<float>;
    sink ^= stable_type_id<double>;
    sink ^= stable_type_id<void>;
    sink ^= stable_type_id<unsigned char>;
    sink ^= stable_type_id<long>;
    sink ^= stable_type_id<short>;
    (void)sink;

    volatile std::size_t name_sink = 0;
    name_sink ^= stable_name_of<int>.size();
    name_sink ^= stable_name_of<float>.size();
    name_sink ^= stable_name_of<void>.size();
    (void)name_sink;

    using sorted2 = canonicalize_pack_t<int, float>;
    using sorted2b = canonicalize_pack_t<float, int>;
    bool const same = std::is_same_v<sorted2, sorted2b>;
    volatile bool sink_b = same;
    (void)sink_b;

    auto const fn_ptr = +[](int) noexcept -> int { return 0; };
    volatile std::uint64_t fid_sink = stable_function_id<+[](int) noexcept -> int { return 0; }>;
    fid_sink ^= std::bit_cast<std::uintptr_t>(fn_ptr);
    (void)fid_sink;
}

}  // namespace foundation::reflect
