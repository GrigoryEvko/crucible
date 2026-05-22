#pragma once

// ── crucible::safety::ThreadLocalRef<Tag, T> ───────────────────────
//
// Phantom-tagged thread-local accessor.  Wraps a `thread_local`
// storage cell in a Tag-distinguished function-template so that two
// call sites with distinct logical slots cannot silently share
// storage via inline-static or template-instantiation merging.
//
//   Substrate: stateless handle; storage lives in a function-local
//              `thread_local T storage{}` inside a static helper
//              parameterized by (Tag, T).  Two call sites that name
//              the SAME (Tag, T) share storage (intentional — same
//              logical slot); two call sites that name DIFFERENT
//              Tags get distinct cells per-thread.
//   Regime:    structural — NOT a Graded wrapper.  ThreadLocalRef
//              encodes a STORAGE-IDENTITY discipline, not a graded-
//              modality property; per CLAUDE.md §XVI it joins the
//              structural-wrapper family alongside Pinned, Machine,
//              ScopedView, OwnedRegion, Workload, ConstantTime,
//              NotInherited/FinalBy.
//
//   Use case (V-208 production wiring):
//     HdrHistogram::thread_shard previously stored a `thread_local
//     const std::size_t shard = next_thread_shard_.fetch_add(...)`
//     inside a static member function.  All HdrHistogram instances
//     sharing the same template parameters collapsed onto the same
//     thread-local shard-index cell — thread T's writes always
//     landed on shard k across EVERY histogram with matching
//     parameters.  Wrapping the storage with a per-instance Tag
//     (typically a local struct declared next to the field) forces
//     each logical histogram to its own per-thread cell.
//
//   Axiom coverage:
//     TypeSafe — Tag distinguishes otherwise-identical
//                instantiations.  Two Tags = two types = two
//                distinct ThreadLocalRef classes = two distinct
//                static helper templates = two distinct storage
//                cells per thread.  Use-site discipline: pick a
//                unique Tag per logical slot.
//     DetSafe — operations route through stable static thread_local
//                storage.  Same thread, same Tag → same cell, same
//                value (within the thread's history).  Cross-thread:
//                each thread has its own cell, observation order
//                is per-thread.
//     MemSafe — storage lifetime is the thread's lifetime; freed
//                automatically at thread exit per C++ thread_local
//                rules.  No new/delete on hot path.
//     InitSafe — `thread_local T storage{}` is value-initialized
//                on first access per thread; the requires-clause
//                `std::is_default_constructible_v<T>` ensures the
//                init path is well-formed.
//     ThreadSafe — thread_local IS the thread-safety primitive;
//                no synchronization needed for per-thread access.
//                Cross-thread observation requires the user to
//                pump values through a separate synchronized
//                channel (e.g. an aggregation step).
//     LeakSafe — automatic cleanup at thread exit.
//   Runtime cost:
//     sizeof(ThreadLocalRef<Tag, T>) == 1 (empty handle, single-
//     byte minimum); zero data members; all ops route through the
//     static helper.  `get_or_storage` access is one indirection
//     through the thread_local resolution (typically one TLS-slot
//     lookup ~1-3 ns).
//
// ── Why STRUCTURAL, not Graded ──────────────────────────────────────
//
// ThreadLocalRef doesn't lift T into a lattice-ordered universe —
// the storage location IS the discipline.  There's no "stronger"
// vs "weaker" thread-local storage tier; either you have the
// distinct-per-Tag guarantee or you don't.  Per CLAUDE.md §XVI the
// structural-wrapper discipline applies: no row_hash specialization
// (no federation cache key — storage is per-thread per-Tag, not a
// composable value), no DimensionTraits axis assignment (no
// graded-modality dimension to occupy), no canonical wrapper-
// nesting position.
//
// ── §XXI Universal Mint factory ─────────────────────────────────────
//
// `mint_thread_local_ref<Tag, T>()` is a token mint (no Ctx).  Per
// §XXI: every authorization factory is named `mint_<noun>` so
// `grep "mint_thread_local_ref"` finds every site that opts into
// the Tag-distinguished storage discipline.  Constructing
// `ThreadLocalRef<Tag, T>{}` directly is functionally equivalent
// — both gate on `std::is_default_constructible_v<T>` — the mint
// exists for grep-discoverability AND because tag-handle mints are
// the SOUND moment where the user must have already chosen a
// distinct Tag (the substrate cannot verify Tag uniqueness; the
// discipline is at the use site, marked by the mint call).
//
// HS14 gate: two HS14 neg-compile fixtures at test/safety_neg/
// witness the mint's requires-clause fires across distinct
// mismatch classes:
//   1. T-not-default-constructible — neg_thread_local_ref_mint_
//      requires_default_ctor.cpp witnesses the class-level
//      `is_default_constructible_v<T>` rejection.
//   2. store-assignment-mismatch — neg_thread_local_ref_store_
//      wrong_type.cpp witnesses the `store<U>()` requires-clause
//      rejecting a U that's not assignable to T.

#include <crucible/Platform.h>

