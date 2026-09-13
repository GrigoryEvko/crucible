#pragma once

// This header joins a runtime death signal to a compile-time session
// protocol.  One flag exists per remote peer.  Whatever detects the death —
// a transport completion error, a membership protocol, a closed socket —
// signals that flag once.  Every consumer method of the watched handle peeks
// the flag before its protocol step, and on the true path takes an acquire
// fence that pairs with the signalling release, moves the resource out of the
// inner handle, detaches it, mints the inherited survivor permissions and
// returns them as the error arm of an expected.
//
// The mapping is deliberately lossy in one direction.  A crash does not move
// the session into its Stop state: the inner handle is destroyed and the
// recovered resource handed back instead, so the caller re-establishes rather
// than continues.  What the protocol declared as a crash continuation is not
// entered by this wrapper.
//
// The peer tag carries no runtime state.  It exists so that the returned
// event names the responsible peer at compile time.  A session with several
// peers nests one watched handle per peer, and the resulting type records
// every watched peer.
//
// The flag is held by pointer and never owned.  Whoever allocates it has to
// outlive the session that watches it.
//
// On death, permissions pass only along the declared survivor lattice.  A
// peer tag with no declared survivors cannot be watched at all, because there
// would be no answer to the question of who inherits.
//
// A watched handle cannot be graded no-throw.  A peer guarded by a crash flag
// is by construction one that can crash, so declaring it unable to is a
// contradiction rather than an optimisation.
//
// The mint accepts a bare handle as well as a permissioned one.  A bare
// handle enters the permissioned crash surface with an empty permission set.

#include <crucible/Platform.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/permissions/PermissionInherit.h>
#include <crucible/safety/IsSessionHandle.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <source_location>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

template <typename PeerTag, typename InnerHandle, typename Resource, typename Reason>
[[nodiscard]] constexpr auto wrap_crash_return(InnerHandle&& inner, Reason reason_tag, Resource recovered) noexcept;

// A crash event may only be constructed together with the detach of the inner
// handle it recovers from.  A crash path that built the event and forgot the
// detach would leave the inner handle at a non-terminal protocol state, and
// its destructor would abort for abandonment.  Bundling both into one helper
// and keeping the event constructible only by that helper makes the omission
// unspellable rather than merely discouraged.
//
// The pass-key below is the token that helper holds: an empty type whose
// constructor is private, so no other caller can produce the first argument.
namespace detail {
struct WrapCrashReturnAuthorizer;
}  // namespace detail

class WrapCrashReturnKey {
    constexpr WrapCrashReturnKey() noexcept = default;

    // A key that can be duplicated can be exfiltrated, through a lambda
    // capture or a friend method that hands one back.  Deleting copy and move
    // costs the production paths nothing: mandatory copy elision carries the
    // minted prvalue straight into a value parameter or a reference binding
    // without ever constructing a second key.
    WrapCrashReturnKey(const WrapCrashReturnKey&) = delete("passkey cannot be copied");
    WrapCrashReturnKey(WrapCrashReturnKey&&) = delete("passkey cannot be moved");

    friend struct detail::WrapCrashReturnAuthorizer;
};

// An empty class gets copy and move implicitly, so the property these asserts
// name is one a future edit can silently give back.
static_assert(!std::is_copy_constructible_v<WrapCrashReturnKey>, "WrapCrashReturnKey must not be copy-constructible");
static_assert(!std::is_move_constructible_v<WrapCrashReturnKey>, "WrapCrashReturnKey must not be move-constructible");
static_assert(!std::is_copy_assignable_v<WrapCrashReturnKey>, "WrapCrashReturnKey must not be copy-assignable");
static_assert(!std::is_move_assignable_v<WrapCrashReturnKey>, "WrapCrashReturnKey must not be move-assignable");

