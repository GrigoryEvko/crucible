#pragma once

// An MPSC channel of Box values, behind fractional producer shares and one
// linear consumer permission, as PermissionedMpscChannel.  A cell of the
// ring of fixy/concurrent/MpscRing.h holds only a trivially copyable value,
// and a Box is no such value.  So the channel holds a PermissionedMpscChannel
// of the words of a Box (THE WORD OF A BOX of foundation/core/Ref.h), and the
// channel class is the authority of those words.  Each handle of this channel
// holds the handle of the same role of the inner channel.
//
// THE OWNERS
//     Each producer handle belongs to its producer thread, and the consumer
//     handle belongs to the consumer thread.  A push gives the object of its
//     Box to a word.  The ring writes the word to its cell and then sets the
//     ready bit of the cell with a release store.  A pop reads the ready bit
//     with an acquire load before it reads the cell.  So the consumer sees
//     each write to the object that its producer made before the push, and
//     the Box of the pop owns the object alone.
//
// THE WORDS
//     A word leaves the channel only as a Box.  A push that finds the ring
//     full gives its Box back in the Option of its result, so no object is
//     lost.  The ring gives each cell to one pop, so each word is claimed one
//     time.  The end of the channel drains the ring and claims each word
//     that it still holds, and the Box of each claim ends its object.  The
//     drain takes the exclusive of the producer pool and the claim of the
//     consumer role, so a live handle ends the process there.  The end of a
//     linear object drops it, so the channel takes only a payload that is
//     not linear (DroppableBoxPayload of foundation/core/Ref.h).
//
// THE END OF THE STREAM
//     The end of the last producer handle ends the last producer handle of
//     the words, so it closes the channel as fixy/concurrent/ChannelEnd.h
//     tells.  try_receive of the consumer then gives each Box that the ring
//     still holds, and NoItem::Closed after the last one.