#include <bit>            // std::bit_cast for runtime smoke FP compare
#include <cstdint>        // std::uint64_t in runtime smoke FP compare
#include <cstdlib>        // std::abort in runtime smoke
#include <meta>           // std::meta::display_string_of for diag names
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename Tag, typename T>
    requires std::is_default_constructible_v<T>
class [[nodiscard]] ThreadLocalRef {
public:
    using tag_type   = Tag;
    using value_type = T;

private:
    // Storage helper — function-local thread_local cell, distinct per
    // (Tag, T) instantiation.  Two call sites naming the SAME (Tag, T)
    // share this cell (intentional — same logical slot); two call
    // sites naming DIFFERENT Tags get distinct cells.
    //
    // The `thread_local T storage{}` is value-initialized on first
    // access per thread; for trivially-zero T (int{} == 0, double{} ==
    // 0.0) this is zero-init; for non-trivial T, the default ctor
    // runs per-thread-on-first-access per C++ thread_local rules.
    [[nodiscard]] static T& storage_() noexcept(
        std::is_nothrow_default_constructible_v<T>)
    {
        thread_local T storage{};
        return storage;
    }

public:
    // ── Handle construction ─────────────────────────────────────────
    //
    // Default ctor — stateless handle.  The storage proxy is the
    // static helper; this constructor doesn't materialize a cell
    // (that happens on first `peek` / `peek_mut` / `store` / `reset`).
    constexpr ThreadLocalRef() noexcept = default;

    // Defaulted copy/move — the handle is empty; all instances of
    // `ThreadLocalRef<Tag, T>` are interchangeable references to the
    // SAME per-thread storage cell.  Copying a handle does NOT
    // duplicate storage.
    constexpr ThreadLocalRef(ThreadLocalRef const&) noexcept            = default;
    constexpr ThreadLocalRef(ThreadLocalRef&&) noexcept                 = default;
    constexpr ThreadLocalRef& operator=(ThreadLocalRef const&) noexcept = default;
    constexpr ThreadLocalRef& operator=(ThreadLocalRef&&) noexcept      = default;
    ~ThreadLocalRef()                                                   = default;

    // ── Read-only access ────────────────────────────────────────────
    [[nodiscard]] T const& peek() const noexcept(
        std::is_nothrow_default_constructible_v<T>)
    {
        return storage_();
    }

    // ── Mutable access ──────────────────────────────────────────────
    //
    // Note: `peek_mut` is `const` on the handle because the handle
    // has no instance state.  The mutation goes through the static
    // helper to the per-thread cell, NOT through any state owned
    // by *this.  `const ThreadLocalRef<Tag, T> h; h.peek_mut() = v;`
    // is correct — the handle is const, the per-thread cell is not.
    [[nodiscard]] T& peek_mut() const noexcept(
        std::is_nothrow_default_constructible_v<T>)
    {
        return storage_();
    }

    // ── Store-by-value ──────────────────────────────────────────────
    //
    // Constrained by `std::is_assignable_v<T&, U&&>` — the supplied U
    // must be assignable to a T lvalue.  Catches accidental
    // store-of-wrong-type at the mint boundary.  HS14 fixture #2
    // pins the rejection.
    template <typename U>
        requires std::is_assignable_v<T&, U&&>
    void store(U&& v) const noexcept(
        std::is_nothrow_default_constructible_v<T>
        && std::is_nothrow_assignable_v<T&, U&&>)
    {
        storage_() = std::forward<U>(v);
    }

    // ── Reset to default-constructed state ──────────────────────────
    //
    // Typical end-of-iteration discipline — clear the per-thread
    // accumulator back to its default-init.  For HdrHistogram-style
    // sites this is the per-period reset.
    void reset() const noexcept(
        std::is_nothrow_default_constructible_v<T>
        && std::is_nothrow_move_assignable_v<T>)
    {
        storage_() = T{};
    }

    // ── Diagnostic names (P2996 reflection) ─────────────────────────
    //
    // Surface the Tag and T display strings for runtime observer's
    // debug output; production code typically reads tag_type /
    // value_type aliases instead.
    [[nodiscard]] static consteval std::string_view tag_name() noexcept {
        return std::meta::display_string_of(^^Tag);
    }
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return std::meta::display_string_of(^^T);
    }
};

// ── §XXI Universal Mint factory ─────────────────────────────────────
//
// Token mint (no Ctx) per §XXI table.  ThreadLocalRef is type-tagged
// by (Tag, T); the mint synthesizes the handle, the requires-clause
// gates on `std::is_default_constructible_v<T>` — the load-bearing
// soundness check that the per-thread storage cell can be value-
// initialized on first access.
template <typename Tag, typename T>
    requires std::is_default_constructible_v<T>
[[nodiscard]] constexpr ThreadLocalRef<Tag, T> mint_thread_local_ref() noexcept {
    return ThreadLocalRef<Tag, T>{};
}

