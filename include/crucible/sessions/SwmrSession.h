#pragma once

// ── SwmrSession.h — typed-session facade for SWMR snapshots ─────────
//
// Single-writer / multiple-reader publication built on the shipped
// AtomicSnapshot + SharedPermissionPool substrate.  This header adds
// the session-shaped role surface that SpscSession.h provides for
// streaming rings: a writer role that can only publish and a reader
// role that can only load, plus optional PermissionedSessionHandle
// wrappers for code that wants the canonical SWMR protocol shape:
// Loop<Send<ContentAddressed<T>>> / Loop<Recv<Borrowed<T, ReaderTag>>>.
//
// ContentAddressed<T> remains a type-level quotient marker in the
// current session stack, not a runtime carrier.  PermissionedSessionHandle
// accepts raw T sends into Send<ContentAddressed<T>> via the existing
// subsort rule (T <= ContentAddressed<T>), so the runtime transport still
// publishes a T value while the protocol records the dedup-eligible
// content-addressed shape.

#include <crucible/Platform.h>
#include <crucible/concurrent/AtomicSnapshot.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/IsSwmrHandle.h>
#include <crucible/safety/Pinned.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionContentAddressed.h>
#include <crucible/sessions/SessionPermPayloads.h>

#include <cstdint>
#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::swmr_session {

template <typename T>
using WriterProto = Loop<Send<ContentAddressed<T>, Continue>>;

template <typename T, typename ReaderTag>
using ReaderProto = Loop<Recv<Borrowed<T, ReaderTag>, Continue>>;

template <typename T>
using WriterRuntimeProto = Loop<Send<T, Continue>>;

template <typename T>
using ReaderRuntimeProto = Loop<Recv<T, Continue>>;

template <typename S>
concept SwmrSessionSurface = requires {
    typename S::value_type;
    typename S::writer_tag;
    typename S::reader_tag;
    typename S::WriterHandle;
    typename S::ReaderHandle;
    { std::declval<S&>().writer(
          std::declval<::crucible::safety::Permission<typename S::writer_tag>&&>()) }
        -> std::same_as<typename S::WriterHandle>;
    { std::declval<S&>().reader() }
        -> std::same_as<std::optional<typename S::ReaderHandle>>;
} && ::crucible::safety::extract::IsSwmrWriter<typename S::WriterHandle>
  && ::crucible::safety::extract::IsSwmrReader<typename S::ReaderHandle>
  && std::is_same_v<
         ::crucible::safety::extract::swmr_writer_value_t<typename S::WriterHandle>,
         typename S::value_type>
  && std::is_same_v<
         ::crucible::safety::extract::swmr_reader_value_t<typename S::ReaderHandle>,
         typename S::value_type>;

template <::crucible::concurrent::SnapshotValue T,
          typename WriterTag,
          typename ReaderTag>
class SwmrSession : public ::crucible::safety::Pinned<
    SwmrSession<T, WriterTag, ReaderTag>> {
public:
    using value_type = T;
    using writer_tag = WriterTag;
    using reader_tag = ReaderTag;

    SwmrSession() noexcept
        : snapshot_{}
        , reader_pool_{::crucible::safety::mint_permission_root<reader_tag>()} {}

    explicit SwmrSession(T const& initial) noexcept
        : snapshot_{initial}
        , reader_pool_{::crucible::safety::mint_permission_root<reader_tag>()} {}

    class WriterHandle {
        SwmrSession* session_ = nullptr;
        [[no_unique_address]] ::crucible::safety::Permission<writer_tag> perm_;

        constexpr WriterHandle(SwmrSession& session,
                               ::crucible::safety::Permission<writer_tag>&& perm) noexcept
            : session_{&session}, perm_{std::move(perm)} {}

        friend class SwmrSession;

    public:
        using value_type = T;
        using tag_type = writer_tag;

        WriterHandle(WriterHandle const&)
            = delete("SwmrSession::WriterHandle owns the linear writer permission");
        WriterHandle& operator=(WriterHandle const&)
            = delete("SwmrSession::WriterHandle owns the linear writer permission");
        constexpr WriterHandle(WriterHandle&&) noexcept = default;
        constexpr WriterHandle& operator=(WriterHandle&&) noexcept = default;

        void publish(T const& value) noexcept { session_->snapshot_.publish(value); }

        [[nodiscard]] std::uint64_t version() const noexcept {
            return session_->snapshot_.version();
        }
    };

    class ReaderHandle {
        SwmrSession* session_ = nullptr;
        ::crucible::safety::SharedPermissionGuard<reader_tag> guard_;

        constexpr ReaderHandle(SwmrSession& session,
                               ::crucible::safety::SharedPermissionGuard<reader_tag>&& guard) noexcept
            : session_{&session}, guard_{std::move(guard)} {}

        friend class SwmrSession;

    public:
        using value_type = T;
        using tag_type = reader_tag;

        ReaderHandle(ReaderHandle const&)
            = delete("SwmrSession::ReaderHandle owns one SharedPermissionPool share");
        ReaderHandle& operator=(ReaderHandle const&)
            = delete("SwmrSession::ReaderHandle owns one SharedPermissionPool share");
        constexpr ReaderHandle(ReaderHandle&&) noexcept = default;
        ReaderHandle& operator=(ReaderHandle&&)
            = delete("SwmrSession::ReaderHandle share lifetime is fixed at construction");

        [[nodiscard]] T load() const noexcept { return session_->snapshot_.load(); }

        [[nodiscard]] std::optional<T> try_load() const noexcept {
            return session_->snapshot_.try_load();
        }

        [[nodiscard]] std::uint64_t version() const noexcept {
            return session_->snapshot_.version();
        }

        [[nodiscard]] constexpr auto token() const noexcept
            -> ::crucible::safety::SharedPermission<reader_tag>
        {
            return guard_.token();
        }
    };

    [[nodiscard]] WriterHandle writer(
        ::crucible::safety::Permission<writer_tag>&& perm) noexcept
    {
        return WriterHandle{*this, std::move(perm)};
    }

    [[nodiscard]] std::optional<ReaderHandle> reader() noexcept {
        auto guard = reader_pool_.lend();
        if (!guard) return std::nullopt;
        return ReaderHandle{*this, std::move(*guard)};
    }

    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_drained_access(Body&& body)
        noexcept(std::is_nothrow_invocable_v<Body>)
    {
        auto upgrade = reader_pool_.try_upgrade();
        if (!upgrade) return false;
        std::forward<Body>(body)();
        reader_pool_.deposit_exclusive(std::move(*upgrade));
        return true;
    }

    [[nodiscard]] std::uint64_t outstanding_readers() const noexcept {
        return reader_pool_.outstanding();
    }

    [[nodiscard]] bool is_exclusive_active() const noexcept {
        return reader_pool_.is_exclusive_out();
    }

    [[nodiscard]] std::uint64_t version() const noexcept {
        return snapshot_.version();
    }

private:
    ::crucible::concurrent::AtomicSnapshot<T> snapshot_;
    ::crucible::safety::SharedPermissionPool<reader_tag> reader_pool_;
};

