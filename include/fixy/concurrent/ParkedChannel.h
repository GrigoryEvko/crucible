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
//     PermissionedChannel.h privately, so no door of the plain channel
//     reaches its ring.  It has the shape of that header: its claim doors
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

template <class Channel, ::foundation::brand::IsBrand Scope>
class ParkedProducerHandle;
template <class Channel, ::foundation::brand::IsBrand Scope>
class ParkedConsumerHandle;

// The parked form of a wire of values (the shape).  Channel is a wire of
// fixy/concurrent/PermissionedChannel.h.
template <class Channel>
    requires ChannelOfForm<Channel, ChannelForm::wire>
class ParkedChannel : public ::foundation::Pinned<ParkedChannel<Channel>> {
public:
    using value_type = typename Channel::row_payload;
    using brand_type = typename Channel::brand_type;
    using whole_tag = typename Channel::whole_tag;
    using producer_tag = typename Channel::producer_tag;
    using consumer_tag = typename Channel::consumer_tag;
    static constexpr ChannelForm channel_form = ChannelForm::wire;
    static constexpr std::size_t channel_capacity = Channel::channel_capacity;

    // A wire starts with no handle.  The caller keeps the two tokens.
    ParkedChannel() noexcept = default;

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
        static constexpr ChannelForm channel_form = ChannelForm::wire;
        static constexpr std::size_t channel_capacity = Channel::channel_capacity;

        ProducerEnd(ProducerEnd const&) = delete("a parked producer end owns the producer token");
        ProducerEnd& operator=(ProducerEnd const&) = delete("a parked producer end owns the producer token");
        ProducerEnd(ProducerEnd&&) noexcept = default;
        ProducerEnd& operator=(ProducerEnd&&) = delete("a parked producer end binds to one channel for its life");
        ~ProducerEnd() = default;

