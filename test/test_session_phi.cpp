// SPDX-License-Identifier: MIT
//
// The predicates under test form a ladder of increasing strictness
// over binary session protocols.  Safety is the weakest, then
// deadlock freedom, then termination, then liveness, and each further
// liveness predicate adds one more demand.  A protocol admitted at one
// rung is admitted at every weaker rung, and the file checks those
// implications as well as the individual answers.

#include <crucible/sessions/SessionPhi.h>
#include <crucible/Fixy.h>

namespace proto = ::crucible::safety::proto;
namespace fsess = ::crucible::fixy::sess;

namespace cell_1_helpers {

struct A {};
struct B {};
struct C {};

static_assert(!proto::has_empty_branch_v<proto::End>);
static_assert(!proto::has_empty_branch_v<proto::Stop>);
static_assert(!proto::has_empty_branch_v<proto::Continue>);
static_assert(!proto::has_empty_branch_v<proto::Send<A, proto::End>>);
static_assert(!proto::has_empty_branch_v<proto::Recv<A, proto::End>>);
static_assert(proto::has_empty_branch_v<proto::Select<>>);
static_assert(proto::has_empty_branch_v<proto::Offer<>>);
static_assert(!proto::has_empty_branch_v<proto::Select<proto::Send<A, proto::End>>>);
static_assert(!proto::has_empty_branch_v<proto::Offer<proto::Recv<A, proto::End>>>);

static_assert(proto::has_empty_branch_v<proto::Send<A, proto::Recv<B, proto::Select<>>>>,
              "empty Select<> nested beyond Send/Recv must still be caught");
static_assert(proto::has_empty_branch_v<proto::Loop<proto::Send<A, proto::Offer<>>>>,
              "empty Offer<> nested under Loop body must still be caught");
static_assert(proto::has_empty_branch_v<proto::Select<proto::Send<A, proto::End>, proto::Offer<>>>,
              "OR-fold over branches catches the empty branch among siblings");

static_assert(proto::loop_body_terminates_v<proto::End>);
static_assert(proto::loop_body_terminates_v<proto::Stop>);
static_assert(!proto::loop_body_terminates_v<proto::Continue>);
static_assert(proto::loop_body_terminates_v<proto::Send<A, proto::End>>);
static_assert(!proto::loop_body_terminates_v<proto::Send<A, proto::Continue>>);
static_assert(!proto::loop_body_terminates_v<proto::Recv<A, proto::Continue>>);

// A choice terminates when any one branch does, not when all of them
// do, so the fold over branches is a disjunction.
static_assert(proto::loop_body_terminates_v<proto::Select<proto::Send<A, proto::Continue>, proto::Stop>>);
static_assert(
    !proto::loop_body_terminates_v<proto::Select<proto::Send<A, proto::Continue>, proto::Recv<B, proto::Continue>>>,
    "every branch returns to Continue → loop body cannot escape");

// Only a loop whose body cannot terminate is unbounded.
static_assert(!proto::has_unbounded_loop_v<proto::End>);
static_assert(!proto::has_unbounded_loop_v<proto::Send<A, proto::End>>);
static_assert(proto::has_unbounded_loop_v<proto::Loop<proto::Send<A, proto::Continue>>>);
static_assert(!proto::has_unbounded_loop_v<proto::Loop<proto::Select<proto::Send<A, proto::Continue>, proto::Stop>>>);

// When a loop body is itself a loop, it is the inner loop's
// termination that decides the outer one.
static_assert(
    proto::has_unbounded_loop_v<proto::Loop<proto::Select<proto::Loop<proto::Send<A, proto::Continue>>, proto::Stop>>>,
    "nested Loop<Send<A, Continue>> is unbounded — detected even when "
    "wrapped in an outer Loop that itself has an escape branch");

// Branch payloads are compared pairwise, so a duplicate among any two
// branches is caught however many branches sit between them.
static_assert(proto::payloads_distinct_at_choices_v<proto::End>);
static_assert(
    proto::payloads_distinct_at_choices_v<proto::Select<proto::Send<A, proto::End>, proto::Send<B, proto::End>>>);
static_assert(proto::payloads_distinct_at_choices_v<
              proto::Select<proto::Send<A, proto::End>, proto::Send<B, proto::End>, proto::Send<C, proto::End>>>);
static_assert(
    !proto::payloads_distinct_at_choices_v<proto::Select<proto::Send<A, proto::End>, proto::Send<A, proto::End>>>);
static_assert(
    !proto::payloads_distinct_at_choices_v<proto::Offer<proto::Recv<A, proto::End>, proto::Recv<A, proto::End>>>);
static_assert(!proto::payloads_distinct_at_choices_v<
              proto::Select<proto::Send<A, proto::End>, proto::Send<B, proto::End>, proto::Send<A, proto::End>>>);

}  // namespace cell_1_helpers

