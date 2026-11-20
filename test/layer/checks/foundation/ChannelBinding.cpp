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
static_assert(transfer::Synchronized<EndpointClaim>);

namespace detail::channel_binding_self_test {

// A class that keeps its state private and states that two threads write it
// at the same time, as a channel does.
class[[= transfer::synchronized{}]] SharedChannel {
    int head_ = 0;
};

// A class with a plain count that two threads write with no order.
struct PlainChannel {
    int count = 0;
};

// A binding refers to its channel as a reference does: it moves to another
// thread, also beside its sender, only when two threads can write the
// channel.  It owns nothing, so it does not outlive its sender.
using SharedBinding = ChannelBinding<SharedChannel>;
using PlainBinding = ChannelBinding<PlainChannel>;
static_assert(transfer::Sendable<SharedBinding> && transfer::SendableBeside<SharedBinding>
              && transfer::Shareable<SharedBinding> && !transfer::SendableOwned<SharedBinding>);
static_assert(!transfer::Sendable<PlainBinding> && !transfer::SendableBeside<PlainBinding>
              && !transfer::Shareable<PlainBinding>);

// The constructor takes the key of the channel, and only the channel builds
// a key, so no other code binds a channel.
static_assert(!__is_constructible(ChannelBinding<int>, int&));
static_assert(!__is_constructible(SharedBinding, SharedChannel&));
static_assert(__is_constructible(SharedBinding, SharedChannel&, BindingKey<SharedChannel>));
static_assert(!__is_constructible(BindingKey<SharedChannel>) && !__is_constructible(BindingKey<int>));
static_assert(!std::is_trivially_copyable_v<BindingKey<SharedChannel>> && std::is_empty_v<BindingKey<SharedChannel>>);

}  // namespace detail::channel_binding_self_test

}  // namespace foundation