namespace detail {

// This authorizer is a struct rather than the crash-path function template
// itself.  Template friendship matches on the exact signature, so friending
// the function would come undone the moment its parameter list gained a
// source location or another template parameter, and the breakage would
// appear as a private-constructor error at unrelated construction sites.
// Friendship on a non-template class matches by identity and cannot drift.
struct WrapCrashReturnAuthorizer {
    [[nodiscard]] static constexpr WrapCrashReturnKey mint() noexcept { return WrapCrashReturnKey{}; }

    // The one place a crash event is constructed.  The event type comes from
    // the caller, so this stays non-template as a class.
    template <typename Event, typename Resource, typename Perms>
    [[nodiscard]] static constexpr Event mint_event_(Resource&& r, Perms&& perms) noexcept {
        return Event{WrapCrashReturnKey{}, std::forward<Resource>(r), std::forward<Perms>(perms)};
    }
};

}  // namespace detail

template <typename PeerTag, typename Resource, typename... SurvivorTags>
class CrashEvent {
public:
    using peer = PeerTag;
    using resource_type = Resource;
    using survivors = ::crucible::permissions::inheritance_list<SurvivorTags...>;
    using permissions_type = std::tuple<::crucible::safety::Permission<SurvivorTags>...>;

    Resource resource;
    [[no_unique_address]] permissions_type permissions;

    // The whole crash path is noexcept, so a resource whose move can throw
    // would terminate the process during recovery, which is the worst moment
    // for it.  The assert moves that failure to compile time.
    static_assert(std::is_nothrow_move_constructible_v<Resource>,
                  "CrashEvent Resource must be nothrow-move-"
                  "constructible.  The crash-path is noexcept and a throwing "
                  "move would call std::terminate during recovery.");

private:
    // Access control, not the key, is what makes this unreachable: a forged
    // key still cannot name a private constructor.  The key parameter stays
    // as the runtime witness that the authorizer minted both.
    constexpr CrashEvent(WrapCrashReturnKey, Resource&& r, permissions_type perms) noexcept
        : resource{std::move(r)}, permissions{std::move(perms)} {}

    friend struct detail::WrapCrashReturnAuthorizer;
};

