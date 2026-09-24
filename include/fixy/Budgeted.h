#pragma once

// A value paired with two independent resource grades: the bits it
// transferred and the peak bytes it held.  A downstream gate refuses the
// value when either grade is above its threshold.
//
// Both grades order by consumption, so the smaller grade is the stronger
// claim and zero is the strongest of all.  A finite grade therefore does
// not come from the producer.  It comes from a BudgetAuthority, the owner
// of the budgets of a program:
//
//   - A BudgetAuthority is minted only with a context that owns Init, the
//     capability of process startup, so a producer running on a
//     background or foreground thread cannot make one.
//   - Only its owner grants, because grant() needs a mutable authority.
//     A grant is an allowance, and the stamp that records it is a proof
//     type: its constructor is private, and its copy is user-provided, so
//     neither std::bit_cast nor a lifetime start over bytes makes one.
//   - Budgeted is built only from a stamp, or unbounded.  The value spends
//     the stamp, which does not copy, so one grant grades one value.
//
// The rest of the discipline follows from the orientation:
//
//   - The default is UNBOUNDED, the weakest claim.
//   - There is no free(), which claimed zero for any payload given to it.
//   - There is no peek_mut().  Replacing the payload under the current
//     grade would let a payload that used more carry the smaller claim
//     of the one it replaced.
//   - A payload that reaches state outside itself, or that a const
//     reference can write, is refused for the same reason
//     (SelfContained.h).
//   - A move out of a payload that is not trivially copyable leaves the
//     source unbounded.  The moved-from payload holds some unspecified
//     value, and the weakest claim is the only one that stays true for it.
//
// The two compositions keep the payload of the left operand, and that is
// sound here, unlike for a version.  Both produce a grade at or above the
// left operand's own, and a larger grade is a weaker claim.  combine_max
// takes the componentwise maximum, which is the worst case across two
// parallel paths.  accumulate takes the componentwise saturating sum,
// which is the footprint of a chain of stages.  Only the first is a
// lattice operation.
//
// A grant bounds the work only when the work spends through resources
// that draw on the grant.  Work outside such resources is not seen by any
// type here: the shrink-only ledger of
// test/fixy/test_versioned_budgeted_attacks.cpp holds that limit.
//
// Old spelling: include/crucible/safety/Budgeted.h, and the detection
// surface of include/crucible/safety/IsBudgeted.h.

#include <fixy/GradedFacade.h>
#include <fixy/SelfContained.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/effects/Ctx.h>
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
using ::foundation::algebra::lattices::BitsBudgetBound;
using ::foundation::algebra::lattices::BitsBudgetLattice;
using ::foundation::algebra::lattices::PeakBytes;
using ::foundation::algebra::lattices::PeakBytesBound;
using ::foundation::algebra::lattices::PeakBytesLattice;

using BudgetLattice = ::foundation::algebra::lattices::ProductLattice<BitsBudgetLattice, PeakBytesLattice>;

class BudgetAuthority;

// An allowance that a BudgetAuthority granted.  No caller can build one.
//
// An allowance is spent one time.  The stamp does not copy, so one grant
// cannot be put on two payloads, and the sum of the claims of a program
// stays at or below the sum of its grants.  A move leaves the source
// unbounded, the weakest claim, so a stamp spent twice through a move
// claims nothing the second time.
class BudgetStamp {
public:
    BudgetStamp() = delete("a budget stamp comes only from a BudgetAuthority");
    BudgetStamp(BudgetStamp const&) = delete("an allowance is spent one time; a copy would put one grant on two "
                                             "payloads");
    BudgetStamp& operator=(BudgetStamp const&) = delete("an allowance is spent one time");

    // User-provided, so that the stamp is neither trivially copyable nor
    // implicit-lifetime, and no route builds one from bytes.
    constexpr BudgetStamp(BudgetStamp&& other) noexcept : bits_{other.bits_}, peak_{other.peak_} {
        other.bits_ = BitsBudgetLattice::top();
        other.peak_ = PeakBytesLattice::top();
    }
    constexpr BudgetStamp& operator=(BudgetStamp&& other) noexcept {
        if (this != &other) {
            bits_ = other.bits_;
            peak_ = other.peak_;
            other.bits_ = BitsBudgetLattice::top();
            other.peak_ = PeakBytesLattice::top();
        }
        return *this;
    }
    ~BudgetStamp() = default;

    [[nodiscard]] constexpr BitsBudget bits() const noexcept { return bits_; }
    [[nodiscard]] constexpr PeakBytes peak_bytes() const noexcept { return peak_; }

private:
    friend class BudgetAuthority;
    constexpr BudgetStamp(BitsBudget bits, PeakBytes peak) noexcept : bits_{bits}, peak_{peak} {}

