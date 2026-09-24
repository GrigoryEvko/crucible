#pragma once

// Association of a configuration with a global type under crash-stop
// failures: Definition 4.19 of Barwell, Hou, Yoshida and Zhou,
// "Crash-Stop Failures in Asynchronous Multiparty Session Types" (LMCS
// 21:2, 2025).  A configuration Γ; ∆ is associated with ⟨C; G⟩ for the
// reliable roles R when:
//
//   (A1) each live role of G (active_roles_t, roles(G) of the paper) has
//        an entry, and its local type refines the crash-stop projection
//        of G onto it (Definition 4.3, is_subtype_sync_v);
//   (A2) the entries at Stop are exactly the roles of C;
//   (A3) each other entry is at End;
//   (A4) the queues hold what G has en route: under End or a Rec every
//        queue is empty; under p → q† and under a crash pseudo-message the
//        queue from p to a live q is empty; under p† ⇝ q : j, with m_j no
//        crash, the queue from p to q starts with m_j.  Each branch must
//        match the queues, the head m_j taken away.
//
// A queue is the outgoing queue of its sender (fixy/session/Projection.h),
// so ∆(p, q) is the messages in the queue of p whose receiver is q.  The
// unavailable queue ⊘ of the paper is the queue to a role at Stop: rule
// [Γ-↯] of fixy/session/Semantics.h drops each message to a role that
// crashes, and a later send to it is dropped too.  So clause (i) of A4, a
// queue to a crashed role is unavailable, holds when no queue holds a
// message to a role of C.  The walk of A4 asks for empty queues at each
// End and each Rec, so a message to a crashed role fails the walk.
//
// Three checks here are stricter than the paper:
//
//   - G is balanced+ (Pischke, Masters and Yoshida, Definition 17), as
//     association_holds_v of fixy/session/Projection.h asks.  A context can
//     match each projection of a type that is not balanced+ and still be
//     unsafe.
//   - ⟨C; G⟩ is well-annotated for R (Definition 4.15): no reliable role is
//     in C, each role with the crash annotation is in C, and no role of C
//     is live in G.
//   - A queued message has the payload of its branch exactly, as
//     queues_equivalent_v asks.  The paper has no payload subtyping, so
//     there the payloads are equal too.
//
// For EveryRoleReliable no role crashes, C must be empty, and A1 uses the
// projection of project_t.
//
// Complexity: one projection of G per live role, one subtype check per
// live role, and one walk of G for A4.

#include <fixy/session/Crash.h>
#include <fixy/session/Global.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Semantics.h>
#include <fixy/session/Subtype.h>
#include <foundation/contracts/Armed.h>

#include <cstdint>
#include <type_traits>