namespace detail {

template <typename PeerTag, typename Resource, typename Survivors>
struct crash_event_from_survivors;

template <typename PeerTag, typename Resource, typename... SurvivorTags>
struct crash_event_from_survivors<PeerTag, Resource, ::crucible::permissions::inheritance_list<SurvivorTags...>> {
    using type = CrashEvent<PeerTag, Resource, SurvivorTags...>;
};

template <typename PeerTag, typename Resource>
using crash_event_for_t =
    typename crash_event_from_survivors<PeerTag, Resource, ::crucible::permissions::survivors_t<PeerTag>>::type;

template <typename Event>
struct crash_event_matches_survivors : std::false_type {};

template <typename PeerTag, typename Resource, typename... SurvivorTags>
struct crash_event_matches_survivors<CrashEvent<PeerTag, Resource, SurvivorTags...>>
    : std::is_same<CrashEvent<PeerTag, Resource, SurvivorTags...>, crash_event_for_t<PeerTag, Resource>> {};

template <typename Event>
inline constexpr bool crash_event_matches_survivors_v = crash_event_matches_survivors<Event>::value;

template <typename PeerTag>
consteval void require_crash_survivors_declared_() {
    static_assert(!::crucible::permissions::inheritance_list_empty_v<::crucible::permissions::survivors_t<PeerTag>>,
                  "CrashWatchedHandle requires mint_permission_inherit survivors for "
                  "PeerTag. Specialize survivor_registry<PeerTag> at the peer tag "
                  "declaration site before enabling crash recovery.");
}

template <CrashClass C>
inline constexpr bool crash_watched_class_admissible_v = C != CrashClass::NoThrow;

template <CrashClass C>
consteval void require_crash_watched_class_admissible_() {
    static_assert(crash_watched_class_admissible_v<C>, "crucible::session::diagnostic "
                                                       "[CrashWatched_NoThrow_Rejected]: "
                                                       "CrashWatchedHandle cannot be parameterized with "
                                                       "CrashClass::NoThrow for an unreliable watched peer. Remove the "
                                                       "watcher for genuinely no-throw peers, or declare Abort/Throw/"
                                                       "ErrorReturn recovery.");
}

template <typename Proto, CrashClass C>
struct stop_class_compatible : std::true_type {};

template <CrashClass StopC, CrashClass C>
struct stop_class_compatible<Stop_g<StopC>, C> : std::bool_constant<StopC == C> {};

template <typename T, typename R, CrashClass C>
struct stop_class_compatible<Send<T, R>, C> : stop_class_compatible<R, C> {};

template <typename T, typename R, CrashClass C>
struct stop_class_compatible<Recv<T, R>, C> : stop_class_compatible<R, C> {};

template <typename... Branches, CrashClass C>
struct stop_class_compatible<Select<Branches...>, C>
    : std::bool_constant<(stop_class_compatible<Branches, C>::value && ...)> {};

template <typename... Branches, CrashClass C>
struct stop_class_compatible<Offer<Branches...>, C>
    : std::bool_constant<(stop_class_compatible<Branches, C>::value && ...)> {};

template <typename Body, CrashClass C>
struct stop_class_compatible<Loop<Body>, C> : stop_class_compatible<Body, C> {};

template <typename Inner, typename K, CrashClass C>
struct stop_class_compatible<Delegate<Inner, K>, C>
    : std::bool_constant<stop_class_compatible<Inner, C>::value && stop_class_compatible<K, C>::value> {};

template <typename Inner, typename K, CrashClass C>
struct stop_class_compatible<Accept<Inner, K>, C>
    : std::bool_constant<stop_class_compatible<Inner, C>::value && stop_class_compatible<K, C>::value> {};

template <typename Inner, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, CrashClass C>
struct stop_class_compatible<EpochedDelegate<Inner, K, MinEpoch, MinGeneration>, C>
    : std::bool_constant<stop_class_compatible<Inner, C>::value && stop_class_compatible<K, C>::value> {};

template <typename Inner, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, CrashClass C>
struct stop_class_compatible<EpochedAccept<Inner, K, MinEpoch, MinGeneration>, C>
    : std::bool_constant<stop_class_compatible<Inner, C>::value && stop_class_compatible<K, C>::value> {};

template <typename Proto, CrashClass C>
inline constexpr bool stop_class_compatible_v = stop_class_compatible<Proto, C>::value;

template <typename Proto, CrashClass C>
consteval void require_stop_class_compatible_() {
    static_assert(stop_class_compatible_v<Proto, C>, "crucible::session::diagnostic "
                                                     "[CrashWatched_StopClass_Mismatch]: "
                                                     "CrashWatchedHandle's declared CrashClass does not match a "
                                                     "Stop_g<C> reachable in the watched protocol. Align the handle "
                                                     "CrashClass with the protocol Stop_g grade.");
}

template <typename Proto, CrashClass C>
consteval void require_crash_watched_contract_() {
    require_crash_watched_class_admissible_<C>();
    require_stop_class_compatible_<Proto, C>();
}

}  // namespace detail

template <typename Event>
concept CrashEventMatchesSurvivors = detail::crash_event_matches_survivors_v<Event>;

// A crash path detaches the inner handle and returns the survivor-aware event
// as one step.  Detach is what lets the inner handle's destructor see a
// consumed handle instead of one abandoned mid-protocol.  Every crash path in
// this file goes through here, which is also the only construction site for
// the event, so the two halves cannot come apart.

