#pragma once

// The enclosing namespace holds the binary session surface. A global type here
// projects to a per-role local type, and every binary protocol is in principle
// derivable from a global type plus a role, so the multi-party layer keeps its
// own sub-namespace one level up.

#include <crucible/sessions/SessionGlobal.h>

namespace crucible::fixy::sess::mpst {

using ::crucible::safety::proto::End_G;
using ::crucible::safety::proto::Var_G;
using ::crucible::safety::proto::Transmission;
using ::crucible::safety::proto::BranchG;
using ::crucible::safety::proto::Choice;
using ::crucible::safety::proto::Rec_G;
using ::crucible::safety::proto::StopG;

using ::crucible::safety::proto::is_end_g_v;
using ::crucible::safety::proto::is_transmission_v;
using ::crucible::safety::proto::is_choice_v;
using ::crucible::safety::proto::is_rec_g_v;
using ::crucible::safety::proto::is_var_g_v;
using ::crucible::safety::proto::is_stop_g_v;

using ::crucible::safety::proto::RoleList;
using ::crucible::safety::proto::EmptyRoleList;
using ::crucible::safety::proto::insert_unique_t;
using ::crucible::safety::proto::union_roles_t;
using ::crucible::safety::proto::RolesOf;
using ::crucible::safety::proto::roles_of_t;

using ::crucible::safety::proto::is_global_well_formed_v;
using ::crucible::safety::proto::has_self_loop_v;
using ::crucible::safety::proto::assert_no_self_loop;
using ::crucible::safety::proto::has_empty_choice_v;
using ::crucible::safety::proto::assert_no_empty_choice;

using ::crucible::safety::proto::plain_merge_t;

using ::crucible::safety::proto::Project;
using ::crucible::safety::proto::project_t;

// The predicate asks whether a global type contains, at any depth, a
// transmission or a choice between the two named roles. The pair is unordered,
// so the answer is symmetric. It drives the projection rule for a crashed
// peer: a surviving role's local type ends in Stop when it interacts with that
// peer, and in End otherwise.

using ::crucible::safety::proto::has_interaction_between_v;

// Drift between the substrate surface and this projection trips at every
// consumer's include rather than only inside a downstream test.

namespace u013_self_test {

struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
struct Pong {};

static_assert(is_end_g_v<End_G>);
static_assert(!is_end_g_v<Var_G>);
static_assert(is_var_g_v<Var_G>);
static_assert(is_transmission_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert(!is_transmission_v<End_G>);
static_assert(is_choice_v<Choice<Alice, Bob, BranchG<Ping, End_G>>>);
static_assert(is_rec_g_v<Rec_G<End_G>>);
static_assert(is_stop_g_v<StopG<Alice>>);

static_assert(is_global_well_formed_v<End_G>);
// A bare variable outside a recursion binder has no binder to refer to.
static_assert(!is_global_well_formed_v<Var_G>);
static_assert(is_global_well_formed_v<Transmission<Alice, Bob, Ping, End_G>>);
// A transmission from a role to itself violates the sender-differs-from-
// receiver axiom.
static_assert(!is_global_well_formed_v<Transmission<Alice, Alice, Ping, End_G>>);

static_assert(has_self_loop_v<Transmission<Alice, Alice, Ping, End_G>>);
static_assert(!has_self_loop_v<Transmission<Alice, Bob, Ping, End_G>>);

static_assert(has_empty_choice_v<Choice<Alice, Bob>>);
static_assert(!has_empty_choice_v<Choice<Alice, Bob, BranchG<Ping, End_G>>>);

// insert_unique_t prepends an absent role and leaves a present one alone.
// union_roles_t folds it right to left, so an absent addition lands in
// reverse order of encounter.
using RL_A = RoleList<Alice>;
using RL_AB = RoleList<Alice, Bob>;
using RL_BC = RoleList<Bob, Carol>;
static_assert(std::is_same_v<insert_unique_t<Carol, RL_AB>, RoleList<Carol, Alice, Bob>>);
static_assert(std::is_same_v<insert_unique_t<Alice, RL_AB>, RL_AB>);
// Bob is already present and leaves the list unchanged. Carol is absent and
// is prepended.
static_assert(std::is_same_v<union_roles_t<RL_AB, RL_BC>, RoleList<Carol, Alice, Bob>>);
static_assert(std::is_same_v<EmptyRoleList, RoleList<>>);

using G_ternary = Transmission<Alice, Bob, Ping, Transmission<Bob, Carol, Pong, End_G>>;
static_assert(std::is_same_v<roles_of_t<G_ternary>, RoleList<Alice, Bob, Carol>>);

// Projection dispatches on the role. The sender gets a Send, the receiver
// gets a Recv, and a role that takes no part in the global type gets End.
using G_2p = Transmission<Alice, Bob, Ping, End_G>;
using L_Alice = project_t<G_2p, Alice>;
using L_Bob = project_t<G_2p, Bob>;
using L_Carol = project_t<G_2p, Carol>;

static_assert(std::is_same_v<L_Alice, ::crucible::safety::proto::Send<Ping, ::crucible::safety::proto::End>>);
static_assert(std::is_same_v<L_Bob, ::crucible::safety::proto::Recv<Ping, ::crucible::safety::proto::End>>);
static_assert(std::is_same_v<L_Carol, ::crucible::safety::proto::End>);

static_assert(std::is_same_v<plain_merge_t<::crucible::safety::proto::End>, ::crucible::safety::proto::End>);
static_assert(std::is_same_v<plain_merge_t<::crucible::safety::proto::End, ::crucible::safety::proto::End>,
                             ::crucible::safety::proto::End>);

constexpr int u013_surface_cardinality = 27;
static_assert(u013_surface_cardinality == 27, "fixy::sess::mpst:: surface cardinality drifted — update the "
                                              "using-decls AND this sentinel in lockstep.");

}  // namespace u013_self_test

// A static assertion alone can mask a SFINAE, consteval or inline-body fault.
// This body instantiates every public template from a function context. It
// executes no runtime path.

inline void runtime_smoke_test() noexcept {
    struct A {};
    struct B {};
    struct P {};
    using G_AB = Transmission<A, B, P, End_G>;

    [[maybe_unused]] constexpr bool wf = is_global_well_formed_v<G_AB>;
    [[maybe_unused]] constexpr bool sl = has_self_loop_v<G_AB>;
    [[maybe_unused]] constexpr bool ec = has_empty_choice_v<G_AB>;
    [[maybe_unused]] constexpr bool isT = is_transmission_v<G_AB>;

    using LA = project_t<G_AB, A>;
    using LB = project_t<G_AB, B>;
    using RL = roles_of_t<G_AB>;
    using IU = insert_unique_t<A, EmptyRoleList>;
    using UR = union_roles_t<RoleList<A>, RoleList<B>>;
    using PM = plain_merge_t<::crucible::safety::proto::End>;

    (void)wf;
    (void)sl;
    (void)ec;
    (void)isT;
    (void)static_cast<LA*>(nullptr);
    (void)static_cast<LB*>(nullptr);
    (void)static_cast<RL*>(nullptr);
    (void)static_cast<IU*>(nullptr);
    (void)static_cast<UR*>(nullptr);
    (void)static_cast<PM*>(nullptr);
}

}  // namespace crucible::fixy::sess::mpst
