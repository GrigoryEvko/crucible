#pragma once

// A value paired with two independent resource grades: the bits it
// transferred and the peak bytes it held.  A downstream gate refuses the
// value when either grade is above its threshold.
//
// Both grades order by consumption, so the smaller grade is the stronger
// claim and zero is the strongest of all.  The discipline follows from
// that:
//
//   - The default is UNBOUNDED, the weakest claim.  The old wrapper
//     defaulted to zero, so a value built by default passed every gate
//     without any evidence of what it used.
//   - There is no free(), which claimed zero for any payload given to
//     it.  A producer that measured its use states the measurement to
//     the constructor.
//   - There is no peek_mut().  Replacing the payload under the current
//     grade would let a payload that used more carry the smaller claim
//     of the one it replaced.
//   - A payload that is a reference is refused, for the same reason: the
//     referent can be replaced after the claim is made.
//   - A move out of a payload that is not trivially copyable leaves the
//     source unbounded.  The moved-from payload holds some unspecified
//     value, and the weakest claim is the only one that stays true for
//     it.  A trivially copyable payload is copied by a move, so its
//     source keeps its claim.
//
// A mutable member in the payload is admitted, unlike in EpochVersioned.
// The grade describes what producing the payload used, and a later write
// through a mutable member does not change what the production used.
//
// The two compositions keep the payload of the left operand, and that is
// sound here, unlike for a version.  Both produce a grade at or above
// the left operand's own, and a larger grade is a weaker claim, so the
// result never claims less use than its payload had.  combine_max takes
// the componentwise maximum, which is the worst case across two parallel
// paths.  accumulate takes the componentwise saturating sum, which is
// the footprint of a chain of stages.  Only the first is a lattice
// operation.
//
// Old spelling: include/crucible/safety/Budgeted.h, and the detection
// surface of include/crucible/safety/IsBudgeted.h.

#include <fixy/GradedFacade.h>
#include <foundation/Platform.h>
#include <foundation/Saturate.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstdint>
#include <limits>
#include <memory>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy {

using ::foundation::algebra::lattices::BitsBudget;
using ::foundation::algebra::lattices::BitsBudgetLattice;
using ::foundation::algebra::lattices::PeakBytes;
using ::foundation::algebra::lattices::PeakBytesLattice;

using BudgetLattice = ::foundation::algebra::lattices::ProductLattice<BitsBudgetLattice, PeakBytesLattice>;

template <typename T>
    requires std::is_object_v<T>
class [[nodiscard]] Budgeted : public graded_facade<::foundation::algebra::ModalityKind::Absolute, BudgetLattice, T> {
public:
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute, BudgetLattice, T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;
    using budget_t = typename lattice_type::element_type;

private:
    graded_type impl_;

    // Drops the claim of a moved-from source to unbounded.  The payload is
    // moved out and back in, because Graded sets its grade only at
    // construction.
    constexpr void relinquish_budget_() noexcept(std::is_nothrow_move_constructible_v<T>) {
        T left_behind = std::move(impl_).consume();
        std::destroy_at(&impl_);
        std::construct_at(&impl_, std::move(left_behind), lattice_type::top());
    }

public:
    // The weakest claim, because nothing measured this value.
    constexpr Budgeted() noexcept(std::is_nothrow_default_constructible_v<T>)
        requires std::default_initializable<T>
        : impl_{T{}, lattice_type::top()} {}

