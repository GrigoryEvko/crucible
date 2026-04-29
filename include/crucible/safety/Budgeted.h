#pragma once

// ── crucible::safety::Budgeted<T> ───────────────────────────────────
//
// Per-instance resource-budget wrapper.  A value of type T paired
// with TWO independent resource grades — bits transferred + peak
// bytes resident — composed via the binary product lattice:
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     ProductLattice<BitsBudgetLattice, PeakBytesLattice>,
//                     T>
//   Regime:    4 (per-instance grade with TWO non-empty fields,
//                 16 bytes of grade carried per instance — the
//                 first PRODUCT-LATTICE wrapper to ship)
//
// Citation: Resource-bounded type theory (arXiv:2512.06952);
// 25_04_2026.md §2.4 Budgeted primitive; 28_04_2026_effects.md
// §4.4.1 (FOUND-G63/G64).
//
// THE LOAD-BEARING USE CASE: Forge Phase D / Phase E precision-
// budget calibrator + Canopy collective bandwidth bookkeeping.
// A `Budgeted<T>` value carries the actual measured bits-transferred
// and peak-bytes-resident for the path that produced T, so a
// downstream gate can refuse the value if either axis exceeds
// the gate's threshold.
//
// ── Two distinct composition operations ────────────────────────────
//
// Like Stale<T> (the staleness semiring sister), Budgeted has TWO
// natural composition semantics:
//
//   - LATTICE join (componentwise max): pessimistic worst-case fold
//                                       across two parallel paths.
//                                       "Across these two replicas,
//                                       the maximum-budget value
//                                       observed."  Used for fan-in
//                                       admission gates.
//
//   - SUM accumulation (saturating add): additive accumulation along
//                                       a chain.  "Stage A used N1
//                                       bits; stage B used N2; the
//                                       chain's footprint is N1+N2."
//                                       NOT a lattice op — exposed
//                                       as a SEPARATE method
//                                       `accumulate(other)` to keep
//                                       the lattice-vs-arithmetic
//                                       distinction visible at the
//                                       call site.
//
// Budgeted's wrapper exposes BOTH:
//   .combine_max(other)   — lattice join, pessimistic fan-in.
//   .accumulate(other)    — saturating add, chain accumulation.
//
//   Axiom coverage:
//     TypeSafe — BitsBudget and PeakBytes are strong-typed
//                uint64_t newtypes.  Mixing the axes (passing a
//                BitsBudget where PeakBytes is expected, or vice
//                versa) is a compile error — verified by neg-fixtures.
//     DetSafe — every operation is constexpr.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//   Runtime cost:
//     sizeof(Budgeted<T>) == sizeof(T) + 16 bytes (two uint64_t grades)
//     plus alignment padding.  Verified by static_assert below.  This
//     is REGIME-4 (per-instance grade, non-empty), distinct from the
//     ten chain wrappers' regime-1 EBO collapse.
//
// ── Why not move-only ───────────────────────────────────────────────
//
// Same rationale as Stale and TimeOrdered: the Absolute modality
// over a non-Linearity grade encodes a value's RESOURCE-CONSUMPTION
// POSITION (a property of identity, not ownership).  Two Budgeted
// events with identical (value, bits, peak) ARE the same event;
// copying represents replay, not duplication.
//
// ── No relax<>() — grade is RUNTIME, not type-pinned ────────────────
//
// Unlike chain wrappers (Crash<Class, T>, HotPath<Tier, T>, ...)
// where the grade is pinned at the TYPE level via a non-type
// template parameter, Budgeted's grade is RUNTIME data.  There is
// no `relax<>()` template — the analogue is `weaken_to(BitsBudget,
// PeakBytes)` which contract-checks pointwise leq at runtime via
// Graded's `weaken()` method.  A future maintainer wanting type-
// pinned budget bounds (a `BudgetedAt<bits_max, peak_max, T>`
// wrapper) should ship a SEPARATE wrapper next to this one — do
// NOT mutate Budgeted to add the type-pinning, because it would
// fork the API at every production call site that already accepts
// the runtime-grade form.
//
// See FOUND-G63 (algebra/lattices/{BitsBudgetLattice, PeakBytesLattice}.h)
// for the underlying lattices; FOUND-G64 (this file) for the wrapper;
// 28_04_2026_effects.md §4.4.1 for the production-call-site rationale;
// algebra/lattices/ProductLattice.h for the binary-composition
// substrate.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/BitsBudgetLattice.h>
#include <crucible/algebra/lattices/PeakBytesLattice.h>
#include <crucible/algebra/lattices/ProductLattice.h>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the two component types into safety:: under canonical names.
using ::crucible::algebra::lattices::BitsBudget;
using ::crucible::algebra::lattices::BitsBudgetLattice;
using ::crucible::algebra::lattices::PeakBytes;
using ::crucible::algebra::lattices::PeakBytesLattice;

