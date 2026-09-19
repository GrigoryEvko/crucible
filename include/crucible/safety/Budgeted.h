#pragma once

// A value paired with two independent resource grades: the bits it
// transferred and the peak bytes it held. A downstream gate can then
// refuse the value when either axis exceeds its threshold.
//
// Two ways of composing budgets are both meaningful, so both ship.
// The lattice join takes the componentwise maximum, which is the
// worst case across two parallel paths and the right fold for a fan-in
// gate. Accumulation takes the componentwise saturating sum, which is
// the footprint of a chain of stages. Only the first is a lattice
// operation, and they stay separate methods so that which one a call
// site chose is visible there.
//
// Copying is permitted because the grade records a value's resource
// consumption, which is part of its identity rather than of its
// ownership. Two values with the same payload and the same budget are
// the same event, so a copy is a replay and not a duplication.
//
// The grade here is runtime data rather than pinned in the type, so
// there is no type-level relaxation operation. A wrapper that wants
// type-pinned bounds belongs beside this one rather than folded into
// it, because folding would fork the interface at every call site
// that already takes the runtime form.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/BitsBudgetLattice.h>
#include <crucible/algebra/lattices/PeakBytesLattice.h>
#include <crucible/algebra/lattices/_ProductLattice.h>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::BitsBudget;
using ::crucible::algebra::lattices::BitsBudgetLattice;
using ::crucible::algebra::lattices::PeakBytes;
using ::crucible::algebra::lattices::PeakBytesLattice;

namespace detail {
[[nodiscard]] constexpr std::uint64_t sat_add(std::uint64_t a, std::uint64_t b) noexcept {
    std::uint64_t s = a + b;
    return s < a ? std::numeric_limits<std::uint64_t>::max() : s;
}
}  // namespace detail

template <typename T>
class [[nodiscard]] Budgeted {
public:
    using value_type = T;
    using lattice_type = ::crucible::algebra::lattices::ProductLattice<BitsBudgetLattice, PeakBytesLattice>;
    using budget_t = typename lattice_type::element_type;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

private:
    graded_type impl_;

    [[nodiscard]] static constexpr budget_t pack(BitsBudget bits, PeakBytes peak) noexcept {
        return budget_t{bits, peak};
    }

public:
    // A zero budget is the strongest claim, so the default is the
    // claim that no resources were used.
    constexpr Budgeted() noexcept(std::is_nothrow_default_constructible_v<T>) : impl_{T{}, lattice_type::bottom()} {}

