#pragma once

// JoinPolicy<Tier, T> pins a value to how far the producing region
// engaged with the concurrent work that made it.
//
// The tiers form a chain from the loosest engagement to the
// strictest: FORGET, DETACH, ABANDON, CANCEL, WAIT_DEADLINE, then
// JOIN_ALL.  A JOIN_ALL region waited for every child before it
// returned.  A FORGET region waited for none.
//
// satisfies<Required> asks whether the pinned tier covers what a
// consumer demands: stricter satisfies looser.  A JOIN_ALL value is
// admissible wherever CANCEL is required, because a region that
// joined every child also met the weaker promise.
//
// relax<Weaker> moves down the chain and never up.  There is no
// tighten(): a region that already released its children cannot
// afterwards claim it joined them, so the only way to hold a stricter
// tier is to build one where the engagement really happened.  The
// substrate's weaken(), which does move up, is not exposed here.
//
// extract() carries no gate.  The tier is a claim about what the
// producer did, not a restriction on what a consumer may read, so
// reading the value out is sound at every tier.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/JoinPolicyLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// The class template below shadows the lattice enum of the same name
// in this namespace, so the enum is re-exported under a distinct
// alias.
using ::crucible::algebra::lattices::JoinPolicyLattice;
using JoinPolicy_v = ::crucible::algebra::lattices::JoinPolicy;

template <JoinPolicy_v Tier, typename T>
class [[nodiscard]] JoinPolicy {
public:
    using value_type = T;
    using lattice_type = JoinPolicyLattice::At<Tier>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Comonad, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Comonad;

    static constexpr JoinPolicy_v tier = Tier;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a tier no region earned.
    // Deleting it would be the truthful choice, but it is kept so the
    // wrapper can sit in an array element or a default-initialized
    // struct field.  A site that performed the engagement uses the
    // explicit constructor or the mint below.
    constexpr JoinPolicy() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit JoinPolicy(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit JoinPolicy(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                            && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    // Copying is permitted.  The tier records what the producer did,
    // and a copy of the value inherits that same history, so nothing
    // is weakened by duplicating it.
    constexpr JoinPolicy(const JoinPolicy&) = default;
    constexpr JoinPolicy(JoinPolicy&&) = default;
    constexpr JoinPolicy& operator=(const JoinPolicy&) = default;
    constexpr JoinPolicy& operator=(JoinPolicy&&) = default;
    ~JoinPolicy() = default;

    [[nodiscard]] friend constexpr bool operator==(JoinPolicy const& a,
                                                   JoinPolicy const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    // Mutation cannot break the pin.  The tier records how the value
    // came to be, not what its bytes hold now.
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    [[nodiscard]] constexpr T extract() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).extract();
    }

    constexpr void swap(JoinPolicy& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(JoinPolicy& a, JoinPolicy& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <JoinPolicy_v RequiredTier>
    static constexpr bool satisfies = JoinPolicyLattice::leq(RequiredTier, Tier);

    template <JoinPolicy_v WeakerTier>
        requires(JoinPolicyLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr JoinPolicy<WeakerTier, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return JoinPolicy<WeakerTier, T>{this->peek()};
    }

    template <JoinPolicy_v WeakerTier>
        requires(JoinPolicyLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr JoinPolicy<WeakerTier, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return JoinPolicy<WeakerTier, T>{std::move(impl_).consume()};
    }
};

// This factory does the same work as the in-place constructor.  It
// exists so that every site claiming a tier is findable by searching
// for the mint_ prefix, and because the claim is sound only where the
// producer really performed the engagement.  Nothing here can check
// that, so the discipline lives at the call site.
template <JoinPolicy_v Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr JoinPolicy<Tier, T>
mint_join_policy(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return JoinPolicy<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

namespace join_policy_tier {
template <typename T>
using Forget = JoinPolicy<JoinPolicy_v::FORGET, T>;
template <typename T>
using Detach = JoinPolicy<JoinPolicy_v::DETACH, T>;
template <typename T>
using Abandon = JoinPolicy<JoinPolicy_v::ABANDON, T>;
template <typename T>
using Cancel = JoinPolicy<JoinPolicy_v::CANCEL, T>;
template <typename T>
using WaitDeadline = JoinPolicy<JoinPolicy_v::WAIT_DEADLINE, T>;
template <typename T>
using JoinAll = JoinPolicy<JoinPolicy_v::JOIN_ALL, T>;
}  // namespace join_policy_tier

namespace detail::join_policy_layout {

template <typename T>
using JoinAllJP = JoinPolicy<JoinPolicy_v::JOIN_ALL, T>;
template <typename T>
using WaitDeadlineJP = JoinPolicy<JoinPolicy_v::WAIT_DEADLINE, T>;
template <typename T>
using CancelJP = JoinPolicy<JoinPolicy_v::CANCEL, T>;
template <typename T>
using AbandonJP = JoinPolicy<JoinPolicy_v::ABANDON, T>;
template <typename T>
using DetachJP = JoinPolicy<JoinPolicy_v::DETACH, T>;
template <typename T>
using ForgetJP = JoinPolicy<JoinPolicy_v::FORGET, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllJP, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllJP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllJP, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WaitDeadlineJP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CancelJP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CancelJP, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbandonJP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(DetachJP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ForgetJP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ForgetJP, double);

}  // namespace detail::join_policy_layout

static_assert(sizeof(JoinPolicy<JoinPolicy_v::FORGET, int>) == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::DETACH, int>) == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::ABANDON, int>) == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::CANCEL, int>) == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::WAIT_DEADLINE, int>) == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::JOIN_ALL, int>) == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::JOIN_ALL, double>) == sizeof(double));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::JOIN_ALL, char>) == sizeof(char));

