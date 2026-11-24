#pragma once

// A channel that owns the two event counts of its waits (the parked form).
// A wait that parks on an event count ends only when the other side notifies
// the same count after it changes the condition of the wait.  With the counts
// outside the channel, a consumer that pops with try_pop, or that notifies
// another count, compiles, and a producer that parks on a full ring then
// never wakes.  The parked form holds the counts beside the channel, and each
// door of its ends notifies the count that its change can end a wait of:
//
//   - A push notifies items, and the close of the producer notifies items
//     after the end of the stream.  The consumer parks on items.
//   - A pop and a receive notify room.  The close of the consumer stores
//     that the consumer is gone and notifies each waiter of room.  The
//     producer parks on room.
//
// No door of a parked end takes an event count, so no caller can pair a wait
// with the wrong count.  Each door that notifies takes the context of the
// caller, because a notify that finds a waiter makes a system call, and the
// context of the hot path owns no Block.  The plain channel of the record
// path keeps its doors with no count, because each notify costs one seq_cst
// fence and one load of the count.
//
// Shape
//     ParkedChannel<Channel> holds a wire of fixy/concurrent/
//     PermissionedChannel.h or of fixy/concurrent/PermissionedBoxChannel.h
//     privately, so no door of the plain channel reaches its ring.  A push
//     door of a wire of Box objects takes the Box by move, and its refusal,
//     PushRefused, gives the Box back.  The parked form has the shape of
//     PermissionedChannel.h: its claim doors
//     take the key of the frame doors, and its handle aliases name the parked
//     handles below.  So the frame doors of that header open its ends, and
//     the scope brand and the gate of the result of each door hold for a
//     parked handle as for a plain one.  A claim door gives its end inside a
//     HandleKey, the one argument of the constructor of a parked handle, and
//     only the parked channel builds a key.  So only a frame door, through a
//     claim door, builds a parked handle.
//
// Ends
//     The producer end is linear.  close(ctx) && ends the stream and then
//     notifies items, so a consumer that parks after its last receive reads
//     the end.  release() && gives the token back and keeps the stream open,
//     so the condition of a parked consumer does not change, and the next
//     producer of the token pushes or closes.  must-consume reads that each
//     path ends in one of the two doors.  The destructor reports a producer
//     end that no door consumed in the builds with the oracles of the tests,
//     as the destructor of a live Linear of fixy/Qtt.h does.
//
//     The consumer end is linear too.  close(ctx) && stores that the consumer
//     is gone with release order, ends the consumer, and then notifies each
//     waiter of room.  A push that finds the ring full reads that state with
//     acquire order after the failed push, so a producer that parks on a full
//     ring ends with ConsumerGone when the consumer closes.  release() &&
//     gives the token back and keeps the stream, so a later consumer of the
//     token can open.  It is no end of the consumer: it notifies no one, and a
//     parked producer waits for the next consumer.  The open of a consumer
//     stores that a consumer is there before its handle exists.  A channel
//     starts with no consumer yet, and that is not a consumer that ended: a
//     push that finds the ring full before the first consumer opens parks, and
//     the first pop of that consumer wakes it.  So a producer that parks while
//     no consumer is open waits for one, with no bound.  push_waiting_until
//     gives the bound of a deadline.
//
//     Each end and each handle owes a consume at its end (owes_consume of
//     foundation/Linear.h), as a live fixy::Linear does.  So the generic
//     drop door and end_unrun_body refuse them at compile time: an end there
//     would notify nothing, and a waiter of the other side would park with
//     no wake.
//
// Cost
//     Each push and each pop pays one seq_cst fence and one load of the count
//     that it notifies, and a notify that finds a waiter makes one system
//     call.  The pipeline of a background thread pays the same by hand.

#include <fixy/Qtt.h>
#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/EventCount.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/os/Futex.h>

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

// Why a push of a parked producer did not take its item.
enum class PushRefusal : std::uint8_t {
    // The ring is full, and a consumer can still pop.
    Full = 0,
    // The consumer ended, so no pop frees a cell.
    ConsumerGone = 1,
};

// The refusal of a push that moves its item, such as the push of a Box
// through a parked wire of Box objects: the item that the ring did not take,
// and why.  The class is linear, so must-consume reads that the caller takes
// the item back, or ends the refusal at a named door, on each path.  So no
// refusal loses its item in silence, and a Result of a refusal is a linear
// sum, which a caller cannot discard.  The checked lifetime start of
// foundation/Lifetime.h refuses the class, so no start over bytes forges a
// refusal.
template <class Item>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]]
    PushRefused final {
public:
    PushRefused(Item&& item, PushRefusal why) noexcept : item_{std::move(item)}, why_{why} {}

    PushRefused(PushRefused&&) noexcept = default;
    PushRefused(PushRefused const&) = delete("a refusal holds the one item of a push");
    PushRefused& operator=(PushRefused const&) = delete("a refusal holds the one item of a push");
    PushRefused& operator=(PushRefused&&) = delete("a refusal holds the one item of a push");
    ~PushRefused() = default;

    // Why the ring did not take the item.
    [[nodiscard]] PushRefusal reason() const noexcept { return why_; }

    // The item back, for a later push or for its own end.
    [[nodiscard]] Item take_item() && noexcept { return std::move(item_); }

private:
    Item item_;
    PushRefusal why_ = PushRefusal::Full;
};