        // Pushes item, and notifies items when the ring took it.  The error
        // tells a full ring from a consumer that ended.
        template <class Ctx>
            requires ::fixy::futex::CtxMayUseFutex<Ctx>
        [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, PushRefusal>
        try_push(Ctx const& ctx, value_type const& item) noexcept {
            this->require_live("a push through a parked producer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            if (inner_.try_push(item)) [[likely]] {
                channel_->items_.notify_one(ctx);
                return ::foundation::core::Unit{};
            }
            return ::foundation::core::err(channel_->refusal_of_full_());
        }

        // Pushes item, and waits by Policy while the ring is full: Spin spins,
        // and SpinThenPark spins for its budget and then parks on room.  The
        // condition of the wait is a push, or the close of the consumer, so
        // the call ends with ConsumerGone when the consumer closes.
        template <WaitPolicy Policy, class Ctx>
            requires CtxFitsWaitPolicy<Ctx, Policy> && ::fixy::futex::CtxMayUseFutex<Ctx>
        [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, PushRefusal>
        push_waiting(Ctx const& ctx, value_type const& item) noexcept {
            this->require_live("a push through a parked producer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            bool is_pushed = false;
            ParkedChannel& channel = *channel_;
            wait_for<Policy>(ctx, channel.room_, [this, &item, &is_pushed, &channel]() noexcept {
                is_pushed = inner_.try_push(item);
                return is_pushed || channel.is_consumer_gone_();
            });
            if (!is_pushed) return ::foundation::core::err(PushRefusal::ConsumerGone);
            channel.items_.notify_one(ctx);
            return ::foundation::core::Unit{};
        }

        // As push_waiting, but the wait ends at deadline with
        // PushRefusal::Full.  A deadline needs the park, so Policy parks.
        template <WaitPolicy Policy, class Ctx>
            requires CtxFitsWaitPolicy<Ctx, Policy> && (Policy::should_park) && ::fixy::futex::CtxMayUseFutex<Ctx>
        [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, PushRefusal>
        push_waiting_until(Ctx const& ctx, value_type const& item, ::fixy::futex::Deadline const& deadline) noexcept {
            this->require_live("a push through a parked producer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            bool is_pushed = false;
            ParkedChannel& channel = *channel_;
            EventWaitEnd const end = wait_for<Policy>(
                ctx, channel.room_,
                [this, &item, &is_pushed, &channel]() noexcept {
                    is_pushed = inner_.try_push(item);
                    return is_pushed || channel.is_consumer_gone_();
                },
                deadline);
            if (is_pushed) {
                channel.items_.notify_one(ctx);
                return ::foundation::core::Unit{};
            }
            return ::foundation::core::err(end == EventWaitEnd::deadline_passed ? PushRefusal::Full
                                                                                : PushRefusal::ConsumerGone);
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
        // wait changes, so the door notifies nothing.
        [[nodiscard]] ::foundation::permissions::Permission<producer_tag, brand_type> release() && noexcept {
            this->mark_consumed(__PRETTY_FUNCTION__, __LINE__);
            return std::move(inner_).release();
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
        static constexpr ChannelForm channel_form = ChannelForm::wire;
        static constexpr std::size_t channel_capacity = Channel::channel_capacity;

        ConsumerEnd(ConsumerEnd const&) = delete("a parked consumer end owns the consumer token");
        ConsumerEnd& operator=(ConsumerEnd const&) = delete("a parked consumer end owns the consumer token");
        ConsumerEnd(ConsumerEnd&&) noexcept = default;
        ConsumerEnd& operator=(ConsumerEnd&&) = delete("a parked consumer end binds to one channel for its life");
        ~ConsumerEnd() = default;

        // The oldest item, or no value when the ring is empty.  A pop that
        // takes an item notifies room.
        template <class Ctx>
            requires ::fixy::futex::CtxMayUseFutex<Ctx>
        [[nodiscard]] ::foundation::core::Option<value_type> try_pop(Ctx const& ctx) noexcept {
            this->require_live("a pop through a parked consumer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            ::foundation::core::Option<value_type> popped = inner_.try_pop();
            if (popped.is_some()) channel_->room_.notify_one(ctx);
            return popped;
        }

        // The oldest item, NoItem::Empty, or NoItem::Closed after the last
        // item of a closed channel.  A receive that takes an item notifies
        // room.
        template <class Ctx>
            requires ::fixy::futex::CtxMayUseFutex<Ctx>
        [[nodiscard]] ::foundation::core::Result<value_type, NoItem> try_receive(Ctx const& ctx) noexcept {
            this->require_live("a receive through a parked consumer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            ::foundation::core::Result<value_type, NoItem> received = inner_.try_receive();
            if (received.is_ok()) channel_->room_.notify_one(ctx);
            return received;
        }

        // The oldest item, or NoItem::Closed after the last item.  While the
        // ring is empty and the stream is open, the call waits by Policy on
        // items.  A receive that takes an item notifies room.
        template <WaitPolicy Policy, class Ctx>
            requires CtxFitsWaitPolicy<Ctx, Policy> && ::fixy::futex::CtxMayUseFutex<Ctx>
        [[nodiscard]] ::foundation::core::Result<value_type, NoItem> receive_waiting(Ctx const& ctx) noexcept {
            this->require_live("a receive through a parked consumer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            ::foundation::core::Option<::foundation::core::Result<value_type, NoItem>> got{::foundation::core::none};
            wait_for<Policy>(ctx, channel_->items_, [this, &got]() noexcept { return has_received_(got); });
            ::foundation::core::Result<value_type, NoItem> received =
                std::move(got).expect("the wait ends after a receive that gives an item or the end");
            if (received.is_ok()) channel_->room_.notify_one(ctx);
            return received;
        }

        // As receive_waiting, but the wait ends at deadline with
        // NoItem::Empty.  A deadline needs the park, so Policy parks.
        template <WaitPolicy Policy, class Ctx>
            requires CtxFitsWaitPolicy<Ctx, Policy> && (Policy::should_park) && ::fixy::futex::CtxMayUseFutex<Ctx>
        [[nodiscard]] ::foundation::core::Result<value_type, NoItem>
        receive_waiting_until(Ctx const& ctx, ::fixy::futex::Deadline const& deadline) noexcept {
            this->require_live("a receive through a parked consumer end that a door consumed", __PRETTY_FUNCTION__,
                               __LINE__);
            ::foundation::core::Option<::foundation::core::Result<value_type, NoItem>> got{::foundation::core::none};
            EventWaitEnd const end = wait_for<Policy>(
                ctx, channel_->items_, [this, &got]() noexcept { return has_received_(got); }, deadline);
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
        // The condition of a wait of the consumer: a receive that gives an
        // item or the end.
        [[nodiscard]] bool
        has_received_(::foundation::core::Option<::foundation::core::Result<value_type, NoItem>>& got) noexcept {
            ::foundation::core::Result<value_type, NoItem> received = inner_.try_receive();
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
             ::foundation::permissions::Permission<producer_tag, brand_type>&& token) noexcept {
        return HandleKey<ProducerEnd>{ProducerEnd{inner_.producer(key, std::move(token)), *this}};
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

    template <class Ctx>
    [[nodiscard]] auto try_push(Ctx const& ctx, value_type const& item) noexcept
        requires requires(End& end) { end.try_push(ctx, item); }
    {
        return End::try_push(ctx, item);
    }

    template <WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto push_waiting(Ctx const& ctx, value_type const& item) noexcept
        requires requires(End& end) { end.template push_waiting<Policy>(ctx, item); }
    {
        return End::template push_waiting<Policy>(ctx, item);
    }

    template <WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto push_waiting_until(Ctx const& ctx, value_type const& item,
                                          ::fixy::futex::Deadline const& deadline) noexcept
        requires requires(End& end) { end.template push_waiting_until<Policy>(ctx, item, deadline); }
    {
        return End::template push_waiting_until<Policy>(ctx, item, deadline);
    }

    template <class Ctx>
    void close(Ctx const& ctx) && noexcept
        requires requires(End&& end) { static_cast<End&&>(end).close(ctx); }
    {
        static_cast<End&&>(*this).close(ctx);
    }

    [[nodiscard]] auto release() && noexcept { return static_cast<End&&>(*this).release(); }
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

    template <class Ctx>
    [[nodiscard]] auto try_pop(Ctx const& ctx) noexcept
        requires requires(End& end) { end.try_pop(ctx); }
    {
        return End::try_pop(ctx);
    }

    template <class Ctx>
    [[nodiscard]] auto try_receive(Ctx const& ctx) noexcept
        requires requires(End& end) { end.try_receive(ctx); }
    {
        return End::try_receive(ctx);
    }

    template <WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto receive_waiting(Ctx const& ctx) noexcept
        requires requires(End& end) { end.template receive_waiting<Policy>(ctx); }
    {
        return End::template receive_waiting<Policy>(ctx);
    }

    template <WaitPolicy Policy, class Ctx>
    [[nodiscard]] auto receive_waiting_until(Ctx const& ctx, ::fixy::futex::Deadline const& deadline) noexcept
        requires requires(End& end) { end.template receive_waiting_until<Policy>(ctx, deadline); }
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