namespace detail::join_policy_self_test {

using JoinAllInt = JoinPolicy<JoinPolicy_v::JOIN_ALL, int>;
using WaitDeadlineInt = JoinPolicy<JoinPolicy_v::WAIT_DEADLINE, int>;
using CancelInt = JoinPolicy<JoinPolicy_v::CANCEL, int>;
using AbandonInt = JoinPolicy<JoinPolicy_v::ABANDON, int>;
using DetachInt = JoinPolicy<JoinPolicy_v::DETACH, int>;
using ForgetInt = JoinPolicy<JoinPolicy_v::FORGET, int>;

inline constexpr JoinAllInt jp_default{};
static_assert(jp_default.peek() == 0);
static_assert(jp_default.tier == JoinPolicy_v::JOIN_ALL);

inline constexpr JoinAllInt jp_explicit{42};
static_assert(jp_explicit.peek() == 42);

static_assert(JoinAllInt::tier == JoinPolicy_v::JOIN_ALL);
static_assert(WaitDeadlineInt::tier == JoinPolicy_v::WAIT_DEADLINE);
static_assert(CancelInt::tier == JoinPolicy_v::CANCEL);
static_assert(AbandonInt::tier == JoinPolicy_v::ABANDON);
static_assert(DetachInt::tier == JoinPolicy_v::DETACH);
static_assert(ForgetInt::tier == JoinPolicy_v::FORGET);

static_assert(JoinAllInt::satisfies<JoinPolicy_v::JOIN_ALL>);
static_assert(JoinAllInt::satisfies<JoinPolicy_v::WAIT_DEADLINE>);
static_assert(JoinAllInt::satisfies<JoinPolicy_v::CANCEL>);
static_assert(JoinAllInt::satisfies<JoinPolicy_v::ABANDON>);
static_assert(JoinAllInt::satisfies<JoinPolicy_v::DETACH>);
static_assert(JoinAllInt::satisfies<JoinPolicy_v::FORGET>);

static_assert(CancelInt::satisfies<JoinPolicy_v::CANCEL>);
static_assert(CancelInt::satisfies<JoinPolicy_v::ABANDON>);
static_assert(CancelInt::satisfies<JoinPolicy_v::DETACH>);
static_assert(CancelInt::satisfies<JoinPolicy_v::FORGET>);
static_assert(!CancelInt::satisfies<JoinPolicy_v::WAIT_DEADLINE>);
static_assert(!CancelInt::satisfies<JoinPolicy_v::JOIN_ALL>);

static_assert(ForgetInt::satisfies<JoinPolicy_v::FORGET>);
static_assert(!ForgetInt::satisfies<JoinPolicy_v::DETACH>);
static_assert(!ForgetInt::satisfies<JoinPolicy_v::CANCEL>);
static_assert(!ForgetInt::satisfies<JoinPolicy_v::JOIN_ALL>);

inline constexpr auto from_join_all_to_cancel = JoinAllInt{42}.relax<JoinPolicy_v::CANCEL>();
static_assert(from_join_all_to_cancel.peek() == 42);
static_assert(from_join_all_to_cancel.tier == JoinPolicy_v::CANCEL);

inline constexpr auto from_join_all_to_forget = JoinAllInt{99}.relax<JoinPolicy_v::FORGET>();
static_assert(from_join_all_to_forget.peek() == 99);
static_assert(from_join_all_to_forget.tier == JoinPolicy_v::FORGET);

inline constexpr auto from_wait_to_cancel = WaitDeadlineInt{7}.relax<JoinPolicy_v::CANCEL>();
static_assert(from_wait_to_cancel.peek() == 7);
static_assert(from_wait_to_cancel.tier == JoinPolicy_v::CANCEL);

inline constexpr auto from_cancel_to_abandon = CancelInt{55}.relax<JoinPolicy_v::ABANDON>();
static_assert(from_cancel_to_abandon.peek() == 55);
static_assert(from_cancel_to_abandon.tier == JoinPolicy_v::ABANDON);

inline constexpr auto identity_relax = JoinAllInt{100}.relax<JoinPolicy_v::JOIN_ALL>();
static_assert(identity_relax.peek() == 100);
static_assert(identity_relax.tier == JoinPolicy_v::JOIN_ALL);

inline constexpr auto minted_join_all = mint_join_policy<JoinPolicy_v::JOIN_ALL, int>(123);
static_assert(minted_join_all.peek() == 123);
static_assert(minted_join_all.tier == JoinPolicy_v::JOIN_ALL);

static_assert(std::is_same_v<join_policy_tier::JoinAll<int>, JoinAllInt>);
static_assert(std::is_same_v<join_policy_tier::WaitDeadline<int>, WaitDeadlineInt>);
static_assert(std::is_same_v<join_policy_tier::Cancel<int>, CancelInt>);
static_assert(std::is_same_v<join_policy_tier::Abandon<int>, AbandonInt>);
static_assert(std::is_same_v<join_policy_tier::Detach<int>, DetachInt>);
static_assert(std::is_same_v<join_policy_tier::Forget<int>, ForgetInt>);

static_assert(JoinAllInt{42} == JoinAllInt{42});
static_assert(!(JoinAllInt{42} == JoinAllInt{43}));

static_assert(std::is_copy_constructible_v<JoinAllInt>);
static_assert(std::is_copy_assignable_v<JoinAllInt>);
static_assert(std::is_move_constructible_v<JoinAllInt>);
static_assert(std::is_move_assignable_v<JoinAllInt>);

static_assert(JoinAllInt::modality == ::crucible::algebra::ModalityKind::Comonad);

// The arguments below are runtime values so that the operations are
// exercised outside constant evaluation as well.
inline void runtime_smoke_test() {
    int seed = 17;

    JoinAllInt jp{seed * 2};
    if (jp.peek() != 34) std::abort();
    if (jp.tier != JoinPolicy_v::JOIN_ALL) std::abort();

    jp.peek_mut() = 99;
    if (jp.peek() != 99) std::abort();

    JoinAllInt e{seed * 3};
    int extracted = std::move(e).extract();
    if (extracted != 51) std::abort();

    JoinAllInt source{seed * 4};
    auto relaxed = std::move(source).relax<JoinPolicy_v::CANCEL>();
    if (relaxed.peek() != 68) std::abort();
    if (relaxed.tier != JoinPolicy_v::CANCEL) std::abort();

    JoinAllInt full{seed};
    auto step1 = std::move(full).relax<JoinPolicy_v::WAIT_DEADLINE>();
    auto step2 = std::move(step1).relax<JoinPolicy_v::CANCEL>();
    auto step3 = std::move(step2).relax<JoinPolicy_v::ABANDON>();
    auto step4 = std::move(step3).relax<JoinPolicy_v::DETACH>();
    auto step5 = std::move(step4).relax<JoinPolicy_v::FORGET>();
    if (step5.peek() != 17) std::abort();
    if (step5.tier != JoinPolicy_v::FORGET) std::abort();

    auto m = mint_join_policy<JoinPolicy_v::CANCEL, int>(seed);
    int m_out = std::move(m).extract();
    if (m_out != 17) std::abort();

    JoinAllInt c1{seed};
    JoinAllInt c2 = c1;
    if (c1.peek() != c2.peek()) std::abort();
    if (c1.peek() != 17) std::abort();

    JoinAllInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();
}

}  // namespace detail::join_policy_self_test

}  // namespace crucible::safety