    BitsBudget bits_;
    PeakBytes peak_;
};

template <typename Ctx>
    requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
[[nodiscard]] constexpr BudgetAuthority mint_budget_authority(Ctx const& ctx) noexcept;

// The owner of the budgets.  Its address is its identity, so it neither
// copies nor moves.  It holds the Init capability it was minted with,
// because turning an allowance into a count is a read of a count image,
// which needs a context that owns IO.
class [[nodiscard]] BudgetAuthority {
public:
    BudgetAuthority(BudgetAuthority const&) = delete("the authority is the one owner of its budgets; a copy would "
                                                     "be a second owner");
    BudgetAuthority(BudgetAuthority&&) = delete("the authority is the one owner of its budgets, so it stays in place");
    BudgetAuthority& operator=(BudgetAuthority const&) = delete("the authority is the one owner of its budgets");
    BudgetAuthority& operator=(BudgetAuthority&&) = delete("the authority is the one owner of its budgets");
    ~BudgetAuthority() = default;

    // An allowance of at most `bits` bits transferred and `peak` bytes held.
    // Only the owner, which holds the authority mutably, grants.
    [[nodiscard]] constexpr BudgetStamp grant(BitsBudgetBound bits, PeakBytesBound peak) noexcept {
        ::foundation::effects::ExecCtx<::foundation::effects::Init,
                                       ::foundation::effects::Row<::foundation::effects::Effect::Init,
                                                                  ::foundation::effects::Effect::IO>> const ctx{cap_};
        return BudgetStamp{count_of_<BitsBudgetLattice>(ctx, bits.raw()), count_of_<PeakBytesLattice>(ctx, peak.raw())};
    }

private:
    template <typename Ctx>
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
    friend constexpr BudgetAuthority mint_budget_authority(Ctx const& ctx) noexcept;

    constexpr explicit BudgetAuthority(::foundation::effects::Init const& cap) noexcept : cap_{cap} {}

    // The count through the one door that states a count from a number.
    // The image names the axis it is read as, so the read cannot fail.
    template <typename L, typename Ctx>
    [[nodiscard]] static constexpr typename L::element_type count_of_(Ctx const& ctx, std::uint64_t count) noexcept {
        typename L::image_type image{};
        ::foundation::algebra::lattices::detail::count_image::write_word<0>(image, L::image_axis());
        ::foundation::algebra::lattices::detail::count_image::write_word<8>(image, count);
        return *L::mint_from_image(ctx, image);
    }

    ::foundation::effects::Init cap_;
};

// An authority for the budgets of a program.  The context must own Init,
// which only process startup and the test witness hold.
template <typename Ctx>
    requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
[[nodiscard]] constexpr BudgetAuthority mint_budget_authority(Ctx const& ctx) noexcept {
    static_assert(std::is_same_v<::foundation::effects::cap_type_of_t<Ctx>, ::foundation::effects::Init>,
                  "only the Init capability permits the Init atom, so a context that owns Init holds it");
    return BudgetAuthority{ctx.cap()};
}

template <SelfContained T>
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

    // The compositions build their result from a grade at or above the
    // left operand's own, which is a weaker claim than one it holds.
    constexpr Budgeted(T value, budget_t budget) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), budget} {}

