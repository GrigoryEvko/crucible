#pragma once

// ── crucible::safety::FixedArray<T, N> ──────────────────────────────
//
// Bounded-capacity stack-allocated array newtype.  Replaces raw
// `T buf[N]` C arrays where the capacity is fixed at compile time
// and every slot is logically used.  Differs from std::array<T, N>
// in three load-bearing ways:
//
//   1. NSDMI default-initialization (InitSafe axiom).
//      `FixedArray<int, 8>{}` zero-fills, AND `FixedArray<int, 8> a;`
//      (no braces) ALSO zero-fills via the NSDMI on the storage
//      member.  std::array<int, 8> a; (no braces) leaves elements
//      uninitialized — the same gotcha as raw C arrays.  FixedArray's
//      NSDMI catches the uninitialized form.
//
//   2. NO exception-throwing accessor (.at() banned per CLAUDE.md
//      §III opt-out — Crucible is -fno-exceptions).  Bounds-safe
//      access goes through:
//        * operator[](size_t) — UB on OOB; -D_GLIBCXX_DEBUG overlay
//          catches at runtime.
//        * at(Refined<bounded_above<N-1>, size_t>) — proof-token API,
//          bounds check fires ONCE at Refined ctor; subsequent .at()
//          calls are unchecked at the type level.
//
//   3. Distinct type identity.  FixedArray<int, 8> ≠ std::array<int, 8>
//      at the type level.  Catches accidental swaps + signals the
//      wrapper discipline at every callsite.
//
// ── Production call sites (per WRAP-* tasks) ────────────────────────
//
//   #932  Lower.h:        const Expr* sizes[8]    → FixedArray<const Expr*, 8>
//   #1019 StorageNbytes.h: int64_t extents_buf[8] → alignas(64) FixedArray<int64_t, 8>
//
// Both sites are stack scratch buffers exactly 8 elements wide.
// Migration is structural: sizeof preserved (no heap, no growth);
// alignas(K) on the FixedArray instance propagates to its single
// storage member, matching the SIMD-aligned StorageNbytes case.
//
// ── Public API ──────────────────────────────────────────────────────
//
//   Construction:
//     FixedArray()                                 — NSDMI zero-init
//     FixedArray(std::in_place, args...)           — exactly N args,
//                                                   compile-time arity
//                                                   gate via requires
//     fill_with(T const&)                          — static factory
//
//   UB-bounded access (caller asserts bound):
//     operator[](size_t)
//
//   Refined-typed access (compiler-checked via proof token):
//     at(index_type)                               — index_type =
//                                                   Refined<bounded_above<N-1>>;
//                                                   construction-time
//                                                   bounds check
//
//   Iteration / boundary:
//     data() / begin() / end() / size() / empty()
//     front() / back()                             — always valid
//                                                   (N > 0 enforced)
//     as_span()                                    — std::span<T, N>
//                                                   with COMPILE-TIME
//                                                   extent (preserves
//                                                   bounds info to
//                                                   span-accepting APIs)
//
//   Mutation:
//     fill(T const&)                               — matches std::array
//     swap(other) / friend swap(a, b)
//
//   Equality:
//     operator==                                   — element-wise (only
//                                                   when T is
//                                                   equality_comparable)
//
// ── Eight-axiom audit ───────────────────────────────────────────────
//
//   InitSafe — `T data_[N]{}` NSDMI zero-initializes every element.
//              No uninitialized-read path exists.  THIS IS THE LOAD-
//              BEARING DIFFERENTIATOR vs std::array.
//   TypeSafe — distinct from std::array<T, N>; index access offers
//              both UB-bounded operator[] and proof-token at().
//              N > 0 prevents the degenerate-empty case.
//   NullSafe — no pointers internally.
//   MemSafe  — no heap, no allocation, no growth.  Defaulted copy /
//              move / dtor.  Trivially_copyable inherited from T.
//   BorrowSafe — value type; per-instance, no aliasing surface.
//   ThreadSafe — value type; per-thread copies are cheap.
//   LeakSafe — no resource ownership.
//   DetSafe  — pure structural; no FP, no kernel, no allocation.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
//   sizeof(FixedArray<T, N>)  == sizeof(T[N])
//   alignof(FixedArray<T, N>) == alignof(T)   (when no alignas applied)
//
//   alignas(K) on a FixedArray instance propagates to the wrapping
//   struct → propagates to the storage member at offset 0 → exactly
//   K-aligned.  Matches the discipline `alignas(64) int64_t buf[8]`
//   used in SIMD scratch buffers (StorageNbytes case).
//
//   Trivially_copyable when T is trivially_copyable; standard_layout
//   always.  memcpy-safe for serialization/replay paths.
//
// ── Why structural (not Graded) ─────────────────────────────────────
//
// A bounded array is a STRUCTURAL constraint on the carrier (the
// storage layout itself encodes the bound) — not a graded modal
// value.  No Lattice operation applies.  Joins ConstantTime, Pinned,
// Machine, Bits, Borrowed/BorrowedRef as a deliberately-not-graded
// structural wrapper per CLAUDE.md §XVI.
//
// ── References ──────────────────────────────────────────────────────
//
//   CLAUDE.md §II        — 8 axioms
//   CLAUDE.md §XVI       — safety wrapper catalog (structural family)
//   CLAUDE.md §XVIII HS14 — neg-compile fixture requirement (≥2)
//   safety/Refined.h     — bounded_above<N> predicate

