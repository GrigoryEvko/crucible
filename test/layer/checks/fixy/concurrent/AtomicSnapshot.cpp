// The compile-time checks of fixy/concurrent/AtomicSnapshot.h.

#include <fixy/concurrent/AtomicSnapshot.h>

namespace fixy::concurrent {

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "AtomicSnapshot's seq counter requires lock-free uint64_t atomic");

}  // namespace fixy::concurrent