namespace cell_2_phi {

struct Probe {};
using FiniteFwd = proto::Send<Probe, proto::Recv<Probe, proto::End>>;
using LoopBounded = proto::Loop<proto::Select<proto::Send<Probe, proto::Continue>, proto::Stop>>;
using LoopUnbounded = proto::Loop<proto::Send<Probe, proto::Continue>>;
using Choice2 = proto::Select<proto::Send<int, proto::End>, proto::Send<float, proto::End>>;
using ChoiceDup = proto::Select<proto::Send<int, proto::End>, proto::Send<int, proto::End>>;
using EmptyChoice = proto::Select<>;

// Safety admits every one of these, which is what makes it the
// weakest rung.
static_assert(proto::phi_safe_v<FiniteFwd>);
static_assert(proto::phi_safe_v<LoopBounded>);
static_assert(proto::phi_safe_v<LoopUnbounded>);
static_assert(proto::phi_safe_v<Choice2>);
static_assert(proto::phi_safe_v<ChoiceDup>);
static_assert(proto::phi_safe_v<EmptyChoice>);

// Deadlock freedom additionally rejects a choice with no branches, at
// any depth.
static_assert(proto::phi_df_v<FiniteFwd>);
static_assert(proto::phi_df_v<LoopBounded>);
static_assert(proto::phi_df_v<LoopUnbounded>);
static_assert(proto::phi_df_v<Choice2>);
static_assert(proto::phi_df_v<ChoiceDup>);
static_assert(!proto::phi_df_v<EmptyChoice>);

// Termination additionally requires every loop to be escapable.
static_assert(proto::phi_term_v<FiniteFwd>);
static_assert(proto::phi_term_v<LoopBounded>);
static_assert(!proto::phi_term_v<LoopUnbounded>);
static_assert(proto::phi_term_v<Choice2>);

// Non-termination is the complement of termination, so no protocol
// satisfies both.
static_assert(!proto::phi_nterm_v<FiniteFwd>);
static_assert(!proto::phi_nterm_v<LoopBounded>);
static_assert(proto::phi_nterm_v<LoopUnbounded>);

// On a binary protocol liveness coincides with deadlock freedom.
static_assert(proto::phi_live_v<FiniteFwd>);
static_assert(proto::phi_live_v<LoopBounded>);
static_assert(proto::phi_live_v<LoopUnbounded>);
static_assert(!proto::phi_live_v<EmptyChoice>);

// The stronger liveness additionally requires branch payloads to be
// distinct, so that a receiver can tell branches apart.
static_assert(proto::phi_live_plus_v<Choice2>);
static_assert(!proto::phi_live_plus_v<ChoiceDup>);
static_assert(proto::phi_live_plus_v<FiniteFwd>);

// The strongest rung additionally rejects a loop that runs forever,
// however productive it is.
static_assert(proto::phi_live_pp_v<Choice2>);
static_assert(!proto::phi_live_pp_v<LoopUnbounded>);
static_assert(proto::phi_live_pp_v<LoopBounded>, "A loop with an escape branch and distinct branch payloads must "
                                                 "satisfy the strongest liveness predicate.");
static_assert(!proto::phi_live_pp_v<ChoiceDup>);

}  // namespace cell_2_phi

// Each protocol above is checked against the whole implication chain,
// so a predicate that admits something a weaker one rejects fails
// here rather than passing quietly.