template <SwmrSessionSurface Swmr>
[[nodiscard]] auto mint_swmr_writer(
    Swmr& session,
    ::crucible::safety::Permission<typename Swmr::writer_tag>&& perm) noexcept
{
    return session.writer(std::move(perm));
}

template <SwmrSessionSurface Swmr>
[[nodiscard]] auto mint_swmr_reader(Swmr& session) noexcept {
    return session.reader();
}

template <SwmrSessionSurface Swmr>
[[nodiscard]] auto mint_swmr_reader(
    Swmr& session,
    ::crucible::safety::SharedPermission<typename Swmr::reader_tag> proof) noexcept
{
    (void)proof;
    return session.reader();
}

template <SwmrSessionSurface Swmr>
[[nodiscard]] constexpr auto
mint_writer_session(typename Swmr::WriterHandle& handle) noexcept
{
    using T = typename Swmr::value_type;
    return mint_permissioned_session<WriterProto<T>,
                                     typename Swmr::WriterHandle*>(&handle);
}

template <SwmrSessionSurface Swmr>
[[nodiscard]] constexpr auto
mint_reader_session(typename Swmr::ReaderHandle& handle) noexcept
{
    using T = typename Swmr::value_type;
    return mint_permissioned_session<ReaderProto<T, typename Swmr::reader_tag>,
                                     typename Swmr::ReaderHandle*>(&handle);
}

template <SwmrSessionSurface Swmr>
[[nodiscard]] constexpr auto
mint_writer_runtime_session(typename Swmr::WriterHandle& handle) noexcept
{
    using T = typename Swmr::value_type;
    return mint_permissioned_session<WriterRuntimeProto<T>,
                                     typename Swmr::WriterHandle*>(&handle);
}

template <SwmrSessionSurface Swmr>
[[nodiscard]] constexpr auto
mint_reader_runtime_session(typename Swmr::ReaderHandle& handle) noexcept
{
    using T = typename Swmr::value_type;
    return mint_permissioned_session<ReaderRuntimeProto<T>,
                                     typename Swmr::ReaderHandle*>(&handle);
}

inline constexpr auto publish_value = [](auto& hp, auto&& value) noexcept {
    hp->publish(std::forward<decltype(value)>(value));
};

inline constexpr auto load_value = [](auto& hp) noexcept {
    return hp->load();
};

inline constexpr auto load_borrowed_value = [](auto& hp) noexcept {
    using handle_pointer = std::remove_reference_t<decltype(hp)>;
    using handle_type = std::remove_pointer_t<handle_pointer>;
    using value_type = typename handle_type::value_type;
    using tag_type = typename handle_type::tag_type;
    return Borrowed<value_type, tag_type>{hp->load()};
};

namespace detail::swmr_session_self_test {

struct WriterTag {};
struct ReaderTag {};
using SmallSession = SwmrSession<int, WriterTag, ReaderTag>;
using WriterHandle = SmallSession::WriterHandle;
using ReaderHandle = SmallSession::ReaderHandle;

static_assert(sizeof(WriterHandle) == sizeof(SmallSession*),
    "SwmrSession::WriterHandle must EBO-collapse the writer Permission.");
static_assert(sizeof(ReaderHandle) ==
              sizeof(SmallSession*) +
              sizeof(::crucible::safety::SharedPermissionGuard<ReaderTag>),
    "SwmrSession::ReaderHandle must only store a session pointer plus guard.");
static_assert(!std::is_copy_constructible_v<WriterHandle>);
static_assert(!std::is_copy_constructible_v<ReaderHandle>);
static_assert(std::is_move_constructible_v<WriterHandle>);
static_assert(std::is_move_constructible_v<ReaderHandle>);

static_assert(std::is_same_v<
    WriterProto<int>,
    Loop<Send<ContentAddressed<int>, Continue>>>);
static_assert(std::is_same_v<
    ReaderProto<int, ReaderTag>,
    Loop<Recv<Borrowed<int, ReaderTag>, Continue>>>);
static_assert(std::is_same_v<WriterRuntimeProto<int>, Loop<Send<int, Continue>>>);
static_assert(std::is_same_v<ReaderRuntimeProto<int>, Loop<Recv<int, Continue>>>);

}  // namespace detail::swmr_session_self_test

}  // namespace crucible::safety::proto::swmr_session
