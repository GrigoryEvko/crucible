#pragma once

// A channel over a ring behind linear permissions.  The handle types carry
// the role, so a push from the consumer side or a pop from the producer side
// does not compile.  The form of the channel (ChannelForm) chooses the ring
// and the producer side.  The consumer handle, the tags, the splits and the
// end of the stream are written one time for each form.
//
// THE FORMS
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
//     In each form the consumer side is linear, because the pop of each ring
//     assumes one consumer: the tail has one writer and no synchronization.
//
// THE BRAND
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
// THE END OF THE STREAM
//     The end of the last live producer handle with its token closes the
//     channel, and try_receive of the consumer then gives NoItem::Closed after
//     the last item (fixy/concurrent/ChannelEnd.h).  A release does not close
//     the channel, and the next producer handle opens it again.
//
// The endpoint accessors are members named producer and consumer rather
// than mint_ free functions.  Their admission is the parameter type and the
// claim or the pool.  A caller that cannot produce a Permission<producer_tag,
// Brand> cannot open a wire producer, so there is no requires-clause for a
// negative fixture to fire, and a mint_ name would claim a gate that they do
// not have.

#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/SpscRing.h>
#include <fixy/concurrent/WorkingSet.h>

#include <foundation/Brand.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
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
// structs are the claims of the wire form, and the mpsc structs are the
// claims of the merge form.
namespace fixy::row_discipline {
struct spsc_channel;
struct spsc_producer;
struct spsc_consumer;
struct mpsc_channel;
struct mpsc_producer;
struct mpsc_consumer;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

// The form of a permissioned channel (THE FORMS).  A planned form, such as
// a queue of many producers and many consumers or a broadcast channel, adds
// a value here and a specialization of detail::channel_form_parts.
enum class ChannelForm : std::uint8_t {
    wire = 0,
    merge = 1,
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

// What a form chooses: the ring, the share that a producer handle holds, the
// state of the producer side in the channel, the row disciplines, and the
// cache lines of one call of each handle.
template <ChannelForm Form, RingValue T, std::size_t Capacity, typename ProducerTag, typename Brand>
struct channel_form_parts;

template <RingValue T, std::size_t Capacity, typename ProducerTag, typename Brand>
struct channel_form_parts<ChannelForm::wire, T, Capacity, ProducerTag, Brand> {
    using ring_type = SpscRing<T, Capacity>;
    // The producer handle holds the linear token.
    using producer_share = ::foundation::permissions::Permission<ProducerTag, Brand>;
    // The channel holds the claim of the producer role.
    using producer_side = ::foundation::EndpointClaim;
    using channel_discipline = ::fixy::row_discipline::spsc_channel;
    using producer_discipline = ::fixy::row_discipline::spsc_producer;
    using consumer_discipline = ::fixy::row_discipline::spsc_consumer;
    // A push reads the head and the tail.  A pop reads the head, the tail
    // and the roster of the producers, which try_receive reads when the ring
    // is empty.
    static constexpr std::size_t producer_lines = 2;
    static constexpr std::size_t consumer_lines = 3;
};

template <RingValue T, std::size_t Capacity, typename ProducerTag, typename Brand>
struct channel_form_parts<ChannelForm::merge, T, Capacity, ProducerTag, Brand> {
    using ring_type = MpscRing<T, Capacity>;
    // The producer handle holds one share of the pool.
    using producer_share = ::foundation::permissions::SharedPermissionGuard<ProducerTag, Brand>;
    // The channel holds the pool, which parks the producer token.
    using producer_side = ::foundation::permissions::SharedPermissionPool<ProducerTag, Brand>;
    using channel_discipline = ::fixy::row_discipline::mpsc_channel;
    using producer_discipline = ::fixy::row_discipline::mpsc_producer;
    using consumer_discipline = ::fixy::row_discipline::mpsc_consumer;
    // A push reads the head, the tail and one word of the ready bitmap.  A
    // pop reads the head, the tail, the bitmap word and the roster.
    static constexpr std::size_t producer_lines = 3;
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
    // of a closed channel (THE END OF THE STREAM of fixy/concurrent/
    // ChannelEnd.h).
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

// UserTag and Brand have no default.  A default tag would give every channel
// that omits it one set of Permission types.  The erased brand would let a
// token of every root site open the endpoints.
template <ChannelForm Form, RingValue T, std::size_t Capacity, typename UserTag,
          ::foundation::brand::IsFreshBrand Brand>
class PermissionedChannel : public ::foundation::Pinned<PermissionedChannel<Form, T, Capacity, UserTag, Brand>> {
    using parts = detail::channel_form_parts<Form, T, Capacity, channel_tag::Producer<Form, UserTag>, Brand>;

public:
    using value_type = T;
    using user_tag = UserTag;
    using brand_type = Brand;
    using whole_tag = channel_tag::Whole<Form, UserTag>;
    using producer_tag = channel_tag::Producer<Form, UserTag>;
    using consumer_tag = channel_tag::Consumer<Form, UserTag>;
    using ring_type = typename parts::ring_type;

