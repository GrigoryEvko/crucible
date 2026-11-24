#pragma once

// A channel over a ring behind linear permissions.  The handle types carry
// the role, so a push from the consumer side or a pop from the producer side
// does not compile.  The form of the channel (ChannelForm) chooses the ring
// and the producer side.  The consumer handle, the tags, the splits and the
// end of the stream are written one time for each form.
//
// Forms
//     wire    One linear producer token and one linear consumer token, over
//             the SPSC ring of fixy/concurrent/SpscRing.h.  In the net of a
//             program the channel is a plain wire.  The producer handle is
//             linear (foundation/Linear.h), and its doors are release(),
//             which gives the token back, and close(), which ends the stream.
//     merge   Producer shares that a pool lends, and one linear consumer
//             token, over the MPSC ring of fixy/concurrent/MpscRing.h.  In
//             the net of a program the channel is a merge agent: the schedule
//             decides the order of the items of many producers, so each pop
//             takes the bare tag of Nondet.  The producer side is fractional
//             because the ring has many producers and its membership changes.
//             A linear producer token would force a handoff between producer
//             threads or a split into a fixed number of slots.  The pool
//             parks the producer token of the split root, and each producer
//             handle holds one share of it.
//     lanes   Producer shares that a pool lends, each with one lane of the
//             ring of lanes of fixy/concurrent/LaneRing.h, and one linear
//             consumer token.  The consumer reads the lanes in one fixed
//             order (the order of that header): the items of a lane in the
//             order of their pushes, and one item of each lane in turn, in
//             the order of the lane indexes.  So the merged stream is a
//             function of the items of each lane, and a pop takes no tag of
//             Nondet.  In the net of a program the channel is a fixed join of
//             its lanes, and not a merge agent.  The count of the lanes is
//             the sixth parameter, and Capacity is the capacity of each lane.
//             The caller names the lane of each producer handle, so the lane
//             of a producer is an input of the program, and not a result of
//             the schedule.  A claim of each lane keeps one live producer of
//             a lane.  The consumer waits for the lane of its turn (the stall
//             of LaneRing.h), so each lane needs a producer that ends it.
//     In each form the consumer side is linear, because the pop of each ring
//     assumes one consumer: the tail has one writer and no synchronization.
//
// Brand
//     A channel has the brand of the root that its permissions grow from.
//     The caller mints the whole root, names its brand in the channel type,
//     and splits the root into the producer token and the consumer token.  A
//     root of another call site has another brand, so its tokens open no
//     endpoint of this channel, and two channels of two root sites are two
//     types, which a pipeline does not join.  The tags hold the form, so a
//     root of one form opens no channel of the other form, and channel_t
//     names the channel of a root type.
//
//     One call site that runs two times mints two roots of one brand, and no
//     type tells them apart.  The run-time floor there is a claim on each
//     linear role (foundation/ChannelBinding.h): a second live handle of one
//     role ends the process, and a handle takes the role again after the last
//     one ended.
//
// End of the stream
//     The end of the last live producer handle with its token closes the
//     channel, and try_receive of the consumer then gives NoItem::Closed after
//     the last item (fixy/concurrent/ChannelEnd.h).  A release does not close
//     the channel, and the next producer handle opens it again.  A channel of
//     lanes closes when each lane ended: the end of a producer handle ends
//     its lane (the end of a lane of fixy/concurrent/LaneRing.h).  A lane
//     ends one time in a stream, so the producer door refuses a lane that
//     ended, and the drain of the channel opens each lane again.
//
// Frame doors
//     A frame door opens the end of one role, gives its body a handle of that
//     end by value, and gives the result of the body.  The handle type takes
//     the scope brand of the call (CRUCIBLE_SCOPE_BRAND of foundation/
//     Brand.h), so no code outside the body can spell it.  The rule
//     brand-escape of the quarantine plugin refuses a result and an object of
//     static or thread storage duration that holds the brand, and the gate of
//     each door refuses at the type a result that names it (FrameResult of
//     foundation/core/Choice.h).  So a handle does not outlive the frame of
//     its door, and each handle ends before its channel.  That gate is a
//     constraint of each door, with no deleted twin: each candidate of a
//     call takes a scope brand of its own, so a twin would compile the body
//     again on a second handle type, and nested frames would double that at
//     each level.  The doors:
//         with_producer(channel, token, body)          a wire
//         with_producer(channel, nondet, body)         a merge
//         with_producer(channel, nondet, lane, body)   a lanes channel
//         with_consumer(channel, token, body)          each form
//         with_ends(channel, producer token, consumer token, body)
//                                                      a wire
//     A door of a pooled form gives an Option of the result of the body (Unit
//     for a body that gives void), and no value while the producer root is
//     out of the pool.
//
//     A door runs its body one time, so it owns the body and calls it through
//     the once door call_once of foundation/Linear.h, and a named body meets
//     a deleted twin with its reason.  A body can then hold a linear value by
//     value, and the rule must-consume reads each consume of it.  The doors
//     take this form and not the gate HoldsNoLinearValue of a borrowed body:
//     nested frames, such as the four frames of the pipeline of
//     src/BackgroundThread.cpp, give an inner body the handles of the outer
//     frames, and the inner body consumes them.  A capture by reference of a
//     linear value hides that consume from each rule, so an inner body takes
//     the outer handles by move.  The doors serve each channel family that names its
//     ends, its tags and its form in the shape of this header:
//     fixy/concurrent/PermissionedBoxChannel.h and crucible/
//     PermissionedMetaLog.h.
//
//     The ends of the roles (ProducerEnd and ConsumerEnd) are nested classes,
//     so the check of synchronized{} of foundation/Transfer.h reads their
//     doors.  A handle (ProducerHandle, ConsumerHandle) derives from its end
//     privately and shows the doors of the end, so no code outside it binds
//     a reference to the end.  The type of an end stays spellable, but no
//     code outside the frame doors gets a value of it: each constructor of an
//     end is private, and each claim door that makes an end takes a
//     ChannelFrameKey, whose only friends are the frame doors.  The doors are
//     free functions and no members, because the door check cannot read a
//     member template with a pack before a defaulted brand.
//
//     What the brand does not cover:
//       - A channel in a Box or an Option that the body ends.  The body can
//         end the owner of the channel while it holds a handle, and the next
//         push then writes a channel that ended.
//       - An owner thread that keeps the closure past the body.  A thread
//         door of an object that outlives the body can keep a closure that
//         holds a handle, and the type of the thread erases the closure.

#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/EventCount.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/LaneRing.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/SpscRing.h>
#include <fixy/concurrent/WorkingSet.h>

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here takes
// a cache slot of its own rather than the zero a bare payload has.  The spsc
// structs are the claims of the wire form, the mpsc structs are the claims
// of the merge form, and the lanes structs are the claims of the lanes form.
namespace fixy::row_discipline {
struct spsc_channel;
struct spsc_producer;
struct spsc_consumer;
struct mpsc_channel;
struct mpsc_producer;
struct mpsc_consumer;
struct lanes_channel;
struct lanes_producer;
struct lanes_consumer;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

// The form of a permissioned channel (the forms).  A planned form, such as
// a queue of many producers and many consumers or a broadcast channel, adds
// a value here and a specialization of detail::channel_form_parts.
enum class ChannelForm : std::uint8_t {
    wire = 0,
    merge = 1,
    lanes = 2,
};

// The triple is specialized for splitting at the foot of this file, so a user
// tag takes no per-tag boilerplate.
//
// A ring is memory the process already owns, so a touch of it through either
// endpoint incurs no effect: the three tags declare the empty row.  They are
// templates, so the row is a member rather than an edge.
namespace channel_tag {

template <ChannelForm Form, typename UserTag>
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
template <ChannelForm Form, typename UserTag>
struct Producer {
    using permission_row = ::foundation::effects::Row<>;
};
template <ChannelForm Form, typename UserTag>
struct Consumer {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace channel_tag

namespace detail {

// The pool of a wire channel: none.  The claim of the producer role is the
// whole producer side of a wire.
struct no_producer_pool {};

// The lane of a producer handle and the claims of the lanes, for a form with
// no lanes: none.  The handle of each such form builds it from the first
// index.
struct no_lane {
    constexpr explicit no_lane(::foundation::core::StaticIndex<0> /*first*/) noexcept {}
};
struct no_lane_claims {};

// The two texts of a refusal of a pooled form, one for each form.  A caller
// names each field, so the two texts cannot swap.
struct PooledTexts {
    ::foundation::core::TextView merge{};
    ::foundation::core::TextView lanes{};
};

// What a form chooses: the ring, the share that a producer handle holds, the
// pool of the producer side, the lane of a producer handle, the claims of the
// lanes, the row disciplines, and the cache lines of one call of each handle.
// Owner is the channel.  The ring of a wire names it, so only the channel
// makes the proofs of the two sides of that ring.  The ring of lanes names it,
// so only the channel reads the end of a lane.
template <ChannelForm Form, RingValue T, std::size_t Capacity, LaneCount Lanes, typename ProducerTag, typename Brand,
          typename Owner>
struct channel_form_parts;

template <RingValue T, std::size_t Capacity, typename ProducerTag, typename Brand, typename Owner>
struct channel_form_parts<ChannelForm::wire, T, Capacity, LaneCount{1}, ProducerTag, Brand, Owner> {
    using ring_type = SpscRing<T, Capacity, Owner>;
    // The producer handle holds the linear token, and the claim of the
    // producer role keeps one live producer handle.
    using producer_share = ::foundation::permissions::Permission<ProducerTag, Brand>;
    using producer_pool = no_producer_pool;
    using lane = no_lane;
    using lane_claims = no_lane_claims;
    using channel_discipline = ::fixy::row_discipline::spsc_channel;
    using producer_discipline = ::fixy::row_discipline::spsc_producer;
    using consumer_discipline = ::fixy::row_discipline::spsc_consumer;
    // A push reads the head and the line of the copies of the producer.  A
    // pop reads the tail, the line of the copies of the consumer, and the
    // roster of the producers, which try_receive reads when the ring is empty.
    // Each side reads the index of the other side only when its cache says
    // that the ring is full or empty.
    static constexpr std::size_t producer_lines = 2;
    static constexpr std::size_t consumer_lines = 3;
};

template <RingValue T, std::size_t Capacity, typename ProducerTag, typename Brand, typename Owner>
struct channel_form_parts<ChannelForm::merge, T, Capacity, LaneCount{1}, ProducerTag, Brand, Owner> {
    using ring_type = MpscRing<T, Capacity, Owner>;
    // The producer handle holds one share of the pool, which parks the
    // producer token.
    using producer_share = ::foundation::permissions::SharedPermissionGuard<ProducerTag, Brand>;
    using producer_pool = ::foundation::permissions::SharedPermissionPool<ProducerTag, Brand>;
    using lane = no_lane;
    using lane_claims = no_lane_claims;
    using channel_discipline = ::fixy::row_discipline::mpsc_channel;
    using producer_discipline = ::fixy::row_discipline::mpsc_producer;
    using consumer_discipline = ::fixy::row_discipline::mpsc_consumer;
    // A push reads the head, and the turn word on the line of its cell.  A pop
    // reads the tail, the turn word on the line of its cell, and the roster.
    static constexpr std::size_t producer_lines = 1;
    static constexpr std::size_t consumer_lines = 2;
};

template <RingValue T, std::size_t Capacity, LaneCount Lanes, typename ProducerTag, typename Brand, typename Owner>
struct channel_form_parts<ChannelForm::lanes, T, Capacity, Lanes, ProducerTag, Brand, Owner> {
    using ring_type = LaneRing<T, Capacity, Lanes, Owner>;
    // The producer handle holds one share of the pool, which parks the
    // producer token, and the proof of its lane.  The channel mints the proof
    // under the claim of the lane, so one live producer handle holds the
    // proof of each lane (the writers of fixy/concurrent/LaneRing.h).
    using producer_share = ::foundation::permissions::SharedPermissionGuard<ProducerTag, Brand>;
    using producer_pool = ::foundation::permissions::SharedPermissionPool<ProducerTag, Brand>;
    using lane = typename ring_type::LaneProof;
    using lane_claims = ::foundation::core::FixedArray<::foundation::EndpointClaim, static_cast<std::size_t>(Lanes)>;
    using channel_discipline = ::fixy::row_discipline::lanes_channel;
    using producer_discipline = ::fixy::row_discipline::lanes_producer;
    using consumer_discipline = ::fixy::row_discipline::lanes_consumer;
    // A push reads the head of its lane and the line of the copies of the
    // producer of the lane.  A pop reads the turn, the tail of its lane and
    // the line of the copies of the consumer of the lane, and the end word of
    // the lane when the lane is empty.
    static constexpr std::size_t producer_lines = 2;
    static constexpr std::size_t consumer_lines = 4;
};

// The pops of a consumer handle of payload T.  Each form gives one
// declaration of each pop, so fixy/concurrent/HandleTraits.h reads the
// consumer pole and its payload off the address of the member.  The handle
// gives the body of each pop in its private pop_ and receive_, and it
// befriends its own specialization of this base.  The consumer handle of a
// Box channel has the same shape in fixy/concurrent/PermissionedBoxChannel.h.
template <ChannelForm Form, typename T, typename Handle>
class consumer_pops;

template <typename T, typename Handle>
class consumer_pops<ChannelForm::wire, T, Handle> {
public:
    // The oldest item, or no value when the ring is empty.  It does not tell
    // an empty ring from a closed channel: try_receive does.
    [[nodiscard, gnu::hot]] ::foundation::core::Option<T> try_pop() noexcept {
        return static_cast<Handle&>(*this).pop_();
    }

