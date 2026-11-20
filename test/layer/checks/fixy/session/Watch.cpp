// The compile-time checks of fixy/session/Watch.h.

#include <fixy/session/Watch.h>

#include <cstdint>

namespace fixy::session::watch {

namespace detail {

static_assert(thread_capacity < (1u << stamp_index_bits),
              "the one-based slot index must fit the low bits of a holder stamp");

// The cell of the protocol text holds one word: the address of a text of
// static storage, which carries the count of its characters.  A record stays
// one cache line.
static_assert(sizeof(endpoint_record{}.protocol_text) == sizeof(std::uint64_t),
              "the cell of the protocol text holds the address of one text");
static_assert(sizeof(endpoint_record) == 64, "an endpoint record is one cache line");

}  // namespace detail

// Each declared read and write of a session opens a wait scope, also under
// check::Off.  The scope holds no wait-for chain, so it costs a few stores.
// The chain of its last trace lives in the suspect of its thread.
static_assert(sizeof(wait_scope) <= 6 * sizeof(std::uint64_t),
              "a wait scope holds no wait-for chain: the chain of its last trace lives in the suspect of its thread");
static_assert(sizeof(detail::wait_chain) > 6 * sizeof(std::uint64_t),
              "the bound above is meaningful only while a chain is larger than it");

// poll() finds a trace point with a mask of the low bits of its count.
static_assert(polls_per_trace_point != 0 && (polls_per_trace_point & (polls_per_trace_point - 1u)) == 0,
              "the polls of a trace point are a power of two");
// A wait traces first at suspect_after_trace_points, and then each
// confirm_after_trace_points.  A confirm of zero traces at each trace point.
static_assert(suspect_after_trace_points != 0 && confirm_after_trace_points != 0,
              "a wait traces only after it passed a trace point");

}  // namespace fixy::session::watch
