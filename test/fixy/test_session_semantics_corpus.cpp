// Operational correspondence on a corpus of global types, with every role
// reliable: each walk from G and its projected context must find the same
// labels at each state, and an associated context after each label.

#include "session_semantics.h"

namespace test_session_semantics {

// The corpus: the examples of fixy/session tests, each balanced+.
using Ring = g::Rec<g::Msg<P, Q, Add, int,
                           g::Comm<Q, R, g::Branch<Add, int, g::Msg<R, P, Add, int, g::Var>>,
                                   g::Branch<Sub, int, g::Msg<R, P, Sub, int, g::Var>>>>>;
using PingPong = g::Rec<g::Comm<P, Q, g::Branch<M1, int, g::Msg<Q, P, M2, int, g::Var>>, g::Branch<M2, int, g::End>>>;
using AfterChoice =
    g::Comm<P, Q, g::Branch<M1, int, g::Msg<Q, R, M1, int, g::End>>, g::Branch<M2, int, g::Msg<Q, R, M2, int, g::End>>>;
using SameSends =
    g::Comm<P, Q, g::Branch<M1, int, g::Msg<R, S, M, int, g::End>>, g::Branch<M2, int, g::Msg<R, S, M, int, g::End>>>;
using Nested = g::Rec<g::Msg<P, Q, M, int, g::Rec<g::Msg<Q, R, M, int, g::Var>>>>;

inline constexpr Tally kOnce = explore_projected<Once, 4>();
inline constexpr Tally kRing = explore_projected<Ring, 8>();
inline constexpr Tally kPingPong = explore_projected<PingPong, 6>();
inline constexpr Tally kAfterChoice = explore_projected<AfterChoice, 6>();
inline constexpr Tally kSameSends = explore_projected<SameSends, 6>();
inline constexpr Tally kNested = explore_projected<Nested, 6>();
inline constexpr Tally kIndependent = explore_projected<Independent, 6>();

static_assert(is_clean(kOnce));
static_assert(is_clean(kRing));
static_assert(is_clean(kPingPong));
static_assert(is_clean(kAfterChoice));
static_assert(is_clean(kSameSends));
static_assert(is_clean(kNested));
static_assert(is_clean(kIndependent));

Walk ring_walk() noexcept { return &explore_projected<Ring, 8>; }
Tally ring_tally() noexcept { return kRing; }
Walk ping_pong_walk() noexcept { return &explore_projected<PingPong, 6>; }
Tally ping_pong_tally() noexcept { return kPingPong; }

}  // namespace test_session_semantics
