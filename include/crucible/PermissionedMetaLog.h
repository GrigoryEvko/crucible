#pragma once

// The MetaLog behind one linear Permission per endpoint.  The handle types
// carry the role, so appending from the consumer side or draining from the
// producer side does not compile, and the move-only Permission keeps the
// log to one producer and one consumer at a time.
//
// Each log needs a UserTag of its own.  Two logs that share a tag share
// Permission types, and their endpoints become interchangeable.  Mint the
// root of each whole tag once per program: nothing checks that at run
// time.
//
// The endpoint accessors are members named producer and consumer, not
// mint_ free functions.  Their whole admission is the parameter type: a
// caller that cannot produce a Permission<producer_tag> cannot call
// producer().  So there is no constraint for a negative fixture to fire,
// and a mint_ name would claim a gate that they do not have.

#include <crucible/MetaLog.h>

#include <fixy/Aliases.h>
#include <fixy/session/NetworkModel.h>
#include <foundation/ChannelBinding.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <algorithm>
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

template <typename UserTag = void>
class PermissionedMetaLog {
public:
    using value_type = ::crucible::TensorMeta;
    using user_tag = UserTag;
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
        // append or drain through the Permission it no longer holds.
        ::foundation::ChannelBinding<::crucible::MetaLog> log_;
        [[no_unique_address]] ::foundation::permissions::Permission<producer_tag> perm_;

        constexpr ProducerHandle(::crucible::MetaLog& log,
                                 ::foundation::permissions::Permission<producer_tag>&& perm) noexcept
            : log_{log}, perm_{std::move(perm)} {}
        friend class PermissionedMetaLog;

    public:
        using value_type = ::crucible::TensorMeta;
        using tag_type = producer_tag;

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

        [[nodiscard, gnu::hot]] ::crucible::MetaIndex try_append(const value_type* metas, std::uint32_t count) {
            return log_->try_append(metas, count);
        }

        [[nodiscard, gnu::hot]] bool try_append_one(const value_type& meta) {
            return log_->try_append(&meta, 1).is_valid();
        }

        // The row gate is this constraint.  The body calls try_append, so the
        // gate does not depend on the row spelling that MetaLog itself reads.
        template <typename CallerRow = ::foundation::effects::Row<>>
            requires ::fixy::IsPure<CallerRow>
        [[nodiscard, gnu::hot]] ::crucible::MetaIndex try_append_pure(const value_type* metas, std::uint32_t count) {
            return log_->try_append(metas, count);
        }

        [[nodiscard]] std::uint32_t size_approx() const { return log_->size().peek(); }
    };

    // The tail index is read relaxed.  The consumer permission is linear, so
    // the thread holding it is the only writer of that index and no ordering
    // is needed to observe the latest value.  The head index is read with get,
    // which acquires against the producer's release and is what makes the
    // appended records visible.
    class ConsumerHandle {
        ::foundation::ChannelBinding<::crucible::MetaLog> log_;
        [[no_unique_address]] ::foundation::permissions::Permission<consumer_tag> perm_;

        constexpr ConsumerHandle(::crucible::MetaLog& log,
                                 ::foundation::permissions::Permission<consumer_tag>&& perm) noexcept
            : log_{log}, perm_{std::move(perm)} {}
        friend class PermissionedMetaLog;

    public:
        using value_type = ::crucible::TensorMeta;
        using tag_type = consumer_tag;

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

        [[nodiscard, gnu::hot]] std::optional<value_type> try_drain_one() {
            const std::uint32_t t = log_->tail.peek_relaxed();
            if (t == log_->head.get()) [[unlikely]] {
                return std::nullopt;
            }

            value_type meta = log_->at(t);
            log_->advance_tail(t + 1);
            return meta;
        }

        template <typename Body>
            requires std::is_invocable_v<Body&, const value_type&>
        [[nodiscard]] std::uint32_t drain(Body&& body, std::uint32_t max_items = ::crucible::MetaLog::CAPACITY) {
            const std::uint32_t t = log_->tail.peek_relaxed();
            const std::uint32_t available = log_->head.get() - t;
            const std::uint32_t count = std::min(available, max_items);

            for (std::uint32_t i = 0; i < count; ++i) {
                std::invoke(body, log_->at(t + i));
            }
            if (count != 0) {
                log_->advance_tail(t + count);
            }
            return count;
        }

        [[nodiscard]] const value_type& at(::crucible::MetaIndex index) const CRUCIBLE_LIFETIMEBOUND {
            return log_->at(index);
        }

        [[nodiscard]] value_type* try_contiguous(std::uint32_t start, std::uint32_t count) const
            CRUCIBLE_LIFETIMEBOUND {
            return log_->try_contiguous(start, count);
        }

        void advance_tail(std::uint32_t new_tail) { log_->advance_tail(new_tail); }

        [[nodiscard]] std::uint32_t head_index() const { return log_->head.get(); }

        [[nodiscard]] std::uint32_t tail_index() const { return log_->tail.get(); }

        [[nodiscard]] std::uint32_t size_approx() const { return log_->size().peek(); }
    };

    [[nodiscard]] constexpr ProducerHandle
    producer(::foundation::permissions::Permission<producer_tag>&& perm) noexcept {
        return ProducerHandle{log_, std::move(perm)};
    }

    [[nodiscard]] constexpr ConsumerHandle
    consumer(::foundation::permissions::Permission<consumer_tag>&& perm) noexcept {
        return ConsumerHandle{log_, std::move(perm)};
    }

    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept { return false; }

private:
    ::crucible::MetaLog& log_;
};

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