// ── Saturating add helper (cstdint-only) ───────────────────────────
//
// Defined BEFORE the Budgeted class so that the class's accumulate()
// body can reference it.  Saturates at UINT64_MAX on overflow rather
// than wrapping — matches Crucible's std::add_sat / std::mul_sat
// discipline from CLAUDE.md §III TypeSafe axiom.
namespace detail {
[[nodiscard]] constexpr std::uint64_t sat_add(std::uint64_t a, std::uint64_t b) noexcept {
    std::uint64_t s = a + b;
    return s < a ? std::numeric_limits<std::uint64_t>::max() : s;
}
}  // namespace detail

template <typename T>
class [[nodiscard]] Budgeted {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = ::crucible::algebra::lattices::ProductLattice<
        BitsBudgetLattice, PeakBytesLattice>;
    using budget_t     = typename lattice_type::element_type;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

private:
    graded_type impl_;

    // Helper: pack the two axes into a budget_t product element.
    [[nodiscard]] static constexpr budget_t pack(BitsBudget bits, PeakBytes peak) noexcept {
        return budget_t{bits, peak};
    }

public:
    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at zero budget — the strongest possible claim
    // (no resources used).
    constexpr Budgeted() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, lattice_type::bottom()} {}

    // Explicit construction from value + both budget axes.  The
    // most common production pattern — a producer reports its
    // measured bits-transferred and peak-bytes-resident at the
    // construction site.
    constexpr Budgeted(T value, BitsBudget bits, PeakBytes peak)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), pack(bits, peak)} {}

    // In-place T construction with explicit budget pair.  Mirrors
    // Stale's std::in_place_t pattern.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr Budgeted(std::in_place_t, BitsBudget bits, PeakBytes peak, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), pack(bits, peak)} {}

    // Convenience factory: value at zero budget — the strongest claim
    // (producer used no resources).  Used when the budget is genuinely
    // zero (e.g. a constexpr arithmetic helper that lives entirely
    // in registers).
    [[nodiscard]] static constexpr Budgeted free(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Budgeted{std::move(value), BitsBudget{0}, PeakBytes{0}};
    }

    // Convenience factory: value at unbounded budget — the weakest
    // claim (saturated cap).  Used for values whose budget is
    // genuinely unknown / unbounded (e.g. opaque external producers).
    [[nodiscard]] static constexpr Budgeted unbounded(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Budgeted{std::move(value), BitsBudgetLattice::top(),
                        PeakBytesLattice::top()};
    }

    // Defaulted copy/move/destroy.
    constexpr Budgeted(const Budgeted&)            = default;
    constexpr Budgeted(Budgeted&&)                 = default;
    constexpr Budgeted& operator=(const Budgeted&) = default;
    constexpr Budgeted& operator=(Budgeted&&)      = default;
    ~Budgeted()                                    = default;

    // Equality: compares value bytes AND both budget axes within the
    // SAME (lattice, T) pair.  Both axes participate — two Budgeted
    // values with identical T but differing budgets are NOT equal.
    [[nodiscard]] friend constexpr bool operator==(
        Budgeted const& a, Budgeted const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek()
            && a.bits()       == b.bits()
            && a.peak_bytes() == b.peak_bytes();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only access ────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── Per-axis accessors ──────────────────────────────────────────
    [[nodiscard]] constexpr BitsBudget bits() const noexcept {
        return impl_.grade().first;
    }

    [[nodiscard]] constexpr PeakBytes peak_bytes() const noexcept {
        return impl_.grade().second;
    }

    [[nodiscard]] constexpr budget_t budget() const noexcept {
        return impl_.grade();
    }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(Budgeted& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Budgeted& a, Budgeted& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── combine_max — pointwise lattice JOIN (worst-case fold) ─────
    //
    // Returns a new Budgeted whose budget is the componentwise max
    // of the two inputs' budgets.  Used for fan-in admission gates
    // where the worst-case across two parallel paths must be tracked.
    //
    // VALUE provenance: takes the value from `*this` (the "left"
    // operand) by default.  For the case where the caller wants
    // the right-hand value, they should swap before calling, or
    // use combine_max_at(other, ...) — not provided here, callers
    // resort to manual construction.  Keeping the API minimal.
    [[nodiscard]] constexpr Budgeted combine_max(Budgeted const& other) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Budgeted{
            this->peek(),
            BitsBudgetLattice::join(this->bits(),       other.bits()),
            PeakBytesLattice::join(this->peak_bytes(), other.peak_bytes())
        };
    }

    [[nodiscard]] constexpr Budgeted combine_max(Budgeted const& other) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        BitsBudget joined_bits =
            BitsBudgetLattice::join(this->bits(),       other.bits());
        PeakBytes  joined_peak =
            PeakBytesLattice::join(this->peak_bytes(), other.peak_bytes());
        return Budgeted{std::move(impl_).consume(), joined_bits, joined_peak};
    }

    // ── accumulate — saturating-add chain accumulation ─────────────
    //
    // Returns a new Budgeted whose budget is the componentwise SUM
    // (saturating at UINT64_MAX) of the two inputs' budgets.  Used
    // for chain accumulation along a sequential pipeline:
    //
    //   Stage A produced output at {bits=1KB, peak=1MB};
    //   Stage B produced output at {bits=2KB, peak=4MB};
    //   Composed chain's footprint = {bits=3KB, peak=5MB}.
    //
    // SATURATION: if either axis would overflow uint64_t, clamp at
    // UINT64_MAX.  This is the canonical "saturating semantics" that
    // matches Crucible's std::add_sat / std::mul_sat discipline from
    // CLAUDE.md §III TypeSafe axiom.
    //
    // NOT a lattice operation — exposed as a separate method to
    // keep the lattice-vs-arithmetic distinction visible at the
    // call site (mirrors Stale's compose_add vs combine_max split).
    [[nodiscard]] constexpr Budgeted accumulate(Budgeted const& other) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Budgeted{this->peek(),
                        BitsBudget{detail::sat_add(this->bits().value,       other.bits().value)},
                        PeakBytes{detail::sat_add(this->peak_bytes().value, other.peak_bytes().value)}};
    }

    [[nodiscard]] constexpr Budgeted accumulate(Budgeted const& other) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        BitsBudget summed_bits{
            detail::sat_add(this->bits().value, other.bits().value)};
        PeakBytes  summed_peak{
            detail::sat_add(this->peak_bytes().value, other.peak_bytes().value)};
        return Budgeted{std::move(impl_).consume(), summed_bits, summed_peak};
    }

    // ── satisfies — runtime admission gate ─────────────────────────
    //
    // Returns true iff this Budgeted's budget is pointwise ≤ the
    // declared bound (i.e., the producer's footprint fits inside
    // the gate's threshold).  The natural production usage:
    //
    //   if (!result.satisfies(BitsBudget{8192}, PeakBytes{1<<20}))
    //       return reject_oversize_value();
    //
    // The two axes are independent — admission requires BOTH to fit.
    [[nodiscard]] constexpr bool satisfies(BitsBudget max_bits,
                                           PeakBytes max_peak) const noexcept
    {
        return BitsBudgetLattice::leq(this->bits(),       max_bits)
            && PeakBytesLattice::leq(this->peak_bytes(), max_peak);
    }
};

