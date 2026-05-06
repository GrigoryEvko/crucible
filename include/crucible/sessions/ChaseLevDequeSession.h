#pragma once

// ── ChaseLevDequeSession.h — typed-session facade for work stealing ─
//
// PermissionedChaseLevDeque already enforces the raw CSL discipline:
// one linear owner may push/pop the bottom, many fractional thieves may
// steal from the top.  This header adds the session-shaped surface that
// SpscSession.h and SwmrSession.h provide for their substrates.
//
// Owner protocol:
//   Loop<Select<Send<T, Continue>, Recv<T, Continue>>>
//
// The owner chooses branch 0 to push work and branch 1 to pop work.
// The choice is local by default for in-process work-stealing pools;
// callers that need a wire-visible branch signal can call select<I>(tx)
// on the returned PermissionedSessionHandle directly.
//
// Thief protocol:
//   Loop<Recv<Borrowed<T, ThiefTag>, Continue>>
//
// A thief observes borrowed work.  The deque remains the authority for
// the work item; the Borrowed marker records the read-share discipline
// without changing the session PermSet.

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionPermPayloads.h>

#include <concepts>
#include <cstddef>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::chaselev_session {

template <typename T>
using OwnerProto = Loop<Select<Send<T, Continue>, Recv<T, Continue>>>;

template <typename T, typename ThiefTag>
using ThiefProto = Loop<Recv<Borrowed<T, ThiefTag>, Continue>>;

inline constexpr std::size_t owner_push_branch = 0;
inline constexpr std::size_t owner_pop_branch  = 1;

template <typename Deque>
concept ChaseLevSessionSurface = requires(
    Deque& deque,
    typename Deque::OwnerHandle& owner,
    typename Deque::ThiefHandle& thief,
    ::crucible::safety::Permission<typename Deque::owner_tag>&& owner_perm)
{
    typename Deque::value_type;
    typename Deque::owner_tag;
    typename Deque::thief_tag;
    typename Deque::OwnerHandle;
    typename Deque::ThiefHandle;

    { deque.owner(std::move(owner_perm)) }
        -> std::same_as<typename Deque::OwnerHandle>;
    { deque.thief() }
        -> std::same_as<std::optional<typename Deque::ThiefHandle>>;
    { owner.try_push(std::declval<typename Deque::value_type>()) }
        -> std::same_as<bool>;
    { owner.try_pop() }
        -> std::same_as<std::optional<typename Deque::value_type>>;
    { thief.try_steal() }
        -> std::same_as<std::optional<typename Deque::value_type>>;
    { thief.token() }
        -> std::same_as<::crucible::safety::SharedPermission<
            typename Deque::thief_tag>>;
} && std::is_same_v<typename Deque::value_type,
                    typename Deque::OwnerHandle::value_type>
  && std::is_same_v<typename Deque::value_type,
                    typename Deque::ThiefHandle::value_type>
  && std::is_same_v<typename Deque::owner_tag,
                    typename Deque::OwnerHandle::tag_type>
  && std::is_same_v<typename Deque::thief_tag,
                    typename Deque::ThiefHandle::tag_type>;

template <ChaseLevSessionSurface Deque>
[[nodiscard]] auto mint_chaselev_owner(
    Deque& deque,
    ::crucible::safety::Permission<typename Deque::owner_tag>&& perm) noexcept
{
    return deque.owner(std::move(perm));
}

template <ChaseLevSessionSurface Deque>
[[nodiscard]] auto mint_chaselev_thief(Deque& deque) noexcept {
    return deque.thief();
}

template <ChaseLevSessionSurface Deque>
[[nodiscard]] auto mint_chaselev_thief(
    Deque& deque,
    ::crucible::safety::SharedPermission<typename Deque::thief_tag> proof) noexcept
{
    (void)proof;
    return deque.thief();
}

template <ChaseLevSessionSurface Deque>
[[nodiscard]] constexpr auto
mint_owner_session(typename Deque::OwnerHandle& handle) noexcept
{
    using T = typename Deque::value_type;
    return mint_permissioned_session<OwnerProto<T>,
                                     typename Deque::OwnerHandle*>(&handle);
}

template <ChaseLevSessionSurface Deque>
[[nodiscard]] constexpr auto
mint_thief_session(typename Deque::ThiefHandle& handle) noexcept
{
    using T = typename Deque::value_type;
    using Tag = typename Deque::thief_tag;
    return mint_permissioned_session<ThiefProto<T, Tag>,
                                     typename Deque::ThiefHandle*>(&handle);
}

template <ChaseLevSessionSurface Deque>
using OwnerSessionHandle = decltype(
    mint_owner_session<Deque>(std::declval<typename Deque::OwnerHandle&>()));

template <ChaseLevSessionSurface Deque>
using ThiefSessionHandle = decltype(
    mint_thief_session<Deque>(std::declval<typename Deque::ThiefHandle&>()));

// Fused one-iteration hot helpers for the infinite EmptyPermSet Loop
// protocols above.  Each helper commits to one branch internally:
// owner_session_try_push = Select branch 0 + Send<T, Continue>,
// owner_session_try_pop  = Select branch 1 + Recv<T, Continue>,
// thief_session_steal_borrowed = Recv<Borrowed<T, ThiefTag>, Continue>.
// Because every path returns to the same loop head with the same PermSet,
// the external PSH object remains at that stable head.  These helpers are
// deliberately narrower than the general PSH API; callers needing explicit
// per-step protocol values should use select_local()/send()/recv().
template <ChaseLevSessionSurface Deque>
[[nodiscard, gnu::always_inline]] inline bool owner_session_try_push(
    OwnerSessionHandle<Deque>& session,
    typename Deque::value_type value) noexcept
{
    return session.resource()->try_push(value);
}

