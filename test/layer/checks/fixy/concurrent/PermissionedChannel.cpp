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
template <
    template <ChannelForm F, ChannelValue, std::size_t, typename, ::foundation::brand::IsFreshBrand, LaneCount... L>
        requires LanesFitForm<F, L...>
    class Channel,
    ChannelForm Form, typename... TagAndBrand>
    concept NamesAChannelOf = requires { typename Channel<Form, int, 8, TagAndBrand...>; };
static_assert(NamesAChannelOf<PermissionedChannel, ChannelForm::wire, WitnessTag, WitnessBrand>);
static_assert(NamesAChannelOf<PermissionedChannel, ChannelForm::merge, WitnessTag, WitnessBrand>);
static_assert(!NamesAChannelOf<PermissionedChannel, ChannelForm::wire>);
static_assert(!NamesAChannelOf<PermissionedChannel, ChannelForm::wire, WitnessTag>);
static_assert(!NamesAChannelOf<PermissionedChannel, ChannelForm::wire, WitnessTag, ::foundation::brand::DefaultBrand>);
static_assert(!NamesAChannelOf<PermissionedChannel, ChannelForm::merge, WitnessTag, ::foundation::brand::DefaultBrand>);
static_assert(!NamesAChannelOf<PermissionedChannel, ChannelForm::wire, WitnessTag, int>);

// The lanes form names the count of its lanes after the brand, and only the
// lanes form names a count.
template <ChannelForm Form, LaneCount... Lanes>
concept NamesALanesChannelOf =
    requires { typename PermissionedChannel<Form, int, 8, WitnessTag, WitnessBrand, Lanes...>; };
static_assert(NamesALanesChannelOf<ChannelForm::lanes, LaneCount{3}>);
static_assert(!NamesALanesChannelOf<ChannelForm::lanes>);
static_assert(!NamesALanesChannelOf<ChannelForm::wire, LaneCount{1}>);
static_assert(!NamesALanesChannelOf<ChannelForm::merge, LaneCount{2}>);

using Wire = PermissionedChannel<ChannelForm::wire, int, 8, WitnessTag, WitnessBrand>;
using Merge = PermissionedChannel<ChannelForm::merge, int, 8, WitnessTag, WitnessBrand>;
using Lanes = PermissionedChannel<ChannelForm::lanes, int, 8, WitnessTag, WitnessBrand, LaneCount{3}>;

// The alias reads the form, the tag and the brand off the root, and takes the
// count of the lanes after it.  The tags hold the form, so the forms of one
// user tag are distinct tag types.
static_assert(
    std::is_same_v<channel_t<int, 8, ::foundation::permissions::Permission<Wire::whole_tag, WitnessBrand>>, Wire>);
static_assert(
    std::is_same_v<channel_t<int, 8, ::foundation::permissions::Permission<Merge::whole_tag, WitnessBrand>>, Merge>);
static_assert(
    std::is_same_v<
        channel_t<int, 8, ::foundation::permissions::Permission<Lanes::whole_tag, WitnessBrand>, LaneCount{3}>, Lanes>);
static_assert(!std::is_same_v<Wire::producer_tag, Merge::producer_tag>);
static_assert(!std::is_same_v<Merge::producer_tag, Lanes::producer_tag>);
static_assert(Wire::channel_form == ChannelForm::wire && Merge::ConsumerEnd::channel_form == ChannelForm::merge);
static_assert(Lanes::channel_form == ChannelForm::lanes && Lanes::ProducerEnd::channel_form == ChannelForm::lanes);

// A scope brand that the checks name.  No frame door gives a handle on it,
// because a frame door takes its scope brand from the call.
struct CheckScope {};
template <typename Channel>
using CheckProducer = ProducerHandle<Channel, CheckScope>;
template <typename Channel>
using CheckConsumer = ConsumerHandle<Channel, CheckScope>;