// ── Cross-axis disjointness — load-bearing for axis-swap fence ────
//
// Both BitsBudget and PeakBytes wrap uint64_t but are STRUCTURALLY
// DISJOINT C++ types.  This assertion catches a refactor that
// accidentally collapses them into a shared `ResourceCount` alias
// for "convenience" — every neg-fixture's compile error would
// dissolve, and downstream gates checking
//   `result.bits().value <= max_bits`
// would silently compare against the peak-bytes counter.  Lives
// at the wrapper layer because that's where both component
// newtypes are guaranteed in scope.
static_assert(!std::is_same_v<BitsBudget, PeakBytes>,
    "BitsBudget and PeakBytes must be structurally distinct C++ "
    "types even though both wrap uint64_t.  If this fires, the "
    "strong-newtype discipline that fences Budgeted axis-swap bugs "
    "has been broken.");

// ── Layout invariants — regime-4 (non-EBO) ──────────────────────────
//
// Budgeted is REGIME-4: per-instance grade with two non-empty
// uint64_t fields (16 bytes of grade) + the value.  Layout is:
//
//   sizeof(Budgeted<T>) == sizeof(graded_type)
//                       == sizeof(T) + 16 bytes + alignment padding
//
// The 16 bytes are the ProductLattice<BitsBudgetLattice,
// PeakBytesLattice>::element_type aggregate carrying the two
// uint64_t budget fields.  This is the FIRST product-lattice
// wrapper, so its layout invariant is the regime-4 reference.
namespace detail::budgeted_layout {

static_assert(sizeof(Budgeted<int>)       >= sizeof(int)    + 16);
static_assert(sizeof(Budgeted<double>)    >= sizeof(double) + 16);
static_assert(sizeof(Budgeted<char>)      >= sizeof(char)   + 16);

// Strict equality on T=uint64_t (alignment-friendly): exactly 24 bytes.
static_assert(sizeof(Budgeted<std::uint64_t>) == 24,
    "Budgeted<uint64_t>: expected 8(value) + 16(grade) = 24 bytes.  If "
    "this fires, the ProductLattice element_type drifted from its "
    "documented two-uint64_t layout — investigate before merging.");

}  // namespace detail::budgeted_layout

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::budgeted_self_test {

using BudgetedInt  = Budgeted<int>;
using BudgetedDbl  = Budgeted<double>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr BudgetedInt b_default{};
static_assert(b_default.peek()       == 0);
static_assert(b_default.bits()       == BitsBudget{0});
static_assert(b_default.peak_bytes() == PeakBytes{0});

inline constexpr BudgetedInt b_explicit{42, BitsBudget{1024}, PeakBytes{4096}};
static_assert(b_explicit.peek()       == 42);
static_assert(b_explicit.bits()       == BitsBudget{1024});
static_assert(b_explicit.peak_bytes() == PeakBytes{4096});

inline constexpr BudgetedInt b_in_place{
    std::in_place, BitsBudget{16}, PeakBytes{64}, 7};
static_assert(b_in_place.peek()       == 7);
static_assert(b_in_place.bits()       == BitsBudget{16});
static_assert(b_in_place.peak_bytes() == PeakBytes{64});

// ── Convenience factories ─────────────────────────────────────────
inline constexpr BudgetedInt b_free        = BudgetedInt::free(99);
static_assert(b_free.peek()                 == 99);
static_assert(b_free.bits()                 == BitsBudget{0});
static_assert(b_free.peak_bytes()           == PeakBytes{0});

inline constexpr BudgetedInt b_unbounded   = BudgetedInt::unbounded(11);
static_assert(b_unbounded.peek()            == 11);
static_assert(b_unbounded.bits()            == BitsBudgetLattice::top());
static_assert(b_unbounded.peak_bytes()      == PeakBytesLattice::top());

// ── combine_max — lattice join semantics ──────────────────────────
//
// Worst-case-across-paths: the combined value's budget is the
// pointwise MAX of the two inputs.
[[nodiscard]] consteval bool combine_max_takes_pointwise_max() noexcept {
    BudgetedInt a{42, BitsBudget{100},  PeakBytes{1024}};
    BudgetedInt b{42, BitsBudget{200},  PeakBytes{512}};
    auto        c = a.combine_max(b);
    return c.bits()       == BitsBudget{200}      // max(100, 200)
        && c.peak_bytes() == PeakBytes{1024}      // max(1024, 512)
        && c.peek()       == 42;
}
static_assert(combine_max_takes_pointwise_max());

// Reflexivity: combining with self is identity (idempotent join).
[[nodiscard]] consteval bool combine_max_idempotent() noexcept {
    BudgetedInt a{42, BitsBudget{100},  PeakBytes{1024}};
    auto        c = a.combine_max(a);
    return c.bits()       == BitsBudget{100}
        && c.peak_bytes() == PeakBytes{1024};
}
static_assert(combine_max_idempotent());

// ── accumulate — saturating-add chain semantics ──────────────────
[[nodiscard]] consteval bool accumulate_sums_pointwise() noexcept {
    BudgetedInt a{42, BitsBudget{1024},  PeakBytes{1<<20}};
    BudgetedInt b{42, BitsBudget{2048},  PeakBytes{4<<20}};
    auto        c = a.accumulate(b);
    return c.bits()       == BitsBudget{3072}                  // 1024 + 2048
        && c.peak_bytes() == PeakBytes{5u<<20}                 // 1MB + 4MB
        && c.peek()       == 42;
}
static_assert(accumulate_sums_pointwise());

// Accumulate at saturation cap.
[[nodiscard]] consteval bool accumulate_saturates_at_max() noexcept {
    constexpr auto MAX = std::numeric_limits<std::uint64_t>::max();
    BudgetedInt a{0, BitsBudget{MAX - 10}, PeakBytes{0}};
    BudgetedInt b{0, BitsBudget{100},      PeakBytes{0}};
    auto        c = a.accumulate(b);
    return c.bits() == BitsBudget{MAX};   // saturated, not wrapped
}
static_assert(accumulate_saturates_at_max());

// ── satisfies — admission gate semantics ─────────────────────────
[[nodiscard]] consteval bool satisfies_passes_within_threshold() noexcept {
    BudgetedInt a{42, BitsBudget{500}, PeakBytes{1024}};
    return  a.satisfies(BitsBudget{1000}, PeakBytes{2048})
        &&  a.satisfies(BitsBudget{500},  PeakBytes{1024})    // boundary
        && !a.satisfies(BitsBudget{499},  PeakBytes{2048})    // bits over
        && !a.satisfies(BitsBudget{1000}, PeakBytes{1023});   // peak over
}
static_assert(satisfies_passes_within_threshold());

// Free budget passes any threshold (including zero).
static_assert(BudgetedInt::free(7).satisfies(BitsBudget{0}, PeakBytes{0}));

// Unbounded budget fails any finite threshold.
static_assert(!BudgetedInt::unbounded(7).satisfies(
    BitsBudget{1000000}, PeakBytes{1u<<30}));

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(BudgetedInt::value_type_name().ends_with("int"));
static_assert(BudgetedInt::lattice_name().size() > 0);

// ── swap exchanges T values within the same lattice pin ──────────
template <typename W>
[[nodiscard]] consteval bool swap_exchanges_within(int x, int y) noexcept {
    W a{x, BitsBudget{10}, PeakBytes{20}};
    W b{y, BitsBudget{30}, PeakBytes{40}};
    a.swap(b);
    return a.peek()       == y
        && b.peek()       == x
        && a.bits()       == BitsBudget{30}
        && b.peak_bytes() == PeakBytes{20};
}
static_assert(swap_exchanges_within<BudgetedInt>(10, 20));

[[nodiscard]] consteval bool free_swap_works() noexcept {
    BudgetedInt a{10, BitsBudget{1}, PeakBytes{2}};
    BudgetedInt b{20, BitsBudget{3}, PeakBytes{4}};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10
        && a.bits() == BitsBudget{3} && b.peak_bytes() == PeakBytes{2};
}
static_assert(free_swap_works());

// ── peek_mut allows in-place mutation of T ────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    BudgetedInt a{10, BitsBudget{1024}, PeakBytes{4096}};
    a.peek_mut() = 99;
    // Mutating T does NOT mutate budget.
    return a.peek() == 99 && a.bits() == BitsBudget{1024};
}
static_assert(peek_mut_works());