    constexpr Budgeted(T value, BitsBudget bits, PeakBytes peak) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), pack(bits, peak)} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr Budgeted(std::in_place_t, BitsBudget bits, PeakBytes peak,
                       Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), pack(bits, peak)} {}

    // The strongest claim, for a producer that genuinely used nothing.
    [[nodiscard]] static constexpr Budgeted free(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Budgeted{std::move(value), BitsBudget{0}, PeakBytes{0}};
    }

    // The weakest claim, for a producer whose budget is unknown.
    [[nodiscard]] static constexpr Budgeted unbounded(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Budgeted{std::move(value), BitsBudgetLattice::top(), PeakBytesLattice::top()};
    }

    constexpr Budgeted(const Budgeted&) = default;
    constexpr Budgeted(Budgeted&&) = default;
    constexpr Budgeted& operator=(const Budgeted&) = default;
    constexpr Budgeted& operator=(Budgeted&&) = default;
    ~Budgeted() = default;

    // The budget participates in equality, so two values with the same
    // payload but different budgets are not equal.
    [[nodiscard]] friend constexpr bool operator==(Budgeted const& a,
                                                   Budgeted const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek() && a.bits() == b.bits() && a.peak_bytes() == b.peak_bytes();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    [[nodiscard]] constexpr BitsBudget bits() const noexcept { return impl_.grade().first; }

    [[nodiscard]] constexpr PeakBytes peak_bytes() const noexcept { return impl_.grade().second; }

    [[nodiscard]] constexpr budget_t budget() const noexcept { return impl_.grade(); }

    constexpr void swap(Budgeted& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(Budgeted& a, Budgeted& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // The budgets join componentwise, and the payload comes from the
    // left operand. A caller wanting the other payload swaps the
    // operands, which keeps this to one method.
    [[nodiscard]] constexpr Budgeted
    combine_max(Budgeted const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Budgeted{this->peek(), BitsBudgetLattice::join(this->bits(), other.bits()),
                        PeakBytesLattice::join(this->peak_bytes(), other.peak_bytes())};
    }

    [[nodiscard]] constexpr Budgeted
    combine_max(Budgeted const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        BitsBudget joined_bits = BitsBudgetLattice::join(this->bits(), other.bits());
        PeakBytes joined_peak = PeakBytesLattice::join(this->peak_bytes(), other.peak_bytes());
        return Budgeted{std::move(impl_).consume(), joined_bits, joined_peak};
    }

    // The budgets add componentwise and clamp rather than wrap, and
    // the payload again comes from the left operand. This is
    // arithmetic, not a lattice operation, which is why it is a
    // separate method from the join.
    [[nodiscard]] constexpr Budgeted
    accumulate(Budgeted const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Budgeted{this->peek(), BitsBudget{detail::sat_add(this->bits().value, other.bits().value)},
                        PeakBytes{detail::sat_add(this->peak_bytes().value, other.peak_bytes().value)}};
    }

    [[nodiscard]] constexpr Budgeted
    accumulate(Budgeted const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        BitsBudget summed_bits{detail::sat_add(this->bits().value, other.bits().value)};
        PeakBytes summed_peak{detail::sat_add(this->peak_bytes().value, other.peak_bytes().value)};
        return Budgeted{std::move(impl_).consume(), summed_bits, summed_peak};
    }

    // The axes are independent, so admission requires both to fit.
    [[nodiscard]] constexpr bool satisfies(BitsBudget max_bits, PeakBytes max_peak) const noexcept {
        return BitsBudgetLattice::leq(this->bits(), max_bits) && PeakBytesLattice::leq(this->peak_bytes(), max_peak);
    }
};

// Both axes wrap the same integer type. Collapsing them into one
// shared alias for convenience would dissolve every compile error that
// currently catches an axis swap, and a gate comparing against one
// bound would silently read the other counter.
static_assert(!std::is_same_v<BitsBudget, PeakBytes>, "BitsBudget and PeakBytes must be structurally distinct C++ "
                                                      "types even though both wrap uint64_t.  If this fires, the "
                                                      "strong-newtype discipline that fences Budgeted axis-swap bugs "
                                                      "has been broken.");

// The grade is carried per instance rather than collapsed away, so
// the wrapper costs the payload plus two 64-bit fields and any
// padding.
namespace detail::budgeted_layout {

static_assert(sizeof(Budgeted<int>) >= sizeof(int) + 16);
static_assert(sizeof(Budgeted<double>) >= sizeof(double) + 16);
static_assert(sizeof(Budgeted<char>) >= sizeof(char) + 16);

// A 64-bit payload needs no padding, so the total is exact here.
static_assert(sizeof(Budgeted<std::uint64_t>) == 24, "Budgeted<uint64_t> must be 8 bytes of value plus 16 bytes of "
                                                     "grade. If this fires, the product lattice's element type no "
                                                     "longer holds exactly two 64-bit fields.");

}  // namespace detail::budgeted_layout

namespace detail::budgeted_self_test {

using BudgetedInt = Budgeted<int>;
using BudgetedDbl = Budgeted<double>;

inline constexpr BudgetedInt b_default{};
static_assert(b_default.peek() == 0);
static_assert(b_default.bits() == BitsBudget{0});
static_assert(b_default.peak_bytes() == PeakBytes{0});

inline constexpr BudgetedInt b_explicit{42, BitsBudget{1024}, PeakBytes{4096}};
static_assert(b_explicit.peek() == 42);
static_assert(b_explicit.bits() == BitsBudget{1024});
static_assert(b_explicit.peak_bytes() == PeakBytes{4096});

inline constexpr BudgetedInt b_in_place{std::in_place, BitsBudget{16}, PeakBytes{64}, 7};
static_assert(b_in_place.peek() == 7);
static_assert(b_in_place.bits() == BitsBudget{16});
static_assert(b_in_place.peak_bytes() == PeakBytes{64});

inline constexpr BudgetedInt b_free = BudgetedInt::free(99);
static_assert(b_free.peek() == 99);
static_assert(b_free.bits() == BitsBudget{0});
static_assert(b_free.peak_bytes() == PeakBytes{0});

inline constexpr BudgetedInt b_unbounded = BudgetedInt::unbounded(11);
static_assert(b_unbounded.peek() == 11);
static_assert(b_unbounded.bits() == BitsBudgetLattice::top());
static_assert(b_unbounded.peak_bytes() == PeakBytesLattice::top());

[[nodiscard]] consteval bool combine_max_takes_pointwise_max() noexcept {
    BudgetedInt a{42, BitsBudget{100}, PeakBytes{1024}};
    BudgetedInt b{42, BitsBudget{200}, PeakBytes{512}};
    auto c = a.combine_max(b);
    return c.bits() == BitsBudget{200} && c.peak_bytes() == PeakBytes{1024} && c.peek() == 42;
}
static_assert(combine_max_takes_pointwise_max());

[[nodiscard]] consteval bool combine_max_idempotent() noexcept {
    BudgetedInt a{42, BitsBudget{100}, PeakBytes{1024}};
    auto c = a.combine_max(a);
    return c.bits() == BitsBudget{100} && c.peak_bytes() == PeakBytes{1024};
}
static_assert(combine_max_idempotent());

[[nodiscard]] consteval bool accumulate_sums_pointwise() noexcept {
    BudgetedInt a{42, BitsBudget{1024}, PeakBytes{1 << 20}};
    BudgetedInt b{42, BitsBudget{2048}, PeakBytes{4 << 20}};
    auto c = a.accumulate(b);
    return c.bits() == BitsBudget{3072} && c.peak_bytes() == PeakBytes{5u << 20} && c.peek() == 42;
}
static_assert(accumulate_sums_pointwise());

[[nodiscard]] consteval bool accumulate_saturates_at_max() noexcept {
    constexpr auto MAX = std::numeric_limits<std::uint64_t>::max();
    BudgetedInt a{0, BitsBudget{MAX - 10}, PeakBytes{0}};
    BudgetedInt b{0, BitsBudget{100}, PeakBytes{0}};
    auto c = a.accumulate(b);
    return c.bits() == BitsBudget{MAX};  // clamped, not wrapped
}
static_assert(accumulate_saturates_at_max());

[[nodiscard]] consteval bool satisfies_passes_within_threshold() noexcept {
    BudgetedInt a{42, BitsBudget{500}, PeakBytes{1024}};
    return a.satisfies(BitsBudget{1000}, PeakBytes{2048}) && a.satisfies(BitsBudget{500}, PeakBytes{1024})
        && !a.satisfies(BitsBudget{499}, PeakBytes{2048}) && !a.satisfies(BitsBudget{1000}, PeakBytes{1023});
}
static_assert(satisfies_passes_within_threshold());

static_assert(BudgetedInt::free(7).satisfies(BitsBudget{0}, PeakBytes{0}));

static_assert(!BudgetedInt::unbounded(7).satisfies(BitsBudget{1000000}, PeakBytes{1u << 30}));

static_assert(BudgetedInt::value_type_name().ends_with("int"));
static_assert(BudgetedInt::lattice_name().size() > 0);

template <typename W>
[[nodiscard]] consteval bool swap_exchanges_within(int x, int y) noexcept {
    W a{x, BitsBudget{10}, PeakBytes{20}};
    W b{y, BitsBudget{30}, PeakBytes{40}};
    a.swap(b);
    return a.peek() == y && b.peek() == x && a.bits() == BitsBudget{30} && b.peak_bytes() == PeakBytes{20};
}
static_assert(swap_exchanges_within<BudgetedInt>(10, 20));

[[nodiscard]] consteval bool free_swap_works() noexcept {
    BudgetedInt a{10, BitsBudget{1}, PeakBytes{2}};
    BudgetedInt b{20, BitsBudget{3}, PeakBytes{4}};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10 && a.bits() == BitsBudget{3} && b.peak_bytes() == PeakBytes{2};
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    BudgetedInt a{10, BitsBudget{1024}, PeakBytes{4096}};
    a.peek_mut() = 99;
    // Mutating the payload leaves the budget alone.
    return a.peek() == 99 && a.bits() == BitsBudget{1024};
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_and_budget() noexcept {
    BudgetedInt a{42, BitsBudget{100}, PeakBytes{200}};
    BudgetedInt b{42, BitsBudget{100}, PeakBytes{200}};
    BudgetedInt c{43, BitsBudget{100}, PeakBytes{200}};
    BudgetedInt d{42, BitsBudget{101}, PeakBytes{200}};
    BudgetedInt e{42, BitsBudget{100}, PeakBytes{201}};
    return (a == b) && !(a == c) && !(a == d) && !(a == e);
}
static_assert(equality_compares_value_and_budget());

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

[[nodiscard]] consteval bool combine_max_works_for_move_only() noexcept {
    Budgeted<MoveOnlyT> a{MoveOnlyT{42}, BitsBudget{100}, PeakBytes{200}};
    Budgeted<MoveOnlyT> b{MoveOnlyT{99}, BitsBudget{500}, PeakBytes{50}};
    auto c = std::move(a).combine_max(b);
    return c.bits() == BitsBudget{500} && c.peak_bytes() == PeakBytes{200}
        && c.peek().v == 42;  // the payload comes from the left operand
}
static_assert(combine_max_works_for_move_only());

// Both composition operations have the same overload pair and the same
// payload provenance, so each is exercised on a move-only payload
// separately: dropping one rvalue overload would only surface here.
[[nodiscard]] consteval bool accumulate_works_for_move_only() noexcept {
    Budgeted<MoveOnlyT> a{MoveOnlyT{42}, BitsBudget{100}, PeakBytes{200}};
    Budgeted<MoveOnlyT> b{MoveOnlyT{99}, BitsBudget{500}, PeakBytes{50}};
    auto c = std::move(a).accumulate(b);
    return c.bits() == BitsBudget{600} && c.peak_bytes() == PeakBytes{250}
        && c.peek().v == 42;  // the payload comes from the left operand
}
static_assert(accumulate_works_for_move_only());

// The detectors below keep the rejection at the concept boundary, so a
// caller reaching for the copying overload on a move-only payload gets
// one diagnostic rather than a copy-construction failure deep inside
// the wrapper.
template <typename W>
concept can_accumulate_lvalue = requires(W const& a, W const& b) {
    { a.accumulate(b) };
};
template <typename W>
concept can_accumulate_rvalue = requires(W&& a, W const& b) {
    { std::move(a).accumulate(b) };
};
static_assert(can_accumulate_lvalue<BudgetedInt>);
static_assert(can_accumulate_rvalue<BudgetedInt>);
static_assert(!can_accumulate_lvalue<Budgeted<MoveOnlyT>>, "accumulate const& on move-only T must be rejected — the "
                                                           "const& overload requires copy_constructible<T>.");
static_assert(can_accumulate_rvalue<Budgeted<MoveOnlyT>>);

template <typename W>
concept can_combine_max_lvalue = requires(W const& a, W const& b) {
    { a.combine_max(b) };
};
template <typename W>
concept can_combine_max_rvalue = requires(W&& a, W const& b) {
    { std::move(a).combine_max(b) };
};
static_assert(can_combine_max_lvalue<BudgetedInt>);
static_assert(can_combine_max_rvalue<BudgetedInt>);
static_assert(!can_combine_max_lvalue<Budgeted<MoveOnlyT>>, "combine_max const& on move-only T must be rejected — the "
                                                            "const& overload requires copy_constructible<T>.");
static_assert(can_combine_max_rvalue<Budgeted<MoveOnlyT>>);

static_assert(BudgetedInt::value_type_name().size() > 0);
static_assert(BudgetedInt::lattice_name().size() > 0);

inline void runtime_smoke_test() {
    BudgetedInt a{};
    BudgetedInt b{42, BitsBudget{1024}, PeakBytes{4096}};
    BudgetedInt c{std::in_place, BitsBudget{16}, PeakBytes{64}, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();
    [[maybe_unused]] auto bb = b.bits();
    [[maybe_unused]] auto bp = b.peak_bytes();

    BudgetedInt d = BudgetedInt::free(99);
    BudgetedInt e = BudgetedInt::unbounded(11);
    if (d.bits() != BitsBudget{0}) std::abort();
    if (e.peak_bytes() != PeakBytesLattice::top()) std::abort();

    BudgetedInt mutable_b{10, BitsBudget{1}, PeakBytes{2}};
    mutable_b.peek_mut() = 99;
    if (mutable_b.peek() != 99) std::abort();

    BudgetedInt sx{1, BitsBudget{10}, PeakBytes{20}};
    BudgetedInt sy{2, BitsBudget{30}, PeakBytes{40}};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    BudgetedInt left{42, BitsBudget{100}, PeakBytes{1024}};
    BudgetedInt right{42, BitsBudget{200}, PeakBytes{512}};
    auto joined = left.combine_max(right);
    if (joined.bits() != BitsBudget{200}) std::abort();
    if (joined.peak_bytes() != PeakBytes{1024}) std::abort();

    BudgetedInt step1{0, BitsBudget{1024}, PeakBytes{1u << 20}};
    BudgetedInt step2{0, BitsBudget{2048}, PeakBytes{4u << 20}};
    auto chain = step1.accumulate(step2);
    if (chain.bits() != BitsBudget{3072}) std::abort();
    if (chain.peak_bytes() != PeakBytes{5u << 20}) std::abort();

    if (!chain.satisfies(BitsBudget{4096}, PeakBytes{8u << 20})) std::abort();
    if (chain.satisfies(BitsBudget{1000}, PeakBytes{8u << 20})) std::abort();

    BudgetedInt eq_a{42, BitsBudget{1}, PeakBytes{2}};
    BudgetedInt eq_b{42, BitsBudget{1}, PeakBytes{2}};
    if (!(eq_a == eq_b)) std::abort();

    [[maybe_unused]] auto budget_pair = b.budget();
    if (budget_pair.first != BitsBudget{1024}) std::abort();
    if (budget_pair.second != PeakBytes{4096}) std::abort();

    BudgetedInt orig{55, BitsBudget{1}, PeakBytes{2}};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();
}

}  // namespace detail::budgeted_self_test

}  // namespace crucible::safety