// The member alias templates name the handles of the frame doors, and two
// scope brands give two handle types.
struct OtherScope {};
static_assert(std::is_same_v<Wire::producer_handle<CheckScope>, CheckProducer<Wire>>);
static_assert(std::is_same_v<Merge::consumer_handle<CheckScope>, CheckConsumer<Merge>>);
static_assert(!std::is_same_v<Wire::producer_handle<CheckScope>, Wire::producer_handle<OtherScope>>);
static_assert(std::is_same_v<CheckProducer<Lanes>::scope_type, CheckScope>);

// A channel of another brand is another type, and so is each end.
struct OtherBrand {};
using OtherWire = PermissionedChannel<ChannelForm::wire, int, 8, WitnessTag, OtherBrand>;
static_assert(!std::is_same_v<Wire, OtherWire>);
static_assert(!std::is_same_v<Wire::ProducerEnd::channel_type, OtherWire::ConsumerEnd::channel_type>);
// An assignment would drop the token of the target end or handle.
static_assert(!std::is_move_assignable_v<Wire::ProducerEnd> && !std::is_move_assignable_v<Wire::ConsumerEnd>);
static_assert(!std::is_move_assignable_v<Merge::ProducerEnd> && !std::is_move_assignable_v<Merge::ConsumerEnd>);
static_assert(!std::is_move_assignable_v<Lanes::ProducerEnd> && !std::is_move_assignable_v<Lanes::ConsumerEnd>);
static_assert(!std::is_move_assignable_v<CheckProducer<Wire>> && !std::is_move_assignable_v<CheckConsumer<Wire>>);
static_assert(!std::is_copy_constructible_v<CheckProducer<Merge>>
              && std::is_move_constructible_v<CheckProducer<Merge>>);

// No code outside the frame doors builds an end or a handle: the key of the
// frame doors has no public constructor and does not copy, the constructor of
// a handle is private, and the ends have no public constructor either.  The
// key is no trivially copyable class, so no byte route builds one.
static_assert(!std::is_default_constructible_v<ChannelFrameKey> && !std::is_copy_constructible_v<ChannelFrameKey>);
static_assert(!std::is_trivially_copyable_v<ChannelFrameKey>);
static_assert(!std::is_constructible_v<CheckProducer<Wire>, Wire::ProducerEnd&&>);
static_assert(!std::is_constructible_v<CheckConsumer<Merge>, Merge::ConsumerEnd&&>);
static_assert(!std::is_constructible_v<Wire::ProducerEnd, Wire&,
                                       ::foundation::permissions::Permission<Wire::producer_tag, WitnessBrand>&&>);
template <typename Channel>
concept OpensAnEndWithNoKey = requires(
    Channel& channel, ::foundation::permissions::Permission<typename Channel::producer_tag, WitnessBrand>&& token) {
    channel.producer(std::move(token));
};
static_assert(!OpensAnEndWithNoKey<Wire>);

// The wire producer is linear, and the producer of a merge or of a lane ends
// at the end of its scope, where its pool share goes back.  Each handle has
// the linearity of its end, and it marks its own end, so the drop door ends
// it (foundation/Linear.h).
static_assert(::foundation::linear::is_linear_type(^^Wire::ProducerEnd));
static_assert(!::foundation::linear::is_linear_type(^^Merge::ProducerEnd));
static_assert(!::foundation::linear::is_linear_type(^^Lanes::ProducerEnd));
static_assert(::foundation::linear::is_linear_type(^^Merge::ConsumerEnd));
static_assert(::foundation::linear::is_linear_type(^^Lanes::ConsumerEnd));
static_assert(::foundation::linear::is_linear_type(^^CheckProducer<Wire>));
static_assert(!::foundation::linear::is_linear_type(^^CheckProducer<Merge>));
static_assert(::foundation::linear::is_linear_type(^^CheckConsumer<Lanes>));
static_assert(::foundation::linear::DroppableLinear<CheckProducer<Wire>>);
static_assert(::foundation::linear::DroppableLinear<CheckConsumer<Merge>>);

