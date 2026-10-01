// Operational correspondence for the third global type that shows the gap
// in Theorem 4.20 of the crash-stop paper (test_session_semantics_sender.cpp
// tells the gap): a message, and then a loop of a choice between two other
// roles.

#include "session_semantics.h"

namespace test_session_semantics {

using Tirore3 = g::Msg<P, Q, K, int, g::Rec<g::Comm<R, S, g::Branch<L1, int, g::End>, g::Branch<L2, int, g::Var>>>>;
inline constexpr Tally kTirore3 = explore_projected<Tirore3, 6>();

static_assert(is_clean(kTirore3));

}  // namespace test_session_semantics