// ── operator== — same-lattice, same-T comparison ─────────────────
//
// Equality compares value AND both budget axes.  Two Budgeteds
// with identical T but different budgets are NOT equal.
[[nodiscard]] consteval bool equality_compares_value_and_budget() noexcept {
    BudgetedInt a{42, BitsBudget{100}, PeakBytes{200}};
    BudgetedInt b{42, BitsBudget{100}, PeakBytes{200}};
    BudgetedInt c{43, BitsBudget{100}, PeakBytes{200}};   // diff value
    BudgetedInt d{42, BitsBudget{101}, PeakBytes{200}};   // diff bits
    BudgetedInt e{42, BitsBudget{100}, PeakBytes{201}};   // diff peak
    return  (a == b)
        && !(a == c)
        && !(a == d)
        && !(a == e);
}
static_assert(equality_compares_value_and_budget());

// ── Move-only T support ──────────────────────────────────────────
struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

static_assert(!std::is_copy_constructible_v<Budgeted<MoveOnlyT>>,
    "Budgeted<T> must transitively inherit T's copy-deletion. "
    "If this fires, MoveOnlyT's deleted copy ctor is no longer "
    "visible through the wrapper.");
static_assert(std::is_move_constructible_v<Budgeted<MoveOnlyT>>);

