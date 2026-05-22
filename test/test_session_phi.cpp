// SPDX-License-Identifier: MIT
// FIXY-V-069 — substrate-side test for sessions/SessionPhi.h.
//
// Witnesses that the φ predicate family from FX §11.18 + HYK24
// behaves correctly across the binary session-type combinator
// universe.  The in-header sentinel battery (v069_self_test::)
// catches drift at every consumer's include time; this dedicated
// TU adds explicit positive + negative cases at FOUR axes:
//
//   1. Each helper metafunction (has_empty_branch / loop_body_terminates /
//      has_unbounded_loop / payloads_distinct_at_choices) over the full
//      combinator universe.
//   2. Each φ predicate (phi_safe / phi_df / phi_term / phi_nterm /
//      phi_live / phi_live_plus / phi_live_pp) on canonical protocols.
//   3. Lattice ordering — every implication in safe ⊇ df ⊇ term ⊇
//      live_pp + safe ⊇ df ⊇ live ⊇ live_plus ⊇ live_pp.
//   4. fixy/Sess.h restoration — the re-introduced aliases at
//      fixy::sess::phi_*_v reach through to the substrate predicates
//      with identical resolved values.

#include <crucible/sessions/SessionPhi.h>
#include <crucible/Fixy.h>  // pulls fixy::sess::phi_*_v

namespace proto = ::crucible::safety::proto;
namespace fsess = ::crucible::fixy::sess;

// ────────────────────────────────────────────────────────────────────
// ── Cell 1 — helper metafunction sweep ────────────────────────────
// ────────────────────────────────────────────────────────────────────

namespace cell_1_helpers {

struct A {};
struct B {};
struct C {};

// has_empty_branch — every combinator + every nesting depth.
static_assert(!proto::has_empty_branch_v<proto::End>);
static_assert(!proto::has_empty_branch_v<proto::Stop>);
static_assert(!proto::has_empty_branch_v<proto::Continue>);
static_assert(!proto::has_empty_branch_v<proto::Send<A, proto::End>>);
static_assert(!proto::has_empty_branch_v<proto::Recv<A, proto::End>>);
static_assert( proto::has_empty_branch_v<proto::Select<>>);
static_assert( proto::has_empty_branch_v<proto::Offer<>>);
static_assert(!proto::has_empty_branch_v<proto::Select<proto::Send<A, proto::End>>>);
static_assert(!proto::has_empty_branch_v<proto::Offer<proto::Recv<A, proto::End>>>);

// Nested empty branch — caught at any depth.
static_assert(proto::has_empty_branch_v<
    proto::Send<A, proto::Recv<B, proto::Select<>>>>,
    "empty Select<> nested beyond Send/Recv must still be caught");
static_assert(proto::has_empty_branch_v<
    proto::Loop<proto::Send<A, proto::Offer<>>>>,
    "empty Offer<> nested under Loop body must still be caught");
static_assert(proto::has_empty_branch_v<
    proto::Select<proto::Send<A, proto::End>, proto::Offer<>>>,
    "OR-fold over branches catches the empty branch among siblings");

// loop_body_terminates — every combinator.
static_assert( proto::loop_body_terminates_v<proto::End>);
static_assert( proto::loop_body_terminates_v<proto::Stop>);
static_assert(!proto::loop_body_terminates_v<proto::Continue>);
static_assert( proto::loop_body_terminates_v<proto::Send<A, proto::End>>);
static_assert(!proto::loop_body_terminates_v<proto::Send<A, proto::Continue>>);
static_assert(!proto::loop_body_terminates_v<proto::Recv<A, proto::Continue>>);

// Select / Offer — OR-fold over branches.
static_assert( proto::loop_body_terminates_v<
    proto::Select<proto::Send<A, proto::Continue>, proto::Stop>>);
static_assert(!proto::loop_body_terminates_v<
    proto::Select<proto::Send<A, proto::Continue>,
                   proto::Recv<B, proto::Continue>>>,
    "every branch returns to Continue → loop body cannot escape");

// has_unbounded_loop — only Loop with non-terminating body trips it.
static_assert(!proto::has_unbounded_loop_v<proto::End>);
static_assert(!proto::has_unbounded_loop_v<proto::Send<A, proto::End>>);
static_assert( proto::has_unbounded_loop_v<proto::Loop<proto::Send<A, proto::Continue>>>);
static_assert(!proto::has_unbounded_loop_v<
    proto::Loop<proto::Select<proto::Send<A, proto::Continue>, proto::Stop>>>);

// Loop nested inside Loop — outer Loop's body is itself a Loop.
// The nested-Loop's termination is what matters for the outer.
static_assert( proto::has_unbounded_loop_v<
    proto::Loop<proto::Select<proto::Loop<proto::Send<A, proto::Continue>>, proto::Stop>>>,
    "nested Loop<Send<A, Continue>> is unbounded — detected even when "
    "wrapped in an outer Loop that itself has an escape branch");

// payloads_distinct_at_choices — pairwise distinctness check.
static_assert( proto::payloads_distinct_at_choices_v<proto::End>);
static_assert( proto::payloads_distinct_at_choices_v<
    proto::Select<proto::Send<A, proto::End>, proto::Send<B, proto::End>>>);
static_assert( proto::payloads_distinct_at_choices_v<
    proto::Select<proto::Send<A, proto::End>, proto::Send<B, proto::End>,
                   proto::Send<C, proto::End>>>);
static_assert(!proto::payloads_distinct_at_choices_v<
    proto::Select<proto::Send<A, proto::End>, proto::Send<A, proto::End>>>);
static_assert(!proto::payloads_distinct_at_choices_v<
    proto::Offer<proto::Recv<A, proto::End>, proto::Recv<A, proto::End>>>);
// Pairwise check — even three-branch duplication trips it.
static_assert(!proto::payloads_distinct_at_choices_v<
    proto::Select<proto::Send<A, proto::End>, proto::Send<B, proto::End>,
                   proto::Send<A, proto::End>>>);

}  // namespace cell_1_helpers

