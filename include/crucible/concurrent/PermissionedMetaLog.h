#pragma once

// PermissionedMetaLog<UserTag> — role-typed facade over MetaLog.
//
// MetaLog is already an SPSC buffer: foreground appends TensorMeta
// records at head, background drains them at tail.  This adapter adds
// the same CSL role split used by PermissionedSpscChannel without
// replacing MetaLog's storage or hot-path implementation:
//
//   * ProducerHandle owns Permission<MetaLogProducer<UserTag>>
//     and exposes append-only operations.
//   * ConsumerHandle owns Permission<MetaLogConsumer<UserTag>>
//     and exposes drain/read/tail-advance operations.
//
// The adapter itself stores only a MetaLog reference.  Handles store a
// MetaLog reference plus an empty Permission token, so release-mode
// handle size remains one pointer.

#include <crucible/MetaLog.h>
#include <crucible/permissions/Permission.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

namespace metalog_tag {

template <typename UserTag> struct Whole    {};
template <typename UserTag> struct Producer {};
template <typename UserTag> struct Consumer {};

}  // namespace metalog_tag

template <typename UserTag = void>
class PermissionedMetaLog {
public:
    using value_type   = ::crucible::TensorMeta;
    using user_tag     = UserTag;
    using whole_tag    = metalog_tag::Whole<UserTag>;
    using producer_tag = metalog_tag::Producer<UserTag>;
    using consumer_tag = metalog_tag::Consumer<UserTag>;

    explicit constexpr PermissionedMetaLog(::crucible::MetaLog& log) noexcept
        : log_{log} {}

    PermissionedMetaLog(const PermissionedMetaLog&) = delete;
    PermissionedMetaLog& operator=(const PermissionedMetaLog&) = delete;
    PermissionedMetaLog(PermissionedMetaLog&&) = delete;
    PermissionedMetaLog& operator=(PermissionedMetaLog&&) = delete;

    class ProducerHandle {
        ::crucible::MetaLog& log_;
        [[no_unique_address]] safety::Permission<producer_tag> perm_;

        constexpr ProducerHandle(::crucible::MetaLog& log,
                                 safety::Permission<producer_tag>&& perm) noexcept
            : log_{log}, perm_{std::move(perm)} {}
        friend class PermissionedMetaLog;

    public:
        using value_type = ::crucible::TensorMeta;
        using tag_type   = producer_tag;

        ProducerHandle(const ProducerHandle&)
            = delete("MetaLog ProducerHandle owns the Producer Permission — copy would duplicate the linear token");
        ProducerHandle& operator=(const ProducerHandle&)
            = delete("MetaLog ProducerHandle owns the Producer Permission — assignment would overwrite the linear token");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        ProducerHandle& operator=(ProducerHandle&&)
            = delete("MetaLog ProducerHandle binds to one MetaLog for life — rebinding would orphan the original Permission");

        [[nodiscard, gnu::hot]] ::crucible::MetaIndex
        try_append(const value_type* metas, std::uint32_t count)
        {
            return log_.try_append(metas, count);
        }

        [[nodiscard, gnu::hot]] bool try_append_one(const value_type& meta) {
            return log_.try_append(&meta, 1).is_valid();
        }

        template <typename CallerRow = ::crucible::effects::Row<>>
            requires ::crucible::effects::IsPure<CallerRow>
        [[nodiscard, gnu::hot]] ::crucible::MetaIndex
        try_append_pure(const value_type* metas, std::uint32_t count)
        {
            return log_.template try_append_pure<CallerRow>(metas, count);
        }

        [[nodiscard]] std::uint32_t size_approx() const {
            return log_.size();
        }
    };

    class ConsumerHandle {
        ::crucible::MetaLog& log_;
        [[no_unique_address]] safety::Permission<consumer_tag> perm_;

        constexpr ConsumerHandle(::crucible::MetaLog& log,
                                 safety::Permission<consumer_tag>&& perm) noexcept
            : log_{log}, perm_{std::move(perm)} {}
        friend class PermissionedMetaLog;

    public:
        using value_type = ::crucible::TensorMeta;
        using tag_type   = consumer_tag;

        ConsumerHandle(const ConsumerHandle&)
            = delete("MetaLog ConsumerHandle owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerHandle& operator=(const ConsumerHandle&)
            = delete("MetaLog ConsumerHandle owns the Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&)
            = delete("MetaLog ConsumerHandle binds to one MetaLog for life — rebinding would orphan the original Permission");

        [[nodiscard, gnu::hot]] std::optional<value_type> try_drain_one() {
            const std::uint32_t t = log_.tail.peek_relaxed();
            if (t == log_.head.get()) [[unlikely]] {
                return std::nullopt;
            }

            value_type meta = log_.at(t);
            log_.advance_tail(t + 1);
            return meta;
        }

        template <typename Body>
            requires std::is_invocable_v<Body&, const value_type&>
        [[nodiscard]] std::uint32_t drain(
            Body&& body,
            std::uint32_t max_items = ::crucible::MetaLog::CAPACITY)
        {
            const std::uint32_t t = log_.tail.peek_relaxed();
            const std::uint32_t available = log_.head.get() - t;
            const std::uint32_t count = std::min(available, max_items);

            for (std::uint32_t i = 0; i < count; ++i) {
                std::invoke(body, log_.at(t + i));
            }
            if (count != 0) {
                log_.advance_tail(t + count);
            }
            return count;
        }

        [[nodiscard]] const value_type& at(::crucible::MetaIndex index) const
            CRUCIBLE_LIFETIMEBOUND
        {
            return log_.at(index);
        }

        [[nodiscard]] value_type* try_contiguous(std::uint32_t start,
                                                 std::uint32_t count) const
            CRUCIBLE_LIFETIMEBOUND
        {
            return log_.try_contiguous(start, count);
        }

        void advance_tail(std::uint32_t new_tail) {
            log_.advance_tail(new_tail);
        }

        [[nodiscard]] std::uint32_t head_index() const {
            return log_.head.get();
        }

        [[nodiscard]] std::uint32_t tail_index() const {
            return log_.tail.get();
        }

        [[nodiscard]] std::uint32_t size_approx() const {
            return log_.size();
        }
    };

    [[nodiscard]] ProducerHandle
    producer(safety::Permission<producer_tag>&& perm) noexcept {
        return ProducerHandle{log_, std::move(perm)};
    }

    [[nodiscard]] ConsumerHandle
    consumer(safety::Permission<consumer_tag>&& perm) noexcept {
        return ConsumerHandle{log_, std::move(perm)};
    }

    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept {
        return false;
    }

private:
    ::crucible::MetaLog& log_;
};

}  // namespace crucible::concurrent

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::metalog_tag::Whole<UserTag>,
                   concurrent::metalog_tag::Producer<UserTag>,
                   concurrent::metalog_tag::Consumer<UserTag>>
    : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::metalog_tag::Whole<UserTag>,
                        concurrent::metalog_tag::Producer<UserTag>,
                        concurrent::metalog_tag::Consumer<UserTag>>
    : std::true_type {};

}  // namespace crucible::safety
