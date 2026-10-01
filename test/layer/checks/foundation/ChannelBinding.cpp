// The compile-time checks of foundation/ChannelBinding.h.

#include <foundation/ChannelBinding.h>

namespace foundation {

// The binding must cost what a reference costs, or every handle grows.
static_assert(sizeof(ChannelBinding<int>) == sizeof(int*));
static_assert(alignof(ChannelBinding<int>) == alignof(int*));
static_assert(!std::is_copy_constructible_v<ChannelBinding<int>>);
static_assert(!std::is_copy_assignable_v<ChannelBinding<int>>);
static_assert(std::is_nothrow_move_constructible_v<ChannelBinding<int>>);
static_assert(!std::is_default_constructible_v<ChannelBinding<int>>);

// An identity is one pointer, and it names an instance only by equality.
static_assert(sizeof(ChannelIdentity<int>) == sizeof(int*));
static_assert(!ChannelIdentity<int>{}.is_bound());
static_assert(ChannelIdentity<int>{} == ChannelIdentity<int>{nullptr});

// A claim is one flag, and it lives in the channel, which does not move.
static_assert(sizeof(EndpointClaim) == sizeof(std::atomic<bool>));
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(!std::is_copy_constructible_v<EndpointClaim> && !std::is_move_constructible_v<EndpointClaim>);

}  // namespace foundation
