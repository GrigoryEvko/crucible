#pragma once

// An MPSC ring behind fractional producer shares and one linear
// consumer permission.  The handle types carry the role, so pushing
// from the consumer side or popping from a producer side does not
// compile.
//
// The producer side is fractional because the ring is structurally
// many-producer and its membership is dynamic.  A linear producer token
// would force either a handoff between producer threads, which defeats
// concurrency, or a split into a fixed number of slots, which defeats
// dynamic membership.  The pool's share count doubles as telemetry, and
// draining it is what gives exclusive access.
//
// The consumer side stays linear because the ring's pop path assumes a
// sole consumer: the tail is consumer-owned and carries no
// synchronization, so two consumers would race on the cell reads.  The
// linear permission turns that race into a compile error.
//
// A channel has the brand of the root that its permissions grow from.
// The caller mints the whole root, names its brand in the channel type,
// and splits the root into the producer token and the consumer token.
// The pool of the channel parks the producer token, and the caller keeps
// the consumer token.  A root of another call site has another brand, so
// its tokens open no endpoint of this channel.  One call site that runs
// two times mints two roots of one brand.  A claim on the consumer role
// (foundation/ChannelBinding.h) refuses a second live consumer there.

#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/WorkingSet.h>

#include <foundation/Brand.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct mpsc_channel;
struct mpsc_producer;
struct mpsc_consumer;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

// The triple is specialized for splitting at the foot of this file, so
// a user tag takes no per-tag boilerplate.

// A ring is memory the process already owns, so touching it through
// either endpoint incurs no effect: the three tags declare the empty
// row.  They are templates, so the row is a member rather than an edge.

namespace mpsc_tag {

template <typename UserTag>
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
template <typename UserTag>
struct Producer {
    using permission_row = ::foundation::effects::Row<>;
};
template <typename UserTag>
struct Consumer {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace mpsc_tag

// UserTag and Brand have no default, for the reason fixy/concurrent/
// PermissionedSpscChannel.h gives.
template <RingValue T, std::size_t Capacity, typename UserTag, ::foundation::brand::IsFreshBrand Brand>
class PermissionedMpscChannel : public ::foundation::Pinned<PermissionedMpscChannel<T, Capacity, UserTag, Brand>> {
public:
    using value_type = T;
    using user_tag = UserTag;
    using brand_type = Brand;
    using whole_tag = mpsc_tag::Whole<UserTag>;
    using producer_tag = mpsc_tag::Producer<UserTag>;
    using consumer_tag = mpsc_tag::Consumer<UserTag>;

    static constexpr std::size_t channel_capacity = Capacity;
    using row_discipline = ::fixy::row_discipline::mpsc_channel;
    using row_payload = T;

    // The pool parks the producer token of the split root, and each
    // producer handle holds one share of it.
    explicit PermissionedMpscChannel(
        ::foundation::permissions::Permission<producer_tag, Brand>&& producer_root) noexcept
        : producer_pool_{std::move(producer_root)} {}

    // Holds a pool share for its whole lifetime and gives it back on
    // destruction.

    class ProducerHandle {
        // The move clears the binding.  A moved-from producer that kept
        // its channel would push with no pool share, and so would push
        // during the drained window that assumes every producer out.
        ::foundation::ChannelBinding<PermissionedMpscChannel> ch_;
        ::foundation::permissions::SharedPermissionGuard<producer_tag, Brand> guard_;

        constexpr ProducerHandle(PermissionedMpscChannel& channel,
                                 ::foundation::permissions::SharedPermissionGuard<producer_tag, Brand>&& guard) noexcept
            : ch_{channel}, guard_{std::move(guard)} {}
        friend class PermissionedMpscChannel;

    public:
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<3, T>;
        using row_discipline = ::fixy::row_discipline::mpsc_producer;
        using row_payload = T;
        using brand_type = Brand;
        // The channel this handle acts on.  A pipeline joins two stages
        // only when the producer of one and the consumer of the next name
        // the same channel, and at the mint, the same channel instance.
        using channel_type = PermissionedMpscChannel;

        ProducerHandle(const ProducerHandle&) =
            delete("ProducerHandle owns a Pool refcount share — copy would double-count");
        ProducerHandle& operator=(const ProducerHandle&) =
            delete("ProducerHandle owns a Pool refcount share — assignment would double-count");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        // Move assignment stays deleted, because the share's lifetime
        // is fixed at construction.

        // The value comes by reference to const, which is the shape of a
        // producer pole in fixy/concurrent/HandleTraits.h.  A value
        // parameter hides the pole, and no stage can then feed the channel.
        [[nodiscard, gnu::hot]] bool try_push(T const& item) noexcept { return ch_->ring_.try_push(item); }

        // Snapshots.  Sound for telemetry and for deciding whether to
        // keep retrying, never for a correctness invariant.
        [[nodiscard]] bool empty_approx() const noexcept { return ch_->ring_.empty_approx(); }
        [[nodiscard]] std::size_t size_approx() const noexcept { return ch_->ring_.size_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

        [[nodiscard]] constexpr ::foundation::ChannelIdentity<PermissionedMpscChannel>
        channel_identity() const noexcept {
            return ch_.identity();
        }
    };

    class ConsumerHandle {
        // The move clears the binding, so a moved-from handle cannot
        // pop: a defaulted move of an empty Permission is a no-op, and
        // a reference or a plain pointer would let the source and the
        // target both go on using the one linear token.
        ::foundation::ChannelBinding<PermissionedMpscChannel> ch_;
        [[no_unique_address]] ::foundation::permissions::Permission<consumer_tag, Brand> perm_;