#include <crucible/Platform.h>
#include <crucible/safety/Refined.h>

#include <algorithm>
#include <array>           // for is_same_v with std::array in self-test
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── FixedArray<T, N> ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T, std::size_t N>
    requires (N > 0)
class [[nodiscard]] FixedArray {
public:
    using element_type    = T;
    using value_type      = T;
    using size_type       = std::size_t;
    using reference       = T&;
    using const_reference = T const&;
    using pointer         = T*;
    using const_pointer   = T const*;
    using iterator        = T*;
    using const_iterator  = T const*;

    static constexpr size_type capacity = N;

    static constexpr std::string_view wrapper_kind() noexcept {
        return "structural::FixedArray";
    }

    // The Refined-typed index proof-token.  bounded_above<N-1> means
    // "x ≤ N-1", i.e., index ∈ [0, N-1] — exactly the valid range.
    // Use lowercase `bounded_above` (constexpr instance), NOT
    // BoundedAbove<...> (the type) — Refined takes auto-NTTP value,
    // not type.
    using index_type = Refined<bounded_above<N - 1>, size_type>;

private:
    T data_[N]{};   // ── NSDMI: every element = T{} (load-bearing) ──

public:
    // ── Construction ────────────────────────────────────────────────

    constexpr FixedArray() noexcept(
        std::is_nothrow_default_constructible_v<T>) = default;

    // In-place construction from EXACTLY N elements.  Compile error
    // (concept rejection) if sizeof...(Args) != N — partial-fill is
    // not silently admissible.  std::in_place_t tag disambiguates
    // from copy-init when N == 1 (FixedArray<int, 1>(42) would be
    // ambiguous without it).
    template <typename... Args>
        requires (sizeof...(Args) == N) &&
                 (std::convertible_to<Args, T> && ...)
    constexpr explicit FixedArray(std::in_place_t, Args&&... args)
        noexcept((std::is_nothrow_constructible_v<T, Args> && ...))
        : data_{static_cast<T>(std::forward<Args>(args))...} {}

    // Defaulted copy/move/dtor.
    constexpr FixedArray(FixedArray const&)            = default;
    constexpr FixedArray(FixedArray&&)                 = default;
    constexpr FixedArray& operator=(FixedArray const&) = default;
    constexpr FixedArray& operator=(FixedArray&&)      = default;
    ~FixedArray()                                      = default;

    // ── Static factories ────────────────────────────────────────────

