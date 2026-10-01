#pragma once

// An SPSC ring behind one linear Permission per endpoint.  The handle
// types carry the role, so pushing from the consumer side or popping
// from the producer side does not compile, and the move-only Permission
// keeps a channel to one producer and one consumer at a time.
//
// A channel has the brand of the root that its permissions grow from.
// The caller mints the whole root, names its brand in the channel type,
// and splits the root into the producer token and the consumer token.  A
// root of another call site has another brand, so its tokens open no
// endpoint of this channel, and two channels of two root sites are two
// types, which a pipeline does not join.  spsc_channel_t names the channel
// of a root type.
//
// One call site that runs two times mints two roots of one brand, and no
// type tells them apart.  The run-time floor there is a claim on each
// role (foundation/ChannelBinding.h): a second live handle of one role
// ends the process, and a handle takes the role again after the last one
// ended.
//
// The endpoint accessors are members named producer and consumer rather
// than mint_ free functions.  Their admission is the parameter type and
// the claim.  A caller that cannot produce a Permission<producer_tag,
// Brand> cannot call producer(), so there is no requires-clause for a
// negative fixture to fire, and a mint_ name would claim a gate that they
// do not have.

#include <fixy/concurrent/SpscRing.h>
#include <fixy/concurrent/WorkingSet.h>

#include <foundation/Brand.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct spsc_channel;
struct spsc_producer;
struct spsc_consumer;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

// The triple is specialized for splitting at the foot of this file, so
// a user tag takes no per-tag boilerplate.

// A ring is memory the process already owns, so touching it through
// either endpoint incurs no effect: the three tags declare the empty
// row.  They are templates, so the row is a member rather than an edge.

namespace spsc_tag {

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

}  // namespace spsc_tag

// UserTag and Brand have no default.  A default tag would give every
// channel that omits it one set of Permission types.  The erased brand
// would let a token of every root site open the endpoints.
template <RingValue T, std::size_t Capacity, typename UserTag, ::foundation::brand::IsFreshBrand Brand>
class PermissionedSpscChannel : public ::foundation::Pinned<PermissionedSpscChannel<T, Capacity, UserTag, Brand>> {
public:
    using value_type = T;
    using user_tag = UserTag;
    using brand_type = Brand;
    using whole_tag = spsc_tag::Whole<UserTag>;
    using producer_tag = spsc_tag::Producer<UserTag>;
    using consumer_tag = spsc_tag::Consumer<UserTag>;

    static constexpr std::size_t channel_capacity = Capacity;
    using row_discipline = ::fixy::row_discipline::spsc_channel;
    using row_payload = T;

    // The channel's identity is its address, since the ring's atomics
    // depend on a stable one.

    PermissionedSpscChannel() noexcept = default;

    class ProducerHandle {
        // The move clears the binding, so a moved-from handle cannot
        // push: a defaulted move of an empty Permission is a no-op, and
        // a reference or a plain pointer would let the source and the
        // target both go on using the one linear token.
        ::foundation::ChannelBinding<PermissionedSpscChannel> ch_;
        [[no_unique_address]] ::foundation::permissions::Permission<producer_tag, Brand> perm_;

        constexpr ProducerHandle(PermissionedSpscChannel& channel,
                                 ::foundation::permissions::Permission<producer_tag, Brand>&& perm) noexcept
            : ch_{channel}, perm_{std::move(perm)} {}
        friend class PermissionedSpscChannel;

    public:
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<2, T>;
        using row_discipline = ::fixy::row_discipline::spsc_producer;
        using row_payload = T;
        using brand_type = Brand;
        // The channel this handle acts on.  A pipeline joins two stages
        // only when the producer of one and the consumer of the next name
        // the same channel, and at the mint, the same channel instance.
        using channel_type = PermissionedSpscChannel;

        ProducerHandle(const ProducerHandle&) =
            delete("ProducerHandle owns the Producer Permission — copy would duplicate the linear token");
        ProducerHandle& operator=(const ProducerHandle&) =
            delete("ProducerHandle owns the Producer Permission — assignment would overwrite the linear token");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&) = delete(
            "ProducerHandle binds to ONE channel for life — rebinding would orphan the original Permission "
            "and silently allow a second producer to coexist");

        // A moved-from handle holds no claim, so only the bound one gives
        // the role back.
        ~ProducerHandle() {
            if (ch_.is_bound()) ch_->producer_claim_.release();
        }

        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept { return ch_->ring_.try_push(item); }

        // Snapshots.  Sound for telemetry and for deciding whether to
        // keep retrying, never for a correctness invariant.
        [[nodiscard]] bool empty_approx() const noexcept { return ch_->ring_.empty_approx(); }
        [[nodiscard]] std::size_t size_approx() const noexcept { return ch_->ring_.size_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

        [[nodiscard]] constexpr ::foundation::ChannelIdentity<PermissionedSpscChannel>
        channel_identity() const noexcept {
            return ch_.identity();
        }
    };

    class ConsumerHandle {
        // A binding that the move clears, for the same reason as in
        // ProducerHandle.
        ::foundation::ChannelBinding<PermissionedSpscChannel> ch_;
        [[no_unique_address]] ::foundation::permissions::Permission<consumer_tag, Brand> perm_;