        constexpr ConsumerHandle(PermissionedMpscChannel& channel,
                                 ::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept
            : ch_{channel}, perm_{std::move(perm)} {}
        friend class PermissionedMpscChannel;

    public:
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<3, T>;
        using row_discipline = ::fixy::row_discipline::mpsc_consumer;
        using row_payload = T;
        using brand_type = Brand;
        using channel_type = PermissionedMpscChannel;

        ConsumerHandle(const ConsumerHandle&) =
            delete("ConsumerHandle owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerHandle& operator=(const ConsumerHandle&) =
            delete("ConsumerHandle owns the Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&) = delete(
            "ConsumerHandle binds to ONE channel for life — rebinding would orphan the original Permission "
            "and silently allow a second consumer to coexist (MpscRing's try_pop is single-consumer-only)");

        // A moved-from handle holds no claim, so only the bound one gives
        // the role back.
        ~ConsumerHandle() {
            if (ch_.is_bound()) ch_->consumer_claim_.release();
        }

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return ch_->ring_.try_pop(); }

        [[nodiscard]] bool empty_approx() const noexcept { return ch_->ring_.empty_approx(); }
        [[nodiscard]] std::size_t size_approx() const noexcept { return ch_->ring_.size_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

        [[nodiscard]] constexpr ::foundation::ChannelIdentity<PermissionedMpscChannel>
        channel_identity() const noexcept {
            return ch_.identity();
        }
    };

    // Lends a pool share, and refuses while an exclusive transition is
    // in flight.  Several producer handles coexisting is the point of
    // the fractional side.
    [[nodiscard]] std::optional<ProducerHandle> producer() noexcept {
        auto guard = producer_pool_.lend();
        if (!guard) return std::nullopt;
        return ProducerHandle{*this, std::move(*guard)};
    }

    // Takes the claim of the consumer role first, so a second live
    // consumer handle ends the process here.
    [[nodiscard]] ConsumerHandle consumer(::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept {
        consumer_claim_.take("two live consumer handles of one MPSC channel. One call site minted the root of the "
                             "channel two times.");
        return ConsumerHandle{*this, std::move(perm)};
    }

    // Runs the body with every producer out.  Returns false when producers
    // were still out and the body did not run.  A producer can be lent
    // again once the body returns.  The body gets no ring, because the
    // consumer handle is still alive and pops from the same cells: the
    // body acts through the handles it holds.  For example, the consumer
    // drains the ring to empty, and no push lands at the same time.
    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_drained_access(Body&& body) noexcept(std::is_nothrow_invocable_v<Body>) {
        auto upgrade = producer_pool_.try_upgrade();
        if (!upgrade) return false;
        std::forward<Body>(body)();
        producer_pool_.deposit_exclusive(std::move(*upgrade));
        return true;
    }

    [[nodiscard]] std::uint64_t outstanding_producers() const noexcept { return producer_pool_.outstanding(); }

    [[nodiscard]] bool is_exclusive_active() const noexcept { return producer_pool_.is_exclusive_out(); }

    [[nodiscard]] bool empty_approx() const noexcept { return ring_.empty_approx(); }
    [[nodiscard]] std::size_t size_approx() const noexcept { return ring_.size_approx(); }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    MpscRing<T, Capacity> ring_;
    ::foundation::permissions::SharedPermissionPool<producer_tag, Brand> producer_pool_;
    ::foundation::EndpointClaim consumer_claim_;
};

namespace detail {

template <typename Root>
struct mpsc_root_parts;

template <typename UserTag, typename Brand>
struct mpsc_root_parts<::foundation::permissions::Permission<mpsc_tag::Whole<UserTag>, Brand>> {
    using user_tag = UserTag;
    using brand = Brand;
};

}  // namespace detail

// The channel of a whole root of type Root.  The alias reads the user tag
// and the brand off the root.
template <RingValue T, std::size_t Capacity, typename Root>
using mpsc_channel_t = PermissionedMpscChannel<T, Capacity, typename detail::mpsc_root_parts<Root>::user_tag,
                                               typename detail::mpsc_root_parts<Root>::brand>;

// The tag and the brand the witness roster names for this channel.  The
// check file of this header names them too, and no code builds a channel
// of them.
namespace detail::mpsc_channel_witness {
struct WitnessTag {};
struct WitnessBrand {};
}  // namespace detail::mpsc_channel_witness

}  // namespace fixy::concurrent

// Both the binary and the variadic split forms are specialized, so a
// caller can reach for either one.

namespace foundation::permissions {

template <typename UserTag>
struct can_split_into<::fixy::concurrent::mpsc_tag::Whole<UserTag>, ::fixy::concurrent::mpsc_tag::Producer<UserTag>,
                      ::fixy::concurrent::mpsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct can_split_into_pack<::fixy::concurrent::mpsc_tag::Whole<UserTag>,
                           ::fixy::concurrent::mpsc_tag::Producer<UserTag>,
                           ::fixy::concurrent::mpsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct has_split_authoring_witness<::fixy::concurrent::mpsc_tag::Whole<UserTag>,
                                   ::fixy::concurrent::mpsc_tag::Producer<UserTag>,
                                   ::fixy::concurrent::mpsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct has_split_pack_authoring_witness<::fixy::concurrent::mpsc_tag::Whole<UserTag>,
                                        ::fixy::concurrent::mpsc_tag::Producer<UserTag>,
                                        ::fixy::concurrent::mpsc_tag::Consumer<UserTag>> : std::true_type {};

}  // namespace foundation::permissions