namespace fixy::session::config {

// The question "is the configuration Ctx associated with the state S for
// the reliable roles Reliable".  It is a type, so that the predicate
// takes one argument and can hold an armed cell.
template <typename Ctx, typename S, typename Reliable>
struct CrashAssociation {};

enum class CrashAssociationFault : std::uint8_t {
    None,
    NotWellAnnotated,
    NotBalancedPlus,
    CrashedRoleMismatch,
    LiveRoleMismatch,
    UnfinishedRole,
    QueueMismatch,
};

namespace detail::crash_assoc {

namespace g = ::fixy::session::global;
namespace proj = ::fixy::session::detail::proj;

template <typename...>
inline constexpr bool dependent_false_v = false;

// The reliable roles as a role list, for is_well_annotated_v.
template <typename Reliable, typename G>
struct reliable_roles;
template <typename G>
struct reliable_roles<EveryRoleReliable, G> {
    using type = g::roles_t<G>;
};
template <typename... Rs, typename G>
struct reliable_roles<ReliableSet<Rs...>, G> {
    using type = g::Roles<Rs...>;
};

template <typename Reliable, typename Role>
inline constexpr bool reliable_v = proj::is_reliable_role_v<Reliable, Role>;

template <typename Ctx, typename R>
using entry_t = typename ::fixy::session::config::detail::entry_of<Ctx, R>::type;

// Definition 4.15, with WA2 read against C.
template <typename C, typename G, typename Reliable>
struct annotation_check;
template <typename... Cr, typename G, typename Reliable>
struct annotation_check<g::Roles<Cr...>, G, Reliable> {
    template <typename... As>
    static consteval bool annotated_in_crashed(g::Roles<As...>*) noexcept {
        return (g::role_in_v<As, g::Roles<Cr...>> && ...);
    }
    static constexpr bool is_satisfied =
        g::is_well_annotated_v<G, typename reliable_roles<Reliable, G>::type>
        && annotated_in_crashed(static_cast<g::crashed_roles_t<G>*>(nullptr))
        && (!reliable_v<Reliable, Cr> && ...) && (!g::role_in_v<Cr, g::active_roles_t<G>> && ...);
};

// A2: each role of C is at Stop, and each entry at Stop is in C.
template <typename Ctx, typename C>
struct crashed_check;
template <typename... Es, typename... Cr>
struct crashed_check<TypingContext<Es...>, g::Roles<Cr...>> {
    template <typename R>
    static consteval bool is_stopped() noexcept {
        using entry = entry_t<TypingContext<Es...>, R>;
        if constexpr (std::is_void_v<entry>) {
            return false;
        } else {
            return std::is_same_v<typename entry::local, Stop>;
        }
    }
    static constexpr bool is_satisfied =
        (is_stopped<Cr>() && ...)
        && ((!std::is_same_v<typename Es::local, Stop> || g::role_in_v<typename Es::role, g::Roles<Cr...>>) && ...);
};

// A1 for one live role.
template <typename Ctx, typename G, typename Reliable, typename R>
consteval bool live_role_refines() noexcept {
    using entry = entry_t<Ctx, R>;
    if constexpr (std::is_void_v<entry>) {
        return false;
    } else {
        using projected = typename decltype(proj::project_under<G, R, Reliable>())::type;
        if constexpr (is_projection_failure_v<projected>) {
            return false;
        } else {
            return !std::is_same_v<typename entry::local, Stop>
                   && is_subtype_sync_v<typename entry::local, typename projected::local>;
        }
    }
}

template <typename Ctx, typename G, typename Reliable, typename Live>
struct live_check;
template <typename Ctx, typename G, typename Reliable, typename... Rs>
struct live_check<Ctx, G, Reliable, g::Roles<Rs...>> {
    static constexpr bool is_satisfied = (live_role_refines<Ctx, G, Reliable, Rs>() && ...);
};

// A3: an entry that is neither live nor crashed is at End.
template <typename Ctx, typename Live, typename C>
struct finished_check;
template <typename... Es, typename Live, typename C>
struct finished_check<TypingContext<Es...>, Live, C> {
    static constexpr bool is_satisfied =
        ((g::role_in_v<typename Es::role, Live> || g::role_in_v<typename Es::role, C>
          || std::is_same_v<typename Es::local, End>)
         && ...);
};

// ── A4: the queues ───────────────────────────────────────────────────

template <typename Ctx>
inline constexpr bool every_queue_empty_v = false;
template <typename... Es>
inline constexpr bool every_queue_empty_v<TypingContext<Es...>> = (std::is_same_v<typename Es::queue, OutQueue<>> && ...);

// True when the queue of From holds no message to To.
template <typename Ctx, typename From, typename To>
inline constexpr bool no_message_v =
    std::is_void_v<typename ::fixy::session::config::detail::message_from<Ctx, To, From>::type>;

// Ctx with the first message of From to To taken away.
template <typename Ctx, typename From, typename To>
struct take_message;
template <typename... Es, typename From, typename To>
struct take_message<TypingContext<Es...>, From, To> {
    template <typename E>
    using image = std::conditional_t<
        std::is_same_v<typename E::role, From>,
        RoleState<From, typename ::fixy::session::config::detail::drop_head_for<typename E::queue, To>::type,
                  typename E::local>,
        E>;
    using type = TypingContext<image<Es>...>;
};

// The walk of A4.  A Var is never reached, because the walk stops at
// each Rec, so the primary refuses what no clause names.
template <typename Ctx, typename C, typename G>
struct queue_walk {
    static constexpr bool is_matched = false;
};
template <typename Ctx, typename C>
struct queue_walk<Ctx, C, g::End> {
    static constexpr bool is_matched = every_queue_empty_v<Ctx>;
};
template <typename Ctx, typename C, typename B>
struct queue_walk<Ctx, C, g::Rec<B>> {
    static constexpr bool is_matched = every_queue_empty_v<Ctx>;
};
template <typename Ctx, typename C, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!g::detail::is_crashed_v<To>)
struct queue_walk<Ctx, C, g::Comm<From, To, g::Branch<Ls, Ps, Cs>...>> {
    static constexpr bool is_matched = no_message_v<Ctx, From, To> && (queue_walk<Ctx, C, Cs>::is_matched && ...);
};
template <typename Ctx, typename C, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct queue_walk<Ctx, C, g::Comm<From, g::Crashed<To>, g::Branch<Ls, Ps, Cs>...>> {
    static constexpr bool is_matched = (queue_walk<Ctx, C, Cs>::is_matched && ...);
};
template <typename Ctx, typename C, typename From, typename To, typename Chosen, typename... Ls, typename... Ps,
          typename... Cs>
struct queue_walk<Ctx, C, g::EnRouteChoice<From, To, Chosen, g::Branch<Ls, Ps, Cs>...>> {
    using sender = g::detail::bare_role_t<From>;
    static consteval bool compute() noexcept {
        if constexpr (g::detail::is_crash_label_v<Chosen>) {
            // (iii): the pseudo-message is in no queue.
            return (g::role_in_v<To, C> || no_message_v<Ctx, sender, To>)
                   && (queue_walk<Ctx, C, Cs>::is_matched && ...);
        } else {
            // (iv): the message leads the queue from the sender to To.
            using head = typename ::fixy::session::config::detail::message_from<Ctx, To, sender>::type;
            using expected = Queued<To, Chosen, typename g::detail::payload_of<Chosen, g::Branch<Ls, Ps, Cs>...>::type>;
            if constexpr (!std::is_same_v<head, expected>) {
                return false;
            } else {
                using rest = typename take_message<Ctx, sender, To>::type;
                return (queue_walk<rest, C, Cs>::is_matched && ...);
            }
        }
    }
    static constexpr bool is_matched = compute();
};

template <typename Ctx, typename S, typename Reliable>
consteval CrashAssociationFault fault_of() noexcept {
    using G = typename S::type;
    using C = typename S::crashed;
    using live = g::active_roles_t<G>;
    if constexpr (!annotation_check<C, G, Reliable>::is_satisfied) {
        return CrashAssociationFault::NotWellAnnotated;
    } else if constexpr (!g::is_balanced_plus_v<G>) {
        return CrashAssociationFault::NotBalancedPlus;
    } else if constexpr (!crashed_check<Ctx, C>::is_satisfied) {
        return CrashAssociationFault::CrashedRoleMismatch;
    } else if constexpr (!live_check<Ctx, G, Reliable, live>::is_satisfied) {
        return CrashAssociationFault::LiveRoleMismatch;
    } else if constexpr (!finished_check<Ctx, live, C>::is_satisfied) {
        return CrashAssociationFault::UnfinishedRole;
    } else if constexpr (!queue_walk<Ctx, C, G>::is_matched) {
        return CrashAssociationFault::QueueMismatch;
    } else {
        return CrashAssociationFault::None;
    }
}

// The context that projects G onto each of its roles, or the first
// projection failure.
template <typename G, typename Reliable, typename... Rs>
consteval auto context_of(g::Roles<Rs...>) {
    using failure = typename proj::first_failure<
        typename decltype(proj::project_under<G, Rs, Reliable>())::type...>::type;
    if constexpr (!std::is_void_v<failure>) {
        return std::type_identity<failure>{};
    } else {
        return std::type_identity<TypingContext<
            RoleState<Rs, typename decltype(proj::project_under<G, Rs, Reliable>())::type::queue,
                      typename decltype(proj::project_under<G, Rs, Reliable>())::type::local>...>>{};
    }
}

}  // namespace detail::crash_assoc

