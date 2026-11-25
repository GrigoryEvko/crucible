// The compile-time checks of fixy/handle/PublishOnce.h.

#include <fixy/handle/PublishOnce.h>

namespace fixy::handle {

static_assert(PublishOncePointee<int>);
static_assert(PublishOncePointee<void>);
static_assert(PublishOncePointee<const int>);
static_assert(PublishOncePointee<const void>);
// A publish takes a Ref, and a Ref has no array target and no volatile
// target.  So no publish could fill such a slot, and the guard refuses it: a
// slot that can never be published must not exist.
static_assert(!PublishOncePointee<int[4]>, "a slot of an array can never be published");
static_assert(!PublishOncePointee<volatile int>, "a slot of a volatile type can never be published");
static_assert(!PublishOncePointee<int*>, "a pointee that is itself a pointer is the T-versus-T* mistake");
static_assert(!PublishOncePointee<void*>);
static_assert(!PublishOncePointee<int&>, "T* over a reference type is ill-formed");
static_assert(!PublishOncePointee<int&&>);

// The targets of a Ref that a slot takes: the same type, a type with less
// const, and each object for a slot of void.
static_assert(PublishesInto<int, int>);
static_assert(PublishesInto<int, int const>);
static_assert(PublishesInto<int, void>);
static_assert(PublishesInto<int const, void const>);
static_assert(!PublishesInto<int const, int>, "a publish never drops a const");
static_assert(!PublishesInto<int const, void>, "a publish never drops a const");
static_assert(!PublishesInto<long, int>);

namespace detail::publish_once_guard_probe {
// An incomplete type must pass the guard, because MerkleDag's slot is
// declared against one.
struct Incomplete;
static_assert(PublishOncePointee<Incomplete>);
}  // namespace detail::publish_once_guard_probe

static_assert(sizeof(PublishOnce<int>) == sizeof(std::atomic<int*>));
static_assert(sizeof(PublishOnce<void>) == sizeof(std::atomic<void*>));

// A slot of a brand holds one word.  It shares the transfer facts of the Ref
// that it holds: two threads can observe a slot of a const target that two
// threads can read, and a slot of a target that one thread writes goes to no
// second thread.  It takes a target of a Ref, so it takes no void.
struct CheckOwner {};
static_assert(sizeof(BrandedPublishOnce<int, CheckOwner>) == sizeof(PublishOnce<int>));
static_assert(::foundation::transfer::Shareable<BrandedPublishOnce<int const, CheckOwner>>);
static_assert(!::foundation::transfer::Shareable<BrandedPublishOnce<int, CheckOwner>>);
static_assert(BrandedPublishOncePointee<int> && BrandedPublishOncePointee<int const>);
static_assert(!BrandedPublishOncePointee<void> && !BrandedPublishOncePointee<int*>);
static_assert(!BrandedPublishOncePointee<int[4]> && !BrandedPublishOncePointee<int&>);

static_assert(alignof(PublishSlot<int>) >= 64, "PublishSlot must be cache-line aligned: repeated publish/"
                                               "exchange traffic invalidates the consumer's cached line "
                                               "every iteration, so the slot must NOT share a line with "
                                               "unrelated embedder state.");
static_assert(alignof(PublishSlot<void>) >= 64);
static_assert(sizeof(PublishSlot<int>) >= 64);
static_assert(sizeof(PublishSlot<void>) >= 64);

}  // namespace fixy::handle