template <ChaseLevSessionSurface Deque>
[[nodiscard, gnu::always_inline]]
inline std::optional<typename Deque::value_type>
owner_session_try_pop(OwnerSessionHandle<Deque>& session) noexcept
{
    return session.resource()->try_pop();
}

inline constexpr auto blocking_owner_push =
    [](auto& hp, auto&& value) noexcept {
        while (!hp->try_push(std::forward<decltype(value)>(value))) {
            std::this_thread::yield();
        }
    };

inline constexpr auto blocking_owner_pop = [](auto& hp) noexcept {
    for (;;) {
        if (auto v = hp->try_pop()) return *v;
        std::this_thread::yield();
    }
};

inline constexpr auto blocking_steal_borrowed = [](auto& hp) noexcept {
    using handle_pointer = std::remove_reference_t<decltype(hp)>;
    using handle_type = std::remove_pointer_t<handle_pointer>;
    using value_type = typename handle_type::value_type;
    using tag_type = typename handle_type::tag_type;

    for (;;) {
        if (auto v = hp->try_steal()) {
            return Borrowed<value_type, tag_type>{*v};
        }
        std::this_thread::yield();
    }
};

template <ChaseLevSessionSurface Deque>
[[nodiscard, gnu::always_inline]]
inline Borrowed<typename Deque::value_type, typename Deque::thief_tag>
thief_session_steal_borrowed(
    ThiefSessionHandle<Deque>& session) noexcept
{
    return blocking_steal_borrowed(session.resource());
}

namespace detail::chaselev_session_self_test {

struct Tag {};
using Deque = ::crucible::concurrent::PermissionedChaseLevDeque<int, 16, Tag>;
using OwnerHandle = Deque::OwnerHandle;
using ThiefHandle = Deque::ThiefHandle;
using OwnerSession = OwnerSessionHandle<Deque>;
using ThiefSession = ThiefSessionHandle<Deque>;

static_assert(ChaseLevSessionSurface<Deque>);
static_assert(std::is_same_v<
    OwnerProto<int>,
    Loop<Select<Send<int, Continue>, Recv<int, Continue>>>>);
static_assert(std::is_same_v<
    ThiefProto<int, Deque::thief_tag>,
    Loop<Recv<Borrowed<int, Deque::thief_tag>, Continue>>>);
static_assert(std::is_same_v<
    typename OwnerSession::protocol,
    Select<Send<int, Continue>, Recv<int, Continue>>>);
static_assert(std::is_same_v<
    typename ThiefSession::protocol,
    Recv<Borrowed<int, Deque::thief_tag>, Continue>>);
static_assert(std::is_same_v<typename OwnerSession::perm_set, EmptyPermSet>);
static_assert(std::is_same_v<typename ThiefSession::perm_set, EmptyPermSet>);
static_assert(std::is_same_v<
    typename OwnerSession::loop_ctx,
    LoopContext<Select<Send<int, Continue>, Recv<int, Continue>>,
                EmptyPermSet>>);
static_assert(std::is_same_v<
    typename ThiefSession::loop_ctx,
    LoopContext<Recv<Borrowed<int, Deque::thief_tag>, Continue>,
                EmptyPermSet>>);

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                OwnerHandle*>)
              == sizeof(SessionHandle<End, OwnerHandle*>),
              "chaselev_session: owner PSH pointer resource must remain "
              "same size as bare SessionHandle.");
static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                ThiefHandle*>)
              == sizeof(SessionHandle<End, ThiefHandle*>),
              "chaselev_session: thief PSH pointer resource must remain "
              "same size as bare SessionHandle.");
static_assert(sizeof(PermissionedSessionHandle<Select<Send<int, End>,
                                                     Recv<int, End>>,
                                                EmptyPermSet, OwnerHandle*>)
              == sizeof(SessionHandle<Select<Send<int, End>, Recv<int, End>>,
                                      OwnerHandle*>),
              "chaselev_session: owner Select head must preserve the "
              "PSH-vs-bare size equality witness.");
static_assert(sizeof(PermissionedSessionHandle<
                  Recv<Borrowed<int, Deque::thief_tag>, End>,
                  EmptyPermSet, ThiefHandle*>)
              == sizeof(SessionHandle<
                  Recv<Borrowed<int, Deque::thief_tag>, End>,
                  ThiefHandle*>),
              "chaselev_session: thief Recv head must preserve the "
              "PSH-vs-bare size equality witness.");
static_assert(sizeof(OwnerSession)
              == sizeof(SessionHandle<typename OwnerSession::protocol,
                                      OwnerHandle*,
                                      typename OwnerSession::loop_ctx>),
              "chaselev_session: actual minted owner loop-head PSH must "
              "remain the same size as the bare session handle.");
static_assert(sizeof(ThiefSession)
              == sizeof(SessionHandle<typename ThiefSession::protocol,
                                      ThiefHandle*,
                                      typename ThiefSession::loop_ctx>),
              "chaselev_session: actual minted thief loop-head PSH must "
              "remain the same size as the bare session handle.");

}  // namespace detail::chaselev_session_self_test

}  // namespace crucible::safety::proto::chaselev_session