// The fault that Definition 4.19 finds, or None.
template <typename Ctx, typename S, typename Reliable>
    requires is_configuration_v<Ctx> && global::is_global_state_v<S> && global::is_reliability_v<Reliable>
inline constexpr CrashAssociationFault crash_association_fault_v =
    detail::crash_assoc::fault_of<Ctx, S, Reliable>();

template <typename Q>
struct is_crash_associated : std::false_type {};
template <typename Ctx, typename S, typename Reliable>
    requires is_configuration_v<Ctx> && global::is_global_state_v<S> && global::is_reliability_v<Reliable>
struct is_crash_associated<CrashAssociation<Ctx, S, Reliable>>
    : std::bool_constant<crash_association_fault_v<Ctx, S, Reliable> == CrashAssociationFault::None> {};

// Γ; ∆ ⊑_R ⟨C; G⟩ (Definition 4.19).
template <typename Ctx, typename S, typename Reliable>
    requires is_configuration_v<Ctx> && global::is_global_state_v<S> && global::is_reliability_v<Reliable>
inline constexpr bool crash_association_holds_v = is_crash_associated<CrashAssociation<Ctx, S, Reliable>>::value;

// The context that projects a global type with no crashed role onto
// each of its roles: the configuration a protocol starts from.  It is a
// NotProjectable when a projection fails.
template <typename G, typename Reliable>
    requires global::is_global_well_formed_v<G> && global::is_reliability_v<Reliable>
             && (global::crashed_roles_t<G>::size == 0)
