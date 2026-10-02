#pragma once

// The MetaLog behind one linear Permission per endpoint.  The handle types
// carry the role, so appending from the consumer side or draining from the
// producer side does not compile, and the move-only Permission keeps the
// log to one producer and one consumer at a time.
//
// A log has the brand of the root that its permissions grow from.  The
// caller mints the whole root, names its brand in the log type, and
// splits the root into the producer token and the consumer token.  A root
// of another call site has another brand, so its tokens open no endpoint
// of this log.  One call site that runs two times mints two roots of one
// brand, and no type tells them apart.  A claim on each role
// (foundation/ChannelBinding.h) refuses a second live handle of one role
// there.
//
// The endpoint accessors are members named producer and consumer, not
// mint_ free functions.  Their admission is the parameter type and the
// claim: a caller that cannot produce a Permission<producer_tag, Brand>
// cannot call producer().  So there is no constraint for a negative
// fixture to fire, and a mint_ name would claim a gate that they do not
// have.

#include <crucible/MetaLog.h>

#include <fixy/Aliases.h>
#include <fixy/session/NetworkModel.h>
#include <foundation/Brand.h>
#include <foundation/ChannelBinding.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible {

// The log is memory that the process already owns, so an endpoint that
// touches it incurs no effect: the three tags declare the empty row.
namespace metalog_tag {

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

}  // namespace metalog_tag

// UserTag and Brand have no default.  A default tag would give every log
// that omits it one set of Permission types.  The erased brand would let a
// token of every root site open the endpoints.
template <typename UserTag, ::foundation::brand::IsFreshBrand Brand>
class PermissionedMetaLog {
public:
    using value_type = ::crucible::TensorMeta;
    using user_tag = UserTag;
    using brand_type = Brand;
    using whole_tag = metalog_tag::Whole<UserTag>;
    using producer_tag = metalog_tag::Producer<UserTag>;
    using consumer_tag = metalog_tag::Consumer<UserTag>;

    explicit constexpr PermissionedMetaLog(::crucible::MetaLog& log) noexcept : log_{log} {}

    PermissionedMetaLog(const PermissionedMetaLog&) =
        delete("the wrapper names one MetaLog, and the endpoints it made hold that log");
    PermissionedMetaLog& operator=(const PermissionedMetaLog&) =
        delete("the wrapper names one MetaLog, and the endpoints it made hold that log");
    PermissionedMetaLog(PermissionedMetaLog&&) =
        delete("the wrapper names one MetaLog, and the endpoints it made hold that log");
    PermissionedMetaLog&
    operator=(PermissionedMetaLog&&) = delete("the wrapper names one MetaLog, and the endpoints it made hold that log");

    class ProducerHandle {
        // The move clears the binding, so a moved-from handle cannot
        // append or drain through the Permission it no longer holds.  The
        // binding names the wrapper, which holds the log and the claims.
        ::foundation::ChannelBinding<PermissionedMetaLog> ch_;
        [[no_unique_address]] ::foundation::permissions::Permission<producer_tag, Brand> perm_;

        constexpr ProducerHandle(PermissionedMetaLog& wrapper,
                                 ::foundation::permissions::Permission<producer_tag, Brand>&& perm) noexcept
            : ch_{wrapper}, perm_{std::move(perm)} {}
        friend class PermissionedMetaLog;

    public:
        using value_type = ::crucible::TensorMeta;
        using tag_type = producer_tag;
        using brand_type = Brand;

        // The producer picks alone when it appends and when it stops, and no
        // peer reads a label, so a session over the handle states the Local
        // network (fixy/session/NetworkModel.h).
        static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::Local;

        ProducerHandle(const ProducerHandle&) =
            delete("MetaLog ProducerHandle owns the Producer Permission — copy would duplicate the linear token");
        ProducerHandle& operator=(const ProducerHandle&) =
            delete("MetaLog ProducerHandle owns the Producer Permission — assignment would overwrite the linear token");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&) = delete(
            "MetaLog ProducerHandle binds to one MetaLog for life — rebinding would orphan the original Permission");

        // A moved-from handle holds no claim, so only the bound one gives
        // the role back.
        ~ProducerHandle() {
            if (ch_.is_bound()) ch_->producer_claim_.release();
        }

        [[nodiscard, gnu::hot]] ::crucible::MetaIndex try_append(const value_type* metas, std::uint32_t count) {
            return ch_->log_.try_append(metas, count);
        }

        [[nodiscard, gnu::hot]] bool try_append_one(const value_type& meta) {
            return ch_->log_.try_append(&meta, 1).is_valid();
        }

        // The row gate is this constraint.  The body calls try_append, so the
        // gate does not depend on the row spelling that MetaLog itself reads.
        template <typename CallerRow = ::foundation::effects::Row<>>
            requires ::fixy::IsPure<CallerRow>
        [[nodiscard, gnu::hot]] ::crucible::MetaIndex try_append_pure(const value_type* metas, std::uint32_t count) {
            return ch_->log_.try_append(metas, count);
        }