// ────────────────────────────────────────────────────────────────────
// ── Cell 2 — φ predicate sweep ────────────────────────────────────
// ────────────────────────────────────────────────────────────────────

namespace cell_2_phi {

struct Probe {};
using FiniteFwd   = proto::Send<Probe, proto::Recv<Probe, proto::End>>;
using LoopBounded = proto::Loop<proto::Select<
                                    proto::Send<Probe, proto::Continue>,
                                    proto::Stop>>;
using LoopUnbounded = proto::Loop<proto::Send<Probe, proto::Continue>>;
using Choice2     = proto::Select<proto::Send<int, proto::End>,
                                    proto::Send<float, proto::End>>;
using ChoiceDup   = proto::Select<proto::Send<int, proto::End>,
                                    proto::Send<int, proto::End>>;
using EmptyChoice = proto::Select<>;

// phi_safe
static_assert( proto::phi_safe_v<FiniteFwd>);
static_assert( proto::phi_safe_v<LoopBounded>);
static_assert( proto::phi_safe_v<LoopUnbounded>);
static_assert( proto::phi_safe_v<Choice2>);
static_assert( proto::phi_safe_v<ChoiceDup>);
static_assert( proto::phi_safe_v<EmptyChoice>);

// phi_df — rejects empty Select / Offer at any depth.
static_assert( proto::phi_df_v<FiniteFwd>);
static_assert( proto::phi_df_v<LoopBounded>);
static_assert( proto::phi_df_v<LoopUnbounded>);
static_assert( proto::phi_df_v<Choice2>);
static_assert( proto::phi_df_v<ChoiceDup>);
static_assert(!proto::phi_df_v<EmptyChoice>);

// phi_term — requires bounded Loop.
static_assert( proto::phi_term_v<FiniteFwd>);
static_assert( proto::phi_term_v<LoopBounded>);
static_assert(!proto::phi_term_v<LoopUnbounded>);
static_assert( proto::phi_term_v<Choice2>);

// phi_nterm — sibling of phi_term.  Mutually exclusive.
static_assert(!proto::phi_nterm_v<FiniteFwd>);
static_assert(!proto::phi_nterm_v<LoopBounded>);
static_assert( proto::phi_nterm_v<LoopUnbounded>);

// phi_live — for binary protocols ≈ phi_df.
static_assert( proto::phi_live_v<FiniteFwd>);
static_assert( proto::phi_live_v<LoopBounded>);
static_assert( proto::phi_live_v<LoopUnbounded>);
static_assert(!proto::phi_live_v<EmptyChoice>);

// phi_live_plus — requires distinct branch payloads.
static_assert( proto::phi_live_plus_v<Choice2>);
static_assert(!proto::phi_live_plus_v<ChoiceDup>);
static_assert( proto::phi_live_plus_v<FiniteFwd>);

// phi_live_pp — strictest.  Productive infinite loop rejected.
static_assert( proto::phi_live_pp_v<Choice2>);
static_assert(!proto::phi_live_pp_v<LoopUnbounded>);
static_assert( proto::phi_live_pp_v<LoopBounded>,
    "LoopBounded has Stop escape AND distinct branch payloads — live_pp");
static_assert(!proto::phi_live_pp_v<ChoiceDup>);

}  // namespace cell_2_phi

