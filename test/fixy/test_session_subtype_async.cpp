// The bounded asynchronous relation of fixy/session/Subtype.h: an output
// moves ahead of an input inside the capacity of the channel type, and the
// relation holds each synchronous pair.

#include "session_subtype.h"

namespace test_session_subtype_types {

// ── The asynchronous relation ────────────────────────────────────────

using Early = Send<PingReq, Recv<StopReq, End>>;
using Late = Recv<StopReq, Send<PingReq, End>>;
static_assert(!s::is_subtype_sync_v<Early, Late>, "the synchronous relation keeps the order");
static_assert(s::is_subtype_async_v<Early, Late, Slots<1>>, "an output moves ahead of an input");
static_assert(!s::is_subtype_async_v<Late, Early, Slots<8>>, "an input never moves ahead of an output");

// The capacity comes from a channel type, never from a number.  A
// channel with no buffer states no capacity, and the synchronous
// relation covers it.  A number in place of the channel is refused
// (neg_sess_subtype_async_number_capacity).
static_assert(s::StatesChannelCapacity<Slots<1>> && !s::StatesChannelCapacity<Slots<0>>
              && !s::StatesChannelCapacity<int> && s::StatesChannelCapacity<Slots<2> const&>);
static_assert(s::channel_capacity_v<Slots<3>> == 3);
static_assert(!s::SubtypeAsync<Early, Late, Slots<0>>, "no buffer, no anticipation");

using Early2 = Send<PingReq, Send<PingReq, Recv<StopReq, Recv<StopReq, End>>>>;
using Late2 = Recv<StopReq, Recv<StopReq, Send<PingReq, Send<PingReq, End>>>>;
static_assert(!s::is_subtype_async_v<Early2, Late2, Slots<1>>, "two messages ahead need a buffer of two");
static_assert(s::is_subtype_async_v<Early2, Late2, Slots<2>>);

// An orphan: the subtype sends a message the supertype never sends.
static_assert(!s::is_subtype_async_v<Send<PingReq, End>, End, Slots<4>>);
static_assert(!s::is_subtype_async_v<End, Send<PingReq, End>, Slots<4>>);

// A loop that anticipates one message for ever needs an unbounded
// buffer, so no capacity proves it.
using Flood = Loop<Send<PingReq, Continue>>;
using Paced = Loop<Recv<StopReq, Send<PingReq, Continue>>>;
static_assert(!s::is_subtype_async_v<Flood, Paced, Slots<4>>);

// A loop that sends one message ahead and then keeps the pace.
using Ahead = Send<PingReq, Loop<Recv<StopReq, Send<PingReq, Continue>>>>;
using Beat = Loop<Recv<StopReq, Send<PingReq, Continue>>>;
static_assert(!s::is_subtype_sync_v<Ahead, Beat>);

// The synchronous relation is a subset, at every capacity.
static_assert(s::is_subtype_async_v<DS1, DS2, Slots<1>> && s::is_subtype_async_v<DO1, DO2, Slots<3>>);
static_assert(s::is_subtype_async_v<Loop<Send<int, Continue>>, Send<int, Loop<Send<int, Continue>>>, Slots<1>>);

// Closure under duality holds by construction.
static_assert(s::is_subtype_async_v<s::dual_of_t<Late>, s::dual_of_t<Early>, Slots<1>>);
static_assert(s::is_subtype_async_v<Early2, Late2, Slots<2>>
              == s::is_subtype_async_v<s::dual_of_t<Late2>, s::dual_of_t<Early2>, Slots<2>>);

// A choice moves as one message.
using EarlyPick = Select<Send<PingReq, Recv<StopReq, End>>, Send<Job, Recv<StopReq, End>>>;
using LatePick = Recv<StopReq, Select<Send<PingReq, End>, Send<Job, End>>>;
static_assert(s::is_subtype_async_v<Select<Recv<StopReq, End>>, Recv<StopReq, Select<End>>, Slots<1>>);
static_assert(!s::is_subtype_async_v<EarlyPick, LatePick, Slots<1>>,
              "the label moves ahead, but the payloads differ in order");

static_assert(foundation::contracts::armed_cell_holds_v<s::is_sync_subtype>);
static_assert(foundation::contracts::armed_cell_holds_v<s::is_async_subtype>);

}  // namespace test_session_subtype_types