using crash_projected_context_t =
    typename decltype(detail::crash_assoc::context_of<G, Reliable>(global::roles_t<G>{}))::type;

// The association gate.  Each fault names the clause of Definition 4.19
// that failed.
template <typename Ctx, typename S, typename Reliable>
consteval void ensure_crash_associated() noexcept {
    if constexpr (!is_configuration_v<Ctx> || !global::is_global_state_v<S> || !global::is_reliability_v<Reliable>) {
        static_assert(detail::crash_assoc::dependent_false_v<Ctx, S, Reliable>,
                      "fixy::session::diagnostic [Crash_Association_Malformed]: the arguments must be a "
                      "configuration (is_configuration_v), a state State<Roles<Crashed...>, G> with a "
                      "well-formed G (is_global_state_v), and a ReliableSet or EveryRoleReliable.");
    } else {
        using F = CrashAssociationFault;
        constexpr F fault = crash_association_fault_v<Ctx, S, Reliable>;
        if constexpr (fault == F::NotWellAnnotated) {
            static_assert(detail::crash_assoc::dependent_false_v<Ctx, S, Reliable>,
                          "fixy::session::diagnostic [Crash_Association_Not_Well_Annotated]: the state is not "
                          "well-annotated (Definition 4.15).  No reliable role may crash, each role that carries "
                          "the crash annotation must be in the crashed set, and no crashed role may still act in G.");
        } else if constexpr (fault == F::NotBalancedPlus) {
            static_assert(detail::crash_assoc::dependent_false_v<Ctx, S, Reliable>,
                          "fixy::session::diagnostic [Crash_Association_Not_Balanced_Plus]: the global type is not "
                          "balanced+ (Pischke, Masters, Yoshida, Definition 17).  A context can match each projection "
                          "of such a type and still be unsafe.");
        } else if constexpr (fault == F::CrashedRoleMismatch) {
            static_assert(detail::crash_assoc::dependent_false_v<Ctx, S, Reliable>,
                          "fixy::session::diagnostic [Crash_Association_Crashed_Role_Mismatch]: clause A2 of "
                          "Definition 4.19 fails.  The entries at Stop must be exactly the crashed roles of the "
                          "state.");
        } else if constexpr (fault == F::LiveRoleMismatch) {
            static_assert(detail::crash_assoc::dependent_false_v<Ctx, S, Reliable>,
                          "fixy::session::diagnostic [Crash_Association_Live_Role_Mismatch]: clause A1 of "
                          "Definition 4.19 fails.  A live role of G has no entry, or its local type does not refine "
                          "the crash-stop projection of G onto it (is_subtype_sync_v).");
        } else if constexpr (fault == F::UnfinishedRole) {
            static_assert(detail::crash_assoc::dependent_false_v<Ctx, S, Reliable>,
                          "fixy::session::diagnostic [Crash_Association_Unfinished_Role]: clause A3 of Definition "
                          "4.19 fails.  A role that is neither live in G nor crashed must be at End.");
        } else if constexpr (fault == F::QueueMismatch) {
            static_assert(detail::crash_assoc::dependent_false_v<Ctx, S, Reliable>,
                          "fixy::session::diagnostic [Crash_Association_Queue_Mismatch]: clause A4 of Definition "
                          "4.19 fails.  A queue holds a message that G does not have en route, misses one that G "
                          "has, or holds a message to a crashed role.");
        }
    }
}

}  // namespace fixy::session::config