#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>
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
// (foundation/diag/RowHash.h).
namespace fixy::row_discipline {
struct mpsc_box_channel;
struct mpsc_box_producer;
struct mpsc_box_consumer;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

template <::foundation::core::DroppableBoxPayload T, std::size_t Capacity, typename UserTag,
          ::foundation::brand::IsFreshBrand Brand>
class PermissionedMpscBoxChannel
    : public ::foundation::Pinned<PermissionedMpscBoxChannel<T, Capacity, UserTag, Brand>> {
    using word_type = ::foundation::core::BoxWord<T, PermissionedMpscBoxChannel>;
    using cell_channel = PermissionedMpscChannel<word_type, Capacity, UserTag, Brand>;

public:
    using value_type = ::foundation::core::Box<T>;
    using user_tag = UserTag;
    using brand_type = Brand;
    using whole_tag = typename cell_channel::whole_tag;
    using producer_tag = typename cell_channel::producer_tag;
    using consumer_tag = typename cell_channel::consumer_tag;

    static constexpr std::size_t channel_capacity = Capacity;
    using row_discipline = ::fixy::row_discipline::mpsc_box_channel;
    using row_payload = value_type;

    // The pool of the inner channel parks the producer token of the split
    // root, and each producer handle holds one share of it.
    explicit PermissionedMpscBoxChannel(
        ::foundation::permissions::Permission<producer_tag, Brand>&& producer_root) noexcept
        : cells_{std::move(producer_root)} {}

    // Each object that the ring still holds ends here (THE WORDS).
    ~PermissionedMpscBoxChannel() {
        cells_.drain([](word_type word) noexcept { static_cast<void>(open_(word)); });
    }

    class ProducerHandle {
        typename cell_channel::ProducerHandle cells_;

        explicit ProducerHandle(typename cell_channel::ProducerHandle&& cells) noexcept : cells_{std::move(cells)} {}
        friend class PermissionedMpscBoxChannel;

    public:
        // A push touches the lines of the channel of the words and one cell
        // of a word, and not the object of the Box.
        static constexpr std::size_t per_call_working_set = cell_channel::ProducerHandle::per_call_working_set;
        using row_discipline = ::fixy::row_discipline::mpsc_box_producer;
        using row_payload = value_type;
        using brand_type = Brand;
        // The channel of the cells.  The handles of this channel report it,
        // so a pipeline joins them as it joins the handles of the inner
        // channel.
        using channel_type = cell_channel;

        ProducerHandle(ProducerHandle const&) =
            delete("ProducerHandle owns a Pool refcount share — copy would double-count");
        ProducerHandle& operator=(ProducerHandle const&) =
            delete("ProducerHandle owns a Pool refcount share — assignment would double-count");
        ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&) =
            delete("ProducerHandle binds to ONE channel for life — rebinding would orphan the original share");
        ~ProducerHandle() = default;

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

    class[[= ::foundation::linear::linear{}]] ConsumerHandle {
        typename cell_channel::ConsumerHandle cells_;

        explicit ConsumerHandle(typename cell_channel::ConsumerHandle&& cells) noexcept : cells_{std::move(cells)} {}
        friend class PermissionedMpscBoxChannel;

    public:
        // A pop touches the same lines, and not the object of the Box.
        static constexpr std::size_t per_call_working_set = cell_channel::ConsumerHandle::per_call_working_set;
        using row_discipline = ::fixy::row_discipline::mpsc_box_consumer;
        using row_payload = value_type;
        using brand_type = Brand;
        using channel_type = cell_channel;

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

        // A pop is a merge, as the pop of the channel of the words is, so
        // each pop takes the bare tag of Nondet and gives it to that pop.

        // The Box of the oldest object, or no value when the ring is empty.
        // It does not tell an empty ring from a closed channel: try_receive
        // does.
        [[nodiscard, gnu::hot]] ::foundation::core::Option<value_type>
        try_pop(::foundation::effects::cap::Nondet nondet) noexcept {
            ::foundation::core::Option<word_type> const popped = cells_.try_pop(nondet);
            return popped.match(
                [](word_type word) noexcept { return ::foundation::core::Option<value_type>::some(open_(word)); },
                []() noexcept { return ::foundation::core::Option<value_type>{::foundation::core::none}; });
        }

        // The Box of the oldest object, NoItem::Empty, or NoItem::Closed
        // after the last object of a closed channel (THE END OF THE STREAM of
        // fixy/concurrent/ChannelEnd.h).
        [[nodiscard, gnu::hot]] ::foundation::core::Result<value_type, NoItem>
        try_receive(::foundation::effects::cap::Nondet nondet) noexcept {
            ::foundation::core::Result<word_type, NoItem> const received = cells_.try_receive(nondet);
            return received.match(
                [](word_type word) noexcept { return ::foundation::core::Result<value_type, NoItem>{open_(word)}; },
                [](NoItem why) noexcept {
                    return ::foundation::core::Result<value_type, NoItem>{::foundation::core::err(why)};
                });
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

    // Lends a share of the producer pool, or no value while an exclusive
    // transition is in flight.  A lent handle opens the channel.
    [[nodiscard]] ::foundation::core::Option<ProducerHandle> producer() noexcept {
        auto lent = cells_.producer();
        if (!lent) return ::foundation::core::none;
        return ::foundation::core::Option<ProducerHandle>::some(ProducerHandle{std::move(*lent)});
    }

    // Takes the claim of the consumer role first, so a second live consumer
    // handle ends the process here.
    [[nodiscard]] ConsumerHandle consumer(::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept {
        return ConsumerHandle{cells_.consumer(std::move(perm))};
    }

    // Runs the body with every producer out, as the inner channel does.
    // Returns false when producers were still out and the body did not run.
    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_drained_access(Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        return cells_.with_drained_access(std::forward<Body>(body));
    }

    [[nodiscard]] std::uint64_t outstanding_producers() const noexcept { return cells_.outstanding_producers(); }
    [[nodiscard]] bool is_exclusive_active() const noexcept { return cells_.is_exclusive_active(); }
    [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return cells_.empty_approx(nondet);
    }
    [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return cells_.size_approx(nondet);
    }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    // True when the last producer handle ended and no handle was lent after
    // it.  A snapshot for a test: a consumer reads the end through
    // try_receive.
    [[nodiscard]] bool is_closed() const noexcept { return cells_.is_closed(); }

private:
    // The two doors of the words of this authority.  Only the members of
    // this class build its key.
    [[nodiscard]] static word_type seal_(value_type&& box) noexcept {
        return std::move(box).release_to_word(::foundation::core::box_word_key<PermissionedMpscBoxChannel>{});
    }

    [[nodiscard]] static value_type open_(word_type word) noexcept {
        return ::foundation::core::claim_box(word, ::foundation::core::box_word_key<PermissionedMpscBoxChannel>{});
    }

    cell_channel cells_;
};

// The Box channel of a whole root of type Root, as mpsc_channel_t.
template <::foundation::core::DroppableBoxPayload T, std::size_t Capacity, typename Root>
using mpsc_box_channel_t = PermissionedMpscBoxChannel<T, Capacity, typename detail::mpsc_root_parts<Root>::user_tag,
                                                      typename detail::mpsc_root_parts<Root>::brand>;

}  // namespace fixy::concurrent