// ── Layout invariant ─────────────────────────────────────────────────
//
// Handle is stateless — the substrate cell lives in the static
// helper, the handle itself is an empty class (C++ guarantees
// sizeof(empty class) >= 1).  This is the STRUCTURAL-wrapper
// signature: minimal handle, all state delegated to a discipline-
// enforcing site.
namespace detail { struct LayoutAnchorTag {}; }
static_assert(sizeof(ThreadLocalRef<detail::LayoutAnchorTag, int>)    == 1);
static_assert(sizeof(ThreadLocalRef<detail::LayoutAnchorTag, double>) == 1);
static_assert(std::is_empty_v<ThreadLocalRef<detail::LayoutAnchorTag, int>>);
static_assert(std::is_trivially_copyable_v<ThreadLocalRef<detail::LayoutAnchorTag, int>>);

// ── Self-test ────────────────────────────────────────────────────────
namespace detail::thread_local_ref_self_test {

struct CounterTag {};
struct AccumulatorTag {};
struct OtherTag {};

using IntCounter      = ThreadLocalRef<CounterTag,     int>;
using IntAccumulator  = ThreadLocalRef<AccumulatorTag, int>;
using DoubleOther     = ThreadLocalRef<OtherTag,       double>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr IntCounter c_default{};
inline constexpr IntCounter c_copy = c_default;
static_assert(sizeof(IntCounter) == 1);

// ── Type aliases ───────────────────────────────────────────────────
static_assert(std::is_same_v<IntCounter::tag_type,   CounterTag>);
static_assert(std::is_same_v<IntCounter::value_type, int>);
static_assert(std::is_same_v<DoubleOther::tag_type,  OtherTag>);
static_assert(std::is_same_v<DoubleOther::value_type, double>);

// ── Distinct Tags → distinct types ─────────────────────────────────
static_assert(!std::is_same_v<IntCounter, IntAccumulator>);
static_assert(!std::is_same_v<IntCounter, DoubleOther>);

// ── Copyability — handle is trivially copyable, no state ───────────
static_assert(std::is_copy_constructible_v<IntCounter>);
static_assert(std::is_copy_assignable_v<IntCounter>);
static_assert(std::is_move_constructible_v<IntCounter>);
static_assert(std::is_move_assignable_v<IntCounter>);
static_assert(std::is_trivially_copyable_v<IntCounter>);

// ── §XXI mint factory ──────────────────────────────────────────────
inline constexpr auto minted_counter = mint_thread_local_ref<CounterTag, int>();
static_assert(std::is_same_v<decltype(minted_counter), const IntCounter>);

// ── Diagnostic names — reflection-derived ─────────────────────────
static_assert(IntCounter::value_type_name() == "int");
static_assert(DoubleOther::value_type_name() == "double");

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// the wrapper's ops with non-constant arguments at runtime.  Catches
// consteval-vs-constexpr traps the static_assert tests miss; per
// feedback_header_only_static_assert_blind_spot memory, the sentinel
// TU in test/ forces the bodies through under project warnings-as-
// errors.
//
// CRITICAL: thread_local storage is per-thread.  This smoke runs on
// ONE thread (the test runner), so we exercise the per-thread cell
// lifecycle: first access materializes (default-init to 0), store
// updates, reset returns to 0, distinct-Tag witnesses get distinct
// cells.
inline void runtime_smoke_test() {
    int seed = 17;                                          // non-constant

    IntCounter c{};
    // First access — default-init to 0.
    if (c.peek() != 0) std::abort();

    // store — writes to per-thread cell.
    c.store(seed * 2);
    if (c.peek() != 34) std::abort();

    // peek_mut — direct lvalue mutation.
    c.peek_mut() = seed * 3;
    if (c.peek() != 51) std::abort();

    // reset — back to default-init.
    c.reset();
    if (c.peek() != 0) std::abort();

    // Distinct-Tag check — IntCounter and IntAccumulator share T=int
    // but distinct Tags → distinct cells.  Writing to one must not
    // change the other.
    IntAccumulator a{};
    c.store(100);
    a.store(200);
    if (c.peek() != 100) std::abort();
    if (a.peek() != 200) std::abort();

    // Same-Tag handle aliasing — two handles with the same (Tag, T)
    // share the SAME cell.  Writing through h1 is visible through h2.
    IntCounter c1{};
    IntCounter c2{};
    c1.store(seed);
    if (c2.peek() != 17) std::abort();    // shared cell — same Tag

    // mint factory path.
    auto m = mint_thread_local_ref<CounterTag, int>();
    if (m.peek() != 17) std::abort();     // mint returns handle to SAME cell

    // Distinct T witness — DoubleOther cell is double, separate
    // template instantiation entirely.  Bit-exact compare via bit_cast
    // (project's -Werror=float-equal bans `!=` on floating-point).
    DoubleOther d{};
    d.store(2.5);
    {
        auto observed = std::bit_cast<std::uint64_t>(d.peek());
        auto expected = std::bit_cast<std::uint64_t>(2.5);
        if (observed != expected) std::abort();
    }

    // Reset everything we touched so a parallel test runner doesn't
    // see stale state.
    c.reset();
    c1.reset();
    a.reset();
    d.reset();
}

}  // namespace detail::thread_local_ref_self_test

}  // namespace crucible::safety
