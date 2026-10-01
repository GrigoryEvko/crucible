// The compile-time checks of fixy/session/Watch.h.

#include <fixy/session/Watch.h>

namespace fixy::session::watch {

namespace detail {

static_assert(thread_capacity < (1u << stamp_index_bits),
              "the one-based slot index must fit the low bits of a holder stamp");

}  // namespace detail

}  // namespace fixy::session::watch
