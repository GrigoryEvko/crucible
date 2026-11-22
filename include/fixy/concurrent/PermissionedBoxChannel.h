#pragma once

// A channel of Box values, behind the permissions of PermissionedChannel, in
// one of the forms of ChannelForm.  A cell of a ring holds only a trivially
// copyable value, and a Box is no such value.  So the channel holds a
// PermissionedChannel of the same form over the words of a Box (THE WORD OF
// A BOX of foundation/core/Ref.h), and the channel class is the authority of
// those words.  Each handle of this channel holds the handle of the same role
// of the inner channel.
//
// THE OWNERS
//     Each producer handle belongs to its producer thread, and the consumer
//     handle belongs to the consumer thread.  A push gives the object of its
//     Box to a word.  The ring writes the word to its cell and then publishes
//     the cell with a release store: the head of the SPSC ring of a wire, or
//     the turn word of the cell of the MPSC ring of a merge.  A pop reads that
//     store with an acquire load before it reads the cell.  So the consumer
//     sees each write to the object that its producer made before the push,
//     and the Box of the pop owns the object alone.
//
// THE WORDS
//     A word leaves the channel only as a Box.  A push that finds the ring
//     full gives its Box back in the Option of its result, so no object is
//     lost.  The ring gives each cell to one pop, so each word is claimed one
//     time.  The end of the channel drains the ring and claims each word that
//     it still holds, and the Box of each claim ends its object.  The drain
//     takes the exclusive of each role, as the drain of PermissionedChannel
//     tells, so a live handle ends the process there.  The end of a linear
//     object drops it, so the channel takes only a payload that is not linear
//     (DroppableBoxPayload of foundation/core/Ref.h).
//
// THE END OF THE STREAM
//     The end of the last producer handle ends the last producer handle of
//     the words, so it closes the channel as fixy/concurrent/ChannelEnd.h
//     tells.  try_receive of the consumer then gives each Box that the ring
//     still holds, and NoItem::Closed after the last one.

#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/WorkingSet.h>

#include <foundation/Brand.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

// The claims of the carriers of this header that no lattice grades
// (foundation/diag/RowHash.h).  The spsc structs are the claims of the wire
// form, and the mpsc structs are the claims of the merge form.
namespace fixy::row_discipline {
struct spsc_box_channel;
struct spsc_box_producer;
struct spsc_box_consumer;
struct mpsc_box_channel;
struct mpsc_box_producer;
struct mpsc_box_consumer;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

namespace detail {

// The row disciplines of each form of the Box channel.
template <ChannelForm Form>
struct box_channel_disciplines;

template <>
struct box_channel_disciplines<ChannelForm::wire> {
    using channel = ::fixy::row_discipline::spsc_box_channel;
    using producer = ::fixy::row_discipline::spsc_box_producer;
    using consumer = ::fixy::row_discipline::spsc_box_consumer;
};

template <>
struct box_channel_disciplines<ChannelForm::merge> {
    using channel = ::fixy::row_discipline::mpsc_box_channel;
    using producer = ::fixy::row_discipline::mpsc_box_producer;
    using consumer = ::fixy::row_discipline::mpsc_box_consumer;
};

// The pops of a consumer handle of a Box channel, with the shape of
// detail::consumer_pops of fixy/concurrent/PermissionedChannel.h: one
// declaration of each pop, so fixy/concurrent/HandleTraits.h reads the pole
// off the address of the member.  The handle gives the body of each pop in
// its private pop_ and receive_, and it befriends its own specialization of
// this base.
template <ChannelForm Form, typename T, typename Handle>
class box_consumer_pops;

template <typename T, typename Handle>
class box_consumer_pops<ChannelForm::wire, T, Handle> {
public:
    // The oldest Box, or no value when the ring is empty.
    [[nodiscard, gnu::hot]] ::foundation::core::Option<T> try_pop() noexcept {
        return static_cast<Handle&>(*this).pop_();
    }

    // The oldest Box, NoItem::Empty, or NoItem::Closed after the last Box of
    // a closed channel.
    [[nodiscard, gnu::hot]] ::foundation::core::Result<T, NoItem> try_receive() noexcept {
        return static_cast<Handle&>(*this).receive_();
    }
};

// A pop of a merge takes the tag of Nondet, as the pop of the merge form of
// PermissionedChannel does.
template <typename T, typename Handle>
class box_consumer_pops<ChannelForm::merge, T, Handle> {
public:
    [[nodiscard, gnu::hot]] ::foundation::core::Option<T> try_pop(::foundation::effects::cap::Nondet nondet) noexcept {
        return static_cast<Handle&>(*this).pop_(nondet);
    }

