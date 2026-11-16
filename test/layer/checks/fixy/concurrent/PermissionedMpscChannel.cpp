// The compile-time checks of fixy/concurrent/PermissionedMpscChannel.h.

#include <fixy/concurrent/PermissionedMpscChannel.h>

namespace fixy::concurrent {

namespace detail::mpsc_channel_self_test {

using mpsc_channel_witness::WitnessTag;
using mpsc_channel_witness::WitnessBrand;

// A channel names its tag and a brand of one root.  A channel that omits
// the tag or the brand names no type, and no channel is on the erased
// brand or on a type that is not a brand.
template <template <RingValue, std::size_t, typename, ::foundation::brand::IsFreshBrand> class Channel,
          typename... TagAndBrand>
concept NamesAChannelOf = requires { typename Channel<int, 8, TagAndBrand...>; };
static_assert(NamesAChannelOf<PermissionedMpscChannel, WitnessTag, WitnessBrand>);
static_assert(!NamesAChannelOf<PermissionedMpscChannel>);
static_assert(!NamesAChannelOf<PermissionedMpscChannel, WitnessTag>);
static_assert(!NamesAChannelOf<PermissionedMpscChannel, WitnessTag, ::foundation::brand::DefaultBrand>);
static_assert(!NamesAChannelOf<PermissionedMpscChannel, WitnessTag, int>);

using Witness = PermissionedMpscChannel<int, 8, WitnessTag, WitnessBrand>;
static_assert(
    std::is_same_v<mpsc_channel_t<int, 8, ::foundation::permissions::Permission<Witness::whole_tag, WitnessBrand>>,
                   Witness>);

// The producer root is the one door into the pool, and no default
// constructor builds a channel with an empty pool.
static_assert(!std::is_default_constructible_v<Witness>);
static_assert(sizeof(Witness::ConsumerHandle) == sizeof(void*));
// An assignment would drop the token of the target handle.
static_assert(!std::is_move_assignable_v<Witness::ConsumerHandle>);

// Each handle states the capacity of its channel, so a check of the waits
// reads it from the type of the handle.
static_assert(Witness::ProducerHandle::channel_capacity == 8 && Witness::ConsumerHandle::channel_capacity == 8);
using WideWitness = PermissionedMpscChannel<int, 64, WitnessTag, WitnessBrand>;
static_assert(WideWitness::ProducerHandle::channel_capacity == WideWitness::channel_capacity
              && WideWitness::ConsumerHandle::channel_capacity == 64);

}  // namespace detail::mpsc_channel_self_test

}  // namespace fixy::concurrent
