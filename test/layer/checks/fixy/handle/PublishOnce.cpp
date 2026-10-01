// The compile-time checks of fixy/handle/PublishOnce.h.

#include <fixy/handle/PublishOnce.h>

namespace fixy::handle {

static_assert(PublishOncePointee<int>);
static_assert(PublishOncePointee<void>);
static_assert(PublishOncePointee<const int>);
static_assert(PublishOncePointee<int[4]>);
static_assert(!PublishOncePointee<int*>, "a pointee that is itself a pointer is the T-versus-T* mistake");
static_assert(!PublishOncePointee<void*>);
static_assert(!PublishOncePointee<int&>, "T* over a reference type is ill-formed");
static_assert(!PublishOncePointee<int&&>);

namespace detail::publish_once_guard_probe {
// An incomplete type must pass the guard, because MerkleDag's slot is
// declared against one.
struct Incomplete;
static_assert(PublishOncePointee<Incomplete>);
}  // namespace detail::publish_once_guard_probe

static_assert(sizeof(PublishOnce<int>) == sizeof(std::atomic<int*>));
static_assert(sizeof(PublishOnce<void>) == sizeof(std::atomic<void*>));

static_assert(alignof(PublishSlot<int>) >= 64, "PublishSlot must be cache-line aligned: repeated publish/"
                                               "exchange traffic invalidates the consumer's cached line "
                                               "every iteration, so the slot must NOT share a line with "
                                               "unrelated embedder state.");
static_assert(alignof(PublishSlot<void>) >= 64);
static_assert(sizeof(PublishSlot<int>) >= 64);
static_assert(sizeof(PublishSlot<void>) >= 64);

}  // namespace fixy::handle