namespace cell_3_lattice {

template <typename P>
constexpr bool df_chain = !proto::phi_df_v<P> || proto::phi_safe_v<P>;
template <typename P>
constexpr bool term_chain = !proto::phi_term_v<P> || proto::phi_df_v<P>;
template <typename P>
constexpr bool live_chain = !proto::phi_live_v<P> || proto::phi_df_v<P>;
template <typename P>
constexpr bool live_plus_chain = !proto::phi_live_plus_v<P> || proto::phi_live_v<P>;
template <typename P>
constexpr bool live_pp_chain = !proto::phi_live_pp_v<P> || (proto::phi_live_plus_v<P> && proto::phi_term_v<P>);
template <typename P>
constexpr bool nterm_excludes_term = !(proto::phi_nterm_v<P> && proto::phi_term_v<P>);

using Probe = cell_2_phi::FiniteFwd;
static_assert(df_chain<cell_2_phi::FiniteFwd>);
static_assert(df_chain<cell_2_phi::LoopBounded>);
static_assert(df_chain<cell_2_phi::LoopUnbounded>);
static_assert(df_chain<cell_2_phi::EmptyChoice>);

static_assert(term_chain<cell_2_phi::FiniteFwd>);
static_assert(term_chain<cell_2_phi::LoopBounded>);
static_assert(term_chain<cell_2_phi::LoopUnbounded>);

static_assert(live_chain<cell_2_phi::FiniteFwd>);
static_assert(live_chain<cell_2_phi::LoopBounded>);

static_assert(live_plus_chain<cell_2_phi::Choice2>);
static_assert(live_plus_chain<cell_2_phi::ChoiceDup>);

static_assert(live_pp_chain<cell_2_phi::Choice2>);
static_assert(live_pp_chain<cell_2_phi::LoopUnbounded>);

static_assert(nterm_excludes_term<cell_2_phi::FiniteFwd>);
static_assert(nterm_excludes_term<cell_2_phi::LoopBounded>);
static_assert(nterm_excludes_term<cell_2_phi::LoopUnbounded>);

}  // namespace cell_3_lattice

// The predicates are also re-exported under a second namespace, and
// each re-exported name must answer exactly as the original does.

namespace cell_4_fixy_reach {

struct Q {};
using P = proto::Send<Q, proto::End>;
using P_loop_inf = proto::Loop<proto::Send<Q, proto::Continue>>;
using P_loop_fin = proto::Loop<proto::Select<proto::Send<Q, proto::Continue>, proto::Stop>>;
using P_distinct = proto::Select<proto::Send<int, proto::End>, proto::Send<float, proto::End>>;
using P_dup = proto::Select<proto::Send<int, proto::End>, proto::Send<int, proto::End>>;

static_assert(fsess::phi_safe_v<P> == proto::phi_safe_v<P>);
static_assert(fsess::phi_df_v<P> == proto::phi_df_v<P>);
static_assert(fsess::phi_term_v<P> == proto::phi_term_v<P>);
static_assert(fsess::phi_nterm_v<P> == proto::phi_nterm_v<P>);
static_assert(fsess::phi_live_v<P> == proto::phi_live_v<P>);
static_assert(fsess::phi_live_plus_v<P> == proto::phi_live_plus_v<P>);
static_assert(fsess::phi_live_pp_v<P> == proto::phi_live_pp_v<P>);

// Agreement on one protocol proves little if that protocol satisfies
// every predicate.  These separate the rungs from each other.
static_assert(!fsess::phi_term_v<P_loop_inf>);
static_assert(fsess::phi_nterm_v<P_loop_inf>);
static_assert(fsess::phi_term_v<P_loop_fin>);
static_assert(!fsess::phi_nterm_v<P_loop_fin>);
static_assert(fsess::phi_live_plus_v<P_distinct>);
static_assert(!fsess::phi_live_plus_v<P_dup>);
static_assert(!fsess::phi_live_pp_v<P_loop_inf>);
static_assert(fsess::phi_live_pp_v<P_loop_fin>);

}  // namespace cell_4_fixy_reach

int main() {
    // The assertions above run in the constant evaluator.  This call
    // reaches every predicate with arguments the compiler cannot fold,
    // where a substitution or evaluation bug would show instead.
    ::crucible::safety::proto::session_phi_runtime_smoke_test();
    return 0;
}
