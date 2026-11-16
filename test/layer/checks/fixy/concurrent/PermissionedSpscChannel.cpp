// The compile-time checks of fixy/concurrent/PermissionedSpscChannel.h.

#include <fixy/concurrent/PermissionedSpscChannel.h>

namespace fixy::concurrent {

namespace detail::spsc_channel_self_test {

using spsc_channel_witness::WitnessTag;
using spsc_channel_witness::WitnessBrand;

// A channel names its tag and a brand of one root.  A channel that omits
// the tag or the brand names no type, and no channel is on the erased
// brand or on a type that is not a brand.
template <template <RingValue, std::size_t, typename, ::foundation::brand::IsFreshBrand> class Channel,
          typename... TagAndBrand>
concept NamesAChannelOf = requires { typename Channel<int, 8, TagAndBrand...>; };
static_assert(NamesAChannelOf<PermissionedSpscChannel, WitnessTag, WitnessBrand>);
static_assert(!NamesAChannelOf<PermissionedSpscChannel>);
static_assert(!NamesAChannelOf<PermissionedSpscChannel, WitnessTag>);
static_assert(!NamesAChannelOf<PermissionedSpscChannel, WitnessTag, ::foundation::brand::DefaultBrand>);
static_assert(!NamesAChannelOf<PermissionedSpscChannel, WitnessTag, int>);

using Witness = PermissionedSpscChannel<int, 8, WitnessTag, WitnessBrand>;
static_assert(
    std::is_same_v<spsc_channel_t<int, 8, ::foundation::permissions::Permission<Witness::whole_tag, WitnessBrand>>,
                   Witness>);

// A channel of another brand is another type, and so is each handle.
struct OtherBrand {};
using OtherWitness = PermissionedSpscChannel<int, 8, WitnessTag, OtherBrand>;
static_assert(!std::is_same_v<Witness, OtherWitness>);
static_assert(!std::is_same_v<Witness::ProducerHandle::channel_type, OtherWitness::ConsumerHandle::channel_type>);
// An assignment would drop the token of the target handle.
static_assert(!std::is_move_assignable_v<Witness::ProducerHandle>
              && !std::is_move_assignable_v<Witness::ConsumerHandle>);

// The binding and the claim keep the handles one pointer wide.
static_assert(sizeof(Witness::ProducerHandle) == sizeof(void*));
static_assert(sizeof(Witness::ConsumerHandle) == sizeof(void*));

// Each handle states the capacity of its channel, so a check of the waits
// reads it from the type of the handle.
static_assert(Witness::ProducerHandle::channel_capacity == 8 && Witness::ConsumerHandle::channel_capacity == 8);
using WideWitness = PermissionedSpscChannel<int, 64, WitnessTag, OtherBrand>;
static_assert(WideWitness::ProducerHandle::channel_capacity == WideWitness::channel_capacity
              && WideWitness::ConsumerHandle::channel_capacity == 64);

}  // namespace detail::spsc_channel_self_test

}  // namespace fixy::concurrent