// ── Armed cells ──────────────────────────────────────────────────────

namespace fixy::session::config::detail::crash_assoc::witness {

namespace gw = ::fixy::session::global::detail::witness;
namespace gl = ::fixy::session::global;

using Guarded = gl::Comm<gw::RoleA, gw::RoleB, gl::Branch<gw::LabelX, int, gl::End>, gl::Branch<gl::CrashLabel, void, gl::End>>;
using OnlyB = ReliableSet<gw::RoleB>;
using Start = gl::State<gl::Roles<>, Guarded>;
using StartCtx = crash_projected_context_t<Guarded, OnlyB>;
using ACrashed = gl::State<gl::Roles<gw::RoleA>, gl::remove_role_t<Guarded, gw::RoleA>>;
using ACrashedCtx =
    TypingContext<RoleState<gw::RoleA, OutQueue<>, Stop>,
                  RoleState<gw::RoleB, OutQueue<>, typename entry_t<StartCtx, gw::RoleB>::local>>;
// A2 fails: A is at Stop, and the state says no role crashed.
using StoppedButLive = ACrashedCtx;
// A4 fails: B already has a message that A has not sent.
using EarlyMessage =
    TypingContext<RoleState<gw::RoleA, OutQueue<Queued<gw::RoleB, gw::LabelX, int>>, typename entry_t<StartCtx, gw::RoleA>::local>,
                  RoleState<gw::RoleB, OutQueue<>, typename entry_t<StartCtx, gw::RoleB>::local>>;

}  // namespace fixy::session::config::detail::crash_assoc::witness

template <>
struct foundation::contracts::armed_cell<::fixy::session::config::is_crash_associated> {
    using accepts = witnesses<
        ::fixy::session::config::CrashAssociation<::fixy::session::config::detail::crash_assoc::witness::StartCtx,
                                                  ::fixy::session::config::detail::crash_assoc::witness::Start,
                                                  ::fixy::session::config::detail::crash_assoc::witness::OnlyB>,
        ::fixy::session::config::CrashAssociation<::fixy::session::config::detail::crash_assoc::witness::ACrashedCtx,
                                                  ::fixy::session::config::detail::crash_assoc::witness::ACrashed,
                                                  ::fixy::session::config::detail::crash_assoc::witness::OnlyB>>;
    using refuses = witnesses<
        int,
        ::fixy::session::config::CrashAssociation<::fixy::session::config::detail::crash_assoc::witness::StoppedButLive,
                                                  ::fixy::session::config::detail::crash_assoc::witness::Start,
                                                  ::fixy::session::config::detail::crash_assoc::witness::OnlyB>,
        ::fixy::session::config::CrashAssociation<::fixy::session::config::detail::crash_assoc::witness::EarlyMessage,
                                                  ::fixy::session::config::detail::crash_assoc::witness::Start,
                                                  ::fixy::session::config::detail::crash_assoc::witness::OnlyB>>;
};
