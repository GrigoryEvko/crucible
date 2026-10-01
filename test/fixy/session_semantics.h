#pragma once

// The shared part of test_session_semantics: the roles, the labels, the
// label sets, the global types and the local types that more than one group
// reads, the walk of operational correspondence, and the doors of the walks
// that main runs again.  test_session_semantics.cpp lists the source files
// of the test.

#include <fixy/Tagged.h>
#include <fixy/session/Semantics.h>

#include <type_traits>

namespace test_session_semantics {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct R {};
struct S {};

struct Add {};
struct Sub {};
struct M {};
struct M1 {};
struct M2 {};
struct K {};
struct L1 {};
struct L2 {};

using Reliable = s::EveryRoleReliable;
using Crash = g::CrashLabel;

template <typename G>
using Start = g::State<g::Roles<>, G>;

// ── Label sets ───────────────────────────────────────────────────────

template <typename List, typename A>
inline constexpr bool holds_v = false;
template <typename... As, typename A>
inline constexpr bool holds_v<g::Actions<As...>, A> = (std::is_same_v<As, A> || ...);

template <typename A, typename B>
inline constexpr bool includes_v = false;
template <typename A, typename... Bs>
inline constexpr bool includes_v<A, g::Actions<Bs...>> = (holds_v<A, Bs> && ...);

// Two label lists hold the same labels, in any order.
template <typename A, typename B>
inline constexpr bool same_labels_v = includes_v<A, B> && includes_v<B, A>;

// One message, and two messages between two pairs of roles that share no
// role.  The rules of the global types check them, and the walk reads them.
using Once = g::Msg<P, Q, M, int, g::End>;
using Independent = g::Msg<P, Q, M, int, g::Msg<R, S, M1, int, g::End>>;

// One send and one receive of M.  The configuration rules check them, and
// the self-attack reads them.
using SendM = s::Send<s::PeerMsg<Q, M, int>, s::End>;
using RecvM = s::Recv<s::PeerMsg<P, M, int>, s::End>;

// ── Operational correspondence ───────────────────────────────────────

struct Tally {
    int states = 0;
    int labels = 0;
    int enabled_mismatches = 0;
    int unassociated = 0;

    constexpr Tally operator+(Tally other) const noexcept {
        return {states + other.states, labels + other.labels, enabled_mismatches + other.enabled_mismatches,
                unassociated + other.unassociated};
    }
};

template <typename Ctx, typename G, int Depth>
constexpr Tally explore() noexcept;

// One label of G: the context takes it too, and the result stays
// associated.  Complexity: one projection of G per role, per label.
template <typename Ctx, typename G, int Depth, typename A>
constexpr Tally explore_label() noexcept {
    using next_state = g::state_step_t<Start<G>, A, Reliable>;
    using next_ctx = c::step_t<Ctx, A, Reliable>;
    Tally tally{.labels = 1};
    if constexpr (std::is_same_v<next_state, g::NoTransition> || std::is_same_v<next_ctx, g::NoTransition>) {
        tally.unassociated = 1;
    } else {
        if (!s::association_holds_v<next_ctx, typename next_state::type>) tally.unassociated = 1;
        if constexpr (Depth > 0) tally = tally + explore<next_ctx, typename next_state::type, Depth - 1>();
    }
    return tally;
}

template <typename Ctx, typename G, int Depth, typename... As>
constexpr Tally explore_each(g::Actions<As...>*) noexcept {
    return (Tally{} + ... + explore_label<Ctx, G, Depth, As>());
}

// Complexity: the labels of each state, to the power of Depth.  The
// compiler keeps one instance per (context, type, depth), so a state
// that two paths reach costs once per depth.
template <typename Ctx, typename G, int Depth>
constexpr Tally explore() noexcept {
    using global_labels = g::state_enabled_t<Start<G>, Reliable>;
    using context_labels = c::enabled_t<Ctx, Reliable>;
    Tally tally{.states = 1};
    if (!same_labels_v<global_labels, context_labels>) tally.enabled_mismatches = 1;
    return tally + explore_each<Ctx, G, Depth>(static_cast<global_labels*>(nullptr));
}

template <typename G, int Depth>
constexpr Tally explore_projected() noexcept {
    return explore<s::projected_context_t<G>, G, Depth>();
}

constexpr bool is_clean(Tally tally) noexcept {
    return tally.states > 1 && tally.enabled_mismatches == 0 && tally.unassociated == 0;
}

// The runtime half evaluates a walk through a function pointer, so the
// compiler cannot fold the call into the constant of the walk.  The source
// file that holds the global type gives the walk and the constant.
using Walk = Tally (*)() noexcept;

// The ring and the ping pong of the corpus (test_session_semantics_corpus.cpp).
Walk ring_walk() noexcept;
Tally ring_tally() noexcept;
Walk ping_pong_walk() noexcept;
Tally ping_pong_tally() noexcept;

// The sender that acts before its message arrives
// (test_session_semantics_sender.cpp).
Walk sender_goes_on_walk() noexcept;
Tally sender_goes_on_tally() noexcept;

}  // namespace test_session_semantics
