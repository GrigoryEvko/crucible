// The compile-time checks of fixy/concurrent/PermissionedChannel.h.

#include <fixy/concurrent/PermissionedChannel.h>

#include <fixy/concurrent/HandleTraits.h>

namespace fixy::concurrent {

namespace detail::channel_self_test {

using channel_witness::WitnessTag;
using channel_witness::WitnessBrand;

// A channel names its form, its tag and a brand of one root.  A channel that
// omits the tag or the brand names no type, and no channel is on the erased
// brand or on a type that is not a brand.
template <template <ChannelForm, RingValue, std::size_t, typename, ::foundation::brand::IsFreshBrand> class Channel,
          ChannelForm Form, typename... TagAndBrand>
concept NamesAChannelOf = requires { typename Channel<Form, int, 8, TagAndBrand...>; };
static_assert(NamesAChannelOf<PermissionedChannel, ChannelForm::wire, WitnessTag, WitnessBrand>);
static_assert(NamesAChannelOf<PermissionedChannel, ChannelForm::merge, WitnessTag, WitnessBrand>);
static_assert(!NamesAChannelOf<PermissionedChannel, ChannelForm::wire>);
static_assert(!NamesAChannelOf<PermissionedChannel, ChannelForm::wire, WitnessTag>);
static_assert(!NamesAChannelOf<PermissionedChannel, ChannelForm::wire, WitnessTag, ::foundation::brand::DefaultBrand>);
static_assert(!NamesAChannelOf<PermissionedChannel, ChannelForm::merge, WitnessTag, ::foundation::brand::DefaultBrand>);
static_assert(!NamesAChannelOf<PermissionedChannel, ChannelForm::wire, WitnessTag, int>);

using Wire = PermissionedChannel<ChannelForm::wire, int, 8, WitnessTag, WitnessBrand>;
using Merge = PermissionedChannel<ChannelForm::merge, int, 8, WitnessTag, WitnessBrand>;

// The alias reads the form, the tag and the brand off the root.  The tags
// hold the form, so the two forms of one user tag are two tag types.
static_assert(
    std::is_same_v<channel_t<int, 8, ::foundation::permissions::Permission<Wire::whole_tag, WitnessBrand>>, Wire>);
static_assert(
    std::is_same_v<channel_t<int, 8, ::foundation::permissions::Permission<Merge::whole_tag, WitnessBrand>>, Merge>);
static_assert(!std::is_same_v<Wire::producer_tag, Merge::producer_tag>);
static_assert(Wire::channel_form == ChannelForm::wire && Merge::ConsumerHandle::channel_form == ChannelForm::merge);

// A channel of another brand is another type, and so is each handle.
struct OtherBrand {};
using OtherWire = PermissionedChannel<ChannelForm::wire, int, 8, WitnessTag, OtherBrand>;
static_assert(!std::is_same_v<Wire, OtherWire>);
static_assert(!std::is_same_v<Wire::ProducerHandle::channel_type, OtherWire::ConsumerHandle::channel_type>);
// An assignment would drop the token of the target handle.
static_assert(!std::is_move_assignable_v<Wire::ProducerHandle> && !std::is_move_assignable_v<Wire::ConsumerHandle>);
static_assert(!std::is_move_assignable_v<Merge::ProducerHandle> && !std::is_move_assignable_v<Merge::ConsumerHandle>);

// The wire producer is linear, and the merge producer ends at the end of its
// scope, where its pool share goes back.
static_assert(::foundation::linear::is_linear_type(^^Wire::ProducerHandle));
static_assert(!::foundation::linear::is_linear_type(^^Merge::ProducerHandle));
static_assert(::foundation::linear::is_linear_type(^^Merge::ConsumerHandle));

// The binding and the claim keep the handles of a wire one pointer wide.  A
// merge producer holds its pool share beside the binding.
static_assert(sizeof(Wire::ProducerHandle) == sizeof(void*));
static_assert(sizeof(Wire::ConsumerHandle) == sizeof(void*));
static_assert(sizeof(Merge::ConsumerHandle) == sizeof(void*));

// The pool of a merge is the one door of its producer root, and no default
// constructor builds a merge channel with an empty pool.  A wire starts with
// no handle.
static_assert(std::is_default_constructible_v<Wire>);
static_assert(!std::is_default_constructible_v<Merge>);

// Each handle is the pole of its role, with one pop and one push, and the pop
// of a merge takes the tag of Nondet.
static_assert(is_producer_handle_v<Wire::ProducerHandle> && is_producer_handle_v<Merge::ProducerHandle>);
static_assert(is_consumer_handle_v<Wire::ConsumerHandle> && !is_merge_consumer_handle_v<Wire::ConsumerHandle>);
static_assert(is_merge_consumer_handle_v<Merge::ConsumerHandle>);
static_assert(std::is_same_v<producer_handle_value_t<Merge::ProducerHandle>, int>
              && std::is_same_v<consumer_handle_value_t<Wire::ConsumerHandle>, int>);

// Each handle states the capacity of its channel, so a check of the waits
// reads it from the type of the handle.
static_assert(Wire::ProducerHandle::channel_capacity == 8 && Wire::ConsumerHandle::channel_capacity == 8);
static_assert(Merge::ProducerHandle::channel_capacity == 8 && Merge::ConsumerHandle::channel_capacity == 8);
using WideWire = PermissionedChannel<ChannelForm::wire, int, 64, WitnessTag, OtherBrand>;
static_assert(WideWire::ProducerHandle::channel_capacity == WideWire::channel_capacity
              && WideWire::ConsumerHandle::channel_capacity == 64);

}  // namespace detail::channel_self_test

}  // namespace fixy::concurrent