    // The oldest item, NoItem::Empty, or NoItem::Closed after the last item
    // of a closed channel (the end of the stream of fixy/concurrent/
    // ChannelEnd.h).
    [[nodiscard, gnu::hot]] ::foundation::core::Result<T, NoItem> try_receive() noexcept {
        return static_cast<Handle&>(*this).receive_();
    }
};

// The pops of the lanes form take no tag, as the pops of a wire do: the
// order of the items is a function of the items of each lane (the order of
// fixy/concurrent/LaneRing.h).
template <typename T, typename Handle>
class consumer_pops<ChannelForm::lanes, T, Handle> {
public:
    // The next item of the merged stream, or no value while the lane of the
    // turn is empty.  It does not tell a wait from the end: try_receive does.
    [[nodiscard, gnu::hot]] ::foundation::core::Option<T> try_pop() noexcept {
        return static_cast<Handle&>(*this).pop_();
    }

    // The next item, NoItem::Empty while the lane of the turn is empty and
    // open, or NoItem::Closed after the last item of the last lane.
    [[nodiscard, gnu::hot]] ::foundation::core::Result<T, NoItem> try_receive() noexcept {
        return static_cast<Handle&>(*this).receive_();
    }
};

// A pop of a merge is a merge.  The order of the items of many producers
// depends on the schedule, so two runs with the same inputs can pop two
// sequences.  Thus each pop takes the bare tag of Nondet, and only a context
// whose row holds Nondet gives one.  The tag is an empty value, so the call
// costs what a pop with no argument costs.
template <typename T, typename Handle>
class consumer_pops<ChannelForm::merge, T, Handle> {
public:
    // The pops of the wire form, with the tag of Nondet.
    [[nodiscard, gnu::hot]] ::foundation::core::Option<T> try_pop(::foundation::effects::cap::Nondet nondet) noexcept {
        return static_cast<Handle&>(*this).pop_(nondet);
    }

