// The compile-time checks of fixy/session/Watch.h.

#include <fixy/session/Watch.h>

#include <cstdint>

namespace fixy::session::watch {

namespace detail {

static_assert(thread_capacity < (1u << stamp_index_bits),
              "the one-based slot index must fit the low bits of a holder stamp");

}  // namespace detail

// Each declared read and write of a session opens a wait scope, also under
// check::Off.  The scope holds no wait-for chain, so it costs a few stores.
// The chain of its last trace lives in the suspect of its thread.
static_assert(sizeof(wait_scope) <= 6 * sizeof(std::uint64_t),
              "a wait scope holds no wait-for chain: the chain of its last trace lives in the suspect of its thread");
static_assert(sizeof(detail::wait_chain) > 6 * sizeof(std::uint64_t),
              "the bound above is meaningful only while a chain is larger than it");

}  // namespace fixy::session::watch