// combine_max && rvalue overload works on move-only T.
[[nodiscard]] consteval bool combine_max_works_for_move_only() noexcept {
    Budgeted<MoveOnlyT> a{MoveOnlyT{42}, BitsBudget{100}, PeakBytes{200}};
    Budgeted<MoveOnlyT> b{MoveOnlyT{99}, BitsBudget{500}, PeakBytes{50}};
    auto                c = std::move(a).combine_max(b);
    return c.bits()       == BitsBudget{500}
        && c.peak_bytes() == PeakBytes{200}
        && c.peek().v     == 42;        // value from `a`, not `b`
}
static_assert(combine_max_works_for_move_only());

// accumulate && rvalue overload works on move-only T.
//
// AUDIT-PASS COVERAGE EXTENSION: combine_max already exercised on
// move-only T; accumulate has the same overload pair (& and &&)
// and the same value-provenance discipline (LHS keeps its value,
// budgets are added).  A refactor that broke accumulate for
// move-only T (e.g., dropped the && overload, accidentally
// requiring copy_constructible) would surface here.
[[nodiscard]] consteval bool accumulate_works_for_move_only() noexcept {
    Budgeted<MoveOnlyT> a{MoveOnlyT{42}, BitsBudget{100}, PeakBytes{200}};
    Budgeted<MoveOnlyT> b{MoveOnlyT{99}, BitsBudget{500}, PeakBytes{50}};
    auto                c = std::move(a).accumulate(b);
    return c.bits()       == BitsBudget{600}    // 100 + 500
        && c.peak_bytes() == PeakBytes{250}     // 200 + 50
        && c.peek().v     == 42;                // value from `a`, not `b`
}
static_assert(accumulate_works_for_move_only());