template <class Channel, ::foundation::brand::IsBrand Scope>
class ParkedProducerHandle;
template <class Channel, ::foundation::brand::IsBrand Scope>
class ParkedConsumerHandle;

// The parked form of a wire (the shape).  Channel is a wire of fixy/
// concurrent/PermissionedChannel.h, whose push takes a value by reference to
// const, or a wire of fixy/concurrent/PermissionedBoxChannel.h, whose push
// takes a Box by move.  A push door of a wire of values refuses with a
// PushRefusal, and the caller keeps its value.  A push door of a wire that
// moves its items refuses with a PushRefused, which gives the item back.
//
// Channel can also be a merge of the two headers (the merge form).  The pool
// of the merge holds the producer root, and each producer handle holds one
// share, which a frame door lends.  The producer end of a merge has close and
// no release: the close of the last producer ends the stream.  A pop of a
// merge reads the schedule, so each pop door takes a context that owns
// Nondet.  Each pop that takes an item notifies room once, because it frees
// one cell, so it wakes one parked producer.  The close of the consumer wakes
// each parked producer.
//
// The producers and the consumer use the channel on their threads at the same
// time by design.  Each part is private: the inner channel states
// synchronized{}, and the two counts and the state of the consumer are atomic
// cells.  So the parked channel states synchronized{} (foundation/
// Transfer.h).
template <class Channel>
    requires ChannelOfForm<Channel, ChannelForm::wire> || ChannelOfForm<Channel, ChannelForm::merge>
class[[= ::foundation::transfer::synchronized{}]] ParkedChannel : public ::foundation::Pinned<ParkedChannel<Channel>> {
public:
    using value_type = typename Channel::row_payload;
    using brand_type = typename Channel::brand_type;
    using whole_tag = typename Channel::whole_tag;
    using producer_tag = typename Channel::producer_tag;
    using consumer_tag = typename Channel::consumer_tag;
    static constexpr ChannelForm channel_form = Channel::channel_form;
    static constexpr std::size_t channel_capacity = Channel::channel_capacity;

    // True for the merge form.
    static constexpr bool is_merge = channel_form == ChannelForm::merge;

    // True when a pop door can take the context: each context for a wire, and
    // a context that owns Nondet for a merge, whose pop reads the schedule.
    template <class Ctx>
    static constexpr bool pops_with =
        !is_merge || ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::Nondet>;

    // True when the push of the wire takes its item by move, as the push of a
    // wire of Box objects does.  Such a push gives a refused item back.
    static constexpr bool moves_its_items =
        !requires(typename Channel::ProducerEnd& end, value_type const& item) { end.try_push(item); };

    // The result of each push door.
    using push_result =
        ::foundation::core::Result<::foundation::core::Unit,
                                   std::conditional_t<moves_its_items, PushRefused<value_type>, PushRefusal>>;

    // A wire starts with no handle.  The caller keeps the two tokens.
    ParkedChannel() noexcept
        requires(!is_merge)
    = default;

    // The pool of the merge parks the producer root, and each producer handle
    // holds one share of it.
    explicit ParkedChannel(::foundation::permissions::Permission<producer_tag, brand_type>&& producer_root) noexcept
        requires is_merge
        : inner_{std::move(producer_root)} {}

    // The key of the constructor of a parked handle (shape).  It holds the
    // end that a claim door opened.  Only the parked channel builds a key, and
    // each claim door takes the key of the frame doors, so only a frame door
    // builds a parked handle.  The constructor of the handle takes the end out
    // of the key.  A key does not copy, and the seal closes the byte routes to
    // a key.  It moves, so it is linear by its end, and no door ends a key
    // that holds its end: the end owes a consume.
    template <class End>
    class HandleKey final {
        End end_;
        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

        explicit HandleKey(End&& end) noexcept : end_{std::move(end)} {}

        friend class ParkedChannel;
        template <class, ::foundation::brand::IsBrand>
        friend class ParkedProducerHandle;
        template <class, ::foundation::brand::IsBrand>
        friend class ParkedConsumerHandle;