// The binding and the claim keep the ends of a wire one pointer wide.  A
// merge producer holds its pool share beside the binding.  A handle adds no
// byte to its end.
static_assert(sizeof(Wire::ProducerEnd) == sizeof(void*));
static_assert(sizeof(Wire::ConsumerEnd) == sizeof(void*));
static_assert(sizeof(Merge::ConsumerEnd) == sizeof(void*));
static_assert(sizeof(CheckProducer<Wire>) == sizeof(Wire::ProducerEnd));
static_assert(sizeof(CheckProducer<Merge>) == sizeof(Merge::ProducerEnd));
static_assert(sizeof(CheckConsumer<Lanes>) == sizeof(Lanes::ConsumerEnd));

// The pool of a merge is the one door of its producer root, and no default
// constructor builds a merge channel with an empty pool.  A wire starts with
// no handle.
static_assert(std::is_default_constructible_v<Wire>);
static_assert(!std::is_default_constructible_v<Merge>);
static_assert(!std::is_default_constructible_v<Lanes>);

// Each end and each handle is the pole of its role, with one pop and one
// push.  The pop of a merge takes the tag of Nondet, and the pop of a lanes
// channel takes none, because its order is a function of the items of its
// lanes.
static_assert(is_producer_handle_v<Wire::ProducerEnd> && is_producer_handle_v<Merge::ProducerEnd>);
static_assert(is_producer_handle_v<Lanes::ProducerEnd>);
static_assert(is_consumer_handle_v<Wire::ConsumerEnd> && !is_merge_consumer_handle_v<Wire::ConsumerEnd>);
static_assert(is_merge_consumer_handle_v<Merge::ConsumerEnd>);
static_assert(is_consumer_handle_v<Lanes::ConsumerEnd> && !is_merge_consumer_handle_v<Lanes::ConsumerEnd>);
static_assert(std::is_same_v<producer_handle_value_t<Merge::ProducerEnd>, int>
              && std::is_same_v<consumer_handle_value_t<Wire::ConsumerEnd>, int>);
static_assert(is_producer_handle_v<CheckProducer<Wire>> && is_producer_handle_v<CheckProducer<Lanes>>);
static_assert(is_merge_consumer_handle_v<CheckConsumer<Merge>> && is_consumer_handle_v<CheckConsumer<Wire>>);
static_assert(std::is_same_v<producer_handle_value_t<CheckProducer<Merge>>, int>);
static_assert(ProducerPoleOf<Wire::ProducerEnd, Wire> && ProducerPoleOf<CheckProducer<Wire>, Wire>);
static_assert(ConsumerPoleOf<CheckConsumer<Merge>, Merge> && !ConsumerPoleOf<CheckProducer<Merge>, Merge>);
static_assert(FrameProducerHandleOf<CheckProducer<Wire>, Wire> && !FrameProducerHandleOf<Wire::ProducerEnd, Wire>);

// The one scoped-access verb gives the ring to its body in each form.  A wire
// takes the whole.  A merge and a lanes channel take the lease of the
// producer root out of their pool and the consumer token, because a whole
// would hold no identity of the pool.  Only the pooled forms have a pool, so
// only they have the doors of the producer root.
template <typename Channel>
concept GivesRecombinedAccess = requires(
    Channel& channel, ::foundation::permissions::Permission<typename Channel::whole_tag, WitnessBrand>&& whole) {
    channel.with_recombined_access(std::move(whole), [](typename Channel::RecombinedRing&) noexcept {});
};
template <typename Channel>
concept GivesLeasedAccess = requires(Channel& channel, typename Channel::RecombinedParts&& parts) {
    channel.with_recombined_access(std::move(parts), [](typename Channel::RecombinedRing&) noexcept {});
};
template <typename Channel>
concept HasRootDoors = requires(Channel& channel, ::foundation::effects::cap::Nondet nondet) {
    channel.try_withdraw_producer_root(nondet);
};
static_assert(GivesRecombinedAccess<Wire> && !GivesRecombinedAccess<Merge> && !GivesRecombinedAccess<Lanes>);
static_assert(!GivesLeasedAccess<Wire> && GivesLeasedAccess<Merge> && GivesLeasedAccess<Lanes>);
static_assert(HasRootDoors<Merge> && HasRootDoors<Lanes> && !HasRootDoors<Wire>);