    [[nodiscard, gnu::hot]] ::foundation::core::Result<T, NoItem>
    try_receive(::foundation::effects::cap::Nondet nondet) noexcept {
        return static_cast<Handle&>(*this).receive_(nondet);
    }
};

}  // namespace detail

// The object of a Box moves from the thread of a push to the thread of a pop,
// so the payload is Sendable (foundation/Transfer.h).  The producer can end
// before the pop, so the payload holds no borrow: it is SendableOwned.
template <ChannelForm Form, ::foundation::core::DroppableBoxPayload T, std::size_t Capacity, typename UserTag,
          ::foundation::brand::IsFreshBrand Brand>
    requires ::foundation::transfer::Sendable<T> && ::foundation::transfer::SendableOwned<T>
class PermissionedBoxChannel : public ::foundation::Pinned<PermissionedBoxChannel<Form, T, Capacity, UserTag, Brand>> {
    using word_type = ::foundation::core::BoxWord<T, PermissionedBoxChannel>;
    using cell_channel = PermissionedChannel<Form, word_type, Capacity, UserTag, Brand>;
    using disciplines = detail::box_channel_disciplines<Form>;

public:
    using value_type = ::foundation::core::Box<T>;
    using user_tag = UserTag;
    using brand_type = Brand;
    using whole_tag = typename cell_channel::whole_tag;
    using producer_tag = typename cell_channel::producer_tag;
    using consumer_tag = typename cell_channel::consumer_tag;

    static constexpr ChannelForm channel_form = Form;
    static constexpr std::size_t channel_capacity = Capacity;
    using row_discipline = typename disciplines::channel;
    using row_payload = value_type;

    // A wire channel starts with no handle.  The caller keeps both tokens.
    PermissionedBoxChannel() noexcept
        requires(Form == ChannelForm::wire)
    = default;

    // The pool of the inner merge channel parks the producer token of the
    // split root, and each producer handle holds one share of it.
    explicit PermissionedBoxChannel(::foundation::permissions::Permission<producer_tag, Brand>&& producer_root) noexcept
        requires(Form == ChannelForm::merge)
        : cells_{std::move(producer_root)} {}

    // Each object that the ring still holds ends here (THE WORDS).  The drop
    // holds no state, as the drain of a merge asks.
    ~PermissionedBoxChannel() {
        typename cell_channel::DrainClaim claim = cells_.claim_drain();
        cells_.drain(claim, [](word_type word) noexcept { static_cast<void>(open_(word)); });
    }

    class[[= ::foundation::linear::linear_when<Form == ChannelForm::wire>]] ProducerHandle {
        typename cell_channel::ProducerHandle cells_;

        explicit ProducerHandle(typename cell_channel::ProducerHandle&& cells) noexcept : cells_{std::move(cells)} {}
        friend class PermissionedBoxChannel;

    public:
        // A push touches the lines of the channel of the words and one cell of
        // a word, and not the object of the Box.
        static constexpr std::size_t per_call_working_set = cell_channel::ProducerHandle::per_call_working_set;
        using row_discipline = typename disciplines::producer;
        using row_payload = value_type;
        using brand_type = Brand;
        // The channel of the cells.  The handles of this channel report it,
        // so a pipeline joins them as it joins the handles of the inner
        // channel.
        using channel_type = cell_channel;
        static constexpr ChannelForm channel_form = Form;
        // The capacity of the channel.  A check of the waits reads it from the
        // type of the handle, as from the type of the channel.
        static constexpr std::size_t channel_capacity = Capacity;

        ProducerHandle(ProducerHandle const&) =
            delete("ProducerHandle owns the producer token or one pool share — copy would duplicate it");
        ProducerHandle& operator=(ProducerHandle const&) =
            delete("ProducerHandle owns the producer token or one pool share — assignment would overwrite it");
        ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&) =
            delete("ProducerHandle binds to ONE channel for life — rebinding would orphan the original token");
        ~ProducerHandle() = default;

        // The release door of the inner handle of a wire.
        [[nodiscard]] ::foundation::permissions::Permission<producer_tag, Brand> release() && noexcept
            requires(Form == ChannelForm::wire)
        {
            return std::move(cells_).release();
        }