    constexpr Budgeted(T value, BitsBudget bits, PeakBytes peak) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), budget_t{bits, peak}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr Budgeted(std::in_place_t, BitsBudget bits, PeakBytes peak,
                       Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), budget_t{bits, peak}} {}

    // The weakest claim, for a producer whose use is unknown.
    [[nodiscard]] static constexpr Budgeted unbounded(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Budgeted{std::move(value), BitsBudgetLattice::top(), PeakBytesLattice::top()};
    }

    // A copy is a replay: the grade records what producing the payload
    // used, and two values with the same payload and grade are the same
    // event.
    constexpr Budgeted(const Budgeted&) = default;
    constexpr Budgeted& operator=(const Budgeted&) = default;

    // A move of a trivially copyable payload is a copy, and the source
    // keeps a claim that is still true.
    constexpr Budgeted(Budgeted&&)
        requires std::is_trivially_copyable_v<T>
    = default;
    constexpr Budgeted& operator=(Budgeted&&)
        requires std::is_trivially_copyable_v<T>
    = default;

    // Any other move leaves the source unbounded.
    constexpr Budgeted(Budgeted&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(!std::is_trivially_copyable_v<T>)
        : impl_{std::move(other.impl_)} {
        other.relinquish_budget_();
    }
    constexpr Budgeted& operator=(Budgeted&& other) noexcept(std::is_nothrow_move_constructible_v<T>
                                                             && std::is_nothrow_move_assignable_v<T>)
        requires(!std::is_trivially_copyable_v<T> && std::is_move_assignable_v<T>)
    {
        if (this != &other) {
            impl_ = std::move(other.impl_);
            other.relinquish_budget_();
        }
        return *this;
    }

    ~Budgeted() = default;

    [[nodiscard]] friend constexpr bool operator==(Budgeted const& a,
                                                   Budgeted const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek() && a.budget() == b.budget();
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr BitsBudget bits() const noexcept { return impl_.grade().first; }
    [[nodiscard]] constexpr PeakBytes peak_bytes() const noexcept { return impl_.grade().second; }
    [[nodiscard]] constexpr budget_t budget() const noexcept { return impl_.grade(); }

    [[nodiscard]] constexpr bool is_unbounded() const noexcept { return budget() == lattice_type::top(); }

    constexpr void swap(Budgeted& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(Budgeted& a, Budgeted& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    [[nodiscard]] constexpr Budgeted
    combine_max(Budgeted const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        budget_t const joined = lattice_type::join(budget(), other.budget());
        return Budgeted{this->peek(), joined.first, joined.second};
    }

    [[nodiscard]] constexpr Budgeted
    combine_max(Budgeted const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        budget_t const joined = lattice_type::join(budget(), other.budget());
        return Budgeted{std::move(impl_).consume(), joined.first, joined.second};
    }

    // The sum clamps at the top rather than wrapping, so a chain whose
    // use overflows the counter reads as unbounded.
    [[nodiscard]] constexpr Budgeted
    accumulate(Budgeted const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Budgeted{this->peek(), summed_bits(other), summed_peak(other)};
    }

    [[nodiscard]] constexpr Budgeted accumulate(Budgeted const& other) && noexcept(
        std::is_nothrow_move_constructible_v<T>) {
        BitsBudget const bits_sum = summed_bits(other);
        PeakBytes const peak_sum = summed_peak(other);
        return Budgeted{std::move(impl_).consume(), bits_sum, peak_sum};
    }

    // The axes are independent, so admission requires both to fit.
    [[nodiscard]] constexpr bool satisfies(BitsBudget max_bits, PeakBytes max_peak) const noexcept {
        return lattice_type::leq(budget(), budget_t{max_bits, max_peak});
    }

private:
    [[nodiscard]] constexpr BitsBudget summed_bits(Budgeted const& other) const noexcept {
        return BitsBudget{::foundation::sat::add_sat(bits().raw(), other.bits().raw())};
    }
    [[nodiscard]] constexpr PeakBytes summed_peak(Budgeted const& other) const noexcept {
        return PeakBytes{::foundation::sat::add_sat(peak_bytes().raw(), other.peak_bytes().raw())};
    }
};

// The detection surface of the old IsBudgeted.h, answered by one
// reflection query.
template <typename T>
concept IsBudgeted = ::foundation::reflect::IsInstanceOf<T, ^^Budgeted>;

template <typename T>
inline constexpr bool is_budgeted_v = IsBudgeted<T>;

template <typename T>
    requires IsBudgeted<T>
using budgeted_value_t = typename std::remove_cvref_t<T>::value_type;

namespace detail::budgeted_self_test {

using B = Budgeted<int>;
inline constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

static_assert(!std::is_same_v<BitsBudget, PeakBytes>);
static_assert(!std::is_constructible_v<B, int, PeakBytes, BitsBudget>,
              "the two grades are distinct types, so passing them in the wrong order does not compile");
static_assert(!std::is_constructible_v<B, int, std::uint64_t, std::uint64_t>, "a raw integer is not a budget");
static_assert(sizeof(Budgeted<std::uint64_t>) == 24);
static_assert(std::is_trivially_copyable_v<B>, "a trivially copyable payload keeps a trivially copyable wrapper");

template <typename T>
concept can_budget = requires { typename Budgeted<T>; };
static_assert(can_budget<int>);
static_assert(!can_budget<int&>, "a reference payload is refused");

// A payload whose move is not a copy.  The source must drop to unbounded.
struct Counted {
    int v{0};
    constexpr Counted() = default;
    constexpr explicit Counted(int x) : v{x} {}
    constexpr Counted(Counted const&) = default;
    constexpr Counted(Counted&& other) noexcept : v{other.v} { other.v = -1; }
    constexpr Counted& operator=(Counted const&) = default;
    constexpr Counted& operator=(Counted&& other) noexcept {
        v = other.v;
        other.v = -1;
        return *this;
    }
};
static_assert(!std::is_trivially_copyable_v<Counted>);

[[nodiscard]] consteval bool moved_from_source_is_unbounded() noexcept {
    Budgeted<Counted> source{Counted{3}, BitsBudget{8}, PeakBytes{64}};
    Budgeted<Counted> target{std::move(source)};
    Budgeted<Counted> assigned{Counted{4}, BitsBudget{1}, PeakBytes{1}};
    assigned = std::move(target);
    return source.is_unbounded() && !source.satisfies(BitsBudget{8}, PeakBytes{64}) && target.is_unbounded()
        && assigned.peek().v == 3 && assigned.satisfies(BitsBudget{8}, PeakBytes{64});
}
static_assert(moved_from_source_is_unbounded());

// The default claims nothing: it is the top of both axes.
inline constexpr B b_default{};
static_assert(b_default.is_unbounded());
static_assert(b_default.bits() == BitsBudgetLattice::top());
static_assert(b_default.peak_bytes() == PeakBytesLattice::top());
static_assert(!b_default.satisfies(BitsBudget{kMax - 1}, PeakBytes{kMax - 1}),
              "a value that nothing measured must not pass a finite gate");

inline constexpr B b_explicit{42, BitsBudget{1024}, PeakBytes{4096}};
static_assert(b_explicit.peek() == 42);
static_assert(b_explicit.bits() == BitsBudget{1024});
static_assert(b_explicit.peak_bytes() == PeakBytes{4096});
static_assert(!b_explicit.is_unbounded());

inline constexpr B b_in_place{std::in_place, BitsBudget{16}, PeakBytes{64}, 7};
static_assert(b_in_place.peek() == 7);

static_assert(B::unbounded(11).is_unbounded());

// The join is componentwise, and the payload is the left operand's.
inline constexpr B b_left{1, BitsBudget{100}, PeakBytes{1024}};
inline constexpr B b_right{2, BitsBudget{200}, PeakBytes{512}};
static_assert(b_left.combine_max(b_right).bits() == BitsBudget{200});
static_assert(b_left.combine_max(b_right).peak_bytes() == PeakBytes{1024});
static_assert(b_left.combine_max(b_right).peek() == 1);
static_assert(b_left.combine_max(b_left).budget() == b_left.budget());

// The join gives a grade at or above the left operand's own, so the
// payload it keeps never claims less use than it had.
//
// accumulate has no constant-evaluated check in this header.  Its sums,
// its clamp at the top and the same never-tighten grid run at run time in
// test/fixy/test_versioned_budgeted.cpp.  In a constant evaluation, the
// patched g++-16p 16.2.1 reports "contract condition is not constant" at
// the stored-grade contract of the Graded constructor once a translation
// unit evaluates accumulate more than once, and which evaluation trips it
// moves with the unrelated assertions around it.  The contract is true in
// each case, and each case alone passes.
[[nodiscard]] consteval bool joins_never_tighten() noexcept {
    B const inputs[] = {b_left, b_right, b_explicit, B{3, BitsBudget{0}, PeakBytes{0}}, B{4, BitsBudget{kMax - 5},
                                                                                           PeakBytes{kMax}}};
    for (B const& a : inputs) {
        for (B const& b : inputs) {
            if (!BudgetLattice::leq(a.budget(), a.combine_max(b).budget())) return false;
        }
    }
    return true;
}
static_assert(joins_never_tighten());

static_assert(b_explicit.satisfies(BitsBudget{1024}, PeakBytes{4096}));
static_assert(b_explicit.satisfies(BitsBudget{2000}, PeakBytes{5000}));
static_assert(!b_explicit.satisfies(BitsBudget{1023}, PeakBytes{4096}));
static_assert(!b_explicit.satisfies(BitsBudget{1024}, PeakBytes{4095}));

struct MoveOnly {
    int v{0};
    constexpr MoveOnly() = default;
    constexpr explicit MoveOnly(int x) : v{x} {}
    constexpr MoveOnly(MoveOnly&&) = default;
    constexpr MoveOnly& operator=(MoveOnly&&) = default;
    MoveOnly(MoveOnly const&) = delete;
    MoveOnly& operator=(MoveOnly const&) = delete;
};

static_assert(!std::is_copy_constructible_v<Budgeted<MoveOnly>>);

[[nodiscard]] consteval bool rvalue_join_moves_the_payload() noexcept {
    Budgeted<MoveOnly> a{MoveOnly{42}, BitsBudget{100}, PeakBytes{200}};
    Budgeted<MoveOnly> b{MoveOnly{99}, BitsBudget{500}, PeakBytes{50}};
    auto joined = std::move(a).combine_max(b);
    return joined.peek().v == 42 && joined.bits() == BitsBudget{500};
}
static_assert(rvalue_join_moves_the_payload());

template <typename W>
concept can_combine_lvalue = requires(W const& a, W const& b) { a.combine_max(b); };
static_assert(can_combine_lvalue<B>);
static_assert(!can_combine_lvalue<Budgeted<MoveOnly>>);

struct Lookalike {
    using value_type = int;
    using budget_t = int;
};

static_assert(is_budgeted_v<B>);
static_assert(is_budgeted_v<B const&>);
static_assert(!is_budgeted_v<int>);
static_assert(!is_budgeted_v<Lookalike>);
static_assert(std::is_same_v<budgeted_value_t<B const&>, int>);

static_assert(B::value_type_name().ends_with("int"));

}  // namespace detail::budgeted_self_test

}  // namespace fixy