// The producer door of a lanes channel takes the lane, and the other forms
// take none.
template <typename Channel>
concept LendsALane =
    requires(Channel& channel, ::foundation::effects::cap::Nondet nondet, typename Channel::lane_index lane) {
        with_producer(channel, nondet, lane, [](auto /*producer*/) noexcept {});
    };
static_assert(LendsALane<Lanes> && !LendsALane<Merge> && !LendsALane<Wire>);

// The lend of a producer share and the withdraw of the producer root read the
// schedule, so each pooled door takes the tag of Nondet, and a call with no
// tag finds no door.
template <typename Channel>
concept LendsWithNoTag = requires(Channel& channel) { with_producer(channel, [](auto /*producer*/) noexcept {}); };
template <typename Channel>
concept LendsWithTag = requires(Channel& channel, ::foundation::effects::cap::Nondet nondet) {
    with_producer(channel, nondet, [](auto /*producer*/) noexcept {});
};
template <typename Channel>
concept WithdrawsRootWithNoTag = requires(Channel& channel) { channel.try_withdraw_producer_root(); };
static_assert(LendsWithTag<Merge> && !LendsWithNoTag<Merge> && !LendsWithNoTag<Lanes>);
static_assert(!WithdrawsRootWithNoTag<Merge> && !WithdrawsRootWithNoTag<Lanes>);

// The claim of a drain of a pooled form takes the lease of the producer root,
// which proves that no producer handle lives.  A wire claims with no lease.
// Only a pooled form takes its root at rest, for an owner with no tag.
template <typename Channel>
concept ClaimsDrainWithNoRoot = requires(Channel& channel) { channel.claim_drain(); };
template <typename Channel>
concept ClaimsDrainWithRoot =
    requires(Channel& channel, typename Channel::producer_lease const& root) { channel.claim_drain(root); };
template <typename Channel>
concept ClaimsRootAtRest = requires(Channel& channel) { channel.claim_producer_root_at_rest(); };
// The drain and the recombined access borrow their body, so a body that
// holds a linear value by value finds no door.  The gate is a constraint and
// no deleted twin: the door check of foundation/Transfer.h finds no
// specialization of a deleted member template for its probe body, and
// Synchronized<Wire>, <Merge> and <Lanes> hold below.  The drain of a ring of
// lanes has the same gate.
struct PlainDrop {
    void operator()(int /*item*/) const noexcept {}
};
struct LinearDrop {
    ::foundation::permissions::Permission<Wire::producer_tag, WitnessBrand> held;
    void operator()(int /*item*/) const noexcept {}
};
struct PlainAccess {
    void operator()(Wire::RecombinedRing& /*ring*/) const noexcept {}
};
struct LinearAccess {
    ::foundation::permissions::Permission<Wire::producer_tag, WitnessBrand> held;
    void operator()(Wire::RecombinedRing& /*ring*/) const noexcept {}
};
template <class Channel, class Drop>
concept DrainsWith = requires(Channel& channel, typename Channel::DrainClaim& claim, Drop&& drop) {
    channel.drain(claim, std::move(drop));
};
template <class Channel, class Body>
concept AccessesWith = requires(Channel& channel, typename Channel::recombined_token&& token, Body&& body) {
    channel.with_recombined_access(std::move(token), std::move(body));
};
template <class Ring, class Drop>
concept DrainsRingWith = requires(Ring& ring, typename Ring::ConsumerProof const& proof, Drop&& drop) {
    ring.drain(proof, std::move(drop));
};
static_assert(DrainsWith<Wire, PlainDrop> && !DrainsWith<Wire, LinearDrop>);
static_assert(DrainsWith<Lanes, PlainDrop> && !DrainsWith<Lanes, LinearDrop>);
static_assert(AccessesWith<Wire, PlainAccess> && !AccessesWith<Wire, LinearAccess>);
static_assert(DrainsRingWith<Lanes::ring_type, PlainDrop> && !DrainsRingWith<Lanes::ring_type, LinearDrop>);

