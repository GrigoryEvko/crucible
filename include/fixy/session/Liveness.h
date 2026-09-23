#pragma once

// Liveness of one multiparty session, by construction.
//
// A typing context is live when, on each fair path, each message in a
// queue is eventually received and each role that waits for a message
// eventually receives one (Pischke, Masters and Yoshida, "Asynchronous
// Global Protocols, Precisely", arXiv 2505.17676 version 4, Definition
// 12).  Theorem 13 of that paper: a context associated with a
// well-formed, balanced+ global type is safe, deadlock-free and live.
// This header therefore does not walk local types to find liveness.  It
// asks the global type for the three conditions that give it:
//
//   - The global type is well-formed (fixy/session/Global.h).
//   - The global type is balanced+ (Definitions 13 to 17).
//   - The global type projects onto each of its roles
//     (fixy/session/Projection.h).
//
// A context that associates with such a global type is live.  The
// projection of this tree is a subset of the coinductive projection of
// Definition 4, and its association uses synchronous subtyping in place
// of precise asynchronous subtyping, which is a subset of Definition 21.
// So each verdict "live" here is a verdict of Theorem 13.  The
// synchronous analogue is Theorem 5.8 of Keskin, Yoshida and van
// Glabbeek, "Formally Verified Liveness with Multiparty Session Types in
// Rocq" (ITP 2026), which is mechanised.
//
// Scope.  The result covers ONE session.  Two sessions that each are
// live can deadlock when a role waits in one session for a peer that
// waits in the other.  Freedom from that deadlock needs an acyclic
// ownership of channels (a channel is made with the peer that holds the
// other end, as in LinearActris, POPL 2024) or priorities.  It also
// needs each handle to reach End or to cancel: a handle that is dropped
// before End breaks the liveness of its peers.  Those conditions belong
// to the handle, not to the global type.
//
// Crash.  The paper has no crash, and is_live_by_construction refuses
// each crash branch, because project_t refuses it.  For a protocol with
// crash branches, use crash_live_by_construction_v<G, ReliableSet<...>>
// and ensure_crash_live_by_construction in fixy/session/Projection.h:
// balanced+, no runtime construct, and a crash-stop projection onto each
// role (Theorem 4.31 of Barwell, Hou, Yoshida and Zhou, LMCS 2025).  The
// fair path of that paper needs one more clause than the fair path
// here: a crash detection that stays enabled eventually occurs.
// Without that clause a path can ignore a crash for ever and still
// count as fair.
//
// Runtime monitors.  A monitor that checks liveness on a recorded run
// checks the obligations of Definition 12 on that run.  A path is fair
// when:
//
//   F1  each send that stays enabled for role p to role q is followed by
//       a send from p to q (the label can differ);
//   F2  each receive that stays enabled for p from q with label m is
//       followed by that receive.
//
// A fair path is live when:
//
//   L1  each message from p to q in a queue is followed by a receive of
//       q from p with its label;
//   L2  each role p that waits on an external choice from q is followed
//       by a receive of p from q.
//
// The synchronous form of the same obligations is Definition 5.4 of
// Keskin, Yoshida and van Glabbeek, where one label (p, q) stands for a
// send and its receive together.  On a finite run a monitor can report
// only the obligations that are still open at the end, because
// "eventually" has no witness on a prefix.
//
// Principal global types.  Pischke and Yoshida, "Top-down = Bottom-up"
// (OOPSLA 2026), Theorem 6.21, build from each safe and live
// synchronous context a balanced global type that associates with it
// and has the smallest set of traces.  The construction explores the
// reachable contexts with a record of visited states.  The count of
// states is exponential in the size of the context, and the input
// condition, liveness of the context, is PSPACE-complete to decide in
// the synchronous case and undecidable in the asynchronous case.  This
// header does not implement it.  A protocol author writes the global
// type, and the chain above checks it.

#include <fixy/session/Global.h>
#include <fixy/session/Projection.h>
#include <foundation/contracts/Armed.h>

#include <type_traits>

namespace fixy::session {

namespace detail::live {

template <typename...>
inline constexpr bool dependent_false_v = false;

template <typename G, typename RL>
struct each_role_projects;
template <typename G, typename... Rs>
struct each_role_projects<G, global::Roles<Rs...>> : std::bool_constant<(projects_v<G, Rs> && ...)> {};

// The first role of RL onto which G does not project, or void.
template <typename G, typename RL>
struct first_unprojectable {
    using type = void;
};
template <typename G, typename R, typename... Rest>
struct first_unprojectable<G, global::Roles<R, Rest...>> {
    using type =
        std::conditional_t<projects_v<G, R>, typename first_unprojectable<G, global::Roles<Rest...>>::type, R>;
};

}  // namespace detail::live

template <typename G>
struct is_live_by_construction : std::bool_constant<[] {
    if constexpr (global::is_balanced_plus_v<G>) {
        return detail::live::each_role_projects<G, global::roles_t<G>>::value;
    } else {
        return false;
    }
}()> {};

template <typename G>
inline constexpr bool is_live_by_construction_v = is_live_by_construction<G>::value;

// A context that associates with a global type that is live by
// construction is live (Theorem 13).
template <typename Ctx, typename G>
inline constexpr bool context_is_live_v = [] {
    if constexpr (is_live_by_construction_v<G>) {
        return association_holds_v<Ctx, G>;
    } else {
        return false;
    }
}();

template <typename G>
consteval void ensure_live_by_construction() noexcept {
    global::ensure_balanced_plus<G>();
    if constexpr (global::is_balanced_plus_v<G>) {
        using role = typename detail::live::first_unprojectable<G, global::roles_t<G>>::type;
        if constexpr (!std::is_void_v<role>) {
            ensure_projectable<G, role>();
        }
    }
}

template <typename Ctx, typename G>
consteval void ensure_context_live() noexcept {
    ensure_live_by_construction<G>();
    if constexpr (is_live_by_construction_v<G>) {
        ensure_associated<Ctx, G>();
    }
}

namespace detail::live::witness {

using global::detail::witness::RoleA;
using global::detail::witness::RoleB;
using global::detail::witness::RoleC;
using global::detail::witness::LabelX;
using global::detail::witness::LabelY;

// Balanced, but RoleC cannot tell which label RoleA chose, and it must
// send a different label in each branch.
using UnprojectableForC = global::Comm<RoleA, RoleB, global::Branch<LabelX, int, global::Msg<RoleC, RoleA, LabelX, int, global::End>>,
                                       global::Branch<LabelY, int, global::Msg<RoleC, RoleA, LabelY, int, global::End>>>;

}  // namespace detail::live::witness

}  // namespace fixy::session

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_live_by_construction> {
    using accepts = witnesses<::fixy::session::global::End, ::fixy::session::global::detail::witness::Once,
                              ::fixy::session::global::detail::witness::Forever,
                              ::fixy::session::global::detail::witness::SentOnce>;
    using refuses = witnesses<int, ::fixy::session::global::detail::witness::Starves,
                              ::fixy::session::global::detail::witness::SentEachLoop,
                              ::fixy::session::detail::live::witness::UnprojectableForC>;
};