    // The form, which the net of a program reads: a wire or a merge agent.
    static constexpr ChannelForm channel_form = Form;
    static constexpr std::size_t channel_capacity = Capacity;
    using row_discipline = typename parts::channel_discipline;
    using row_payload = T;

    // The channel's identity is its address, since the ring's atomics depend
    // on a stable one.

    // A wire channel starts with no handle.  The caller keeps both tokens.
    PermissionedChannel() noexcept
        requires(Form == ChannelForm::wire)
    = default;

    // The pool of a merge channel parks the producer token of the split root,
    // and each producer handle holds one share of it.  No default constructor
    // builds a merge channel with an empty pool.
    explicit PermissionedChannel(::foundation::permissions::Permission<producer_tag, Brand>&& producer_root) noexcept
        requires(Form == ChannelForm::merge)
        : producer_side_{std::move(producer_root)} {}

    class[[= ::foundation::linear::linear_when<Form == ChannelForm::wire>]] ProducerHandle {
        // The move clears the binding, so a moved-from handle cannot push: a
        // defaulted move of an empty Permission is a no-op, and a reference
        // or a plain pointer would let the source and the target both go on
        // using the one token or the one share.  A moved-from producer of a
        // merge would push with no pool share, during the window that
        // assumes every producer out.
        ::foundation::ChannelBinding<PermissionedChannel> ch_;
        [[no_unique_address]] typename parts::producer_share share_;

        constexpr ProducerHandle(PermissionedChannel& channel, typename parts::producer_share&& share) noexcept
            : ch_{channel}, share_{std::move(share)} {}
        friend class PermissionedChannel;

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
        static constexpr std::size_t channel_capacity = Capacity;

        ProducerHandle(const ProducerHandle&) =
            delete("ProducerHandle owns the producer token or one pool share — copy would duplicate it");
        ProducerHandle& operator=(const ProducerHandle&) =
            delete("ProducerHandle owns the producer token or one pool share — assignment would overwrite it");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&) = delete(
            "ProducerHandle binds to ONE channel for life — rebinding would orphan the original token "
            "and silently allow a second producer to coexist");

