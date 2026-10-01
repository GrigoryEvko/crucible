// Operational correspondence for a sender that acts before its message
// arrives: the three global types that show the gap in Theorem 4.20 of
// the crash-stop paper correspond when only the chosen branch is live.

#include "session_semantics.h"

namespace test_session_semantics {

// ── The sender acts before its message arrives ───────────────────────
//
// Figure 7 of the crash-stop paper lets a label pass an en-route prefix
// only when every branch of the prefix takes it.  In our reading that is
// a gap in its Theorem 4.20: p → q : {m1.p → r : a.end, m2.p → r : b.end}
// sends m1, and then the configuration can send a while the global type
// cannot (misc/session_types_literature.md, section 5, item 12).  With
// the chosen branch as the only live one, the three types that show the
// gap correspond, and the minimal one sends a after m1.  The walk of the
// third type is in test_session_semantics_sender_tirore3.cpp.

using Loop1 = g::Rec<g::Msg<P, Q, M1, int, g::Var>>;
using Ex4 = g::Comm<P, R, g::Branch<M1, int, g::EnRoute<Q, P, M, int, Loop1>>,
                    g::Branch<M2, int, g::EnRoute<Q, P, M, int, g::Msg<P, Q, M2, int, Loop1>>>>;

using SenderGoesOn =
    g::Comm<P, Q, g::Branch<M1, int, g::Msg<P, R, L1, int, g::End>>, g::Branch<M2, int, g::Msg<P, R, L2, int, g::End>>>;
inline constexpr Tally kSenderGoesOn = explore_projected<SenderGoesOn, 4>();
inline constexpr Tally kEx4 = explore_projected<Ex4, 6>();

static_assert(is_clean(kSenderGoesOn));
static_assert(is_clean(kEx4));

static_assert(
    holds_v<g::state_enabled_t<g::state_step_t<Start<SenderGoesOn>, g::SendAction<P, Q, M1, int>, Reliable>, Reliable>,
            g::SendAction<P, R, L1, int>>);
static_assert(
    holds_v<
        c::enabled_t<c::step_t<s::projected_context_t<SenderGoesOn>, g::SendAction<P, Q, M1, int>, Reliable>, Reliable>,
        g::SendAction<P, R, L1, int>>);

Walk sender_goes_on_walk() noexcept { return &explore_projected<SenderGoesOn, 4>; }
Tally sender_goes_on_tally() noexcept { return kSenderGoesOn; }

}  // namespace test_session_semantics
