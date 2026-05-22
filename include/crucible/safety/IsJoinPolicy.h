#pragma once

// FIXY-V-079 — wrapper-detection predicate for `JoinPolicy<Tier, T>`.
// Mirrors the IsWait.h / IsCrash.h / IsProgress.h pattern: a partial-
// specialization sniffs the wrapper, exposes the inner value type and
// the pinned tier as alias members, then the variable template +
// concept lift the test to the type system.
//
// Use:
//   template <typename Result>
//       requires ::crucible::safety::extract::IsJoinPolicy<Result>
//   void consume_engaged(Result&& r) noexcept {
//       static_assert(::crucible::safety::extract::join_policy_tier_v<Result>
//                     == JoinPolicy_v::JOIN_ALL,
//           "only fully-joined results admitted here");
//       auto value = std::forward<Result>(r).extract();
//       /* ... */
//   }

#include <crucible/safety/JoinPolicy.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::JoinPolicy_v;

namespace detail {

template <typename T>
struct is_join_policy_impl : std::false_type {
    using value_type = void;
};

template <JoinPolicy_v Tier, typename U>
struct is_join_policy_impl<::crucible::safety::JoinPolicy<Tier, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr JoinPolicy_v tier = Tier;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_join_policy_v =
    detail::is_join_policy_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsJoinPolicy = is_join_policy_v<T>;

template <typename T>
    requires is_join_policy_v<T>
using join_policy_value_t =
    typename detail::is_join_policy_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_join_policy_v<T>
inline constexpr JoinPolicy_v join_policy_tier_v =
    detail::is_join_policy_impl<std::remove_cvref_t<T>>::tier;

namespace detail::is_join_policy_self_test {

using JP_int_join_all      = ::crucible::safety::JoinPolicy<JoinPolicy_v::JOIN_ALL,      int>;
using JP_int_cancel        = ::crucible::safety::JoinPolicy<JoinPolicy_v::CANCEL,        int>;
using JP_int_forget        = ::crucible::safety::JoinPolicy<JoinPolicy_v::FORGET,        int>;
using JP_double_wait       = ::crucible::safety::JoinPolicy<JoinPolicy_v::WAIT_DEADLINE, double>;

// ── Positive — variable template ───────────────────────────────────
static_assert(is_join_policy_v<JP_int_join_all>);
static_assert(is_join_policy_v<JP_int_cancel>);
static_assert(is_join_policy_v<JP_int_forget>);
static_assert(is_join_policy_v<JP_double_wait>);

// Reference/cv handling — the strip_cvref_t pass-through catches every
// passed-by-reference / passed-by-const-reference path.
static_assert(is_join_policy_v<JP_int_join_all&>);
static_assert(is_join_policy_v<JP_int_join_all const&>);
static_assert(is_join_policy_v<JP_int_join_all&&>);
static_assert(is_join_policy_v<JP_int_join_all const>);
static_assert(is_join_policy_v<JP_int_join_all volatile>);

// ── Negative — bare types and lookalikes ───────────────────────────
static_assert(!is_join_policy_v<int>);
static_assert(!is_join_policy_v<int*>);
static_assert(!is_join_policy_v<JP_int_join_all*>);

struct LookalikeJoinPolicy { int v; JoinPolicy_v t; };
static_assert(!is_join_policy_v<LookalikeJoinPolicy>);

// ── Positive — concept form ────────────────────────────────────────
static_assert(IsJoinPolicy<JP_int_join_all>);
static_assert(IsJoinPolicy<JP_int_cancel&>);
static_assert(IsJoinPolicy<JP_double_wait&&>);
static_assert(!IsJoinPolicy<int>);

// ── Value-type extraction ──────────────────────────────────────────
static_assert(std::is_same_v<join_policy_value_t<JP_int_join_all>,   int>);
static_assert(std::is_same_v<join_policy_value_t<JP_int_cancel&>,    int>);
static_assert(std::is_same_v<join_policy_value_t<JP_double_wait>,    double>);

// ── Tier extraction ────────────────────────────────────────────────
static_assert(join_policy_tier_v<JP_int_join_all>     == JoinPolicy_v::JOIN_ALL);
static_assert(join_policy_tier_v<JP_int_cancel>       == JoinPolicy_v::CANCEL);
static_assert(join_policy_tier_v<JP_int_forget>       == JoinPolicy_v::FORGET);
static_assert(join_policy_tier_v<JP_double_wait>      == JoinPolicy_v::WAIT_DEADLINE);
static_assert(join_policy_tier_v<JP_int_join_all&>    == JoinPolicy_v::JOIN_ALL);

}  // namespace detail::is_join_policy_self_test

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Forces the variable template + concept through a non-constant-
// expression evaluation context.  Mirrors IsWait.h's smoke harness.
inline bool is_join_policy_smoke_test() noexcept {
    using namespace detail::is_join_policy_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_join_policy_v<JP_int_join_all>;
        ok = ok && !is_join_policy_v<int>;
        ok = ok && IsJoinPolicy<JP_int_join_all&&>;
        ok = ok && (join_policy_tier_v<JP_int_cancel> == JoinPolicy_v::CANCEL);
        ok = ok && (join_policy_tier_v<JP_int_forget> == JoinPolicy_v::FORGET);
    }
    return ok;
}

}  // namespace crucible::safety::extract