        // The close door of the inner handle: the producer ends its stream
        // before the end of its scope.
        void close() && noexcept { std::move(cells_).close(); }

        // Gives the object of the Box to the ring.  The result is empty when
        // the ring took the object, and holds the same Box when the ring was
        // full.
        [[nodiscard, gnu::hot]] ::foundation::core::Option<value_type> try_push(value_type&& box) noexcept {
            word_type const word = seal_(std::move(box));
            if (cells_.try_push(word)) [[likely]] {
                return ::foundation::core::none;
            }
            return ::foundation::core::Option<value_type>::some(open_(word));
        }

        // Gives the object of the Box to the ring, with the wait of
        // push_waiting of the channel of the words while the ring is full.
        // The word of the Box is sealed one time, so a wait moves no Box.
        template <WaitPolicy Policy, class Ctx>
            requires CtxFitsWaitPolicy<Ctx, Policy>
        void push_waiting(Ctx const& ctx, EventCount& room, value_type&& box) noexcept {
            word_type const word = seal_(std::move(box));
            ::fixy::concurrent::push_waiting<Policy>(ctx, cells_, room, word);
        }

        [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return cells_.empty_approx(nondet);
        }
        [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return cells_.size_approx(nondet);
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

        [[nodiscard]] constexpr ::foundation::ChannelIdentity<cell_channel> channel_identity() const noexcept {
            return cells_.channel_identity();
        }
    };