template <typename PeerTag, typename InnerHandle, typename Resource, typename Reason>
[[nodiscard]] constexpr auto wrap_crash_return(InnerHandle&& inner, Reason reason_tag, Resource recovered) noexcept {
    using Inner = std::remove_cvref_t<InnerHandle>;
    static_assert(std::is_same_v<std::remove_cvref_t<Resource>, typename Inner::resource_type>,
                  "wrap_crash_return recovered resource type must match inner "
                  "resource_type.");

    std::move(inner).detach(reason_tag);
    detail::require_crash_survivors_declared_<PeerTag>();

    // The witness key stands for the death having been observed.  The detach
    // above is that observation, which is why the key is minted here and
    // nowhere earlier.
    return std::unexpected{detail::WrapCrashReturnAuthorizer::mint_event_<detail::crash_event_for_t<PeerTag, Resource>>(
        std::move(recovered),
        ::crucible::permissions::mint_permission_inherit<PeerTag>(
            ::crucible::permissions::crash_witness_key{detail::WrapCrashReturnAuthorizer::mint()}))};
}

template <typename Proto, typename Resource, typename PeerTag, CrashClass C = CrashClass::Abort,
          typename LoopCtx = void, typename PS = EmptyPermSet>
class CrashWatchedHandle;

namespace detail {

template <typename LoopCtx, typename PS>
struct permissioned_loop_ctx_from_bare {
    using type = LoopCtx;
};

template <typename PS>
struct permissioned_loop_ctx_from_bare<void, PS> {
    using type = void;
};

template <typename Body, typename PS>
struct permissioned_loop_ctx_from_bare<Loop<Body>, PS> {
    using type = LoopContext<Body, PS>;
};

template <typename Body, typename EntryPS, typename PS>
struct permissioned_loop_ctx_from_bare<LoopContext<Body, EntryPS>, PS> {
    using type = LoopContext<Body, EntryPS>;
};

template <std::uint64_t CurrentEpoch, std::uint64_t CurrentGeneration, typename InnerLoopCtx, typename PS>
struct permissioned_loop_ctx_from_bare<EpochCtx<CurrentEpoch, CurrentGeneration, InnerLoopCtx>, PS> {
    using type =
        EpochCtx<CurrentEpoch, CurrentGeneration, typename permissioned_loop_ctx_from_bare<InnerLoopCtx, PS>::type>;
};

template <VendorBackend V, typename InnerLoopCtx, typename PS>
struct permissioned_loop_ctx_from_bare<VendorCtx<V, InnerLoopCtx>, PS> {
    using type = VendorCtx<V, typename permissioned_loop_ctx_from_bare<InnerLoopCtx, PS>::type>;
};

template <typename LoopCtx, typename PS>
using permissioned_loop_ctx_from_bare_t = typename permissioned_loop_ctx_from_bare<LoopCtx, PS>::type;

template <typename PeerTag, CrashClass C, typename NextHandle>
[[nodiscard]] constexpr auto wrap_crash_next_(NextHandle inner, OneShotFlag& flag) noexcept {
    using NextProto = typename NextHandle::protocol;
    using NextResource = typename NextHandle::resource_type;
    using NextLoopCtx = typename NextHandle::loop_ctx;
    using NextPS = typename NextHandle::perm_set;
    return CrashWatchedHandle<NextProto, NextResource, PeerTag, C, NextLoopCtx, NextPS>{std::move(inner), flag};
}

}  // namespace detail

// Closing at the terminal state does not consult the flag.  The protocol has
// already completed, so a death signal arriving afterwards changes nothing
// that the caller could act on.  This is the one consumer that never takes
// the crash arm.

