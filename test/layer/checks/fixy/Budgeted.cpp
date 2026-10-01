// The compile-time checks of fixy/Budgeted.h.

#include <fixy/Budgeted.h>

namespace fixy {

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

static_assert(IsBudgeted<B>);
static_assert(IsBudgeted<B const&>);
static_assert(!IsBudgeted<int>);
static_assert(!IsBudgeted<Lookalike>);

static_assert(B::value_type_name().ends_with("int"));

// The cases that need an authority, and so an Init context, run in
// test/fixy/test_versioned_budgeted.cpp and its attack file.

}  // namespace detail::budgeted_self_test

}  // namespace fixy