        [[nodiscard]] std::uint32_t size_approx() const { return ch_->log_.size().peek(); }
    };

    // The tail index is read relaxed.  The consumer permission is linear, so
    // the thread holding it is the only writer of that index and no ordering
    // is needed to observe the latest value.  The head index is read with get,
    // which acquires against the producer's release and is what makes the
    // appended records visible.
    class ConsumerHandle {
        ::foundation::ChannelBinding<PermissionedMetaLog> ch_;
        [[no_unique_address]] ::foundation::permissions::Permission<consumer_tag, Brand> perm_;

        constexpr ConsumerHandle(PermissionedMetaLog& wrapper,
                                 ::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept
            : ch_{wrapper}, perm_{std::move(perm)} {}
        friend class PermissionedMetaLog;

    public:
        using value_type = ::crucible::TensorMeta;
        using tag_type = consumer_tag;
        using brand_type = Brand;

        // The consumer picks alone when it drains and when it stops, so a
        // session over the handle states the Local network.
        static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::Local;

        ConsumerHandle(const ConsumerHandle&) =
            delete("MetaLog ConsumerHandle owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerHandle& operator=(const ConsumerHandle&) =
            delete("MetaLog ConsumerHandle owns the Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&) = delete(
            "MetaLog ConsumerHandle binds to one MetaLog for life — rebinding would orphan the original Permission");

        ~ConsumerHandle() {
            if (ch_.is_bound()) ch_->consumer_claim_.release();
        }

        [[nodiscard, gnu::hot]] std::optional<value_type> try_drain_one() {
            ::crucible::MetaLog& log = ch_->log_;
            const std::uint32_t t = log.tail.peek_relaxed();
            if (t == log.head.get()) [[unlikely]] {
                return std::nullopt;
            }

            value_type meta = log.at(t);
            log.advance_tail(t + 1);
            return meta;
        }

        template <typename Body>
            requires std::is_invocable_v<Body&, const value_type&>
        [[nodiscard]] std::uint32_t drain(Body&& body, std::uint32_t max_items = ::crucible::MetaLog::CAPACITY) {
            ::crucible::MetaLog& log = ch_->log_;
            const std::uint32_t t = log.tail.peek_relaxed();
            const std::uint32_t available = log.head.get() - t;
            const std::uint32_t count = max_items < available ? max_items : available;

            for (std::uint32_t i = 0; i < count; ++i) {
                std::invoke(body, log.at(t + i));
            }
            if (count != 0) {
                log.advance_tail(t + count);
            }
            return count;
        }

        [[nodiscard]] const value_type& at(::crucible::MetaIndex index) const CRUCIBLE_LIFETIMEBOUND {
            return ch_->log_.at(index);
        }

        void advance_tail(std::uint32_t new_tail) { ch_->log_.advance_tail(new_tail); }

        [[nodiscard]] std::uint32_t head_index() const { return ch_->log_.head.get(); }

        [[nodiscard]] std::uint32_t tail_index() const { return ch_->log_.tail.get(); }

        [[nodiscard]] std::uint32_t size_approx() const { return ch_->log_.size().peek(); }
    };

    // Each accessor takes the claim of its role first, so a second live
    // handle of the role ends the process here.
    [[nodiscard]] ProducerHandle producer(::foundation::permissions::Permission<producer_tag, Brand>&& perm) noexcept {
        producer_claim_.take("two live producer handles of one MetaLog. One call site minted the root of the log "
                             "two times.");
        return ProducerHandle{*this, std::move(perm)};
    }

    [[nodiscard]] ConsumerHandle consumer(::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept {
        consumer_claim_.take("two live consumer handles of one MetaLog. One call site minted the root of the log "
                             "two times.");
        return ConsumerHandle{*this, std::move(perm)};
    }

    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept { return false; }

private:
    ::crucible::MetaLog& log_;
    ::foundation::EndpointClaim producer_claim_;
    ::foundation::EndpointClaim consumer_claim_;
};

namespace detail {

template <typename Root>
struct metalog_root_parts;

template <typename UserTag, typename Brand>
struct metalog_root_parts<::foundation::permissions::Permission<metalog_tag::Whole<UserTag>, Brand>> {
    using user_tag = UserTag;
    using brand = Brand;
};

}  // namespace detail

// The log of a whole root of type Root.  The alias reads the user tag and
// the brand off the root.
template <typename Root>
using permissioned_metalog_t = PermissionedMetaLog<typename detail::metalog_root_parts<Root>::user_tag,
                                                   typename detail::metalog_root_parts<Root>::brand>;

}  // namespace crucible

// Both the binary and the variadic split forms are specialized, so a caller
// can reach for either one.  The authoring witnesses are deliberately
// redundant: a forged can_split_into alone is caught by the missing witness
// beside it.

namespace foundation::permissions {

template <typename UserTag>
struct can_split_into<::crucible::metalog_tag::Whole<UserTag>, ::crucible::metalog_tag::Producer<UserTag>,
                      ::crucible::metalog_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct can_split_into_pack<::crucible::metalog_tag::Whole<UserTag>, ::crucible::metalog_tag::Producer<UserTag>,
                           ::crucible::metalog_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct has_split_authoring_witness<::crucible::metalog_tag::Whole<UserTag>, ::crucible::metalog_tag::Producer<UserTag>,
                                   ::crucible::metalog_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct has_split_pack_authoring_witness<::crucible::metalog_tag::Whole<UserTag>,
                                        ::crucible::metalog_tag::Producer<UserTag>,
                                        ::crucible::metalog_tag::Consumer<UserTag>> : std::true_type {};

}  // namespace foundation::permissions