// accumulate const& overload requires copy_constructible<T>; verify
// it's REJECTED for move-only T at the SFINAE-detector level (so a
// production caller routing const& accumulate on a move-only T
// gets a clean concept-rejection diagnostic, not a deep
// copy-construction error inside the wrapper body).
template <typename W>
concept can_accumulate_lvalue = requires(W const& a, W const& b) {
    { a.accumulate(b) };
};
template <typename W>
concept can_accumulate_rvalue = requires(W&& a, W const& b) {
    { std::move(a).accumulate(b) };
};
static_assert( can_accumulate_lvalue<BudgetedInt>);
static_assert( can_accumulate_rvalue<BudgetedInt>);
static_assert(!can_accumulate_lvalue<Budgeted<MoveOnlyT>>,
    "accumulate const& on move-only T must be rejected — the "
    "const& overload requires copy_constructible<T>.");
static_assert( can_accumulate_rvalue<Budgeted<MoveOnlyT>>);

// Same SFINAE detectors for combine_max — completing the parity
// between the two composition operations.
template <typename W>
concept can_combine_max_lvalue = requires(W const& a, W const& b) {
    { a.combine_max(b) };
};
template <typename W>
concept can_combine_max_rvalue = requires(W&& a, W const& b) {
    { std::move(a).combine_max(b) };
};
static_assert( can_combine_max_lvalue<BudgetedInt>);
static_assert( can_combine_max_rvalue<BudgetedInt>);
static_assert(!can_combine_max_lvalue<Budgeted<MoveOnlyT>>,
    "combine_max const& on move-only T must be rejected — the "
    "const& overload requires copy_constructible<T>.");