    public:
        HandleKey(HandleKey const&) = delete("a key of a parked handle lives for one build of a handle");
        HandleKey& operator=(HandleKey const&) = delete("a key of a parked handle lives for one build of a handle");
        HandleKey(HandleKey&&) noexcept = default;
        HandleKey& operator=(HandleKey&&) = delete("a key of a parked handle lives for one build of a handle");
        ~HandleKey() = default;
    };

    class[[= ::foundation::linear::linear{}]][[= ::foundation::linear::owes_consume{}]] ProducerEnd
        : private ::fixy::detail::ConsumeTracker<::foundation::algebra::lattices::QttGrade::One, ProducerEnd> {
        typename Channel::ProducerEnd inner_;
        ::foundation::core::Ref<ParkedChannel, typename Channel::brand_type> channel_;

        ProducerEnd(typename Channel::ProducerEnd&& inner, ParkedChannel& channel) noexcept
            : inner_{std::move(inner)}, channel_{::foundation::core::to(channel)} {}
        friend class ParkedChannel;

    public:
        using brand_type = typename Channel::brand_type;
        using value_type = typename Channel::row_payload;
        static constexpr ChannelForm channel_form = Channel::channel_form;
        static constexpr std::size_t channel_capacity = Channel::channel_capacity;

        ProducerEnd(ProducerEnd const&) = delete("a parked producer end owns the producer token");
        ProducerEnd& operator=(ProducerEnd const&) = delete("a parked producer end owns the producer token");
        ProducerEnd(ProducerEnd&&) noexcept = default;
        ProducerEnd& operator=(ProducerEnd&&) = delete("a parked producer end binds to one channel for its life");
        ~ProducerEnd() = default;

        // Pushes item, and notifies items when the ring took it.  The error
        // tells a full ring from a consumer that closed.  A wire of values
        // takes the item by reference to const, so the caller keeps it.
        template <class Ctx>
            requires ::fixy::futex::CtxMayUseFutex<Ctx> && (!moves_its_items)
        [[nodiscard]] push_result try_push(Ctx const& ctx, value_type const& item) noexcept {
            this->require_live("a push through a parked producer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            auto attempt = [this, &item]() noexcept { return inner_.try_push(item); };
            PushRefusal why = PushRefusal::Full;
            if (push_once_(ctx, attempt, why)) [[likely]]
                return ::foundation::core::Unit{};
            return ::foundation::core::err(why);
        }

        // As try_push of a wire of values, for a wire that moves its items:
        // the refusal gives the item back.
        template <class Ctx>
            requires ::fixy::futex::CtxMayUseFutex<Ctx> && moves_its_items
        [[nodiscard]] push_result try_push(Ctx const& ctx, value_type&& item) noexcept {
            this->require_live("a push through a parked producer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            ::foundation::core::Option<value_type> pending =
                ::foundation::core::Option<value_type>::some(std::move(item));
            auto attempt = [this, &pending]() noexcept { return push_pending_(pending); };
            PushRefusal why = PushRefusal::Full;
            if (push_once_(ctx, attempt, why)) [[likely]]
                return ::foundation::core::Unit{};
            return refuse_owner_(pending, why);
        }

        // Pushes item, and waits by Policy while the ring is full: Spin spins,
        // and SpinThenPark spins for its budget and then parks on room.  The
        // condition of the wait is a push, or the close of the consumer, so
        // the call ends with ConsumerGone when the consumer closes.
        template <WaitPolicy Policy, class Ctx>
            requires CtxFitsWaitPolicy<Ctx, Policy> && ::fixy::futex::CtxMayUseFutex<Ctx> && (!moves_its_items)
        [[nodiscard]] push_result push_waiting(Ctx const& ctx, value_type const& item) noexcept {
            this->require_live("a push through a parked producer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            auto attempt = [this, &item]() noexcept { return inner_.try_push(item); };
            PushRefusal why = PushRefusal::Full;
            if (push_waiting_<Policy>(ctx, attempt, why)) [[likely]]
                return ::foundation::core::Unit{};
            return ::foundation::core::err(why);
        }

        // As push_waiting of a wire of values, for a wire that moves its
        // items: the refusal gives the item back.
        template <WaitPolicy Policy, class Ctx>
            requires CtxFitsWaitPolicy<Ctx, Policy> && ::fixy::futex::CtxMayUseFutex<Ctx> && moves_its_items
        [[nodiscard]] push_result push_waiting(Ctx const& ctx, value_type&& item) noexcept {
            this->require_live("a push through a parked producer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            ::foundation::core::Option<value_type> pending =
                ::foundation::core::Option<value_type>::some(std::move(item));
            auto attempt = [this, &pending]() noexcept { return push_pending_(pending); };
            PushRefusal why = PushRefusal::Full;
            if (push_waiting_<Policy>(ctx, attempt, why)) [[likely]]
                return ::foundation::core::Unit{};
            return refuse_owner_(pending, why);
        }

        // As push_waiting, but the wait ends at deadline with
        // PushRefusal::Full.  A deadline needs the park, so Policy parks.
        template <WaitPolicy Policy, class Ctx>
            requires CtxFitsWaitPolicy<Ctx, Policy> && (Policy::should_park)
                  && ::fixy::futex::CtxMayUseFutex<Ctx> && (!moves_its_items)
        [[nodiscard]] push_result push_waiting_until(Ctx const& ctx, value_type const& item,
                                                     ::fixy::futex::Deadline const& deadline) noexcept {
            this->require_live("a push through a parked producer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            auto attempt = [this, &item]() noexcept { return inner_.try_push(item); };
            PushRefusal why = PushRefusal::Full;
            if (push_waiting_<Policy>(ctx, attempt, why, deadline)) [[likely]]
                return ::foundation::core::Unit{};
            return ::foundation::core::err(why);
        }

        // As push_waiting_until of a wire of values, for a wire that moves its
        // items: the refusal gives the item back.
        template <WaitPolicy Policy, class Ctx>
            requires CtxFitsWaitPolicy<Ctx, Policy> && (Policy::should_park)
                  && ::fixy::futex::CtxMayUseFutex<Ctx> && moves_its_items
        [[nodiscard]] push_result push_waiting_until(Ctx const& ctx, value_type&& item,
                                                     ::fixy::futex::Deadline const& deadline) noexcept {
            this->require_live("a push through a parked producer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            ::foundation::core::Option<value_type> pending =
                ::foundation::core::Option<value_type>::some(std::move(item));
            auto attempt = [this, &pending]() noexcept { return push_pending_(pending); };
            PushRefusal why = PushRefusal::Full;
            if (push_waiting_<Policy>(ctx, attempt, why, deadline)) [[likely]]
                return ::foundation::core::Unit{};
            return refuse_owner_(pending, why);
        }

        // Ends the stream, and then notifies items, so a consumer that parks
        // after its last receive reads the end.
        template <class Ctx>
            requires ::fixy::futex::CtxMayUseFutex<Ctx>
        void close(Ctx const& ctx) && noexcept {
            this->mark_consumed(__PRETTY_FUNCTION__, __LINE__);
            std::move(inner_).close();
            channel_->items_.notify_one(ctx);
        }

        // Gives the token back and keeps the stream open.  No condition of a
        // wait changes, so the door notifies nothing.  A producer of a merge
        // holds a share of the pool and no token, so it has no release.
        [[nodiscard]] ::foundation::permissions::Permission<producer_tag, brand_type> release() && noexcept
            requires(!is_merge)
        {
            this->mark_consumed(__PRETTY_FUNCTION__, __LINE__);
            return std::move(inner_).release();
        }

    private:
        // One push of an item that moves: the ring takes the item of pending,
        // or gives it back into pending.  True when the ring took it.
        [[nodiscard]] bool push_pending_(::foundation::core::Option<value_type>& pending) noexcept {
            pending = inner_.try_push(pending.take().expect("a push holds its item until the ring takes it"));
            return pending.is_none();
        }

        // The two bodies below hold the one notify of items of the push doors.
        // A body gives true when the ring took the item, and it writes the
        // reason of a refusal into why.  Each door then builds its result in
        // its own two arms, so a push that the ring took returns a constant,
        // as a door with its own body does.  A body that gives a result or an
        // optional reason makes GCC build the result at the join of the arms,
        // which adds instructions to that path.

        // The push of each door with no wait: attempt pushes one time, and a
        // push that the ring took notifies items.  A refusal writes Full, or
        // ConsumerGone when the consumer closed.
        template <class Ctx, class Attempt>
        [[nodiscard]] bool push_once_(Ctx const& ctx, Attempt& attempt, PushRefusal& why) noexcept {
            if (attempt()) [[likely]] {
                channel_->items_.notify_one(ctx);
                return true;
            }
            why = channel_->refusal_of_full_();
            return false;
        }

        // The wait of each door that waits: attempt pushes, by Policy, until
        // the ring takes the item, the consumer closes, or the deadline in
        // bound passes.  A push that the ring took notifies items.  A refusal
        // writes ConsumerGone at a close, and Full at the deadline.
        template <WaitPolicy Policy, class Ctx, class Attempt, class... Bound>
        [[nodiscard]] bool push_waiting_(Ctx const& ctx, Attempt& attempt, PushRefusal& why,
                                         Bound const&... bound) noexcept {
            bool is_pushed = false;
            ParkedChannel& channel = *channel_;
            auto condition = [&attempt, &is_pushed, &channel]() noexcept {
                is_pushed = attempt();
                return is_pushed || channel.is_consumer_gone_();
            };
            PushRefusal refusal = PushRefusal::ConsumerGone;
            if constexpr (sizeof...(Bound) == 0) {
                wait_for<Policy>(ctx, channel.room_, condition);
            } else if (wait_for<Policy>(ctx, channel.room_, condition, bound...) == EventWaitEnd::deadline_passed) {
                refusal = PushRefusal::Full;
            }
            if (!is_pushed) {
                why = refusal;
                return false;
            }
            channel.items_.notify_one(ctx);
            return true;
        }

        // The error of a push door of a wire that moves its items: the item
        // that pending still holds, back to the caller, and the reason.
        [[nodiscard]] static push_result refuse_owner_(::foundation::core::Option<value_type>& pending,
                                                       PushRefusal why) noexcept {
            return ::foundation::core::err(
                PushRefused<value_type>{pending.take().expect("a refused push keeps its item"), why});
        }
    };

    class[[= ::foundation::linear::linear{}]][[= ::foundation::linear::owes_consume{}]] ConsumerEnd
        : private ::fixy::detail::ConsumeTracker<::foundation::algebra::lattices::QttGrade::One, ConsumerEnd> {
        typename Channel::ConsumerEnd inner_;
        ::foundation::core::Ref<ParkedChannel, typename Channel::brand_type> channel_;

        ConsumerEnd(typename Channel::ConsumerEnd&& inner, ParkedChannel& channel) noexcept
            : inner_{std::move(inner)}, channel_{::foundation::core::to(channel)} {}
        friend class ParkedChannel;

    public:
        using brand_type = typename Channel::brand_type;
        using value_type = typename Channel::row_payload;
        static constexpr ChannelForm channel_form = Channel::channel_form;
        static constexpr std::size_t channel_capacity = Channel::channel_capacity;

        ConsumerEnd(ConsumerEnd const&) = delete("a parked consumer end owns the consumer token");
        ConsumerEnd& operator=(ConsumerEnd const&) = delete("a parked consumer end owns the consumer token");
        ConsumerEnd(ConsumerEnd&&) noexcept = default;
        ConsumerEnd& operator=(ConsumerEnd&&) = delete("a parked consumer end binds to one channel for its life");
        ~ConsumerEnd() = default;

        // The oldest item, or no value when the ring is empty.  A pop that
        // takes an item notifies room.
        template <class Ctx>
            requires ::fixy::futex::CtxMayUseFutex<Ctx> && pops_with<Ctx>
        [[nodiscard]] ::foundation::core::Option<value_type> try_pop(Ctx const& ctx) noexcept {
            this->require_live("a pop through a parked consumer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            ::foundation::core::Option<value_type> popped = pop_(ctx);
            if (popped.is_some()) channel_->room_.notify_one(ctx);
            return popped;
        }

        // The oldest item, NoItem::Empty, or NoItem::Closed after the last
        // item of a closed channel.  A receive that takes an item notifies
        // room.
        template <class Ctx>
            requires ::fixy::futex::CtxMayUseFutex<Ctx> && pops_with<Ctx>
        [[nodiscard]] ::foundation::core::Result<value_type, NoItem> try_receive(Ctx const& ctx) noexcept {
            this->require_live("a receive through a parked consumer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            ::foundation::core::Result<value_type, NoItem> received = receive_(ctx);
            if (received.is_ok()) channel_->room_.notify_one(ctx);
            return received;
        }

        // The oldest item, or NoItem::Closed after the last item.  While the
        // ring is empty and the stream is open, the call waits by Policy on
        // items.  A receive that takes an item notifies room.
        template <WaitPolicy Policy, class Ctx>
            requires CtxFitsWaitPolicy<Ctx, Policy> && ::fixy::futex::CtxMayUseFutex<Ctx> && pops_with<Ctx>
        [[nodiscard]] ::foundation::core::Result<value_type, NoItem> receive_waiting(Ctx const& ctx) noexcept {
            this->require_live("a receive through a parked consumer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            ::foundation::core::Option<::foundation::core::Result<value_type, NoItem>> got{::foundation::core::none};
            wait_for<Policy>(ctx, channel_->items_, [this, &ctx, &got]() noexcept { return has_received_(ctx, got); });
            ::foundation::core::Result<value_type, NoItem> received =
                std::move(got).expect("the wait ends after a receive that gives an item or the end");
            if (received.is_ok()) channel_->room_.notify_one(ctx);
            return received;
        }

        // As receive_waiting, but the wait ends at deadline with
        // NoItem::Empty.  A deadline needs the park, so Policy parks.
        template <WaitPolicy Policy, class Ctx>
            requires CtxFitsWaitPolicy<Ctx, Policy> && (Policy::should_park)
                  && ::fixy::futex::CtxMayUseFutex<Ctx> && pops_with<Ctx>
        [[nodiscard]] ::foundation::core::Result<value_type, NoItem>
        receive_waiting_until(Ctx const& ctx, ::fixy::futex::Deadline const& deadline) noexcept {
            this->require_live("a receive through a parked consumer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            ::foundation::core::Option<::foundation::core::Result<value_type, NoItem>> got{::foundation::core::none};
            EventWaitEnd const end = wait_for<Policy>(
                ctx, channel_->items_, [this, &ctx, &got]() noexcept { return has_received_(ctx, got); }, deadline);
            if (end == EventWaitEnd::deadline_passed || got.is_none()) {
                return ::foundation::core::err(NoItem::Empty);
            }
            ::foundation::core::Result<value_type, NoItem> received =
                std::move(got).expect("the test above found a receive");
            if (received.is_ok()) channel_->room_.notify_one(ctx);
            return received;
        }

        // True when the stream ended, with acquire order.
        [[nodiscard]] bool is_closed() const noexcept { return inner_.is_closed(); }

        // Ends the consumer: stores that the consumer is gone, ends the end of
        // the wire, and then wakes each waiter of room.
        template <class Ctx>
            requires ::fixy::futex::CtxMayUseFutex<Ctx>
        void close(Ctx const& ctx) && noexcept {
            this->mark_consumed(__PRETTY_FUNCTION__, __LINE__);
            ParkedChannel& channel = *channel_;
            channel.consumer_gone_.store_release(true);
            ::foundation::linear::drop(std::move(inner_));
            channel.room_.notify_all(ctx);
        }

        // Gives the token back and keeps the stream, so a later consumer of the
        // token can open the channel again.  No condition of a wait changes, so
        // the door notifies nothing: a producer that parks on a full ring waits
        // for the pop of the next consumer.
        [[nodiscard]] ::foundation::permissions::Permission<consumer_tag, brand_type> release() && noexcept {
            this->mark_consumed(__PRETTY_FUNCTION__, __LINE__);
            return std::move(inner_).release();
        }

    private:
        // The pop and the receive of the inner end.  The pop of a merge reads
        // the schedule, so it takes the tag of Nondet from the context, and the
        // pop of a wire takes none.
        template <class Ctx>
        [[nodiscard]] ::foundation::core::Option<value_type> pop_(Ctx const& ctx) noexcept {
            if constexpr (is_merge) {
                return inner_.try_pop(
                    ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx));
            } else {
                static_cast<void>(ctx);
                return inner_.try_pop();
            }
        }

        template <class Ctx>
        [[nodiscard]] ::foundation::core::Result<value_type, NoItem> receive_(Ctx const& ctx) noexcept {
            if constexpr (is_merge) {
                return inner_.try_receive(
                    ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(ctx));
            } else {
                static_cast<void>(ctx);
                return inner_.try_receive();
            }
        }

        // The condition of a wait of the consumer: a receive that gives an
        // item or the end.
        template <class Ctx>
        [[nodiscard]] bool
        has_received_(Ctx const& ctx,
                      ::foundation::core::Option<::foundation::core::Result<value_type, NoItem>>& got) noexcept {
            ::foundation::core::Result<value_type, NoItem> received = receive_(ctx);
            if (received.is_err_and([](NoItem why) noexcept { return why == NoItem::Empty; })) return false;
            got = ::foundation::core::Option<::foundation::core::Result<value_type, NoItem>>::some(std::move(received));
            return true;
        }
    };

    // The handles that the frame doors of fixy/concurrent/
    // PermissionedChannel.h give a body on the scope brand Scope.
    template <::foundation::brand::IsBrand Scope>
    using producer_handle = ParkedProducerHandle<ParkedChannel, Scope>;
    template <::foundation::brand::IsBrand Scope>
    using consumer_handle = ParkedConsumerHandle<ParkedChannel, Scope>;

    // The claim doors of the ends.  Each takes the key of the frame doors and
    // gives it to the door of the wire, so only a frame door opens an end.
    // The end comes in the key of a parked handle, which only this channel
    // builds.
    [[nodiscard]] HandleKey<ProducerEnd>
    producer(ChannelFrameKey const& key,
             ::foundation::permissions::Permission<producer_tag, brand_type>&& token) noexcept
        requires(!is_merge)
    {
        return HandleKey<ProducerEnd>{ProducerEnd{inner_.producer(key, std::move(token)), *this}};
    }

    // The producer door of a merge.  It lends a share of the pool, or gives
    // no value while the producer root is out of the pool.  The lend reads the
    // schedule, so the door takes the tag of Nondet, as the door of the merge
    // does.
    [[nodiscard]] ::foundation::core::Option<HandleKey<ProducerEnd>>
    producer(ChannelFrameKey const& key, ::foundation::effects::cap::Nondet nondet) noexcept
        requires is_merge
    {
        CRUCIBLE_TRY(lent, inner_.producer(key, nondet));
        return ::foundation::core::Option<HandleKey<ProducerEnd>>::some(
            HandleKey<ProducerEnd>{ProducerEnd{std::move(lent), *this}});
    }

    // The claim of the consumer role comes first, so a second live consumer
    // ends the process before the state changes.  Then the channel stores
    // that a consumer is there, before the handle exists.
    [[nodiscard]] HandleKey<ConsumerEnd>
    consumer(ChannelFrameKey const& key,
             ::foundation::permissions::Permission<consumer_tag, brand_type>&& token) noexcept {
        typename Channel::ConsumerEnd inner = inner_.consumer(key, std::move(token));
        consumer_gone_.store_release(false);
        return HandleKey<ConsumerEnd>{ConsumerEnd{std::move(inner), *this}};
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return channel_capacity; }

    // True when the last producer ended the stream.  A snapshot for a test.
    [[nodiscard]] bool is_closed() const noexcept { return inner_.is_closed(); }

    // True when a producer is in the count of the waiters of room: it parks
    // on a full ring, or it is in the last test before the park.  The answer
    // changes with the threads, so the read takes the bare tag of Nondet.  A
    // test reads it to know that a producer waits before it changes the
    // condition of the wait.
    [[nodiscard]] bool has_parked_producer(::foundation::effects::cap::Nondet nondet) const noexcept {
        return room_.has_waiters(nondet);
    }

    // True when a consumer is in the count of the waiters of items, as
    // has_parked_producer is for the producer.
    [[nodiscard]] bool has_parked_consumer(::foundation::effects::cap::Nondet nondet) const noexcept {
        return items_.has_waiters(nondet);
    }

private:
    // The refusal of a push that found the ring full: ConsumerGone when the
    // consumer closed, with acquire order after the failed push.
    [[nodiscard]] PushRefusal refusal_of_full_() const noexcept {
        return is_consumer_gone_() ? PushRefusal::ConsumerGone : PushRefusal::Full;
    }

    [[nodiscard]] bool is_consumer_gone_() const noexcept { return consumer_gone_.load_acquire(); }

    Channel inner_;
    EventCount items_{};
    EventCount room_{};
    // The close of the consumer stores true, and the open of a consumer
    // stores false.  A release stores nothing, because the stream stays.  A
    // channel starts with no consumer yet, which is not a consumer that ended,
    // so it starts false: a push that finds the ring full before the first
    // consumer opens parks, and the first pop of that consumer wakes it.
    alignas(64)::foundation::core::Atomic<bool> consumer_gone_{false};
};

// The producer handle of a frame of a parked channel, on the scope brand of
// one call of a frame door, as ProducerHandle of fixy/concurrent/
// PermissionedChannel.h is for a plain channel.  The constructor takes the
// end out of a key of a parked handle, and only a claim door of ParkedChannel
// builds a key.  Each claim door takes the key of the frame doors, so only
// build_handle_ of that key has a key to give here.
template <class Channel, ::foundation::brand::IsBrand Scope>
class[[= ::foundation::linear::linear{}]][[= ::foundation::linear::owes_consume{}]] ParkedProducerHandle
    : private Channel::ProducerEnd {
    using End = typename Channel::ProducerEnd;

public:
    explicit ParkedProducerHandle(typename Channel::template HandleKey<End>&& key) noexcept
        : End{std::move(key.end_)} {}

    using brand_type = typename End::brand_type;
    using value_type = typename End::value_type;
    using scope_type = Scope;
    static constexpr ChannelForm channel_form = End::channel_form;
    static constexpr std::size_t channel_capacity = End::channel_capacity;

    ParkedProducerHandle(ParkedProducerHandle&&) noexcept = default;
    ParkedProducerHandle(ParkedProducerHandle const&) = delete("a parked producer handle owns the producer token");
    ParkedProducerHandle&
    operator=(ParkedProducerHandle const&) = delete("a parked producer handle owns the producer token");
    ParkedProducerHandle&
    operator=(ParkedProducerHandle&&) = delete("a parked producer handle binds to one channel for its life");
    ~ParkedProducerHandle() = default;

    // The push doors of a wire of values take the item by reference to const,
    // and the push doors of a wire that moves its items take it by move.
    template <class Ctx>
    [[nodiscard]] auto try_push(Ctx const& ctx, value_type const& item) noexcept
        requires requires(End& end) { end.try_push(ctx, item); }
    {
        return End::try_push(ctx, item);
    }

    template <class Ctx>
    [[nodiscard]] auto try_push(Ctx const& ctx, value_type&& item) noexcept
        requires(Channel::moves_its_items) && requires(End& end) { end.try_push(ctx, std::move(item)); }
    {
        return End::try_push(ctx, std::move(item));
    }

    template <WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto push_waiting(Ctx const& ctx, value_type const& item) noexcept
        requires requires(End& end) { end.template push_waiting<Policy>(ctx, item); }
    {
        return End::template push_waiting<Policy>(ctx, item);
    }

    template <WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto push_waiting(Ctx const& ctx, value_type&& item) noexcept
        requires(Channel::moves_its_items)
             && requires(End& end) { end.template push_waiting<Policy>(ctx, std::move(item)); }
    {
        return End::template push_waiting<Policy>(ctx, std::move(item));
    }

    template <WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto push_waiting_until(Ctx const& ctx, value_type const& item,
                                          ::fixy::futex::Deadline const& deadline) noexcept
        requires requires(End& end) { end.template push_waiting_until<Policy>(ctx, item, deadline); }
    {
        return End::template push_waiting_until<Policy>(ctx, item, deadline);
    }

    template <WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto push_waiting_until(Ctx const& ctx, value_type&& item,
                                          ::fixy::futex::Deadline const& deadline) noexcept
        requires(Channel::moves_its_items)
             && requires(End& end) { end.template push_waiting_until<Policy>(ctx, std::move(item), deadline); }
    {
        return End::template push_waiting_until<Policy>(ctx, std::move(item), deadline);
    }

    template <class Ctx>
    void close(Ctx const& ctx) && noexcept
        requires requires(End&& end) { static_cast<End&&>(end).close(ctx); }
    {
        static_cast<End&&>(*this).close(ctx);
    }

    // A producer of a merge holds a share of the pool and no token, so its
    // handle has no release.
    [[nodiscard]] auto release() && noexcept
        requires(!Channel::is_merge)
    {
        return static_cast<End&&>(*this).release();
    }
};

// The consumer handle of a frame of a parked channel, as ConsumerHandle of
// fixy/concurrent/PermissionedChannel.h is for a plain channel.  The
// constructor takes the end out of a key of a parked handle, for the reason
// of the producer handle.
template <class Channel, ::foundation::brand::IsBrand Scope>
class[[= ::foundation::linear::linear{}]][[= ::foundation::linear::owes_consume{}]] ParkedConsumerHandle
    : private Channel::ConsumerEnd {
    using End = typename Channel::ConsumerEnd;

public:
    explicit ParkedConsumerHandle(typename Channel::template HandleKey<End>&& key) noexcept
        : End{std::move(key.end_)} {}

    using brand_type = typename End::brand_type;
    using value_type = typename End::value_type;
    using scope_type = Scope;
    static constexpr ChannelForm channel_form = End::channel_form;
    static constexpr std::size_t channel_capacity = End::channel_capacity;

    ParkedConsumerHandle(ParkedConsumerHandle&&) noexcept = default;
    ParkedConsumerHandle(ParkedConsumerHandle const&) = delete("a parked consumer handle owns the consumer token");
    ParkedConsumerHandle&
    operator=(ParkedConsumerHandle const&) = delete("a parked consumer handle owns the consumer token");
    ParkedConsumerHandle&
    operator=(ParkedConsumerHandle&&) = delete("a parked consumer handle binds to one channel for its life");
    ~ParkedConsumerHandle() = default;

    // A pop door of a merge takes a context that owns Nondet.  Each pop door
    // names that gate first (pops_with), so a refusal of a context names it.
    template <class Ctx>
    [[nodiscard]] auto try_pop(Ctx const& ctx) noexcept
        requires(Channel::template pops_with<Ctx>) && requires(End& end) { end.try_pop(ctx); }
    {
        return End::try_pop(ctx);
    }

    template <class Ctx>
    [[nodiscard]] auto try_receive(Ctx const& ctx) noexcept
        requires(Channel::template pops_with<Ctx>) && requires(End& end) { end.try_receive(ctx); }
    {
        return End::try_receive(ctx);
    }

    template <WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto receive_waiting(Ctx const& ctx) noexcept
        requires(Channel::template pops_with<Ctx>) && requires(End& end) { end.template receive_waiting<Policy>(ctx); }
    {
        return End::template receive_waiting<Policy>(ctx);
    }

    template <WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto receive_waiting_until(Ctx const& ctx, ::fixy::futex::Deadline const& deadline) noexcept
        requires(Channel::template pops_with<Ctx>)
             && requires(End& end) { end.template receive_waiting_until<Policy>(ctx, deadline); }
    {
        return End::template receive_waiting_until<Policy>(ctx, deadline);
    }

    [[nodiscard]] bool is_closed() const noexcept { return End::is_closed(); }

    template <class Ctx>
    void close(Ctx const& ctx) && noexcept
        requires requires(End&& end) { static_cast<End&&>(end).close(ctx); }
    {
        static_cast<End&&>(*this).close(ctx);
    }

    [[nodiscard]] auto release() && noexcept { return static_cast<End&&>(*this).release(); }
};

}  // namespace fixy::concurrent