template <typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]] CrashWatchedHandle<End, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<End, CrashWatchedHandle<End, Resource, PeerTag, C, LoopCtx, PS>> {
    PermissionedSessionHandle<End, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
                  "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
                  "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(detail::stop_class_compatible_v<End, C>,
                  "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
                  "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol = End;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    using inner_type = PermissionedSessionHandle<End, PS, Resource, LoopCtx>;
    using stop_type = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    constexpr CrashWatchedHandle(inner_type inner, OneShotFlag& flag,
                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<End, CrashWatchedHandle<End, Resource, PeerTag, C, LoopCtx, PS>>{loc},
          inner_{std::move(inner)},
          flag_{&flag} {
        detail::require_crash_watched_contract_<End, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    [[nodiscard]] constexpr Resource close() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->mark_consumed_();
        return std::move(inner_).close();
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return *flag_; }
};

// A crash terminal is terminal like the end state, but its grade has to equal
// the watcher's own.  Widening one against the other would let a session
// declare one recovery family and terminate in another.

template <CrashClass StopC, typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]]
CrashWatchedHandle<Stop_g<StopC>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Stop_g<StopC>, CrashWatchedHandle<Stop_g<StopC>, Resource, PeerTag, C, LoopCtx, PS>> {
    PermissionedSessionHandle<Stop_g<StopC>, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
                  "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
                  "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(StopC == C, "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
                              "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol = Stop_g<StopC>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    using inner_type = PermissionedSessionHandle<Stop_g<StopC>, PS, Resource, LoopCtx>;
    using stop_type = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    constexpr CrashWatchedHandle(inner_type inner, OneShotFlag& flag,
                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Stop_g<StopC>, CrashWatchedHandle<Stop_g<StopC>, Resource, PeerTag, C, LoopCtx, PS>>{loc},
          inner_{std::move(inner)},
          flag_{&flag} {
        detail::require_crash_watched_contract_<Stop_g<StopC>, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    [[nodiscard]] constexpr Resource close() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->mark_consumed_();
        return std::move(inner_).close();
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return *flag_; }
};

template <typename T, typename R, typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]] CrashWatchedHandle<Send<T, R>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Send<T, R>, CrashWatchedHandle<Send<T, R>, Resource, PeerTag, C, LoopCtx, PS>> {
    PermissionedSessionHandle<Send<T, R>, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
                  "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
                  "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(detail::stop_class_compatible_v<Send<T, R>, C>,
                  "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
                  "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol = Send<T, R>;
    using message_type = T;
    using continuation = R;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    using inner_type = PermissionedSessionHandle<Send<T, R>, PS, Resource, LoopCtx>;
    using stop_type = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    constexpr CrashWatchedHandle(inner_type inner, OneShotFlag& flag,
                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Send<T, R>, CrashWatchedHandle<Send<T, R>, Resource, PeerTag, C, LoopCtx, PS>>{loc},
          inner_{std::move(inner)},
          flag_{&flag} {
        detail::require_crash_watched_contract_<Send<T, R>, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    template <typename Transport>
        requires std::is_invocable_v<Transport, Resource&, T&&>
    [[nodiscard]] constexpr auto send(T value, Transport transport) && -> std::expected<
        decltype(detail::wrap_crash_next_<PeerTag, C>(
            std::declval<inner_type>().send(std::move(value), std::move(transport)), std::declval<OneShotFlag&>())),
        detail::crash_event_for_t<PeerTag, Resource>> {
        // The peek is a relaxed load, so the happy path carries no fence.
        // The crash arm pays for one, and it pairs with the release the
        // signalling side performs.
        if (flag_->peek()) [[unlikely]] {
            std::atomic_thread_fence(std::memory_order_acquire);
            // The resource has to come out before the detach: detach ends the
            // inner handle, and nothing can be borrowed from it afterwards.
            // Every crash arm below repeats this order for that reason.
            Resource recovered = std::move(inner_.resource());
            this->mark_consumed_();
            return wrap_crash_return<PeerTag>(std::move(inner_), detach_reason::TransportClosedOutOfBand{},
                                              std::move(recovered));
        }
        auto next = std::move(inner_).send(std::move(value), std::move(transport));
        this->mark_consumed_();
        return detail::wrap_crash_next_<PeerTag, C>(std::move(next), *flag_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return *flag_; }
};

template <typename T, typename R, typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]] CrashWatchedHandle<Recv<T, R>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Recv<T, R>, CrashWatchedHandle<Recv<T, R>, Resource, PeerTag, C, LoopCtx, PS>> {
    PermissionedSessionHandle<Recv<T, R>, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
                  "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
                  "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(detail::stop_class_compatible_v<Recv<T, R>, C>,
                  "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
                  "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol = Recv<T, R>;
    using message_type = T;
    using continuation = R;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    using inner_type = PermissionedSessionHandle<Recv<T, R>, PS, Resource, LoopCtx>;
    using stop_type = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    constexpr CrashWatchedHandle(inner_type inner, OneShotFlag& flag,
                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Recv<T, R>, CrashWatchedHandle<Recv<T, R>, Resource, PeerTag, C, LoopCtx, PS>>{loc},
          inner_{std::move(inner)},
          flag_{&flag} {
        detail::require_crash_watched_contract_<Recv<T, R>, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    // The crash arm carries no message.  A receive that took it never had one
    // to hand back, so the pair exists only on the success side.
    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto recv(Transport transport) && -> std::expected<
        std::pair<T, decltype(detail::wrap_crash_next_<PeerTag, C>(
                         std::declval<inner_type>().recv(std::move(transport)).second, std::declval<OneShotFlag&>()))>,
        detail::crash_event_for_t<PeerTag, Resource>> {
        if (flag_->peek()) [[unlikely]] {
            std::atomic_thread_fence(std::memory_order_acquire);
            Resource recovered = std::move(inner_.resource());
            this->mark_consumed_();
            return wrap_crash_return<PeerTag>(std::move(inner_), detach_reason::TransportClosedOutOfBand{},
                                              std::move(recovered));
        }
        auto [value, next] = std::move(inner_).recv(std::move(transport));
        this->mark_consumed_();
        return std::pair{std::move(value), detail::wrap_crash_next_<PeerTag, C>(std::move(next), *flag_)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return *flag_; }
};

template <typename... Branches, typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]] CrashWatchedHandle<Select<Branches...>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Select<Branches...>,
                               CrashWatchedHandle<Select<Branches...>, Resource, PeerTag, C, LoopCtx, PS>> {
    PermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
                  "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
                  "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(detail::stop_class_compatible_v<Select<Branches...>, C>,
                  "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
                  "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol = Select<Branches...>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    using inner_type = PermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx>;
    using stop_type = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr CrashWatchedHandle(inner_type inner, OneShotFlag& flag,
                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Select<Branches...>,
                            CrashWatchedHandle<Select<Branches...>, Resource, PeerTag, C, LoopCtx, PS>>{loc},
          inner_{std::move(inner)},
          flag_{&flag} {
        detail::require_crash_watched_contract_<Select<Branches...>, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    template <std::size_t I, typename Transport>
        requires(I < sizeof...(Branches)) && std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) && -> std::expected<
        decltype(detail::wrap_crash_next_<PeerTag, C>(
            std::declval<inner_type>().template select<I>(std::move(transport)), std::declval<OneShotFlag&>())),
        detail::crash_event_for_t<PeerTag, Resource>> {
        if (flag_->peek()) [[unlikely]] {
            std::atomic_thread_fence(std::memory_order_acquire);
            Resource recovered = std::move(inner_.resource());
            this->mark_consumed_();
            return wrap_crash_return<PeerTag>(std::move(inner_), detach_reason::TransportClosedOutOfBand{},
                                              std::move(recovered));
        }
        auto next = std::move(inner_).template select<I>(std::move(transport));
        this->mark_consumed_();
        return detail::wrap_crash_next_<PeerTag, C>(std::move(next), *flag_);
    }

    // Nothing reaches the peer here, yet the crash arm still applies: a peer
    // that died before this call is a peer this handle must not keep
    // advancing against, whether or not the step would have touched the wire.
    template <std::size_t I>
        requires(I < sizeof...(Branches))
    [[nodiscard]] constexpr auto select_local() && -> std::expected<
        decltype(detail::wrap_crash_next_<PeerTag, C>(std::declval<inner_type>().template select_local<I>(),
                                                      std::declval<OneShotFlag&>())),
        detail::crash_event_for_t<PeerTag, Resource>> {
        if (flag_->peek()) [[unlikely]] {
            std::atomic_thread_fence(std::memory_order_acquire);
            Resource recovered = std::move(inner_.resource());
            this->mark_consumed_();
            return wrap_crash_return<PeerTag>(std::move(inner_), detach_reason::TransportClosedOutOfBand{},
                                              std::move(recovered));
        }
        auto next = std::move(inner_).template select_local<I>();
        this->mark_consumed_();
        return detail::wrap_crash_next_<PeerTag, C>(std::move(next), *flag_);
    }

    template <std::size_t I>
    void select() && = delete("[Wire_Variant_Required] CrashWatchedHandle<Select<...>>::"
                              "select<I>() without arguments is not available.  "
                              "Choose `select<I>(transport)` for the wire path, or "
                              "`select_local<I>()` for the in-memory variant.  See "
                              "SessionHandle<Select<...>>::select for the full discipline.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return *flag_; }
};

// Only the local pick is offered.  A transport-driven branch would have to
// read the peer's label out of a transport that may fail because the peer is
// the one that died, and the crash arm and the label arm are not separable
// without interposing on the transport, which this wrapper does not do.

template <typename... Branches, typename Resource, typename PeerTag, CrashClass C, typename LoopCtx, typename PS>
class [[nodiscard]] CrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, C, LoopCtx, PS>
    : public SessionHandleBase<Offer<Branches...>,
                               CrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, C, LoopCtx, PS>> {
    PermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx> inner_;
    OneShotFlag* flag_ = nullptr;

public:
    static_assert(detail::crash_watched_class_admissible_v<C>,
                  "crucible::session::diagnostic [CrashWatched_NoThrow_Rejected]: "
                  "CrashWatchedHandle cannot be parameterized with CrashClass::NoThrow.");
    static_assert(detail::stop_class_compatible_v<Offer<Branches...>, C>,
                  "crucible::session::diagnostic [CrashWatched_StopClass_Mismatch]: "
                  "CrashWatchedHandle CrashClass must match reachable Stop_g<C>.");

    using protocol = Offer<Branches...>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using peer = PeerTag;
    using perm_set = PS;
    using inner_type = PermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx>;
    using stop_type = Stop_g<C>;
    static constexpr CrashClass crash_class = C;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr CrashWatchedHandle(inner_type inner, OneShotFlag& flag,
                                 std::source_location loc = std::source_location::current()) noexcept
        : SessionHandleBase<Offer<Branches...>,
                            CrashWatchedHandle<Offer<Branches...>, Resource, PeerTag, C, LoopCtx, PS>>{loc},
          inner_{std::move(inner)},
          flag_{&flag} {
        detail::require_crash_watched_contract_<Offer<Branches...>, C>();
        detail::require_crash_survivors_declared_<PeerTag>();
    }

    constexpr CrashWatchedHandle(CrashWatchedHandle&&) noexcept = default;
    constexpr CrashWatchedHandle& operator=(CrashWatchedHandle&&) noexcept = default;
    ~CrashWatchedHandle() = default;

    // The branch is assumed, not received.  The crash arm still applies for
    // the same reason it does on the local select.
    template <std::size_t I>
        requires(I < sizeof...(Branches))
    [[nodiscard]] constexpr auto pick_local() && -> std::expected<
        decltype(detail::wrap_crash_next_<PeerTag, C>(std::declval<inner_type>().template pick_local<I>(),
                                                      std::declval<OneShotFlag&>())),
        detail::crash_event_for_t<PeerTag, Resource>> {
        if (flag_->peek()) [[unlikely]] {
            std::atomic_thread_fence(std::memory_order_acquire);
            Resource recovered = std::move(inner_.resource());
            this->mark_consumed_();
            return wrap_crash_return<PeerTag>(std::move(inner_), detach_reason::TransportClosedOutOfBand{},
                                              std::move(recovered));
        }
        auto next = std::move(inner_).template pick_local<I>();
        this->mark_consumed_();
        return detail::wrap_crash_next_<PeerTag, C>(std::move(next), *flag_);
    }

    template <std::size_t I>
    void pick() && = delete("[Wire_Variant_Required] CrashWatchedHandle<Offer<...>>::"
                            "pick<I>() without arguments is not available.  "
                            "Use `pick_local<I>()` to advance without receiving a peer "
                            "label, or call the peer-receiving variant when one is "
                            "available.  See SessionHandle<Offer<...>>::pick for the "
                            "full discipline.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr OneShotFlag& crash_flag() const noexcept { return *flag_; }
};

// Ownership of the incoming handle is the proof of authority, so these mints
// take no context.  The peer tag is explicit rather than deduced, because one
// handle can be watched against several peers and each watch is its own
// wrapper in the chain.
//
// The requires clause is satisfied by construction: the parameter type is
// already a handle.  It stays because it is the grep target that makes every
// authorisation point findable, and it does not stand in for the in-body
// asserts, which gate the crash grade and the survivor list instead.

template <typename PeerTag, CrashClass C = CrashClass::Abort, typename Proto, typename Resource, typename LoopCtx>
    requires ::crucible::safety::extract::IsSessionHandle<SessionHandle<Proto, Resource, LoopCtx>>
[[nodiscard]] constexpr auto mint_crash_watched_session(SessionHandle<Proto, Resource, LoopCtx> handle,
                                                        OneShotFlag& flag) noexcept {
    detail::require_crash_watched_contract_<Proto, C>();
    detail::require_crash_survivors_declared_<PeerTag>();

    Resource recovered = std::move(handle.resource());
    std::move(handle).detach(detach_reason::OwnerLifetimeBoundEarlyExit{});
    using PSLoopCtx = detail::permissioned_loop_ctx_from_bare_t<LoopCtx, EmptyPermSet>;
    return CrashWatchedHandle<Proto, Resource, PeerTag, C, PSLoopCtx, EmptyPermSet>{
        detail::permissioned_session_with_loc_<Proto, EmptyPermSet, Resource, PSLoopCtx>(
            std::move(recovered), std::source_location::current()),
        flag};
}

template <typename PeerTag, CrashClass C = CrashClass::Abort, typename Proto, typename PS, typename Resource,
          typename LoopCtx>
    requires ::crucible::safety::extract::IsSessionHandle<PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>>
[[nodiscard]] constexpr auto mint_crash_watched_session(PermissionedSessionHandle<Proto, PS, Resource, LoopCtx> handle,
                                                        OneShotFlag& flag) noexcept {
    detail::require_crash_watched_contract_<Proto, C>();
    detail::require_crash_survivors_declared_<PeerTag>();

    return CrashWatchedHandle<Proto, Resource, PeerTag, C, LoopCtx, PS>{std::move(handle), flag};
}

// The handler consumes the error, so this returns nothing.  Returning the
// expected would be the more chainable shape and is the reason the void is
// deliberate: the returned value would hold a moved-from error, and a caller
// reading it back or chaining on it would see a recovered resource that has
// already been handed to the handler.  Nothing in the type distinguishes that
// from a live one.  A caller who needs the success arm keeps its own
// reference to the expected across this call.

template <typename Expected, typename CrashHandler>
constexpr void on_crash(Expected&& result, CrashHandler&& handler) noexcept(
    std::is_nothrow_invocable_v<CrashHandler&&, typename std::remove_cvref_t<Expected>::error_type>) {
    using Error = typename std::remove_cvref_t<Expected>::error_type;
    static_assert(CrashEventMatchesSurvivors<Error>, "CrashEvent survivor list must match survivors_t<PeerTag>.");

    if (!result) {
        std::forward<CrashHandler>(handler)(std::move(result.error()));
    }
}

}  // namespace crucible::safety::proto