    class[[= ::foundation::linear::linear{}]] ConsumerHandle
        : public detail::box_consumer_pops<Form, value_type, ConsumerHandle> {
        typename cell_channel::ConsumerHandle cells_;

        explicit ConsumerHandle(typename cell_channel::ConsumerHandle&& cells) noexcept : cells_{std::move(cells)} {}
        friend class PermissionedBoxChannel;
        friend class detail::box_consumer_pops<Form, value_type, ConsumerHandle>;

        // The bodies of the pops of detail::box_consumer_pops.  The pack holds
        // the tag of Nondet for a merge and no value for a wire, and each pop
        // gives it to the pop of the channel of the words.
        template <typename... PopTag>
        [[nodiscard, gnu::always_inline]] ::foundation::core::Option<value_type> pop_(PopTag... tag) noexcept {
            ::foundation::core::Option<word_type> const popped = cells_.try_pop(tag...);
            return popped.match(
                [](word_type word) noexcept { return ::foundation::core::Option<value_type>::some(open_(word)); },
                []() noexcept { return ::foundation::core::Option<value_type>{::foundation::core::none}; });
        }

        template <typename... PopTag>
        [[nodiscard, gnu::always_inline]] ::foundation::core::Result<value_type, NoItem>
        receive_(PopTag... tag) noexcept {
            ::foundation::core::Result<word_type, NoItem> const received = cells_.try_receive(tag...);
            return received.match(
                [](word_type word) noexcept { return ::foundation::core::Result<value_type, NoItem>{open_(word)}; },
                [](NoItem why) noexcept {
                    return ::foundation::core::Result<value_type, NoItem>{::foundation::core::err(why)};
                });
        }

    public:
        // A pop touches the same lines, and not the object of the Box.
        static constexpr std::size_t per_call_working_set = cell_channel::ConsumerHandle::per_call_working_set;
        using row_discipline = typename disciplines::consumer;
        using row_payload = value_type;
        using brand_type = Brand;
        using channel_type = cell_channel;
        static constexpr ChannelForm channel_form = Form;
        static constexpr std::size_t channel_capacity = Capacity;

        ConsumerHandle(ConsumerHandle const&) =
            delete("ConsumerHandle owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerHandle& operator=(ConsumerHandle const&) =
            delete("ConsumerHandle owns the Consumer Permission — assignment would overwrite the linear token");
        ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&) =
            delete("ConsumerHandle binds to ONE channel for life — rebinding would orphan the original Permission");
        ~ConsumerHandle() = default;

        // The release door of the inner handle.
        [[nodiscard]] ::foundation::permissions::Permission<consumer_tag, Brand> release() && noexcept {
            return std::move(cells_).release();
        }

        [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return cells_.empty_approx(nondet);
        }
        [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
            return cells_.size_approx(nondet);
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

        // The closed state of the channel of the words, with acquire order.
        [[nodiscard]] bool is_closed() const noexcept { return cells_.is_closed(); }

        // The oldest Box, or NoItem::Closed after the last Box of a closed
        // channel, with the wait of receive_waiting of the channel of the
        // words.
        template <WaitPolicy Policy, class Ctx, typename... PopTag>
            requires CtxFitsWaitPolicy<Ctx, Policy>
        [[nodiscard]] ::foundation::core::Result<value_type, NoItem> receive_waiting(Ctx const& ctx, EventCount& events,
                                                                                     PopTag... tag) noexcept {
            ::foundation::core::Result<word_type, NoItem> const received =
                cells_.template receive_waiting<Policy>(ctx, events, tag...);
            return received.match(
                [](word_type word) noexcept { return ::foundation::core::Result<value_type, NoItem>{open_(word)}; },
                [](NoItem why) noexcept {
                    return ::foundation::core::Result<value_type, NoItem>{::foundation::core::err(why)};
                });
        }

        [[nodiscard]] constexpr ::foundation::ChannelIdentity<cell_channel> channel_identity() const noexcept {
            return cells_.channel_identity();
        }
    };

    // The producer door of a wire, which the inner channel gives: it takes the
    // claim of the producer role.
    [[nodiscard]] ProducerHandle producer(::foundation::permissions::Permission<producer_tag, Brand>&& perm) noexcept
        requires(Form == ChannelForm::wire)
    {
        return ProducerHandle{cells_.producer(std::move(perm))};
    }

    // The producer door of a merge.  It lends a share of the producer pool, or
    // gives no value while an exclusive transition is in flight.  A lent handle
    // opens the channel.
    [[nodiscard]] ::foundation::core::Option<ProducerHandle> producer() noexcept
        requires(Form == ChannelForm::merge)
    {
        auto lent = cells_.producer();
        if (lent.is_none()) return ::foundation::core::none;
        return ::foundation::core::Option<ProducerHandle>::some(
            ProducerHandle{std::move(lent).expect("the test above found a producer")});
    }

    // Takes the claim of the consumer role first, so a second live consumer
    // handle ends the process here.
    [[nodiscard]] ConsumerHandle consumer(::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept {
        return ConsumerHandle{cells_.consumer(std::move(perm))};
    }

    // The two doors of the producer root of a merge, as the inner channel gives
    // them: the root leaves as the lease of the pool of the inner channel, and
    // only that lease comes back.  While the root is out, the pool lends no
    // share, so no Box lands.  The Box channel gives no recombined access,
    // because a body with the ring of the words could pop a word and lose it,
    // and a lost word leaks its object.  The withdraw is a claim door of the
    // inner channel.
    [[nodiscard]] ::foundation::core::Option<typename cell_channel::producer_lease>
    try_withdraw_producer_root() noexcept
        requires(Form == ChannelForm::merge)
    {
        return cells_.try_withdraw_producer_root();
    }

    void deposit_producer_root(typename cell_channel::producer_lease&& root) noexcept
        requires(Form == ChannelForm::merge)
    {
        cells_.deposit_producer_root(std::move(root));
    }

    [[nodiscard]] std::uint64_t outstanding_producers() const noexcept
        requires(Form == ChannelForm::merge)
    {
        return cells_.outstanding_producers();
    }
    [[nodiscard]] bool is_exclusive_active() const noexcept { return cells_.is_exclusive_active(); }
    [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return cells_.empty_approx(nondet);
    }
    [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return cells_.size_approx(nondet);
    }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    // True when the last producer handle ended and no handle opened the
    // channel after it.  A snapshot for a test: a consumer reads the end
    // through try_receive.
    [[nodiscard]] bool is_closed() const noexcept { return cells_.is_closed(); }

private:
    // The two doors of the words of this authority.  Only the members of this
    // class build its key.
    [[nodiscard]] static word_type seal_(value_type&& box) noexcept {
        return std::move(box).release_to_word(::foundation::core::box_word_key<PermissionedBoxChannel>{});
    }

    [[nodiscard]] static value_type open_(word_type word) noexcept {
        return ::foundation::core::claim_box(word, ::foundation::core::box_word_key<PermissionedBoxChannel>{});
    }

    cell_channel cells_;
};

// The Box channel of a whole root of type Root, as channel_t.
template <::foundation::core::DroppableBoxPayload T, std::size_t Capacity, typename Root>
using box_channel_t = PermissionedBoxChannel<detail::channel_root_parts<Root>::form, T, Capacity,
                                             typename detail::channel_root_parts<Root>::user_tag,
                                             typename detail::channel_root_parts<Root>::brand>;

}  // namespace fixy::concurrent
