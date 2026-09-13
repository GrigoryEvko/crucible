#pragma once

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionMint.h>

#include <type_traits>
#include <utility>

namespace crucible::safety::proto::spsc_session {

// Neither protocol has an exit branch, so neither ever reaches End. The caller
// detaches the terminal handle at shutdown.

template <typename T>
using ProducerProto = Loop<Send<T, Continue>>;

template <typename T>
using ConsumerProto = Loop<Recv<T, Continue>>;

// The session stores the address of the handle rather than the handle. A
// by-value resource would need move-assignment, which the handle deletes, and a
// reference resource would need a pinned type, which the handle is not. The
// handle outlives the session it is bound to.

template <typename Channel, ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_producer_session(Ctx const& ctx, typename Channel::ProducerHandle& handle) noexcept {
    using T = typename Channel::value_type;
    return mint_permissioned_session<ProducerProto<T>>(ctx, &handle);
}

template <typename Channel, ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_consumer_session(Ctx const& ctx, typename Channel::ConsumerHandle& handle) noexcept {
    using T = typename Channel::value_type;
    return mint_permissioned_session<ConsumerProto<T>>(ctx, &handle);
}

inline constexpr auto blocking_push = [](auto& hp, auto&& value) noexcept {
    while (!hp->try_push(std::forward<decltype(value)>(value))) {
        CRUCIBLE_SPIN_PAUSE;
    }
};

inline constexpr auto blocking_pop = [](auto& hp) noexcept {
    for (;;) {
        if (auto v = hp->try_pop()) return *v;
        CRUCIBLE_SPIN_PAUSE;
    }
};

}  // namespace crucible::safety::proto::spsc_session

namespace crucible::safety::proto::spsc_session::detail::sizeof_witness {

struct Tag {};
using SmallChannel = ::crucible::concurrent::PermissionedSpscChannel<int, 16, Tag>;
using ProdHandle = SmallChannel::ProducerHandle;
using ConsHandle = SmallChannel::ConsumerHandle;

// The size equality is asserted on the concrete head types a mint returns after
// the Loop unrolls, not on Loop itself. Loop is a shape-only template with no
// handle specialisation, so it has no size to compare.

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, ProdHandle*>)
                  == sizeof(SessionHandle<End, ProdHandle*>),
              "spsc_session: PSH<End, EmptyPermSet, ProdHandle*> must be "
              "same size as bare SessionHandle<End, ProdHandle*> — if this "
              "fails, EBO collapse of EmptyPermSet has been broken or the "
              "abandonment tracker grew asymmetrically between PSH and bare.");

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, ConsHandle*>)
                  == sizeof(SessionHandle<End, ConsHandle*>),
              "spsc_session: PSH<End, EmptyPermSet, ConsHandle*> must be "
              "same size as bare SessionHandle<End, ConsHandle*>.");

static_assert(sizeof(PermissionedSessionHandle<Send<int, End>, EmptyPermSet, ProdHandle*>)
                  == sizeof(SessionHandle<Send<int, End>, ProdHandle*>),
              "spsc_session: PSH<Send<int, End>, EmptyPermSet, ProdHandle*> "
              "must be same size as bare SessionHandle for the same head.");

// No absolute size bound is asserted. A bound that holds in both build modes
// would have to encode the size of the debug-only consumption tracker, which is
// free to change. The equality above already covers what such a bound would say,
// because both sides pay the same tracker cost.

}  // namespace crucible::safety::proto::spsc_session::detail::sizeof_witness