// A lend door ends a body that it did not call at end_unrun_body of
// foundation/Linear.h, so the body holds no linear value, or each linear
// value that it holds states its own end.  A Permission states its own end,
// and an Option of a Permission does not.
struct LendBodyWithToken {
    ::foundation::permissions::Permission<Wire::producer_tag, WitnessBrand> held;
    void operator()(auto /*producer*/) && noexcept {}
};
struct LendBodyWithOptionalToken {
    ::foundation::core::Option<::foundation::permissions::Permission<Wire::producer_tag, WitnessBrand>> held;
    void operator()(auto /*producer*/) && noexcept {}
};
template <class Channel, class Body>
concept LendsProducerWith = requires(Channel& channel, ::foundation::effects::cap::Nondet nondet, Body&& body) {
    with_producer(channel, nondet, std::move(body));
};
static_assert(HandleLendFrameBody<PlainDrop> && HandleLendFrameBody<LendBodyWithToken>);
static_assert(!HandleLendFrameBody<LendBodyWithOptionalToken>);
static_assert(LendsProducerWith<Merge, LendBodyWithToken> && !LendsProducerWith<Merge, LendBodyWithOptionalToken>);

static_assert(ClaimsDrainWithNoRoot<Wire> && !ClaimsDrainWithNoRoot<Merge> && !ClaimsDrainWithNoRoot<Lanes>);
static_assert(!ClaimsDrainWithRoot<Wire> && ClaimsDrainWithRoot<Merge> && ClaimsDrainWithRoot<Lanes>);
static_assert(!ClaimsRootAtRest<Wire> && ClaimsRootAtRest<Merge> && ClaimsRootAtRest<Lanes>);

// Each end and each handle states the capacity of its channel, so a check of
// the waits reads it from the type of the handle.  A lanes channel holds the
// capacity of each lane times the count of its lanes.
static_assert(Wire::ProducerEnd::channel_capacity == 8 && Wire::ConsumerEnd::channel_capacity == 8);
static_assert(Merge::ProducerEnd::channel_capacity == 8 && Merge::ConsumerEnd::channel_capacity == 8);
static_assert(Lanes::ProducerEnd::channel_capacity == 24 && Lanes::ConsumerEnd::channel_capacity == 24);
static_assert(CheckProducer<Lanes>::channel_capacity == 24 && CheckConsumer<Wire>::channel_capacity == 8);
using WideWire = PermissionedChannel<ChannelForm::wire, int, 64, WitnessTag, OtherBrand>;
static_assert(WideWire::ProducerEnd::channel_capacity == WideWire::channel_capacity
              && WideWire::ConsumerEnd::channel_capacity == 64);

// Two threads write a channel at the same time through its handles, so a
// handle moves to another thread, also beside its sender, as the value of a
// body or a stage.  A handle borrows its channel, which can end before a pop
// on another thread, so a handle is no value of a channel.
static_assert(::foundation::transfer::Synchronized<Wire> && ::foundation::transfer::Synchronized<Merge>
              && ::foundation::transfer::Synchronized<Lanes>);
static_assert(::foundation::transfer::Sendable<Merge::ProducerEnd>
              && ::foundation::transfer::SendableBeside<Merge::ProducerEnd>
              && !::foundation::transfer::SendableOwned<Merge::ProducerEnd>);
static_assert(::foundation::transfer::Sendable<Wire::ConsumerEnd>
              && !::foundation::transfer::SendableOwned<Wire::ConsumerEnd>);
static_assert(::foundation::transfer::Sendable<CheckProducer<Merge>>
              && ::foundation::transfer::SendableBeside<CheckProducer<Merge>>
              && !::foundation::transfer::SendableOwned<CheckProducer<Merge>>);
static_assert(::foundation::transfer::Sendable<CheckConsumer<Wire>>
              && !::foundation::transfer::SendableOwned<CheckConsumer<Wire>>);

}  // namespace detail::channel_self_test

}  // namespace fixy::concurrent