// ────────────────────────────────────────────────────────────────────
// ── Cell 3 — lattice ordering ─────────────────────────────────────
// ────────────────────────────────────────────────────────────────────
//
// Every protocol in cell_2 is checked against the full implication
// chain: live_pp ⇒ live_plus ⇒ live ⇒ df ⇒ safe.  Plus live_pp ⇒
// term ⇒ df ⇒ safe.

namespace cell_3_lattice {

template <typename P>
constexpr bool df_chain   = !proto::phi_df_v<P> || proto::phi_safe_v<P>;
template <typename P>
constexpr bool term_chain = !proto::phi_term_v<P> || proto::phi_df_v<P>;
template <typename P>
constexpr bool live_chain = !proto::phi_live_v<P> || proto::phi_df_v<P>;
template <typename P>
constexpr bool live_plus_chain =
    !proto::phi_live_plus_v<P> || proto::phi_live_v<P>;
template <typename P>
constexpr bool live_pp_chain =
    !proto::phi_live_pp_v<P> || (proto::phi_live_plus_v<P> && proto::phi_term_v<P>);
template <typename P>
constexpr bool nterm_excludes_term =
    !(proto::phi_nterm_v<P> && proto::phi_term_v<P>);

// Apply over the full menagerie.
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

// ────────────────────────────────────────────────────────────────────
// ── Cell 4 — fixy::sess::phi_*_v restoration reach ────────────────
// ────────────────────────────────────────────────────────────────────
//
// V-069 restores `phi_df_v`, `phi_live_v`, `phi_live_plus_v`,
// `phi_live_pp_v` at fixy::sess:: as aliases over the substrate.
// `phi_safe_v` / `phi_term_v` / `phi_nterm_v` already existed but
// `phi_term_v` / `phi_nterm_v` are refined to the substrate's
// HONEST semantics (not the previous `is_terminal_state_v` alias).

namespace cell_4_fixy_reach {

struct Q {};
using P = proto::Send<Q, proto::End>;
using P_loop_inf = proto::Loop<proto::Send<Q, proto::Continue>>;
using P_loop_fin = proto::Loop<proto::Select<proto::Send<Q, proto::Continue>,
                                              proto::Stop>>;
using P_distinct = proto::Select<proto::Send<int, proto::End>,
                                  proto::Send<float, proto::End>>;
using P_dup      = proto::Select<proto::Send<int, proto::End>,
                                  proto::Send<int, proto::End>>;

// Cross-binding: fixy alias resolves to substrate predicate exactly.
static_assert(fsess::phi_safe_v<P>       == proto::phi_safe_v<P>);
static_assert(fsess::phi_df_v<P>         == proto::phi_df_v<P>);
static_assert(fsess::phi_term_v<P>       == proto::phi_term_v<P>);
static_assert(fsess::phi_nterm_v<P>      == proto::phi_nterm_v<P>);
static_assert(fsess::phi_live_v<P>       == proto::phi_live_v<P>);
static_assert(fsess::phi_live_plus_v<P>  == proto::phi_live_plus_v<P>);
static_assert(fsess::phi_live_pp_v<P>    == proto::phi_live_pp_v<P>);

// Concrete cases — exercise the fixy aliases on protocols that
// SEPARATE the predicates.
static_assert(!fsess::phi_term_v<P_loop_inf>);
static_assert( fsess::phi_nterm_v<P_loop_inf>);
static_assert( fsess::phi_term_v<P_loop_fin>);
static_assert(!fsess::phi_nterm_v<P_loop_fin>);
static_assert( fsess::phi_live_plus_v<P_distinct>);
static_assert(!fsess::phi_live_plus_v<P_dup>);
static_assert(!fsess::phi_live_pp_v<P_loop_inf>);
static_assert( fsess::phi_live_pp_v<P_loop_fin>);

}  // namespace cell_4_fixy_reach

int main() {
    // Runtime smoke — instantiates every public predicate against
    // non-constant args at function-local scope so any latent
    // SFINAE / consteval bug surfaces here.
    ::crucible::safety::proto::session_phi_runtime_smoke_test();
    return 0;
}