        constexpr ConsumerHandle(PermissionedSpscChannel& channel,
                                 ::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept
            : ch_{channel}, perm_{std::move(perm)} {}
        friend class PermissionedSpscChannel;

    public:
        static constexpr std::size_t per_call_working_set = lines_plus_cell_working_set_v<2, T>;
        using row_discipline = ::fixy::row_discipline::spsc_consumer;
        using row_payload = T;
        using brand_type = Brand;
        using channel_type = PermissionedSpscChannel;

        ConsumerHandle(const ConsumerHandle&) =
            delete("ConsumerHandle owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerHandle& operator=(const ConsumerHandle&) =
            delete("ConsumerHandle owns the Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&) = delete(
            "ConsumerHandle binds to ONE channel for life — rebinding would orphan the original Permission "
            "and silently allow a second consumer to coexist");

        ~ConsumerHandle() {
            if (ch_.is_bound()) ch_->consumer_claim_.release();
        }

        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept { return ch_->ring_.try_pop(); }

        [[nodiscard]] bool empty_approx() const noexcept { return ch_->ring_.empty_approx(); }
        [[nodiscard]] std::size_t size_approx() const noexcept { return ch_->ring_.size_approx(); }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

        [[nodiscard]] constexpr ::foundation::ChannelIdentity<PermissionedSpscChannel>
        channel_identity() const noexcept {
            return ch_.identity();
        }
    };

    // Each accessor takes the claim of its role first, so a second live
    // handle of the role ends the process here.
    [[nodiscard]] ProducerHandle producer(::foundation::permissions::Permission<producer_tag, Brand>&& perm) noexcept {
        producer_claim_.take("two live producer handles of one SPSC channel. One call site minted the root of the "
                             "channel two times.");
        return ProducerHandle{*this, std::move(perm)};
    }

    [[nodiscard]] ConsumerHandle consumer(::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept {
        consumer_claim_.take("two live consumer handles of one SPSC channel. One call site minted the root of the "
                             "channel two times.");
        return ConsumerHandle{*this, std::move(perm)};
    }

    // Scoped exclusive access to the ring.  The whole permission of this
    // brand is the type-level proof that no handle of its root is alive.
    // A second root of the same call site has the same brand, so the body
    // also holds the two claims: a live handle ends the process here, and no
    // handle can start while the body runs.  The body gets the ring: it
    // can fill it before the first session or drain what the last one
    // left.  The whole permission comes back so the caller can split it
    // again for the next session.
    template <typename Body>
        requires std::is_invocable_v<Body, SpscRing<T, Capacity>&>
    [[nodiscard]] ::foundation::permissions::Permission<whole_tag, Brand>
    with_recombined_access(::foundation::permissions::Permission<whole_tag, Brand>&& whole,
                           Body&& body) noexcept(std::is_nothrow_invocable_v<Body, SpscRing<T, Capacity>&>) {
        producer_claim_.take("recombined access to an SPSC channel while its producer handle lives.");
        consumer_claim_.take("recombined access to an SPSC channel while its consumer handle lives.");
        std::forward<Body>(body)(ring_);
        consumer_claim_.release();
        producer_claim_.release();
        return std::move(whole);
    }

    [[nodiscard]] bool empty_approx() const noexcept { return ring_.empty_approx(); }
    [[nodiscard]] std::size_t size_approx() const noexcept { return ring_.size_approx(); }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    // Always false, and present only so this channel matches the shape
    // of the pool-backed ones.  There is no exclusivity flag to read:
    // the linear permissions and the claims are what prove single
    // ownership.
    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept { return false; }

private:
    SpscRing<T, Capacity> ring_;
    ::foundation::EndpointClaim producer_claim_;
    ::foundation::EndpointClaim consumer_claim_;
};

namespace detail {

template <typename Root>
struct spsc_root_parts;

template <typename UserTag, typename Brand>
struct spsc_root_parts<::foundation::permissions::Permission<spsc_tag::Whole<UserTag>, Brand>> {
    using user_tag = UserTag;
    using brand = Brand;
};

}  // namespace detail

// The channel of a whole root of type Root.  The alias reads the user tag
// and the brand off the root.
template <RingValue T, std::size_t Capacity, typename Root>
using spsc_channel_t = PermissionedSpscChannel<T, Capacity, typename detail::spsc_root_parts<Root>::user_tag,
                                               typename detail::spsc_root_parts<Root>::brand>;

// The tag and the brand the witness roster names for this channel.  The
// check file of this header names them too, and no code builds a channel
// of them.
namespace detail::spsc_channel_witness {
struct WitnessTag {};
struct WitnessBrand {};
}  // namespace detail::spsc_channel_witness

}  // namespace fixy::concurrent

// Both the binary and the variadic split forms are specialized, so a
// caller can reach for either one.  The authoring witnesses are
// deliberately redundant: a forged can_split_into alone is caught by the
// missing witness beside it.

namespace foundation::permissions {

template <typename UserTag>
struct can_split_into<::fixy::concurrent::spsc_tag::Whole<UserTag>, ::fixy::concurrent::spsc_tag::Producer<UserTag>,
                      ::fixy::concurrent::spsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct can_split_into_pack<::fixy::concurrent::spsc_tag::Whole<UserTag>,
                           ::fixy::concurrent::spsc_tag::Producer<UserTag>,
                           ::fixy::concurrent::spsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct has_split_authoring_witness<::fixy::concurrent::spsc_tag::Whole<UserTag>,
                                   ::fixy::concurrent::spsc_tag::Producer<UserTag>,
                                   ::fixy::concurrent::spsc_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct has_split_pack_authoring_witness<::fixy::concurrent::spsc_tag::Whole<UserTag>,
                                        ::fixy::concurrent::spsc_tag::Producer<UserTag>,
                                        ::fixy::concurrent::spsc_tag::Consumer<UserTag>> : std::true_type {};

}  // namespace foundation::permissions