public:
    // The weakest claim, because nothing measured this value.
    constexpr Budgeted() noexcept(std::is_nothrow_default_constructible_v<T>)
        requires std::default_initializable<T>
        : impl_{T{}, lattice_type::top()} {}

    // The stamp is spent here, so one grant grades one value.
    constexpr Budgeted(T value, BudgetStamp&& stamp) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), budget_t{stamp.bits(), stamp.peak_bytes()}} {
        BudgetStamp const spent{std::move(stamp)};
    }

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr Budgeted(std::in_place_t, BudgetStamp&& stamp,
                       Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), budget_t{stamp.bits(), stamp.peak_bytes()}} {
        BudgetStamp const spent{std::move(stamp)};
    }

    // The weakest claim, for a producer whose use is unknown.
    [[nodiscard]] static constexpr Budgeted unbounded(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Budgeted{std::move(value), lattice_type::top()};
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

    // Two values are equal when their grades are equal and their payloads
    // are equal by their members.
    [[nodiscard]] friend constexpr bool operator==(Budgeted const& a, Budgeted const& b) noexcept
        requires EqualityByMembers<T>
    {
        return a.budget() == b.budget() && equal_by_members(a.peek(), b.peek());
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
        return Budgeted{this->peek(), lattice_type::join(budget(), other.budget())};
    }

    [[nodiscard]] constexpr Budgeted
    combine_max(Budgeted const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        budget_t const joined = lattice_type::join(budget(), other.budget());
        return Budgeted{std::move(impl_).consume(), joined};
    }

    // The sum clamps at the top rather than wrapping, so a chain whose
    // use overflows the counter reads as unbounded.
    [[nodiscard]] constexpr Budgeted
    accumulate(Budgeted const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Budgeted{this->peek(), summed_(other)};
    }

    [[nodiscard]] constexpr Budgeted accumulate(Budgeted const& other) && noexcept(
        std::is_nothrow_move_constructible_v<T>) {
        budget_t const sum = summed_(other);
        return Budgeted{std::move(impl_).consume(), sum};
    }

    // The axes are independent, so admission requires both to fit.
    [[nodiscard]] constexpr bool satisfies(BitsBudgetBound max_bits, PeakBytesBound max_peak) const noexcept {
        return BitsBudgetLattice::is_at_most(bits(), max_bits) && PeakBytesLattice::is_at_most(peak_bytes(), max_peak);
    }

private:
    [[nodiscard]] constexpr budget_t summed_(Budgeted const& other) const noexcept {
        return budget_t{BitsBudgetLattice::saturating_sum(bits(), other.bits()),
                        PeakBytesLattice::saturating_sum(peak_bytes(), other.peak_bytes())};
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

static_assert(!std::is_same_v<BitsBudget, PeakBytes>);
static_assert(!std::is_constructible_v<B, int, BitsBudget, PeakBytes>, "a budget comes from a stamp, not from counts");
static_assert(!std::is_constructible_v<B, int, std::uint64_t, std::uint64_t>, "a raw integer is not a budget");
static_assert(sizeof(Budgeted<std::uint64_t>) == 24);
static_assert(!std::is_trivially_copyable_v<B> && !::foundation::lifetime::ImplicitLifetimeThroughout<B>,
              "a budgeted value is not built from bytes, so neither bit_cast nor a checked lifetime start forges "
              "a budget");

// A stamp is a proof, and an authority neither copies nor moves.
static_assert(!std::is_default_constructible_v<BudgetStamp>);
static_assert(!std::is_constructible_v<BudgetStamp, BitsBudget, PeakBytes>);
static_assert(!std::is_trivially_copyable_v<BudgetStamp> && !std::is_implicit_lifetime_v<BudgetStamp>);
static_assert(!std::is_copy_constructible_v<BudgetAuthority> && !std::is_move_constructible_v<BudgetAuthority>);

// An allowance is spent one time: the stamp does not copy, a value takes
// it by rvalue only, and a moved-from stamp is unbounded.
static_assert(!std::is_copy_constructible_v<BudgetStamp> && std::is_move_constructible_v<BudgetStamp>);
static_assert(!std::is_constructible_v<B, int, BudgetStamp const&> && !std::is_constructible_v<B, int, BudgetStamp&>);
static_assert(std::is_constructible_v<B, int, BudgetStamp&&>);

// Only the owner grants, and only an Init context mints the owner.
template <typename A>
concept can_grant = requires(A& authority) { authority.grant(BitsBudgetBound{1}, PeakBytesBound{1}); };
static_assert(can_grant<BudgetAuthority> && !can_grant<BudgetAuthority const>);
template <typename Ctx>
concept can_mint_authority = requires(Ctx const& ctx) { mint_budget_authority(ctx); };
static_assert(!can_mint_authority<::foundation::effects::detail::ctx_witnesses::FgWitness>);
static_assert(!can_mint_authority<::foundation::effects::detail::ctx_witnesses::BgBlockWitness>);
static_assert(can_mint_authority<::foundation::effects::detail::ctx_witnesses::InitWitness>);

template <typename T>
concept can_budget = requires { typename Budgeted<T>; };
static_assert(can_budget<int>);
static_assert(!can_budget<int&>, "a reference payload is refused");
static_assert(!can_budget<int*> && !can_budget<int const*>, "the referent of a pointer is not what was measured");
struct HoldsMutable {
    mutable int scratch = 0;
};
static_assert(!can_budget<HoldsMutable>, "a mutable member replaces the payload through peek()");

// The default claims nothing: it is the top of both axes.
inline constexpr B b_default{};
static_assert(b_default.is_unbounded());
static_assert(b_default.bits() == BitsBudgetLattice::top());
static_assert(b_default.peak_bytes() == PeakBytesLattice::top());
static_assert(!b_default.satisfies(BitsBudgetBound{std::numeric_limits<std::uint64_t>::max() - 1},
                                   PeakBytesBound{std::numeric_limits<std::uint64_t>::max() - 1}),
              "a value that nothing measured must not pass a finite gate");
static_assert(B::unbounded(11).is_unbounded() && B::unbounded(11).peek() == 11);

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

// The cases that need an authority, and so an Init context, run in
// test/fixy/test_versioned_budgeted.cpp and its attack file.

}  // namespace detail::budgeted_self_test

}  // namespace fixy
