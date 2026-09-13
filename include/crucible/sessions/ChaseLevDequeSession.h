#pragma once

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionPermPayloads.h>

#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::chaselev_session {

template <typename T>
using OwnerProto = Loop<Select<Send<T, Continue>, Recv<T, Continue>>>;

template <typename T, typename ThiefTag>
using ThiefProto = Loop<Recv<Borrowed<T, ThiefTag>, Continue>>;

inline constexpr std::size_t owner_push_branch = 0;
inline constexpr std::size_t owner_pop_branch = 1;

template <typename Deque>
concept ChaseLevSessionSurface =
    requires(Deque& deque, typename Deque::OwnerHandle& owner, typename Deque::ThiefHandle& thief,
             ::crucible::safety::Permission<typename Deque::owner_tag>&& owner_perm) {
        typename Deque::value_type;
        typename Deque::owner_tag;
        typename Deque::thief_tag;
        typename Deque::OwnerHandle;
        typename Deque::ThiefHandle;

        { deque.owner(std::move(owner_perm)) } -> std::same_as<typename Deque::OwnerHandle>;
        { deque.thief() } -> std::same_as<std::optional<typename Deque::ThiefHandle>>;
        { owner.try_push(std::declval<typename Deque::value_type>()) } -> std::same_as<bool>;
        { owner.try_pop() } -> std::same_as<std::optional<typename Deque::value_type>>;
        { thief.try_steal() } -> std::same_as<std::optional<typename Deque::value_type>>;
        { thief.token() } -> std::same_as<::crucible::safety::SharedPermission<typename Deque::thief_tag>>;
    }
    && std::is_same_v<typename Deque::value_type, typename Deque::OwnerHandle::value_type>
    && std::is_same_v<typename Deque::value_type, typename Deque::ThiefHandle::value_type>
    && std::is_same_v<typename Deque::owner_tag, typename Deque::OwnerHandle::tag_type>
    && std::is_same_v<typename Deque::thief_tag, typename Deque::ThiefHandle::tag_type>;

// §XXI carve-out: cx=alloc — the deque owner holds a runtime refcount.
template <ChaseLevSessionSurface Deque>
[[nodiscard]] constexpr auto
mint_chaselev_owner(Deque& deque, ::crucible::safety::Permission<typename Deque::owner_tag>&& perm) noexcept {
    return deque.owner(std::move(perm));
}

// Not constexpr, deliberately. A steal runs a compare-exchange on the top index
// of the deque and bumps an atomic refcount for the fractional share. Neither
// step is constant-evaluable, so constexpr here would misstate the cost.
// §XXI carve-out: cx=alloc — a steal performs a runtime compare-exchange.
template <ChaseLevSessionSurface Deque>
[[nodiscard]] auto mint_chaselev_thief(Deque& deque) noexcept {
    return deque.thief();
}

// Not constexpr, for the same reason as the overload above.
template <ChaseLevSessionSurface Deque>
[[nodiscard]] auto mint_chaselev_thief(Deque& deque,
                                       ::crucible::safety::SharedPermission<typename Deque::thief_tag> proof) noexcept {
    (void)proof;
    return deque.thief();
}

template <ChaseLevSessionSurface Deque, ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_owner_session(Ctx const& ctx, typename Deque::OwnerHandle& handle) noexcept {
    using T = typename Deque::value_type;
    return mint_permissioned_session<OwnerProto<T>>(ctx, &handle);
}

template <ChaseLevSessionSurface Deque, ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_thief_session(Ctx const& ctx, typename Deque::ThiefHandle& handle) noexcept {
    using T = typename Deque::value_type;
    using Tag = typename Deque::thief_tag;
    return mint_permissioned_session<ThiefProto<T, Tag>>(ctx, &handle);
}

template <ChaseLevSessionSurface Deque, ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using OwnerSessionHandle =
    decltype(mint_owner_session<Deque>(std::declval<Ctx const&>(), std::declval<typename Deque::OwnerHandle&>()));

template <ChaseLevSessionSurface Deque, ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ThiefSessionHandle =
    decltype(mint_thief_session<Deque>(std::declval<Ctx const&>(), std::declval<typename Deque::ThiefHandle&>()));