static_assert( can_combine_max_rvalue<Budgeted<MoveOnlyT>>);

// ── Stable-name introspection (FOUND-E07/H06 surface) ────────────
static_assert(BudgetedInt::value_type_name().size() > 0);
static_assert(BudgetedInt::lattice_name().size()    > 0);

// ── Runtime smoke test ────────────────────────────────────────────
inline void runtime_smoke_test() {
    // Construction paths.
    BudgetedInt a{};
    BudgetedInt b{42, BitsBudget{1024}, PeakBytes{4096}};
    BudgetedInt c{std::in_place, BitsBudget{16}, PeakBytes{64}, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();
    [[maybe_unused]] auto bb = b.bits();
    [[maybe_unused]] auto bp = b.peak_bytes();

    // Convenience factories.
    BudgetedInt d = BudgetedInt::free(99);
    BudgetedInt e = BudgetedInt::unbounded(11);
    if (d.bits() != BitsBudget{0}) std::abort();
    if (e.peak_bytes() != PeakBytesLattice::top()) std::abort();

    // peek_mut.
    BudgetedInt mutable_b{10, BitsBudget{1}, PeakBytes{2}};
    mutable_b.peek_mut() = 99;
    if (mutable_b.peek() != 99) std::abort();

    // Swap at runtime.
    BudgetedInt sx{1, BitsBudget{10}, PeakBytes{20}};
    BudgetedInt sy{2, BitsBudget{30}, PeakBytes{40}};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    // combine_max.
    BudgetedInt left{42,  BitsBudget{100}, PeakBytes{1024}};
    BudgetedInt right{42, BitsBudget{200}, PeakBytes{512}};
    auto        joined = left.combine_max(right);
    if (joined.bits() != BitsBudget{200}) std::abort();
    if (joined.peak_bytes() != PeakBytes{1024}) std::abort();

    // accumulate.
    BudgetedInt step1{0, BitsBudget{1024}, PeakBytes{1u<<20}};
    BudgetedInt step2{0, BitsBudget{2048}, PeakBytes{4u<<20}};
    auto        chain = step1.accumulate(step2);
    if (chain.bits() != BitsBudget{3072}) std::abort();
    if (chain.peak_bytes() != PeakBytes{5u<<20}) std::abort();

    // satisfies — admission gate.
    if (!chain.satisfies(BitsBudget{4096}, PeakBytes{8u<<20})) std::abort();
    if ( chain.satisfies(BitsBudget{1000}, PeakBytes{8u<<20})) std::abort();

    // operator==.
    BudgetedInt eq_a{42, BitsBudget{1}, PeakBytes{2}};
    BudgetedInt eq_b{42, BitsBudget{1}, PeakBytes{2}};
    if (!(eq_a == eq_b)) std::abort();

    // budget() returns ProductElement.
    [[maybe_unused]] auto budget_pair = b.budget();
    if (budget_pair.first  != BitsBudget{1024}) std::abort();
    if (budget_pair.second != PeakBytes{4096})  std::abort();

    // Move-construct from consumed inner.
    BudgetedInt orig{55, BitsBudget{1}, PeakBytes{2}};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();
}

}  // namespace detail::budgeted_self_test

}  // namespace crucible::safety