    [[nodiscard, gnu::hot]] ::foundation::core::Result<T, NoItem>
    try_receive(::foundation::effects::cap::Nondet nondet) noexcept {
        return static_cast<Handle&>(*this).receive_(nondet);
    }
};

}  // namespace detail

// A value of a channel: a value of a ring that moves to another thread
// (foundation/Transfer.h), because a push on one thread gives it to a pop on
// another thread.  The producer can end before the pop, so the value holds
// no borrow: it is SendableOwned, and an owner such as the word of a Box
// passes.
template <class T>
concept ChannelValue = RingValue<T> && ::foundation::transfer::Sendable<T> && ::foundation::transfer::SendableOwned<T>;

namespace detail {

// The count of the lanes of a channel: the one count of the lanes form, and
// one lane for each other form.
template <LaneCount... Lanes>
inline constexpr LaneCount lane_count_of_v = LaneCount{1};
template <LaneCount Lanes>
inline constexpr LaneCount lane_count_of_v<Lanes> = Lanes;

}  // namespace detail

// True when the counts of the lanes fit the form: one count for the lanes
// form, and none for each other form.
template <ChannelForm Form, LaneCount... Lanes>
concept LanesFitForm = sizeof...(Lanes) == (Form == ChannelForm::lanes ? std::size_t{1} : std::size_t{0});

// The handles of the two roles (frame doors).  The definitions follow the
// channel.
template <class Channel, ::foundation::brand::IsBrand Scope>
class ProducerHandle;
template <class Channel, ::foundation::brand::IsBrand Scope>
class ConsumerHandle;

// A channel of the form Form.  Each channel family states its form in
// channel_form.
template <class Channel, ChannelForm Form>
concept ChannelOfForm = requires { Channel::channel_form; } && (Channel::channel_form == Form);

namespace detail {

// The handle that a frame door of the channel gives its body on the scope
// brand Scope.  Each channel family names its handles through the member
// alias templates producer_handle and consumer_handle.
template <class Channel, class Scope>
using producer_handle_of_t = typename Channel::template producer_handle<Scope>;
template <class Channel, class Scope>
using consumer_handle_of_t = typename Channel::template consumer_handle<Scope>;

}  // namespace detail

// The producer door of a wire (frame doors).  The producer token opens the
// end, and the body gets its handle.
template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::wire>
         && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>>
auto with_producer(
    Channel& channel,
    ::foundation::permissions::Permission<typename Channel::producer_tag, typename Channel::brand_type>&& token,
    Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>>;

// The producer door of a merge (frame doors).  It lends a share of the pool,
// and it gives no value while the producer root is out of the pool.
template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::merge>
         && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>> && HandleLendFrameBody<Body>
[[nodiscard]] auto with_producer(Channel& channel, ::foundation::effects::cap::Nondet nondet, Body&& body) noexcept
    -> ::foundation::core::Option<
        detail::handle_frame_value_t<std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>>>>;

// The producer door of a lanes channel (frame doors).  It lends a share of
// the pool for the producer of the lane, and it gives no value while the
// producer root is out of the pool.
template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::lanes>
         && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>> && HandleLendFrameBody<Body>
[[nodiscard]] auto with_producer(Channel& channel, ::foundation::effects::cap::Nondet nondet,
                                 typename Channel::lane_index lane, Body&& body) noexcept
    -> ::foundation::core::Option<
        detail::handle_frame_value_t<std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>>>>;

// The consumer door of each form (frame doors).  The consumer token opens the
// end, and the body gets its handle.
template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && HandleFrameLeaves<Body, Scope, detail::consumer_handle_of_t<Channel, Scope>>
auto with_consumer(
    Channel& channel,
    ::foundation::permissions::Permission<typename Channel::consumer_tag, typename Channel::brand_type>&& token,
    Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::consumer_handle_of_t<Channel, Scope>>;

// The door of the two ends of a wire (frame doors).  The body gets the
// producer handle and the consumer handle of one frame.
template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::wire>
         && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>,
                              detail::consumer_handle_of_t<Channel, Scope>>
auto with_ends(Channel& channel,
               ::foundation::permissions::Permission<typename Channel::producer_tag, typename Channel::brand_type>&&
                   producer_token,
               ::foundation::permissions::Permission<typename Channel::consumer_tag, typename Channel::brand_type>&&
                   consumer_token,
               Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>,
                                                             detail::consumer_handle_of_t<Channel, Scope>>;

// A body that the caller names binds to these overloads, so the refusal
// gives its reason.
template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::wire>
             && HandleNamedFrameBody<Body, detail::producer_handle_of_t<Channel, Scope>>
void with_producer(
    Channel& channel,
    ::foundation::permissions::Permission<typename Channel::producer_tag, typename Channel::brand_type>&& token,
    Body&& body) = delete(CRUCIBLE_HANDLE_FRAME_NAMED_BODY_REFUSAL);

template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::merge>
             && HandleNamedFrameBody<Body, detail::producer_handle_of_t<Channel, Scope>>
void with_producer(Channel& channel, ::foundation::effects::cap::Nondet nondet,
                   Body&& body) = delete(CRUCIBLE_HANDLE_FRAME_NAMED_BODY_REFUSAL);

template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::lanes>
             && HandleNamedFrameBody<Body, detail::producer_handle_of_t<Channel, Scope>>
void with_producer(Channel& channel, ::foundation::effects::cap::Nondet nondet, typename Channel::lane_index lane,
                   Body&& body) = delete(CRUCIBLE_HANDLE_FRAME_NAMED_BODY_REFUSAL);

template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && HandleNamedFrameBody<Body, detail::consumer_handle_of_t<Channel, Scope>>
void with_consumer(
    Channel& channel,
    ::foundation::permissions::Permission<typename Channel::consumer_tag, typename Channel::brand_type>&& token,
    Body&& body) = delete(CRUCIBLE_HANDLE_FRAME_NAMED_BODY_REFUSAL);

template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::wire>
             && HandleNamedFrameBody<Body, detail::producer_handle_of_t<Channel, Scope>,
                                     detail::consumer_handle_of_t<Channel, Scope>>
void with_ends(Channel& channel,
               ::foundation::permissions::Permission<typename Channel::producer_tag, typename Channel::brand_type>&&
                   producer_token,
               ::foundation::permissions::Permission<typename Channel::consumer_tag, typename Channel::brand_type>&&
                   consumer_token,
               Body&& body) = delete(CRUCIBLE_HANDLE_FRAME_NAMED_BODY_REFUSAL);

// A drop of the drain of a channel of the form Form: it takes each item and
// does not throw.  The drop of a merge is empty, because the order of its
// items reads the merge.  A body of the recombined access takes the ring of
// the access.
template <class Drop, class T, ChannelForm Form>
concept ChannelDrainDrop =
    (Form != ChannelForm::merge || __is_empty(__remove_cvref(Drop))) && std::is_nothrow_invocable_v<Drop&, T>;
template <class Body, class Ring>
concept RecombinedAccessBody = std::is_invocable_v<Body, Ring&>;

// The key of the claim doors of a channel (frame doors).  Its constructor is
// private, and its only friends are the frame doors, so only a frame door
// calls a claim door of a channel.  A claim door takes the key by reference to
// const, so a channel that passes the key on to its inner channel copies
// nothing, and the key does not copy.  Its constructor is user-provided, and
// its seal makes its assignment user-provided, so no byte route, such as
// std::bit_cast, builds a key.  Each handle names the key as its one friend,
// and only build_handle_, a private door of the key, calls the constructor of
// a handle.
class ChannelFrameKey final {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr ChannelFrameKey() noexcept {}

    // The handle of a frame over the end that a claim door gave.
    template <class Handle, class End>
    [[nodiscard]] static constexpr Handle build_handle_(End&& end) noexcept {
        return Handle{static_cast<End&&>(end)};
    }

    template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope>
        requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::wire>
             && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>>
    friend auto with_producer(
        Channel& channel,
        ::foundation::permissions::Permission<typename Channel::producer_tag, typename Channel::brand_type>&& token,
        Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>>;

    template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope>
        requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::merge>
             && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>>
             && HandleLendFrameBody<Body>
    friend auto with_producer(Channel& channel, ::foundation::effects::cap::Nondet nondet, Body&& body) noexcept
        -> ::foundation::core::Option<
            detail::handle_frame_value_t<std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>>>>;

    template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope>
        requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::lanes>
             && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>>
             && HandleLendFrameBody<Body>
    friend auto with_producer(Channel& channel, ::foundation::effects::cap::Nondet nondet,
                              typename Channel::lane_index lane, Body&& body) noexcept
        -> ::foundation::core::Option<
            detail::handle_frame_value_t<std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>>>>;

    template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope>
        requires(sizeof...(Refused) == 0)
             && HandleFrameLeaves<Body, Scope, detail::consumer_handle_of_t<Channel, Scope>>
    friend auto with_consumer(
        Channel& channel,
        ::foundation::permissions::Permission<typename Channel::consumer_tag, typename Channel::brand_type>&& token,
        Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::consumer_handle_of_t<Channel, Scope>>;

    template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope>
        requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::wire>
             && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>,
                                  detail::consumer_handle_of_t<Channel, Scope>>
    friend auto
    with_ends(Channel& channel,
              ::foundation::permissions::Permission<typename Channel::producer_tag, typename Channel::brand_type>&&
                  producer_token,
              ::foundation::permissions::Permission<typename Channel::consumer_tag, typename Channel::brand_type>&&
                  consumer_token,
              Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>,
                                                            detail::consumer_handle_of_t<Channel, Scope>>;

public:
    ChannelFrameKey(ChannelFrameKey const&) = delete("a key of the frame doors lives in the frame of one door call");
    ChannelFrameKey& operator=(ChannelFrameKey const&) = delete("a key of the frame doors does not change");
    ~ChannelFrameKey() = default;
};

// UserTag and Brand have no default.  A default tag would give every channel
// that omits it one set of Permission types.  The erased brand would let a
// token of every root site open the endpoints.  The lanes form names the
// count of its lanes after the brand, and each other form names none, so the
// type of a wire or a merge keeps its five arguments.
//
// The producer handles push and the consumer handle pops on their threads at
// the same time by design.  The ring keeps its indexes, and each index has
// the one writer that the role claims and the Permissions of the handles
// name.  Each member is private, and only the handles reach the ring, so the
// channel states synchronized{} (foundation/Transfer.h).
template <ChannelForm Form, ChannelValue T, std::size_t Capacity, typename UserTag,
          ::foundation::brand::IsFreshBrand Brand, LaneCount... Lanes>
    requires LanesFitForm<Form, Lanes...>
class[[= ::foundation::transfer::synchronized{}]] PermissionedChannel
    : public ::foundation::Pinned<PermissionedChannel<Form, T, Capacity, UserTag, Brand, Lanes...>> {
    using parts = detail::channel_form_parts<Form, T, Capacity, detail::lane_count_of_v<Lanes...>,
                                             channel_tag::Producer<Form, UserTag>, Brand, PermissionedChannel>;

    // True for a form whose producer side is a pool of shares.
    static constexpr bool is_pooled = Form != ChannelForm::wire;

public:
    using value_type = T;
    using user_tag = UserTag;
    using brand_type = Brand;
    using whole_tag = channel_tag::Whole<Form, UserTag>;
    using producer_tag = channel_tag::Producer<Form, UserTag>;
    using consumer_tag = channel_tag::Consumer<Form, UserTag>;
    // The producer root of a merge or a lanes channel while it is out of the
    // pool of the channel (the lease of foundation/permissions/
    // SharedPermission.h).
    using producer_lease = ::foundation::permissions::ExclusiveLease<producer_tag, Brand>;
    using ring_type = typename parts::ring_type;
    // The lane of a producer handle of the lanes form.
    using lane_index = LaneIndex<detail::lane_count_of_v<Lanes...>>;

    // The form, which the net of a program reads: a wire, a merge agent, or a
    // fixed join of lanes.
    static constexpr ChannelForm channel_form = Form;
    // The items that the channel holds at most: the capacity of the ring, or
    // of each lane times the count of the lanes.
    static constexpr std::size_t channel_capacity = ring_type::channel_capacity;
    static constexpr LaneCount lane_count = detail::lane_count_of_v<Lanes...>;
    using row_discipline = typename parts::channel_discipline;
    using row_payload = T;

    // The channel's identity is its address, since the ring's atomics depend
    // on a stable one.

private:
    // The parts that a producer of each form writes: the ring, the roster of
    // the producers and the claims of the lanes.  A producer handle of a merge
    // or a lanes channel binds only this part.  For those two forms each part
    // of it is Synchronized: the ring by its tickets or by the proofs of its
    // lanes, and the roster and the claims by their atomic cells.  So two
    // producer handles write it at the same time with no proof, and a producer
    // handle reaches no other part of the channel, such as the pool of its
    // shares.  The identity names the channel for the pipelines, through a
    // pointer to const, which writes nothing.
    class ProducerSide {
    public:
        explicit ProducerSide(PermissionedChannel const& channel) noexcept : identity_{__builtin_addressof(channel)} {}
        ProducerSide(ProducerSide const&) = delete("the producer side lives in its channel, whose address names it");
        ProducerSide& operator=(ProducerSide const&) = delete("the producer side lives in its channel");
        ~ProducerSide() = default;

        // The binding of a producer handle of a merge or a lanes channel.
        [[nodiscard]] ::foundation::ChannelBinding<ProducerSide> bind() noexcept
            requires(is_pooled)
        {
            return ::foundation::ChannelBinding<ProducerSide>{*this, ::foundation::BindingKey<ProducerSide>{}};
        }

        // The end of a producer handle of a merge or a lanes channel with its
        // share.  The last producer of a merge closes the channel.  A producer
        // of the lanes form ends its lane and then gives the claim of the lane
        // back, so the next claim of the lane reads the end.  A handle ends one
        // time, so the end is cold, and a stage that holds the handle keeps one
        // call in place of the loop of the roster.
        CRUCIBLE_COLD void end_producer(typename parts::lane const& lane) noexcept
            requires(is_pooled)
        {
            if constexpr (Form == ChannelForm::lanes) {
                ring_.end_lane(lane);
                lane_claims_.with_element(lane.lane(),
                                          [](::foundation::EndpointClaim& claim) noexcept { claim.release(); });
            } else {
                static_cast<void>(lane);
                producers_.end_producer();
            }
        }

        [[nodiscard]] ::foundation::ChannelIdentity<PermissionedChannel> identity() const noexcept { return identity_; }

    private:
        // The channel and its handles reach the parts.
        friend class PermissionedChannel;

        ring_type ring_;
        // The live producer handles of a wire or a merge.  A lanes channel
        // tells its end through the end words of its lanes.
        ProducerRoster producers_;
        // The claim of each lane of the lanes form, which its producer handle
        // holds for its life.
        [[no_unique_address]] typename parts::lane_claims lane_claims_;
        ::foundation::ChannelIdentity<PermissionedChannel> const identity_;
    };

    // The binding of a producer handle: the producer side of a merge or a
    // lanes channel, and the whole channel of a wire, whose handle holds the
    // linear producer token.
    using producer_binding =
        ::foundation::ChannelBinding<std::conditional_t<is_pooled, ProducerSide, PermissionedChannel>>;

public:
    // A wire channel starts with no handle.  The caller keeps both tokens.
    PermissionedChannel() noexcept
        requires(Form == ChannelForm::wire)
    = default;

    // The pool of a merge or a lanes channel parks the producer token of the
    // split root, and each producer handle holds one share of it.  No default
    // constructor builds such a channel with an empty pool.
    explicit PermissionedChannel(::foundation::permissions::Permission<producer_tag, Brand>&& producer_root) noexcept
        requires(is_pooled)
        : producer_pool_{std::move(producer_root)} {}

    class[[= ::foundation::linear::linear_when<Form == ChannelForm::wire>]] ProducerEnd {
        // The move clears the binding, so a moved-from handle cannot push: a
        // defaulted move of an empty Permission is a no-op, and a reference
        // or a plain pointer would let the source and the target both go on
        // using the one token or the one share.  A moved-from producer of a
        // merge would push with no pool share, during the window that
        // assumes every producer out.
        producer_binding ch_;
        [[no_unique_address]] typename parts::producer_share share_;
        // The proof of the lane of a producer of the lanes form, and no value
        // in another form.  Each constructor builds it.
        [[no_unique_address]] typename parts::lane lane_;

        constexpr ProducerEnd(PermissionedChannel& channel, typename parts::producer_share&& share) noexcept
            requires(Form == ChannelForm::wire)
            : ch_{channel, ::foundation::BindingKey<PermissionedChannel>{}},
              share_{std::move(share)},
              lane_{::foundation::core::index<0>()} {}
        constexpr ProducerEnd(PermissionedChannel& channel, typename parts::producer_share&& share) noexcept
            requires(Form == ChannelForm::merge)
            : ch_{channel.side_.bind()}, share_{std::move(share)}, lane_{::foundation::core::index<0>()} {}
        constexpr ProducerEnd(PermissionedChannel& channel, typename parts::producer_share&& share,
                              typename parts::lane&& lane) noexcept
            requires(Form == ChannelForm::lanes)
            : ch_{channel.side_.bind()}, share_{std::move(share)}, lane_{std::move(lane)} {}
        friend class PermissionedChannel;

        // The ring of the binding.
        [[nodiscard, gnu::always_inline]] ring_type& ring_of_() const noexcept {
            if constexpr (is_pooled) {
                return ch_->ring_;
            } else {
                return ch_->side_.ring_;
            }
        }

        // The end of the producer of this handle (ProducerSide::end_producer).
        void end_() noexcept {
            if constexpr (is_pooled) {
                ch_->end_producer(lane_);
            } else {
                ch_->end_producer_(lane_);
            }
        }

    public:
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<parts::producer_lines, T>;
        using row_discipline = typename parts::producer_discipline;
        using row_payload = T;
        using brand_type = Brand;
        // The channel this handle acts on.  A pipeline joins two stages only
        // when the producer of one and the consumer of the next name the same
        // channel, and at the mint, the same channel instance.
        using channel_type = PermissionedChannel;
        static constexpr ChannelForm channel_form = Form;
        // The capacity of the channel.  A check of the waits reads it from the
        // type of the handle, as from the type of the channel.
        static constexpr std::size_t channel_capacity = ring_type::channel_capacity;

        ProducerEnd(const ProducerEnd&) =
            delete("ProducerEnd owns the producer token or one pool share — copy would duplicate it");
        ProducerEnd& operator=(const ProducerEnd&) =
            delete("ProducerEnd owns the producer token or one pool share — assignment would overwrite it");
        constexpr ProducerEnd(ProducerEnd&&) noexcept = default;
        ProducerEnd& operator=(ProducerEnd&&) = delete(
            "ProducerEnd binds to ONE channel for life — rebinding would orphan the original token "
            "and silently allow a second producer to coexist");

        // A moved-from handle holds no role, so only the bound one ends a
        // producer of the stream.  The last one closes the channel, and a
        // producer of the lanes form ends its lane.  The token or the share
        // ends with the handle, after this body, so no item of this producer
        // comes after the end.
        ~ProducerEnd() {
            if (ch_.is_bound()) end_();
        }

        // The release door of a wire producer.  The handle gives the producer
        // role back to the channel and the token back to the caller, and it is
        // unbound after the call.  The token can open a producer handle again,
        // so the channel stays open.
        [[nodiscard]] ::foundation::permissions::Permission<producer_tag, Brand> release() && noexcept
            requires(Form == ChannelForm::wire)
        {
            ch_->release_producer_();
            ch_.unbind();
            return std::move(share_);
        }

        // The close door.  The handle ends its producer of the stream before
        // the end of its scope, so a reader of the code sees where the stream
        // ends, and it is unbound after the call.  The last producer handle
        // closes the channel, and a producer of the lanes form ends its lane.
        // The token of a wire ends here, and the share of a merge or of a lane
        // goes back to its pool here.
        void close() && noexcept {
            end_();
            ch_.unbind();
            if constexpr (Form == ChannelForm::wire) {
                ::foundation::linear::drop(std::move(share_));
            } else {
                typename parts::producer_share const ended{std::move(share_)};
            }
        }

        // The value comes by reference to const, which is the shape of a
        // producer pole in fixy/concurrent/HandleTraits.h.  A value parameter
        // hides the pole, and no stage can then feed the channel.  The handle
        // of a wire holds the linear producer token and the claim of its
        // role, so the channel gives it the proof of the one producer of the
        // ring (the proven form of fixy/concurrent/SpscRing.h).
        [[nodiscard, gnu::hot]] bool try_push(T const& item) noexcept {
            if constexpr (Form == ChannelForm::wire) {
                return ring_of_().try_push_proven(PermissionedChannel::prove_producer_(), item);
            } else if constexpr (Form == ChannelForm::lanes) {
                return ring_of_().try_push(lane_, item);
            } else {
                return ring_of_().try_push(item);
            }
        }

        // The lane of this producer of the lanes form.
        [[nodiscard]] constexpr lane_index lane() const noexcept
            requires(Form == ChannelForm::lanes)
        {
            return lane_.lane();
        }

        // Snapshots.  Sound for telemetry and for deciding whether to keep
        // retrying, never for a correctness invariant.  Each takes the bare
        // tag of Nondet, as the snapshot of the ring does.
        [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return ring_of_().empty_approx(nondet);
        }
        [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return ring_of_().size_approx(nondet);
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return channel_capacity; }

        [[nodiscard]] constexpr ::foundation::ChannelIdentity<PermissionedChannel> channel_identity() const noexcept {
            if constexpr (is_pooled) {
                return ch_.is_bound() ? ch_->identity() : ::foundation::ChannelIdentity<PermissionedChannel>{};
            } else {
                return ch_.identity();
            }
        }
    };

    class[[= ::foundation::linear::linear{}]] ConsumerEnd : public detail::consumer_pops<Form, T, ConsumerEnd> {
        // A binding that the move clears, for the same reason as in
        // ProducerEnd.
        ::foundation::ChannelBinding<PermissionedChannel> ch_;
        [[no_unique_address]] ::foundation::permissions::Permission<consumer_tag, Brand> perm_;

        constexpr ConsumerEnd(PermissionedChannel& channel,
                              ::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept
            : ch_{channel, ::foundation::BindingKey<PermissionedChannel>{}}, perm_{std::move(perm)} {}
        friend class PermissionedChannel;
        friend class detail::consumer_pops<Form, T, ConsumerEnd>;

        // The bodies of the pops of detail::consumer_pops.  The pack holds
        // the tag of Nondet for a merge and no value for a wire or a lanes
        // channel.  The handle of a wire holds the linear consumer token and
        // the claim of its role, so a wire pops with the proof of the one
        // consumer of the ring.  The ring of lanes tells the end of its stream
        // itself.
        template <typename... PopTag>
        [[nodiscard, gnu::always_inline]] ::foundation::core::Option<T> pop_(PopTag... tag) noexcept {
            if constexpr (Form == ChannelForm::wire) {
                return ch_->side_.ring_.try_pop_proven(PermissionedChannel::prove_consumer_());
            } else {
                return ch_->side_.ring_.try_pop(PermissionedChannel::prove_consumer_(), tag...);
            }
        }

        template <typename... PopTag>
        [[nodiscard, gnu::always_inline]] ::foundation::core::Result<T, NoItem> receive_(PopTag... tag) noexcept {
            if constexpr (Form == ChannelForm::wire) {
                ::foundation::core::Option<T> item =
                    ch_->side_.ring_.try_pop_proven(PermissionedChannel::prove_consumer_());
                if (item.is_some()) [[likely]] {
                    return std::move(item).ok_or(NoItem::Empty);
                }
                return ch_->receive_after_empty_(tag...);
            } else if constexpr (Form == ChannelForm::lanes) {
                return ch_->side_.ring_.try_receive(PermissionedChannel::prove_consumer_());
            } else {
                ::foundation::core::Option<T> item =
                    ch_->side_.ring_.try_pop(PermissionedChannel::prove_consumer_(), tag...);
                if (item.is_some()) [[likely]] {
                    return std::move(item).ok_or(NoItem::Empty);
                }
                return ch_->receive_after_empty_(tag...);
            }
        }

        // The condition of receive_waiting: a receive that gives an item or
        // the end goes into got, and an empty channel gives false.
        template <typename... PopTag>
        [[nodiscard]] bool has_received_(::foundation::core::Option<::foundation::core::Result<T, NoItem>>& got,
                                         PopTag... tag) noexcept {
            ::foundation::core::Result<T, NoItem> received = receive_(tag...);
            if (received.err().value_or(NoItem::Closed) == NoItem::Empty) return false;
            got = ::foundation::core::Option<::foundation::core::Result<T, NoItem>>::some(std::move(received));
            return true;
        }

    public:
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<parts::consumer_lines, T>;
        using row_discipline = typename parts::consumer_discipline;
        using row_payload = T;
        using brand_type = Brand;
        using channel_type = PermissionedChannel;
        static constexpr ChannelForm channel_form = Form;
        static constexpr std::size_t channel_capacity = ring_type::channel_capacity;

        ConsumerEnd(const ConsumerEnd&) =
            delete("ConsumerEnd owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerEnd& operator=(const ConsumerEnd&) =
            delete("ConsumerEnd owns the Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerEnd(ConsumerEnd&&) noexcept = default;
        ConsumerEnd& operator=(ConsumerEnd&&) = delete(
            "ConsumerEnd binds to ONE channel for life — rebinding would orphan the original Permission "
            "and silently allow a second consumer to coexist, and the pop of each ring has one consumer");

        // A moved-from handle holds no claim, so only the bound one gives the
        // role back.
        ~ConsumerEnd() {
            if (ch_.is_bound()) ch_->consumer_claim_.release();
        }

        // The release door.  The handle gives the consumer role back to the
        // channel and the token back to the caller, and it is unbound after
        // the call.  The token can open a consumer handle again.
        [[nodiscard]] ::foundation::permissions::Permission<consumer_tag, Brand> release() && noexcept {
            ch_->consumer_claim_.release();
            ch_.unbind();
            return std::move(perm_);
        }

        [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return ch_->side_.ring_.empty_approx(nondet);
        }
        [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return ch_->side_.ring_.size_approx(nondet);
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return channel_capacity; }

        // True when the last producer handle ended and no handle opened the
        // channel after it, or when each lane of a lanes channel ended, with
        // acquire order.  A consumer that parks reads it with the ring after
        // its mark, so a close that came before the mark and its wake ends the
        // park.
        [[nodiscard]] bool is_closed() const noexcept { return ch_->is_closed(); }

        // The oldest item, or NoItem::Closed after the last item of a closed
        // channel.  While the channel is empty and open, the call waits by
        // Policy (the policy of fixy/concurrent/EventCount.h): Spin spins, and
        // SpinThenPark spins for its budget and then parks on events.  The
        // condition of the wait is a receive, so the receive after the entry
        // of a park in the count takes each item and the end that came before
        // the entry.  A channel whose producers notify events after each push
        // and after their close then loses no wake.  The gate of the policy
        // refuses a policy that parks to a context of the hot path, because
        // that context owns no Block.  A merge gives the tag of Nondet after
        // events, as its pops do.
        template <WaitPolicy Policy, class Ctx, typename... PopTag>
            requires CtxFitsWaitPolicy<Ctx, Policy>
                  && (sizeof...(PopTag) == (Form == ChannelForm::merge ? std::size_t{1} : std::size_t{0}))
        [[nodiscard]] ::foundation::core::Result<T, NoItem> receive_waiting(Ctx const& ctx, EventCount& events,
                                                                            PopTag... tag) noexcept {
            ::foundation::core::Option<::foundation::core::Result<T, NoItem>> got{::foundation::core::none};
            wait_for<Policy>(ctx, events, [this, &got, tag...]() noexcept { return has_received_(got, tag...); });
            return std::move(got).expect("the wait ends after a receive that gives an item or the end");
        }

        [[nodiscard]] constexpr ::foundation::ChannelIdentity<PermissionedChannel> channel_identity() const noexcept {
            return ch_.identity();
        }
    };

    // The handles that the frame doors give a body on the scope brand Scope
    // (the frame doors).
    template <::foundation::brand::IsBrand Scope>
    using producer_handle = ProducerHandle<PermissionedChannel, Scope>;
    template <::foundation::brand::IsBrand Scope>
    using consumer_handle = ConsumerHandle<PermissionedChannel, Scope>;

    // The claim doors of the ends.  Each takes the key of the frame doors, so
    // only a frame door opens an end, and the end lives in a handle of the
    // frame.

    // The producer door of a wire channel.  It takes the claim of the
    // producer role first, so a second live producer handle ends the process
    // here.  A producer handle opens the channel.
    [[nodiscard]] ProducerEnd producer(ChannelFrameKey const& /*key*/,
                                       ::foundation::permissions::Permission<producer_tag, Brand>&& perm) noexcept
        requires(Form == ChannelForm::wire)
    {
        producer_claim_.take("two live producer handles of one wire channel. One call site minted the root of the "
                             "channel two times.");
        side_.producers_.add_producer();
        return ProducerEnd{*this, std::move(perm)};
    }

    // The producer door of a merge channel.  It lends a pool share, and it
    // refuses while the producer root is out of the pool.  Several producer
    // handles coexist, which is the point of the fractional side.  A lent
    // handle opens the channel.
    //
    // A claim door (the doors of a synchronized class of foundation/
    // Transfer.h): one compare-exchange of the pool makes the share, and the
    // handle holds it.  After the compare-exchange the door writes only the
    // count of the producers, an atomic cell, and the handle.  Another thread
    // deposits the producer root at a time that the schedule decides, so the
    // door takes the tag of Nondet.  The producer tag has an empty row, so the
    // tag stands for a context, and a row on the tag stops the lend here.
    [[nodiscard]] ::foundation::core::Option<ProducerEnd> producer(ChannelFrameKey const& /*key*/,
                                                                   ::foundation::effects::cap::Nondet nondet) noexcept
        requires(Form == ChannelForm::merge)
    {
        CRUCIBLE_TRY(lent, producer_pool_.lend(nondet));
        side_.producers_.add_producer();
        return ::foundation::core::Option<ProducerEnd>::some(ProducerEnd{*this, std::move(lent)});
    }

    // The producer door of a lanes channel.  It lends a pool share for the
    // producer of lane, and it refuses while the producer root is out of the
    // pool.  It takes the claim of the lane, so a second live producer handle
    // of one lane ends the process here.  A lane ends one time in a stream, so
    // a producer handle of a lane that ended ends the process too.  The claim
    // of the lane orders the read of its end after the end of its last
    // producer handle, so the read is no snapshot.
    //
    // A claim door (the doors of a synchronized class of foundation/
    // Transfer.h): one compare-exchange of the pool makes the share, and the
    // handle holds it.  After the compare-exchange the door takes the atomic
    // claim of the lane, reads the end of the lane under that claim, and
    // builds the handle.  The lend reads the schedule, as the lend of a merge
    // does, so the door takes the tag of Nondet.
    [[nodiscard]] ::foundation::core::Option<ProducerEnd>
    producer(ChannelFrameKey const& /*key*/, ::foundation::effects::cap::Nondet nondet, lane_index lane) noexcept
        requires(Form == ChannelForm::lanes)
    {
        CRUCIBLE_TRY(lent, producer_pool_.lend(nondet));
        // The claim of a lane is an atomic claim, so two threads can take the
        // claims of two lanes through the element door at the same time.
        side_.lane_claims_.with_element(lane, [](::foundation::EndpointClaim& claim) noexcept {
            claim.take("two live producer handles of one lane of a lanes channel.");
        });
        if (side_.ring_.end_of_lane(lane) == LaneEnd::ended) [[unlikely]] {
            ::foundation::core::fatal("a producer handle of a lane that ended. A lane ends one time in a stream, and "
                                      "the drain of the channel opens each lane again.");
        }
        return ::foundation::core::Option<ProducerEnd>::some(ProducerEnd{*this, std::move(lent), prove_lane_(lane)});
    }

    // Takes the claim of the consumer role first, so a second live consumer
    // handle ends the process here.
    [[nodiscard]] ConsumerEnd consumer(ChannelFrameKey const& /*key*/,
                                       ::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept {
        consumer_claim_.take("two live consumer handles of one channel. One call site minted the root of the "
                             "channel two times.");
        return ConsumerEnd{*this, std::move(perm)};
    }

    // The two tokens of the recombined access of a merge or a lanes channel:
    // the lease of the producer root and the consumer token.  The lease is the
    // producer root out of the pool of this channel, so no producer handle of
    // the channel lives, and the consumer token proves that no consumer handle
    // lives.  The two do not combine into a whole: the whole would hold no
    // identity of the pool, so a whole of a second root of one call site could
    // stand in for it.  The class conveys the authority of its two tokens, and
    // it does not copy, so it is a token of the doors of the channel (the
    // proof of a door of foundation/Transfer.h).
    struct[[= ::foundation::linear::authority{}]] RecombinedParts final {
        producer_lease root;
        [[no_unique_address]] ::foundation::permissions::Permission<consumer_tag, Brand> consumer;
    };

    // The token of the recombined access: the whole permission of a wire, or
    // the two tokens of a merge or a lanes channel.
    using recombined_token =
        std::conditional_t<is_pooled, RecombinedParts, ::foundation::permissions::Permission<whole_tag, Brand>>;

    // The ring of a recombined access (with_recombined_access).  The access
    // holds its token and the claim of each role while the body runs, so the
    // holder of the ring holds each role of the channel, and the ring gives
    // the push of each form.  Each push makes the proof of its writer here:
    // the proof of the producer of a wire, and the proof of a lane of a lanes
    // channel under the claim of the lane.  A push of a merge claims its ticket
    // itself.  The ring is a proof of the channel (the proof of a door of
    // foundation/Transfer.h): only the access builds it, it belongs to the
    // thread of the access, and it does not copy or move, so it lives only in
    // the frame of the access.
    class [[nodiscard]][[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]]
        RecombinedRing {
        PermissionedChannel& channel_;
        // The seal refuses each byte route, such as std::bit_cast, so no bytes
        // become a ring of an access.
        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

        explicit RecombinedRing(PermissionedChannel& channel) noexcept : channel_{channel} {}
        friend class PermissionedChannel;

    public:
        RecombinedRing(RecombinedRing const&) =
            delete("the ring of a recombined access lives in the frame of the access");
        RecombinedRing(RecombinedRing&&) = delete("the ring of a recombined access lives in the frame of the access");
        RecombinedRing& operator=(RecombinedRing const&) = delete("the ring of a recombined access does not copy");
        RecombinedRing& operator=(RecombinedRing&&) = delete("the ring of a recombined access does not move");
        ~RecombinedRing() = default;

        // Pushes item, or gives false when the ring is full.
        [[nodiscard]] bool try_push(T const& item) noexcept
            requires(Form != ChannelForm::lanes)
        {
            if constexpr (Form == ChannelForm::wire) {
                return channel_.side_.ring_.try_push_proven(PermissionedChannel::prove_producer_(), item);
            } else {
                return channel_.side_.ring_.try_push(item);
            }
        }

        // Pushes item into LANE, or gives false when the lane is full.  The
        // claim of the lane and the proof of the lane last for the push.
        [[nodiscard]] bool try_push(lane_index lane, T const& item) noexcept
            requires(Form == ChannelForm::lanes)
        {
            channel_.side_.lane_claims_.with_element(lane, [](::foundation::EndpointClaim& claim) noexcept {
                claim.take("a push of a recombined access into a lane while a producer handle of the lane lives.");
            });
            bool const is_pushed = channel_.side_.ring_.try_push(PermissionedChannel::prove_lane_(lane), item);
            channel_.side_.lane_claims_.with_element(
                lane, [](::foundation::EndpointClaim& claim) noexcept { claim.release(); });
            return is_pushed;
        }

        [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return channel_.side_.ring_.size_approx(nondet);
        }
    };

    // Scoped exclusive access to the ring, the one scoped-access verb of each
    // form.  The token is the type-level proof that no handle of its root is
    // alive: the whole permission of a wire, or the lease and the consumer
    // token of a pooled form.  A second root of the same call site has the
    // same brand, so the body also holds the claim of each role: a live handle
    // ends the process here, and no handle can start while the body runs.  A
    // lease of another channel of the same type ends the process, because only
    // the identity of the pool tells two such channels apart.  The body gets
    // the ring of the access: it can fill the ring before the first session.
    // The drain empties what the last session left.  The token comes back, so
    // the caller can split the whole again or deposit the lease.  The access
    // borrows its body, so the body holds no linear value (HoldsNoLinearValue
    // of foundation/Linear.h).  The access and the drain have no deleted
    // twin: the channel states synchronized{}, and the door check of
    // foundation/Transfer.h finds no specialization of a deleted member
    // template for its probe body, so it would refuse the channel.  A body
    // that holds a linear value meets the failed constraint, which names the
    // gate.
    template <typename Body>
        requires RecombinedAccessBody<Body, RecombinedRing> && ::foundation::linear::HoldsNoLinearValue<Body>
    [[nodiscard]] recombined_token
    with_recombined_access(recombined_token&& token,
                           Body&& body) noexcept(std::is_nothrow_invocable_v<Body, RecombinedRing&>) {
        if constexpr (Form == ChannelForm::wire) {
            producer_claim_.take("recombined access to a wire channel while its producer handle lives.");
        } else {
            producer_claim_.take(pooled_text_({
                .merge = "recombined access to a merge channel while a drain or a deposit of its producer root runs.",
                .lanes = "recombined access to a lanes channel while a drain or a deposit of its producer root runs.",
            }));
            if (!token.root.is_lease_of(producer_pool_)) [[unlikely]] {
                ::foundation::core::fatal("recombined access to a {} channel with the producer root of another "
                                          "channel of its type.  Give the root of this channel.",
                                          pooled_text_({.merge = "merge", .lanes = "lanes"}));
            }
        }
        consumer_claim_.take("recombined access to a channel while its consumer handle lives.");
        {
            RecombinedRing ring{*this};
            std::forward<Body>(body)(ring);
        }
        consumer_claim_.release();
        producer_claim_.release();
        return std::move(token);
    }

    // Takes the producer root of a merge or a lanes channel out of its pool
    // when no producer handle lives, or gives no value while one lives.  The
    // root comes out as the lease of the pool of this channel.  While the
    // root is out, the pool lends no share, so no push lands: a live consumer
    // can drain the ring to empty with no push at the same time.  The lease
    // and the consumer token open with_recombined_access, and the lease opens
    // the claim of a drain.  deposit_producer_root gives the lease back, and
    // the pool lends again.
    //
    // A claim door: its result is the exclusive token of the pool, which one
    // compare-exchange of the pool makes.  The write of the parked root after
    // that compare-exchange happens only on the thread that won it, until that
    // thread deposits the lease.  A producer handle ends at a time that the
    // schedule decides, so the door takes the tag of Nondet.
    [[nodiscard]] ::foundation::core::Option<producer_lease>
    try_withdraw_producer_root(::foundation::effects::cap::Nondet nondet) noexcept
        requires(is_pooled)
    {
        return producer_pool_.try_upgrade(nondet);
    }

    // Gives the lease of the producer root back to the pool of a merge or a
    // lanes channel.  The call takes the claim of the producer side, so a
    // deposit while a recombined access or a drain runs ends the process.  A
    // lease of another pool, and a deposit while the root is in the pool,
    // end the process too (SharedPermissionPool::deposit_exclusive).
    void deposit_producer_root(producer_lease&& root) noexcept
        requires(is_pooled)
    {
        producer_claim_.take(pooled_text_({
            .merge = "a deposit of the producer root of a merge channel while a recombined access or a drain runs.",
            .lanes = "a deposit of the producer root of a lanes channel while a recombined access or a drain runs.",
        }));
        producer_pool_.deposit_exclusive(std::move(root));
        producer_claim_.release();
    }

    // The claim of a drain: the claim of each role of this channel, which a
    // drain takes as its proof (the proof of a door of foundation/Transfer.h).
    // The constructor takes the two claims, so a live handle of a role ends
    // the process there, and no handle starts while the claim lives.  Its end
    // gives the two claims back.  The claim belongs to the thread that took
    // it, and no two threads read it, so it states thread_bound{} and
    // not_shareable{}.  The constructor is private, and the claim doors
    // claim_drain are its callers, so no thread builds a claim past the two
    // claims (the build of a token of foundation/Transfer.h).  The claim of a
    // merge or a lanes channel also takes the lease of the producer root.  A
    // holder of a pooled channel with no tag, such as the end of a Box
    // channel, takes the root with claim_producer_root_at_rest.
    class [[nodiscard]][[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]]
        DrainClaim {
        friend class PermissionedChannel;

        explicit DrainClaim(PermissionedChannel& channel) noexcept : channel_{__builtin_addressof(channel)} {
            if constexpr (Form == ChannelForm::wire) {
                channel_->producer_claim_.take("a drain of a wire channel while its producer handle lives.");
                channel_->consumer_claim_.take("a drain of a wire channel while its consumer handle lives.");
            } else {
                channel_->producer_claim_.take(pooled_text_({
                    .merge = "a drain of a merge channel while a recombined access or a deposit runs.",
                    .lanes = "a drain of a lanes channel while a recombined access or a deposit runs.",
                }));
                channel_->consumer_claim_.take(pooled_text_({
                    .merge = "a drain of a merge channel while its consumer handle lives.",
                    .lanes = "a drain of a lanes channel while its consumer handle lives.",
                }));
            }
        }

    public:
        DrainClaim(DrainClaim const&) = delete("a drain claim holds the claims of one channel, and a copy is a second");
        DrainClaim& operator=(DrainClaim const&) = delete("a drain claim holds the claims of one channel");
        DrainClaim(DrainClaim&&) = delete("a drain claim gives its claims back at its one end");
        DrainClaim& operator=(DrainClaim&&) = delete("a drain claim gives its claims back at its one end");

        ~DrainClaim() {
            channel_->consumer_claim_.release();
            channel_->producer_claim_.release();
        }

        // True when the claim holds the claims of that one channel.  The
        // compare reads two addresses of one type.
        [[nodiscard]] bool is_claim_of(PermissionedChannel const& channel) const noexcept {
            return channel_ == __builtin_addressof(channel);
        }

    private:
        PermissionedChannel* channel_ = nullptr;
    };

    // Takes the claim of each role of a wire for a drain.  A live handle of a
    // role, a recombined access or a second drain claim ends the process
    // here.  A claim door: its result is the claim, which the two atomic
    // exchanges of the claims make, and only the thread that holds the claim
    // drains until the claim ends (the doors of a synchronized class of
    // foundation/Transfer.h).
    [[nodiscard]] DrainClaim claim_drain() noexcept
        requires(Form == ChannelForm::wire)
    {
        return DrainClaim{*this};
    }

    // Takes the claim of each role of a merge or a lanes channel for a drain.
    // The lease of the producer root is the proof that no producer handle
    // lives: the pool gave it with a count of zero, and it lends no share
    // while the root is out.  The claim holds the claim of the producer side,
    // so no deposit gives the root back while the claim lives.  The
    // compare-exchange of the upgrade read the end of each producer handle
    // with acquire order, so the drain finds each item that a producer
    // published.  A lease of another channel of the same type ends the
    // process, because only the identity of the pool tells two such channels
    // apart.  This test of the identity is the residual that the type cannot
    // carry: channels of one type share a type, as the class-level stage
    // proofs share one.  A claim door, as the claim of a wire is.
    [[nodiscard]] DrainClaim claim_drain(producer_lease const& root) noexcept
        requires(is_pooled)
    {
        if (!root.is_lease_of(producer_pool_)) [[unlikely]] {
            ::foundation::core::fatal("a drain of a {} channel with the producer root of another channel of its "
                                      "type.  Give the root of this channel.",
                                      pooled_text_({.merge = "merge", .lanes = "lanes"}));
        }
        return DrainClaim{*this};
    }

    // The producer root of a merge or a lanes channel at rest, for an owner of
    // the channel that ends with no context, such as the end of a channel of
    // Box objects that drains its ring (SharedPermissionPool::
    // claim_exclusive_at_rest).  The door has no context because the end of
    // the Box channel has none.  It ends the process only on a live producer
    // handle or a root out, which correct code never has: the owner holds the
    // fact that each producer handle ended before its end, so the outcome is
    // certain.  A caller with a tag of Nondet takes the root with
    // try_withdraw_producer_root.  A claim door:
    // its result is the exclusive token of the pool, which one
    // compare-exchange of the pool makes.
    [[nodiscard]] producer_lease claim_producer_root_at_rest() noexcept
        requires(is_pooled)
    {
        return producer_pool_.claim_exclusive_at_rest();
    }

    // Gives each item that the ring still holds to drop, the oldest first.
    // The claim holds the claims of the two roles, and the claim of a pooled
    // form came from the lease of its producer root.  So no handle of a role
    // lives, and no handle starts while the drain runs.  The claim holds the
    // role of the consumer, so a wire drains with the proof of the one
    // consumer of its ring (the proven form of fixy/concurrent/SpscRing.h).  A
    // claim of another channel of the same type ends the process, because only
    // the identity of a channel tells two such channels apart.  Each producer
    // handle of a merge published the turn word of each of its items before
    // its end, and the upgrade that gave the lease read that end with acquire
    // order, so the drain finds each item.  The order of the items of a merge
    // reads the merge, so its drop holds no state, as the drain of the MPSC
    // ring asks.  The drain
    // of a lanes channel gives the items in the order of the turn, and then
    // opens each lane again for a new stream (the drain of
    // fixy/concurrent/LaneRing.h), so its drop can hold state.  An owner of
    // items that end only through a door, such as the words of a Box channel,
    // calls it at its own end.  The drain borrows its drop and calls it for
    // each item, so the drop holds no linear value (HoldsNoLinearValue of
    // foundation/Linear.h).  Complexity: linear in the count of the items.
    template <typename Drop>
        requires ChannelDrainDrop<Drop, T, Form> && ::foundation::linear::HoldsNoLinearValue<Drop>
    void drain(DrainClaim& claim, Drop&& drop) noexcept {
        if (!claim.is_claim_of(*this)) [[unlikely]] {
            ::foundation::core::fatal("a drain of a channel with the claim of another channel of its type.  Take "
                                      "the claim of this channel.");
        }
        if constexpr (Form == ChannelForm::wire) {
            // Each popped item goes to drop, and the first empty pop ends the
            // drain.
            bool is_popped = true;
            while (is_popped) {
                is_popped = side_.ring_.try_pop_proven(prove_consumer_())
                                .match(
                                    [&drop](T item) noexcept {
                                        drop(std::move(item));
                                        return true;
                                    },
                                    [] noexcept { return false; });
            }
        } else {
            side_.ring_.drain(prove_consumer_(), std::forward<Drop>(drop));
        }
    }

    // The count of the pool shares of a merge or a lanes channel that producer
    // handles hold.
    [[nodiscard]] std::uint64_t outstanding_producers() const noexcept
        requires(is_pooled)
    {
        return producer_pool_.outstanding();
    }

    // Always false for a wire channel, which has no pool: the linear
    // permissions and the claims are what prove single ownership.
    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept
        requires(Form == ChannelForm::wire)
    {
        return false;
    }

    // True while the producer root of a merge or a lanes channel is out of its
    // pool.
    [[nodiscard]] bool is_exclusive_active() const noexcept
        requires(is_pooled)
    {
        return producer_pool_.is_exclusive_out();
    }

    [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return side_.ring_.empty_approx(nondet);
    }
    [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return side_.ring_.size_approx(nondet);
    }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return channel_capacity; }

    // True when the last producer handle ended and no handle opened the
    // channel after it, or when each lane of a lanes channel ended.  A
    // snapshot for a test: a consumer reads the end through try_receive.
    [[nodiscard]] bool is_closed() const noexcept {
        if constexpr (Form == ChannelForm::lanes) {
            return side_.ring_.is_each_lane_ended();
        } else {
            return side_.producers_.is_closed();
        }
    }

private:
    // The text of a refusal of a pooled form: the text of the merge form, or
    // the text of the lanes form.
    [[nodiscard]] static constexpr ::foundation::core::TextView pooled_text_(detail::PooledTexts texts) noexcept {
        return Form == ChannelForm::lanes ? texts.lanes : texts.merge;
    }

    // The proofs of the two sides of the ring of a wire (the proven form of
    // fixy/concurrent/SpscRing.h), and the proof of the consumer of the ring
    // of a merge or of the lanes (the writers of fixy/concurrent/MpscRing.h
    // and of fixy/concurrent/LaneRing.h).  The ring names this channel as its
    // owner, so only this channel makes them.  Only the handle of each role, a
    // drain, a recombined access and the receive after an empty pop call them.
    // The handle holds the linear token and the claim of its role, and a drain
    // and a recombined access hold the claim of each role, so the one writer
    // of each side is a fact of the types.  The drain of a wire and the receive
    // of a wire after its close use the checked pop of the ring.  The return
    // types are deduced, because the ring of each form names its own proof
    // types.
    [[nodiscard]] static constexpr auto prove_producer_() noexcept
        requires(Form == ChannelForm::wire)
    {
        return typename ring_type::producer_proof{};
    }

    [[nodiscard]] static constexpr auto prove_consumer_() noexcept { return typename ring_type::consumer_proof{}; }

    // The proof of a lane of the lanes form (the writers of
    // fixy/concurrent/LaneRing.h).  The ring names this channel as its owner,
    // so only this channel mints it.  Each caller holds the claim of the lane:
    // the producer door, which gives the proof to the handle for its life, and
    // the push of a recombined access, which ends the proof and the claim
    // after the push.
    [[nodiscard]] static constexpr auto prove_lane_(lane_index lane) noexcept
        requires(Form == ChannelForm::lanes)
    {
        return typename ring_type::LaneProof{lane};
    }

    // The end of a producer handle of a wire with its token.  It is the last
    // producer, so it closes the channel, and it gives its producer role back.
    // A handle ends one time, so the end is cold (ProducerSide::end_producer).
    CRUCIBLE_COLD void end_producer_(typename parts::lane const& /*lane*/) noexcept
        requires(Form == ChannelForm::wire)
    {
        side_.producers_.end_producer();
        producer_claim_.release();
    }

    // The release door of a wire producer: the channel stays open.  It is cold
    // for the reason of end_producer_.
    CRUCIBLE_COLD void release_producer_() noexcept
        requires(Form == ChannelForm::wire)
    {
        side_.producers_.release_producer();
        producer_claim_.release();
    }

    // The receive after a pop that found the ring empty.  The read of the
    // closed state is an acquire, so the pop after it finds each item that the
    // stream still holds, and Closed comes only after the last item.  A
    // producer of a merge publishes the turn word of each item before its
    // handle ends, so the same holds for a merge.
    template <typename... PopTag>
    [[nodiscard]] ::foundation::core::Result<T, NoItem> receive_after_empty_(PopTag... tag) noexcept {
        if (!side_.producers_.is_closed()) [[likely]] {
            return ::foundation::core::err(NoItem::Empty);
        }
        return receive_after_close_(tag...);
    }

    // A stream ends one time, so the pop after its end is cold.  Only the
    // consumer handle reaches it, so a wire pops with the proof of the one
    // consumer of its ring.
    template <typename... PopTag>
    [[nodiscard]] CRUCIBLE_COLD ::foundation::core::Result<T, NoItem> receive_after_close_(PopTag... tag) noexcept {
        if constexpr (Form == ChannelForm::merge) {
            return side_.ring_.try_pop(prove_consumer_(), tag...).ok_or(NoItem::Closed);
        } else {
            return side_.ring_.try_pop_proven(prove_consumer_()).ok_or(NoItem::Closed);
        }
    }

    // The ring, the roster of the producers and the claims of the lanes
    // (ProducerSide).
    ProducerSide side_{*this};
    // The claim of the producer side.  A wire producer handle holds it for its
    // life.  On a pooled form, the recombined access, the drain and the deposit
    // of the producer root hold it, so two of them never overlap.
    ::foundation::EndpointClaim producer_claim_;
    [[no_unique_address]] typename parts::producer_pool producer_pool_;
    ::foundation::EndpointClaim consumer_claim_;
};

// The producer handle of a frame (the frame doors): the producer end of a
// channel on the scope brand of one call of a frame door.  The handle derives
// from its end privately, so no code outside it binds a reference to the end,
// and it shows each door of the end.  The constructor is private, and the key
// of the frame doors is its one friend, so only a frame door builds a handle.
// The end of a wire is linear, so the handle of a wire is linear too.  The
// handle serves the ends of this header and of fixy/concurrent/
// PermissionedBoxChannel.h.  Its first argument is the channel, which is no
// linear value, so the handle marks its own end, and the drop door ends it
// (foundation/Linear.h).
template <class Channel, ::foundation::brand::IsBrand Scope>
class[[= ::foundation::linear::linear_when<Channel::ProducerEnd::channel_form == ChannelForm::wire>]] ProducerHandle
    : private Channel::ProducerEnd {
    using End = typename Channel::ProducerEnd;

    explicit ProducerHandle(End&& end) noexcept : End{std::move(end)} {}
    friend class ChannelFrameKey;

public:
    using row_discipline = typename End::row_discipline;
    using row_payload = typename End::row_payload;
    using brand_type = typename End::brand_type;
    using channel_type = typename End::channel_type;
    // The scope brand of the frame of the handle.
    using scope_type = Scope;
    static constexpr std::size_t per_call_working_set = End::per_call_working_set;
    static constexpr ChannelForm channel_form = End::channel_form;
    static constexpr std::size_t channel_capacity = End::channel_capacity;

    ProducerHandle(ProducerHandle&&) noexcept = default;
    ProducerHandle(ProducerHandle const&) =
        delete("a producer handle owns the producer token or one pool share, and a copy would duplicate it");
    ProducerHandle& operator=(ProducerHandle const&) =
        delete("a producer handle owns the producer token or one pool share, and an assignment would overwrite it");
    ProducerHandle& operator=(ProducerHandle&&) =
        delete("a producer handle binds to one channel for its life, and an assignment would orphan its token");
    ~ProducerHandle() = default;

    // The end whose doors the handle shows.  The shape traits of fixy/
    // concurrent/HandleTraits.h read the signature of each door there.
    using frame_end_type = End;

    // The doors of the end.  Each door forwards to the door of the same name
    // of the end, which gives a value and no reference or address into the
    // channel.  The handle states each door as its own member, with the
    // parameters of the door of the end and a deduced result, so the rule
    // escape-doors reads it and a call converts its arguments as before.  A
    // door that a form of the end lacks takes a constraint, so no other form
    // instantiates it.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return End::capacity(); }

    [[nodiscard]] constexpr auto channel_identity() const noexcept { return End::channel_identity(); }

    void close() && noexcept
        requires requires(End&& end) { static_cast<End&&>(end).close(); }
    {
        static_cast<End&&>(*this).close();
    }

    [[nodiscard]] auto empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return End::empty_approx(nondet);
    }

    [[nodiscard]] auto release() && noexcept
        requires requires(End&& end) { static_cast<End&&>(end).release(); }
    {
        return static_cast<End&&>(*this).release();
    }

    [[nodiscard]] auto size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return End::size_approx(nondet);
    }

    // The push of a value channel takes the value by reference to const, and
    // the push of a channel of owners takes the owner by move.
    [[nodiscard]] auto try_push(row_payload const& item) noexcept
        requires requires(End& end, row_payload const& value) { end.try_push(value); }
    {
        return End::try_push(item);
    }

    [[nodiscard]] auto try_push(row_payload&& item) noexcept
        requires(!requires(End& end, row_payload const& value) { end.try_push(value); })
             && requires(End& end, row_payload&& value) { end.try_push(static_cast<row_payload&&>(value)); }
    {
        return End::try_push(static_cast<row_payload&&>(item));
    }

    // The lane of the producer of a lanes channel.
    [[nodiscard]] constexpr auto lane() const noexcept
        requires(channel_form == ChannelForm::lanes)
    {
        return End::lane();
    }

    // The push that waits of an end that gives it as a member, such as the
    // end of a channel of Box objects.
    template <WaitPolicy Policy, class Ctx>
        requires requires(End& end, Ctx const& ctx, EventCount& room, row_payload&& item) {
            end.template push_waiting<Policy>(ctx, room, std::move(item));
        }
    void push_waiting(Ctx const& ctx, EventCount& room, row_payload&& item) noexcept {
        End::template push_waiting<Policy>(ctx, room, std::move(item));
    }
};

// The consumer handle of a frame (the frame doors): the consumer end of a
// channel on the scope brand of one call of a frame door, as ProducerHandle
// is for the producer.  The consumer end of each form is linear.
template <class Channel, ::foundation::brand::IsBrand Scope>
class[[= ::foundation::linear::linear{}]] ConsumerHandle : private Channel::ConsumerEnd {
    using End = typename Channel::ConsumerEnd;

    explicit ConsumerHandle(End&& end) noexcept : End{std::move(end)} {}
    friend class ChannelFrameKey;

public:
    using row_discipline = typename End::row_discipline;
    using row_payload = typename End::row_payload;
    using brand_type = typename End::brand_type;
    using channel_type = typename End::channel_type;
    // The scope brand of the frame of the handle.
    using scope_type = Scope;
    static constexpr std::size_t per_call_working_set = End::per_call_working_set;
    static constexpr ChannelForm channel_form = End::channel_form;
    static constexpr std::size_t channel_capacity = End::channel_capacity;

    ConsumerHandle(ConsumerHandle&&) noexcept = default;
    ConsumerHandle(ConsumerHandle const&) =
        delete("a consumer handle owns the consumer token, and a copy would duplicate the linear token");
    ConsumerHandle& operator=(ConsumerHandle const&) =
        delete("a consumer handle owns the consumer token, and an assignment would overwrite the linear token");
    ConsumerHandle& operator=(ConsumerHandle&&) =
        delete("a consumer handle binds to one channel for its life, and the pop of each ring has one consumer");
    ~ConsumerHandle() = default;

    // The end whose doors the handle shows, as for the producer handle.
    using frame_end_type = End;

    // The doors of the end, as the producer handle states them.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return End::capacity(); }

    [[nodiscard]] constexpr auto channel_identity() const noexcept { return End::channel_identity(); }

    [[nodiscard]] auto empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return End::empty_approx(nondet);
    }

    [[nodiscard]] bool is_closed() const noexcept { return End::is_closed(); }

    // The wait states the gate of the wait of the end, so a refusal names it.
    template <WaitPolicy Policy, class Ctx, class... PopTag>
        requires CtxFitsWaitPolicy<Ctx, Policy>
              && requires(End& end, Ctx const& ctx, EventCount& events, PopTag... tag) {
                     end.template receive_waiting<Policy>(ctx, events, tag...);
                 }
    [[nodiscard]] auto receive_waiting(Ctx const& ctx, EventCount& events, PopTag... tag) noexcept {
        return End::template receive_waiting<Policy>(ctx, events, tag...);
    }

    [[nodiscard]] auto release() && noexcept { return static_cast<End&&>(*this).release(); }

    [[nodiscard]] auto size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return End::size_approx(nondet);
    }

    // The pop of a wire takes no tag, and the pop of a merge or a lanes
    // channel takes the tag of Nondet.
    [[nodiscard]] auto try_pop() noexcept
        requires requires(End& end) { end.try_pop(); }
    {
        return End::try_pop();
    }

    [[nodiscard]] auto try_pop(::foundation::effects::cap::Nondet nondet) noexcept
        requires requires(End& end, ::foundation::effects::cap::Nondet tag) { end.try_pop(tag); }
    {
        return End::try_pop(nondet);
    }

    [[nodiscard]] auto try_receive() noexcept
        requires requires(End& end) { end.try_receive(); }
    {
        return End::try_receive();
    }

    [[nodiscard]] auto try_receive(::foundation::effects::cap::Nondet nondet) noexcept
        requires requires(End& end, ::foundation::effects::cap::Nondet tag) { end.try_receive(tag); }
    {
        return End::try_receive(nondet);
    }
};

template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::wire>
         && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>>
auto with_producer(
    Channel& channel,
    ::foundation::permissions::Permission<typename Channel::producer_tag, typename Channel::brand_type>&& token,
    Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>> {
    ChannelFrameKey const key{};
    return ::foundation::linear::call_once(static_cast<Body&&>(body),
                                           ChannelFrameKey::build_handle_<detail::producer_handle_of_t<Channel, Scope>>(
                                               channel.producer(key, std::move(token))));
}

template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::merge>
         && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>> && HandleLendFrameBody<Body>
auto with_producer(Channel& channel, ::foundation::effects::cap::Nondet nondet, Body&& body) noexcept
    -> ::foundation::core::Option<
        detail::handle_frame_value_t<std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>>>> {
    using handle_type = detail::producer_handle_of_t<Channel, Scope>;
    using result_type = std::invoke_result_t<Body&&, handle_type>;
    using value_type = detail::handle_frame_value_t<result_type>;
    ChannelFrameKey const key{};
    CRUCIBLE_LET_ELSE(lent, channel.producer(key, nondet), detail::refuse_unlent_body_(static_cast<Body&&>(body)));
    handle_type handle = ChannelFrameKey::build_handle_<handle_type>(std::move(lent));
    if constexpr (std::is_void_v<result_type>) {
        ::foundation::linear::call_once(static_cast<Body&&>(body), std::move(handle));
        return ::foundation::core::Option<value_type>::some(::foundation::core::Unit{});
    } else {
        return ::foundation::core::Option<value_type>::some(
            ::foundation::linear::call_once(static_cast<Body&&>(body), std::move(handle)));
    }
}

template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::lanes>
         && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>> && HandleLendFrameBody<Body>
auto with_producer(Channel& channel, ::foundation::effects::cap::Nondet nondet, typename Channel::lane_index lane,
                   Body&& body) noexcept
    -> ::foundation::core::Option<
        detail::handle_frame_value_t<std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>>>> {
    using handle_type = detail::producer_handle_of_t<Channel, Scope>;
    using result_type = std::invoke_result_t<Body&&, handle_type>;
    using value_type = detail::handle_frame_value_t<result_type>;
    ChannelFrameKey const key{};
    CRUCIBLE_LET_ELSE(lent, channel.producer(key, nondet, lane),
                      detail::refuse_unlent_body_(static_cast<Body&&>(body)));
    handle_type handle = ChannelFrameKey::build_handle_<handle_type>(std::move(lent));
    if constexpr (std::is_void_v<result_type>) {
        ::foundation::linear::call_once(static_cast<Body&&>(body), std::move(handle));
        return ::foundation::core::Option<value_type>::some(::foundation::core::Unit{});
    } else {
        return ::foundation::core::Option<value_type>::some(
            ::foundation::linear::call_once(static_cast<Body&&>(body), std::move(handle)));
    }
}

template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope>
    requires(sizeof...(Refused) == 0) && HandleFrameLeaves<Body, Scope, detail::consumer_handle_of_t<Channel, Scope>>
auto with_consumer(
    Channel& channel,
    ::foundation::permissions::Permission<typename Channel::consumer_tag, typename Channel::brand_type>&& token,
    Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::consumer_handle_of_t<Channel, Scope>> {
    ChannelFrameKey const key{};
    return ::foundation::linear::call_once(static_cast<Body&&>(body),
                                           ChannelFrameKey::build_handle_<detail::consumer_handle_of_t<Channel, Scope>>(
                                               channel.consumer(key, std::move(token))));
}

template <class... Refused, class Channel, class Body, ::foundation::brand::IsBrand Scope>
    requires(sizeof...(Refused) == 0) && ChannelOfForm<Channel, ChannelForm::wire>
         && HandleFrameLeaves<Body, Scope, detail::producer_handle_of_t<Channel, Scope>,
                              detail::consumer_handle_of_t<Channel, Scope>>
auto with_ends(Channel& channel,
               ::foundation::permissions::Permission<typename Channel::producer_tag, typename Channel::brand_type>&&
                   producer_token,
               ::foundation::permissions::Permission<typename Channel::consumer_tag, typename Channel::brand_type>&&
                   consumer_token,
               Body&& body) noexcept -> std::invoke_result_t<Body&&, detail::producer_handle_of_t<Channel, Scope>,
                                                             detail::consumer_handle_of_t<Channel, Scope>> {
    ChannelFrameKey const key{};
    // The producer end opens first, so the claims of the two roles come in
    // one order on each call.
    auto producer = ChannelFrameKey::build_handle_<detail::producer_handle_of_t<Channel, Scope>>(
        channel.producer(key, std::move(producer_token)));
    auto consumer = ChannelFrameKey::build_handle_<detail::consumer_handle_of_t<Channel, Scope>>(
        channel.consumer(key, std::move(consumer_token)));
    return ::foundation::linear::call_once(static_cast<Body&&>(body), std::move(producer), std::move(consumer));
}

// A producer of a channel of values: the producer end of the channel, or a
// handle of that end.  The producer of a channel of Box objects names the
// channel of its words, and its end is no end of that channel, so it is no
// producer of values.
template <class Producer>
concept ValueProducer =
    std::same_as<Producer, typename Producer::channel_type::ProducerEnd>
    || std::same_as<Producer, ProducerHandle<typename Producer::channel_type, typename Producer::scope_type>>;

// Pushes item through the producer.  While the channel is full, the
// call waits by Policy (the policy of fixy/concurrent/EventCount.h): Spin
// spins, and SpinThenPark spins for its budget and then parks on room.  The
// condition of the wait is a push, so the push after the entry of a park in
// the count takes each cell that a pop freed before the entry.  A consumer
// that notifies room after each receive then loses no wake of this producer.
// The gate of the policy refuses a policy that parks to a context of the hot
// path, because that context owns no Block.  The call is a free function, and
// it reaches the channel only through the push of the end, so an end has
// no door that the check of foundation/Transfer.h cannot read.
template <WaitPolicy Policy, class Ctx, class Producer>
    requires CtxFitsWaitPolicy<Ctx, Policy> && ValueProducer<Producer>
void push_waiting(Ctx const& ctx, Producer& producer, EventCount& room,
                  typename Producer::row_payload const& item) noexcept {
    // The closure borrows the handle through a Ref, so the handle stays the
    // linear value of its caller.
    wait_for<Policy>(ctx, room,
                     [handle = ::foundation::core::to(producer), &item]() noexcept { return handle->try_push(item); });
}

namespace detail {

template <typename Root>
struct channel_root_parts;

template <ChannelForm Form, typename UserTag, typename Brand>
struct channel_root_parts<::foundation::permissions::Permission<channel_tag::Whole<Form, UserTag>, Brand>> {
    static constexpr ChannelForm form = Form;
    using user_tag = UserTag;
    using brand = Brand;
};

}  // namespace detail

// The channel of a whole root of type Root.  The alias reads the form, the
// user tag and the brand off the root.  A root of the lanes form names the
// count of the lanes after it.
template <ChannelValue T, std::size_t Capacity, typename Root, LaneCount... Lanes>
using channel_t = PermissionedChannel<detail::channel_root_parts<Root>::form, T, Capacity,
                                      typename detail::channel_root_parts<Root>::user_tag,
                                      typename detail::channel_root_parts<Root>::brand, Lanes...>;

// The tag and the brand the witness roster names for this channel.  The check
// file of this header names them too, and no code builds a channel of them.
namespace detail::channel_witness {
struct WitnessTag {};
struct WitnessBrand {};
}  // namespace detail::channel_witness

// The tag and the brand that the witness row of the drain claim names.  No
// code builds a channel of them.  They are outside a detail namespace, so the
// witness fixture of the row uses no detail name.
namespace drain_claim_witness {
struct WitnessTag {};
struct WitnessBrand {};
}  // namespace drain_claim_witness

}  // namespace fixy::concurrent

// Both the binary and the variadic split forms are specialized, so a caller
// can reach for either one.  The authoring witnesses are deliberately
// redundant: a forged can_split_into alone is caught by the missing witness
// beside it.

namespace foundation::permissions {

template <::fixy::concurrent::ChannelForm Form, typename UserTag>
struct can_split_into<::fixy::concurrent::channel_tag::Whole<Form, UserTag>,
                      ::fixy::concurrent::channel_tag::Producer<Form, UserTag>,
                      ::fixy::concurrent::channel_tag::Consumer<Form, UserTag>> : std::true_type {};

template <::fixy::concurrent::ChannelForm Form, typename UserTag>
struct can_split_into_pack<::fixy::concurrent::channel_tag::Whole<Form, UserTag>,
                           ::fixy::concurrent::channel_tag::Producer<Form, UserTag>,
                           ::fixy::concurrent::channel_tag::Consumer<Form, UserTag>> : std::true_type {};

template <::fixy::concurrent::ChannelForm Form, typename UserTag>
struct has_split_authoring_witness<::fixy::concurrent::channel_tag::Whole<Form, UserTag>,
                                   ::fixy::concurrent::channel_tag::Producer<Form, UserTag>,
                                   ::fixy::concurrent::channel_tag::Consumer<Form, UserTag>> : std::true_type {};

template <::fixy::concurrent::ChannelForm Form, typename UserTag>
struct has_split_pack_authoring_witness<::fixy::concurrent::channel_tag::Whole<Form, UserTag>,
                                        ::fixy::concurrent::channel_tag::Producer<Form, UserTag>,
                                        ::fixy::concurrent::channel_tag::Consumer<Form, UserTag>> : std::true_type {};

}  // namespace foundation::permissions