    // Build a FixedArray with every element = v.  Cheaper to write
    // than repeating v N times in the in_place ctor when N is large
    // or T is expensive to construct individually.
    [[nodiscard]] static constexpr FixedArray fill_with(T const& v)
        noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        FixedArray result{};
        for (auto& e : result.data_) e = v;
        return result;
    }

    // ── UB-bounded access ───────────────────────────────────────────
    //
    // No `pre (i < N)` clause — gotcha-6b (constexpr-pre-from-consteval
    // blows up).  Bound documented; std::span's UB-on-out-of-range +
    // libstdc++ -D_GLIBCXX_DEBUG catch at runtime.  Callers wanting
    // compile-checked bounds use at(index_type) instead.
    [[nodiscard]] constexpr reference operator[](size_type i) noexcept {
        return data_[i];
    }
    [[nodiscard]] constexpr const_reference operator[](size_type i) const noexcept {
        return data_[i];
    }

    // ── Refined-typed access ────────────────────────────────────────
    //
    // The index_type carries a compile-time-checked proof: constructing
    // `index_type{i}` runs the bounded_above<N-1> predicate (under
    // enforce semantic) at the construction site.  Subsequent at(idx)
    // calls TRUST the proof and skip the redundant check.  Hot-path
    // pattern: pay the bounds check once, prove it via Refined, reuse.
    [[nodiscard]] constexpr reference at(index_type i) noexcept {
        return data_[i.value()];
    }
    [[nodiscard]] constexpr const_reference at(index_type i) const noexcept {
        return data_[i.value()];
    }

    // ── Compile-time-bounded access (third tier) ───────────────────
    //
    // `arr.at<I>()` for compile-time-known I — the requires-clause
    // (I < N) makes out-of-range a compile error, no runtime cost,
    // no proof token needed.  Mirrors std::get<I>(arr) for std::array
    // but as a member template (clearer at the call site).
    template <size_type I>
        requires (I < N)
    [[nodiscard]] constexpr reference at() noexcept {
        return data_[I];
    }
    template <size_type I>
        requires (I < N)
    [[nodiscard]] constexpr const_reference at() const noexcept {
        return data_[I];
    }

    // ── Front / back (always valid since N > 0 enforced) ───────────
    [[nodiscard]] constexpr reference       front() noexcept       { return data_[0]; }
    [[nodiscard]] constexpr const_reference front() const noexcept { return data_[0]; }
    [[nodiscard]] constexpr reference       back() noexcept        { return data_[N - 1]; }
    [[nodiscard]] constexpr const_reference back() const noexcept  { return data_[N - 1]; }

    // ── Iteration / data ───────────────────────────────────────────
    [[nodiscard]] constexpr pointer       data() noexcept       { return data_; }
    [[nodiscard]] constexpr const_pointer data() const noexcept { return data_; }
    [[nodiscard]] constexpr iterator       begin() noexcept       { return data_; }
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return data_; }
    [[nodiscard]] constexpr iterator       end() noexcept         { return data_ + N; }
    [[nodiscard]] constexpr const_iterator end() const noexcept   { return data_ + N; }

    [[nodiscard]] constexpr size_type size()  const noexcept { return N; }
    [[nodiscard]] constexpr bool      empty() const noexcept { return false; }

    // ── Fixed-extent span escape ──────────────────────────────────
    //
    // Returns std::span<T, N> with COMPILE-TIME extent — consumers
    // accepting std::span<T, N> get the bound checked at overload
    // resolution, not at runtime.  Distinct from dynamic-extent
    // std::span<T> which loses the size info.
    [[nodiscard]] constexpr std::span<T, N> as_span() noexcept {
        return std::span<T, N>{data_};
    }
    [[nodiscard]] constexpr std::span<const T, N> as_span() const noexcept {
        return std::span<const T, N>{data_};
    }

    // ── Mutation ────────────────────────────────────────────────────
    // Mutator — assign v to every element.  Matches std::array::fill
    // naming convention (FixedArray and std::array are distinct types,
    // but the API surface intentionally matches where it makes sense).
    constexpr void fill(T const& v) noexcept(
        std::is_nothrow_copy_assignable_v<T>)
    {
        for (auto& e : data_) e = v;
    }

    constexpr void swap(FixedArray& other) noexcept(
        std::is_nothrow_swappable_v<T>)
    {
        for (size_type i = 0; i < N; ++i) {
            using std::swap;
            swap(data_[i], other.data_[i]);
        }
    }

    friend constexpr void swap(FixedArray& a, FixedArray& b) noexcept(
        std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── Equality (element-wise) ────────────────────────────────────
    [[nodiscard]] friend constexpr bool operator==(
        FixedArray const& a, FixedArray const& b) noexcept(
        noexcept(a.data_[0] == b.data_[0]))
        requires std::equality_comparable<T>
    {
        for (size_type i = 0; i < N; ++i) {
            if (!(a.data_[i] == b.data_[i])) return false;
        }
        return true;
    }

    // ── Lexicographic ordering (element-wise <=>) ──────────────────
    //
    // Only present when T is three-way comparable.  Useful for sorting
    // containers of FixedArray (e.g., std::set<FixedArray<...>>).
    // Element-wise lexicographic — first non-equal pair decides.
    [[nodiscard]] friend constexpr auto operator<=>(
        FixedArray const& a, FixedArray const& b) noexcept(
        noexcept(a.data_[0] <=> b.data_[0]))
        requires std::three_way_comparable<T>
    {
        using ordering = std::compare_three_way_result_t<T>;
        for (size_type i = 0; i < N; ++i) {
            // std::is_neq avoids the GCC `-Werror=zero-as-null-pointer-
            // constant` trap that triggers on `cmp != 0` (where 0 is
            // interpreted as a null pointer constant against the
            // ordering type).
            if (auto cmp = a.data_[i] <=> b.data_[i]; std::is_neq(cmp)) {
                return cmp;
            }
        }
        return ordering::equivalent;
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── Layout invariants ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

static_assert(sizeof(FixedArray<int,    1>)    == sizeof(int[1]));
static_assert(sizeof(FixedArray<int,    8>)    == sizeof(int[8]));
static_assert(sizeof(FixedArray<int,    64>)   == sizeof(int[64]));
static_assert(sizeof(FixedArray<double, 8>)    == sizeof(double[8]));
static_assert(sizeof(FixedArray<int64_t, 8>)   == sizeof(int64_t[8]));
static_assert(sizeof(FixedArray<char,   16>)   == sizeof(char[16]));

static_assert(std::is_trivially_copyable_v<FixedArray<int, 8>>);
static_assert(std::is_trivially_destructible_v<FixedArray<int, 8>>);
static_assert(std::is_standard_layout_v<FixedArray<int, 8>>);

// alignof preserved — `alignas(64) FixedArray<int64_t, 8>` aligns the
// wrapping struct to 64, hence data_ to offset 0 of that struct = 64
// (matches `alignas(64) int64_t[8]` discipline).
static_assert(alignof(FixedArray<int, 8>)     == alignof(int[8]));
static_assert(alignof(FixedArray<int64_t, 8>) == alignof(int64_t[8]));
static_assert(alignof(FixedArray<double, 8>)  == alignof(double[8]));

// Ranges-protocol conformance — FixedArray models contiguous_range
// (begin/end return T*, which IS the canonical contiguous iterator).
// Enables seamless integration with std::ranges algorithms.
static_assert(std::ranges::contiguous_range<FixedArray<int, 8>>);
static_assert(std::ranges::sized_range<FixedArray<int, 8>>);

// Distinct type identity vs std::array (load-bearing).
static_assert(!std::is_same_v<FixedArray<int, 8>, std::array<int, 8>>,
    "FixedArray<T, N> and std::array<T, N> MUST be distinct types — "
    "this is the load-bearing type-identity claim that prevents "
    "accidental swap of bounded-and-init-safe FixedArray with "
    "uninitialized-by-default std::array.");

// FixedArray<T, 0> is rejected at the (N > 0) concept gate.  We do
// NOT static_assert this here because the rejection is so strong
// that even *naming* `FixedArray<int, 0>` inside a concept's
// requires-expression hard-errors (it's not a SFINAE-friendly soft
// failure — the requires-clause prevents the class type from
// forming).  The neg_fixed_array_zero_sized HS14 fixture is the
// proper witness; it expects exactly this hard error.

// ═════════════════════════════════════════════════════════════════════
// ── Self-test ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::fixed_array_self_test {

using FA8 = FixedArray<int, 8>;

// Default ctor zero-initializes (NSDMI) — load-bearing.
[[nodiscard]] consteval bool default_zeroes() noexcept {
    FA8 a{};
    for (std::size_t i = 0; i < 8; ++i) {
        if (a[i] != 0) return false;
    }
    return a.size() == 8 && !a.empty() && a.capacity == 8;
}
static_assert(default_zeroes());

// in_place ctor with EXACTLY N args.
[[nodiscard]] consteval bool in_place_ctor() noexcept {
    FA8 a{std::in_place, 10, 20, 30, 40, 50, 60, 70, 80};
    return a[0] == 10 && a[7] == 80 && a.size() == 8;
}
static_assert(in_place_ctor());

// fill_with factory.
[[nodiscard]] consteval bool fill_factory() noexcept {
    FA8 a = FA8::fill_with(7);
    for (auto v : a) {
        if (v != 7) return false;
    }
    return true;
}
static_assert(fill_factory());

// fill mutation (matches std::array::fill convention).
[[nodiscard]] consteval bool fill_mutates() noexcept {
    FA8 a{};
    a.fill(99);
    for (std::size_t i = 0; i < 8; ++i) {
        if (a[i] != 99) return false;
    }
    return true;
}
static_assert(fill_mutates());

// Iteration via begin/end and range-for.
[[nodiscard]] consteval bool iteration_works() noexcept {
    FA8 a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    int sum = 0;
    for (auto v : a) sum += v;
    return sum == 36;
}
static_assert(iteration_works());

// front / back accessors (always valid since N > 0).
[[nodiscard]] consteval bool front_back_works() noexcept {
    FA8 a{std::in_place, 100, 0, 0, 0, 0, 0, 0, 200};
    return a.front() == 100 && a.back() == 200;
}
static_assert(front_back_works());

// Equality element-wise (requires equality_comparable<T>).
[[nodiscard]] consteval bool equality_works() noexcept {
    FA8 a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 b{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 c{std::in_place, 1, 2, 3, 4, 5, 6, 7, 9};   // last differs
    return (a == b) && !(a == c);
}
static_assert(equality_works());

// Refined-typed at() proof-token access.
[[nodiscard]] consteval bool refined_at_works() noexcept {
    FA8 a{std::in_place, 10, 20, 30, 40, 50, 60, 70, 80};
    using Idx = FA8::index_type;
    Idx i3{std::size_t{3}};
    Idx i7{std::size_t{7}};
    return a.at(i3) == 40 && a.at(i7) == 80;
}
static_assert(refined_at_works());

// Compile-time-bounded at<I>() — the third bounds-check tier.
[[nodiscard]] consteval bool compile_time_at_works() noexcept {
    FA8 a{std::in_place, 10, 20, 30, 40, 50, 60, 70, 80};
    return a.at<0>() == 10 && a.at<3>() == 40 && a.at<7>() == 80;
}
static_assert(compile_time_at_works());

// at<I>() out-of-range REJECTED at compile time (load-bearing — the
// requires (I < N) makes OOB ill-formed, not UB).
template <class FA, std::size_t I>
concept can_compile_at = requires(FA a) { { a.template at<I>() }; };
static_assert( can_compile_at<FA8, 0>);
static_assert( can_compile_at<FA8, 7>);
static_assert(!can_compile_at<FA8, 8>,
    "at<8>() on FA8 (N=8, valid indices 0..7) MUST be ill-formed.  "
    "If this fires, the (I < N) requires-clause has regressed and "
    "compile-time bounds-checked access can produce OOB.");
static_assert(!can_compile_at<FA8, 99>);

// Lexicographic ordering (operator<=>) — element-wise comparison.
[[nodiscard]] consteval bool ordering_works() noexcept {
    FA8 a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 b{std::in_place, 1, 2, 3, 4, 5, 6, 7, 9};   // > a (last element)
    FA8 c{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};   // == a
    FA8 d{std::in_place, 0, 9, 9, 9, 9, 9, 9, 9};   // < a (first element)
    // Use std::is_lt / is_eq / is_gt rather than `cmp < 0` / `cmp == 0`
    // to avoid the GCC -Werror=zero-as-null-pointer-constant trap.
    return std::is_lt(a <=> b) &&
           std::is_eq(a <=> c) &&
           std::is_gt(a <=> d) &&
           std::is_gt(b <=> a);
}
static_assert(ordering_works());

// as_span returns COMPILE-TIME-FIXED extent (load-bearing).
[[nodiscard]] consteval bool span_escape_fixed_extent() noexcept {
    FA8 a{std::in_place, 1, 1, 1, 1, 1, 1, 1, 1};
    auto s = a.as_span();
    static_assert(decltype(s)::extent == 8,
        "as_span() MUST return std::span<T, 8> with compile-time "
        "extent.  If this fires, the static extent is lost and "
        "consumers can no longer overload-resolve on bound size.");
    int sum = 0;
    for (auto v : s) sum += v;
    return sum == 8;
}
static_assert(span_escape_fixed_extent());

// swap exchanges contents.
[[nodiscard]] consteval bool swap_exchanges() noexcept {
    FA8 a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 b{std::in_place, 11, 12, 13, 14, 15, 16, 17, 18};
    a.swap(b);
    return a[0] == 11 && b[0] == 1 && a[7] == 18 && b[7] == 8;
}
static_assert(swap_exchanges());

// Type-system rejection: in_place ctor with WRONG arg count fails.
template <class FA, class... Args>
concept can_in_place_construct = requires {
    FA{std::in_place, std::declval<Args>()...};
};

static_assert( can_in_place_construct<FA8, int, int, int, int, int, int, int, int>);
static_assert(!can_in_place_construct<FA8, int, int, int>,
    "FixedArray<T, 8>(in_place, 3 args) MUST fail.  Without this "
    "rejection, partially-filled FixedArrays slip through with "
    "uninitialized tail (defeating the InitSafe NSDMI guarantee).");
static_assert(!can_in_place_construct<FA8,
    int, int, int, int, int, int, int, int, int>,
    "FixedArray<T, 8>(in_place, 9 args) MUST fail — too many args.");

// Wrapper-kind diagnostic.
static_assert(FA8::wrapper_kind() == "structural::FixedArray");

// FixedArray DEFAULT ctor is INVOKABLE without args (load-bearing —
// NSDMI is the differentiator vs std::array's aggregate-init semantics).
static_assert(std::is_default_constructible_v<FA8>);
static_assert(std::is_nothrow_default_constructible_v<FA8>);

// ── Runtime smoke test ──────────────────────────────────────────────

inline void runtime_smoke_test() {
    // Default zero-init (NSDMI).
    FA8 a{};
    for (std::size_t i = 0; i < 8; ++i) {
        if (a[i] != 0) std::abort();
    }

    // in_place + iteration.
    FA8 b{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    int sum = 0;
    for (auto v : b) sum += v;
    if (sum != 36) std::abort();

    // fill_with factory.
    auto c = FA8::fill_with(99);
    if (c.front() != 99 || c.back() != 99) std::abort();

    // fill mutation (matches std::array::fill).
    a.fill(7);
    for (std::size_t i = 0; i < 8; ++i) {
        if (a[i] != 7) std::abort();
    }

    // Refined-typed at() — proof token construction + .at() access.
    using Idx = FA8::index_type;
    Idx i5{std::size_t{5}};
    if (b.at(i5) != 6) std::abort();

    // Compile-time-bounded at<I>() at runtime.
    if (b.at<0>() != 1) std::abort();
    if (b.at<7>() != 8) std::abort();

    // Lexicographic ordering at runtime.  Use std::is_lt / is_gt rather
    // than `cmp < 0` / `cmp > 0` to avoid the GCC
    // -Werror=zero-as-null-pointer-constant trap.
    FA8 ord_a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 ord_b{std::in_place, 1, 2, 3, 4, 5, 6, 7, 9};
    if (!std::is_lt(ord_a <=> ord_b)) std::abort();
    if (!std::is_gt(ord_b <=> ord_a)) std::abort();

    // as_span preserves data + extent.
    auto s = b.as_span();
    if (s.size() != 8) std::abort();
    int sspan = 0;
    for (auto v : s) sspan += v;
    if (sspan != 36) std::abort();

    // Equality.
    FA8 eq_a = FA8::fill_with(5);
    FA8 eq_b = FA8::fill_with(5);
    if (!(eq_a == eq_b)) std::abort();
    eq_b.fill(4);
    if (eq_a == eq_b) std::abort();

    // swap exchanges contents.
    FA8 sw_a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 sw_b = FA8::fill_with(0);
    sw_a.swap(sw_b);
    if (sw_b.front() != 1 || sw_a.front() != 0) std::abort();

    // Different-N FixedArrays.
    FixedArray<int, 4> small{};
    if (small.size() != 4) std::abort();

    // alignas propagates: this aligns the whole FixedArray to 64,
    // hence data_ to offset 0 = 64-byte aligned.  Matches the
    // SIMD-aligned StorageNbytes case (#1019).
    alignas(64) FixedArray<int64_t, 8> aligned_buf{};
    if (reinterpret_cast<std::uintptr_t>(aligned_buf.data()) % 64 != 0) {
        std::abort();
    }

    // Default ctor on a different T also zero-inits.
    FixedArray<int64_t, 4> z{};
    for (std::size_t i = 0; i < 4; ++i) {
        if (z[i] != 0) std::abort();
    }
}

}  // namespace detail::fixed_array_self_test

}  // namespace crucible::safety
