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

}  // namespace detail::mpsc_channel_self_test

}  // namespace fixy::concurrent