// These helpers take the session by reference instead of consuming it. Every
// branch of one iteration returns to the same loop head with the same
// permission set, so a single iteration leaves the handle type unchanged.
template <ChaseLevSessionSurface Deque>
[[nodiscard, gnu::always_inline]] inline bool owner_session_try_push(OwnerSessionHandle<Deque>& session,
                                                                     typename Deque::value_type value) noexcept {
    return session.resource()->try_push(value);
}

template <ChaseLevSessionSurface Deque>
[[nodiscard, gnu::always_inline]]
inline std::optional<typename Deque::value_type> owner_session_try_pop(OwnerSessionHandle<Deque>& session) noexcept {
    return session.resource()->try_pop();
}

inline constexpr auto blocking_owner_push = [](auto& hp, auto&& value) noexcept {
    while (!hp->try_push(std::forward<decltype(value)>(value))) {
        CRUCIBLE_SPIN_PAUSE;
    }
};

inline constexpr auto blocking_owner_pop = [](auto& hp) noexcept {
    for (;;) {
        if (auto v = hp->try_pop()) return *v;
        CRUCIBLE_SPIN_PAUSE;
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
        CRUCIBLE_SPIN_PAUSE;
    }
};

template <ChaseLevSessionSurface Deque>
[[nodiscard, gnu::always_inline]]
inline Borrowed<typename Deque::value_type, typename Deque::thief_tag>
thief_session_steal_borrowed(ThiefSessionHandle<Deque>& session) noexcept {
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
static_assert(std::is_same_v<OwnerProto<int>, Loop<Select<Send<int, Continue>, Recv<int, Continue>>>>);
static_assert(std::is_same_v<ThiefProto<int, Deque::thief_tag>, Loop<Recv<Borrowed<int, Deque::thief_tag>, Continue>>>);
static_assert(std::is_same_v<typename OwnerSession::protocol, Select<Send<int, Continue>, Recv<int, Continue>>>);
static_assert(std::is_same_v<typename ThiefSession::protocol, Recv<Borrowed<int, Deque::thief_tag>, Continue>>);
static_assert(std::is_same_v<typename OwnerSession::perm_set, EmptyPermSet>);
static_assert(std::is_same_v<typename ThiefSession::perm_set, EmptyPermSet>);
static_assert(std::is_same_v<typename OwnerSession::loop_ctx,
                             LoopContext<Select<Send<int, Continue>, Recv<int, Continue>>, EmptyPermSet>>);
static_assert(std::is_same_v<typename ThiefSession::loop_ctx,
                             LoopContext<Recv<Borrowed<int, Deque::thief_tag>, Continue>, EmptyPermSet>>);

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, OwnerHandle*>)
                  == sizeof(SessionHandle<End, OwnerHandle*>),
              "chaselev_session: owner PSH pointer resource must remain "
              "same size as bare SessionHandle.");
static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, ThiefHandle*>)
                  == sizeof(SessionHandle<End, ThiefHandle*>),
              "chaselev_session: thief PSH pointer resource must remain "
              "same size as bare SessionHandle.");
static_assert(sizeof(PermissionedSessionHandle<Select<Send<int, End>, Recv<int, End>>, EmptyPermSet, OwnerHandle*>)
                  == sizeof(SessionHandle<Select<Send<int, End>, Recv<int, End>>, OwnerHandle*>),
              "chaselev_session: owner Select head must preserve the "
              "PSH-vs-bare size equality witness.");
static_assert(sizeof(PermissionedSessionHandle<Recv<Borrowed<int, Deque::thief_tag>, End>, EmptyPermSet, ThiefHandle*>)
                  == sizeof(SessionHandle<Recv<Borrowed<int, Deque::thief_tag>, End>, ThiefHandle*>),
              "chaselev_session: thief Recv head must preserve the "
              "PSH-vs-bare size equality witness.");
static_assert(
    sizeof(OwnerSession)
        == sizeof(SessionHandle<typename OwnerSession::protocol, OwnerHandle*, typename OwnerSession::loop_ctx>),
    "chaselev_session: actual minted owner loop-head PSH must "
    "remain the same size as the bare session handle.");
static_assert(
    sizeof(ThiefSession)
        == sizeof(SessionHandle<typename ThiefSession::protocol, ThiefHandle*, typename ThiefSession::loop_ctx>),
    "chaselev_session: actual minted thief loop-head PSH must "
    "remain the same size as the bare session handle.");

}  // namespace detail::chaselev_session_self_test

}  // namespace crucible::safety::proto::chaselev_session