        // A moved-from handle holds no role, so only the bound one ends a
        // producer of the stream.  The last one closes the channel.  The token
        // or the share ends with the handle, after this body, so no item of
        // this producer comes after the end.
        ~ProducerHandle() {
            if (ch_.is_bound()) ch_->end_producer_();
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

        // The value comes by reference to const, which is the shape of a
        // producer pole in fixy/concurrent/HandleTraits.h.  A value parameter
        // hides the pole, and no stage can then feed the channel.
        [[nodiscard, gnu::hot]] bool try_push(T const& item) noexcept { return ch_->ring_.try_push(item); }

        // Snapshots.  Sound for telemetry and for deciding whether to keep
        // retrying, never for a correctness invariant.  Each takes the bare
        // tag of Nondet, as the snapshot of the ring does.
        [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return ch_->ring_.empty_approx(nondet);
        }
        [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return ch_->ring_.size_approx(nondet);
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

        [[nodiscard]] constexpr ::foundation::ChannelIdentity<PermissionedChannel> channel_identity() const noexcept {
            return ch_.identity();
        }
    };

    class[[= ::foundation::linear::linear{}]] ConsumerHandle : public detail::consumer_pops<Form, T, ConsumerHandle> {
        // A binding that the move clears, for the same reason as in
        // ProducerHandle.
        ::foundation::ChannelBinding<PermissionedChannel> ch_;
        [[no_unique_address]] ::foundation::permissions::Permission<consumer_tag, Brand> perm_;

        constexpr ConsumerHandle(PermissionedChannel& channel,
                                 ::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept
            : ch_{channel}, perm_{std::move(perm)} {}
        friend class PermissionedChannel;
        friend class detail::consumer_pops<Form, T, ConsumerHandle>;

        // The bodies of the pops of detail::consumer_pops.  The pack holds
        // the tag of Nondet for a merge and no value for a wire.
        template <typename... PopTag>
        [[nodiscard, gnu::always_inline]] ::foundation::core::Option<T> pop_(PopTag... tag) noexcept {
            return ch_->ring_.try_pop(tag...);
        }

        template <typename... PopTag>
        [[nodiscard, gnu::always_inline]] ::foundation::core::Result<T, NoItem> receive_(PopTag... tag) noexcept {
            ::foundation::core::Option<T> item = ch_->ring_.try_pop(tag...);
            if (item.is_some()) [[likely]] {
                return std::move(item).ok_or(NoItem::Empty);
            }
            return ch_->receive_after_empty_(tag...);
        }

    public:
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<parts::consumer_lines, T>;
        using row_discipline = typename parts::consumer_discipline;
        using row_payload = T;
        using brand_type = Brand;
        using channel_type = PermissionedChannel;
        static constexpr ChannelForm channel_form = Form;
        static constexpr std::size_t channel_capacity = Capacity;

        ConsumerHandle(const ConsumerHandle&) =
            delete("ConsumerHandle owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerHandle& operator=(const ConsumerHandle&) =
            delete("ConsumerHandle owns the Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&) = delete(
            "ConsumerHandle binds to ONE channel for life — rebinding would orphan the original Permission "
            "and silently allow a second consumer to coexist, and the pop of each ring has one consumer");

        // A moved-from handle holds no claim, so only the bound one gives the
        // role back.
        ~ConsumerHandle() {
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
            return ch_->ring_.empty_approx(nondet);
        }
        [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return ch_->ring_.size_approx(nondet);
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

        // True when the last producer handle ended and no handle opened the
        // channel after it, with acquire order.  A consumer that parks reads
        // it with the ring after its mark, so a close that came before the
        // mark and its wake ends the park.
        [[nodiscard]] bool is_closed() const noexcept { return ch_->producers_.is_closed(); }

        [[nodiscard]] constexpr ::foundation::ChannelIdentity<PermissionedChannel> channel_identity() const noexcept {
            return ch_.identity();
        }
    };

    // The producer door of a wire channel.  It takes the claim of the
    // producer role first, so a second live producer handle ends the process
    // here.  A producer handle opens the channel.
    [[nodiscard]] ProducerHandle producer(::foundation::permissions::Permission<producer_tag, Brand>&& perm) noexcept
        requires(Form == ChannelForm::wire)
    {
        producer_side_.take("two live producer handles of one wire channel. One call site minted the root of the "
                            "channel two times.");
        producers_.add_producer();
        return ProducerHandle{*this, std::move(perm)};
    }

    // The producer door of a merge channel.  It lends a pool share, and it
    // refuses while an exclusive transition is in flight.  Several producer
    // handles coexist, which is the point of the fractional side.  A lent
    // handle opens the channel.
    [[nodiscard]] ::foundation::core::Option<ProducerHandle> producer() noexcept
        requires(Form == ChannelForm::merge)
    {
        auto lent = ::foundation::permissions::detail::ChannelPoolDoor::lend(producer_side_);
        if (lent.is_none()) return ::foundation::core::none;
        producers_.add_producer();
        return ::foundation::core::Option<ProducerHandle>::some(
            ProducerHandle{*this, std::move(lent).expect("PermissionedChannel::producer: the pool lent a share")});
    }

    // Takes the claim of the consumer role first, so a second live consumer
    // handle ends the process here.
    [[nodiscard]] ConsumerHandle consumer(::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept {
        consumer_claim_.take("two live consumer handles of one channel. One call site minted the root of the "
                             "channel two times.");
        return ConsumerHandle{*this, std::move(perm)};
    }

    // Scoped exclusive access to the ring of a wire channel.  The whole
    // permission of this brand is the type-level proof that no handle of its
    // root is alive.  A second root of the same call site has the same brand,
    // so the body also holds the two claims: a live handle ends the process
    // here, and no handle can start while the body runs.  The body gets the
    // ring: it can fill it before the first session or drain what the last
    // one left.  The whole permission comes back so the caller can split it
    // again for the next session.
    template <typename Body>
        requires(Form == ChannelForm::wire) && std::is_invocable_v<Body, ring_type&>
    [[nodiscard]] ::foundation::permissions::Permission<whole_tag, Brand>
    with_recombined_access(::foundation::permissions::Permission<whole_tag, Brand>&& whole,
                           Body&& body) noexcept(std::is_nothrow_invocable_v<Body, ring_type&>) {
        producer_side_.take("recombined access to a wire channel while its producer handle lives.");
        consumer_claim_.take("recombined access to a wire channel while its consumer handle lives.");
        std::forward<Body>(body)(ring_);
        consumer_claim_.release();
        producer_side_.release();
        return std::move(whole);
    }

    // Runs the body with every producer of a merge channel out.  Returns false
    // when producers were still out and the body did not run.  A producer can
    // be lent again once the body returns.  The body gets no ring, because
    // the consumer handle is still alive and pops from the same cells: the
    // body acts through the handles it holds.  For example, the consumer
    // drains the ring to empty, and no push lands at the same time.
    template <typename Body>
        requires(Form == ChannelForm::merge) && std::is_invocable_v<Body>
    bool with_drained_access(Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        auto upgrade = ::foundation::permissions::detail::ChannelPoolDoor::try_upgrade(producer_side_);
        if (upgrade.is_none()) return false;
        std::forward<Body>(body)();
        producer_side_.deposit_exclusive(std::move(upgrade).expect("the test above found the exclusive"));
        return true;
    }

    // Gives each item that the ring still holds to drop, the oldest first.
    // The call takes the exclusive of each role: the claims of a wire, and the
    // exclusive of the pool and the claim of the consumer of a merge.  So a
    // live handle of each role ends the process here, and no handle starts
    // while the call runs.  Each producer handle of a merge set the ready bit
    // of each of its items before its end, and the upgrade reads that end
    // with acquire order, so the drain finds each item.  The order of the
    // items of a merge reads the merge, so its drop holds no state, as the
    // drain of the MPSC ring asks.  An owner of items that end only through a
    // door, such as the words of a Box channel, calls it at its own end.
    // Complexity: linear in the count of the items.
    template <typename Drop>
        requires(Form == ChannelForm::wire || __is_empty(__remove_cvref(Drop))) && std::is_nothrow_invocable_v<Drop&, T>
    void drain(Drop&& drop) noexcept {
        if constexpr (Form == ChannelForm::wire) {
            producer_side_.take("a drain of a wire channel while its producer handle lives.");
            consumer_claim_.take("a drain of a wire channel while its consumer handle lives.");
            for (;;) {
                ::foundation::core::Option<T> item = ring_.try_pop();
                if (item.is_none()) break;
                drop(std::move(item).expect("the test above found an item"));
            }
            consumer_claim_.release();
            producer_side_.release();
        } else {
            auto exclusive = ::foundation::permissions::detail::ChannelPoolDoor::try_upgrade(producer_side_);
            if (exclusive.is_none()) [[unlikely]] {
                ::foundation::core::fatal("a drain of a merge channel while a producer handle lives.");
            }
            consumer_claim_.take("a drain of a merge channel while its consumer handle lives.");
            ring_.drain(std::forward<Drop>(drop));
            consumer_claim_.release();
            producer_side_.deposit_exclusive(std::move(exclusive).expect("the test above found the exclusive"));
        }
    }

    // The count of the pool shares of a merge channel that producer handles
    // hold.
    [[nodiscard]] std::uint64_t outstanding_producers() const noexcept
        requires(Form == ChannelForm::merge)
    {
        return producer_side_.outstanding();
    }

    // Always false for a wire channel, which has no pool: the linear
    // permissions and the claims are what prove single ownership.
    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept
        requires(Form == ChannelForm::wire)
    {
        return false;
    }

    // True while the exclusive of the pool of a merge channel is out.
    [[nodiscard]] bool is_exclusive_active() const noexcept
        requires(Form == ChannelForm::merge)
    {
        return producer_side_.is_exclusive_out();
    }

    [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return ring_.empty_approx(nondet);
    }
    [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return ring_.size_approx(nondet);
    }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    // True when the last producer handle ended and no handle opened the
    // channel after it.  A snapshot for a test: a consumer reads the end
    // through try_receive.
    [[nodiscard]] bool is_closed() const noexcept { return producers_.is_closed(); }

private:
    // The end of a producer handle with its token or its share.  The last one
    // closes the channel.  A wire gives its producer role back.  A handle ends
    // one time, so the end is cold, and a stage that holds the handle keeps one
    // call in place of the loop of the roster.
    CRUCIBLE_COLD void end_producer_() noexcept {
        producers_.end_producer();
        if constexpr (Form == ChannelForm::wire) producer_side_.release();
    }

    // The release door of a wire producer: the channel stays open.  It is cold
    // for the reason of end_producer_.
    CRUCIBLE_COLD void release_producer_() noexcept
        requires(Form == ChannelForm::wire)
    {
        producers_.release_producer();
        producer_side_.release();
    }

    // The receive after a pop that found the ring empty.  The read of the
    // closed state is an acquire, so the pop after it finds each item that the
    // stream still holds, and Closed comes only after the last item.  A
    // producer of a merge sets the ready bit of each item before its handle
    // ends, so the same holds for a merge.
    template <typename... PopTag>
    [[nodiscard]] ::foundation::core::Result<T, NoItem> receive_after_empty_(PopTag... tag) noexcept {
        if (!producers_.is_closed()) [[likely]] {
            return ::foundation::core::err(NoItem::Empty);
        }
        return receive_after_close_(tag...);
    }

    // A stream ends one time, so the pop after its end is cold.
    template <typename... PopTag>
    [[nodiscard]] CRUCIBLE_COLD ::foundation::core::Result<T, NoItem> receive_after_close_(PopTag... tag) noexcept {
        return ring_.try_pop(tag...).ok_or(NoItem::Closed);
    }

    ring_type ring_;
    typename parts::producer_side producer_side_;
    ::foundation::EndpointClaim consumer_claim_;
    ProducerRoster producers_;
};

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
// user tag and the brand off the root.
template <RingValue T, std::size_t Capacity, typename Root>
using channel_t = PermissionedChannel<detail::channel_root_parts<Root>::form, T, Capacity,
                                      typename detail::channel_root_parts<Root>::user_tag,
                                      typename detail::channel_root_parts<Root>::brand>;

// The tag and the brand the witness roster names for this channel.  The check
// file of this header names them too, and no code builds a channel of them.
namespace detail::channel_witness {
struct WitnessTag {};
struct WitnessBrand {};
}  // namespace detail::channel_witness

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
