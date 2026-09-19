#pragma once

// A PermissionedSessionHandle carries a type-level permission set beside the
// protocol state.  The set evolves on every send and receive according to the
// payload's flow marker:
//
//   Send<Transferable<T, X>, K>      drops X
//   Send<Returned<T, X>, K>          drops X
//   Send<Borrowed<T, X>, K>          unchanged, the lend is scoped
//   Send<DelegatedSession<P, S>, K>  drops every tag in S
//   Recv<Transferable<T, X>, K>      gains X
//   Recv<Returned<T, X>, K>          gains X
//   Recv<Borrowed<T, X>, K>          unchanged, the receiver gets a read view
//   Recv<DelegatedSession<P, S>, K>  gains every tag in S
//
// A plain payload never moves a tag.
//
// The handle derives from the session base through CRTP rather than holding a
// bare session handle by composition.  Composition would add a pointer to the
// inner handle, so the permissioned handle would no longer match the bare
// handle in size, and the base's abandonment diagnostic would name the inner
// wrapper instead of this one.

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/_VendorLattice.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/permissions/_PermissionFork.h>
#include <crucible/permissions/_PermSet.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/SessionPermPayloads.h>
#include <crucible/sessions/SessionSubtype.h>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <source_location>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

template <typename Body, typename EntryPS>
struct LoopContext {
    using body = Body;
    using entry_perm_set = EntryPS;
};

// Portable is the strongest provider claim, so a portable provider satisfies
// a vendor-specific consumer.  None is an uninitialized sentinel, rejected
// outright rather than admitted through the lattice's bottom rule.

using ::crucible::algebra::lattices::VendorBackend;
using ::crucible::algebra::lattices::VendorLattice;

template <VendorBackend V, typename InnerLoopCtx = void>
struct VendorCtx {
    using inner_loop_ctx = InnerLoopCtx;
    static constexpr VendorBackend vendor_backend = V;
};

template <VendorBackend V, typename InnerLoopCtx>
struct session_loop_ctx_traits<VendorCtx<V, InnerLoopCtx>> {
    using inner_loop_ctx = typename session_loop_ctx_traits<InnerLoopCtx>::inner_loop_ctx;
    static constexpr bool explicit_epoch = session_loop_ctx_traits<InnerLoopCtx>::explicit_epoch;
    static constexpr std::uint64_t current_epoch = session_loop_ctx_traits<InnerLoopCtx>::current_epoch;
    static constexpr std::uint64_t current_generation = session_loop_ctx_traits<InnerLoopCtx>::current_generation;
};

template <VendorBackend V, typename InnerLoopCtx, typename NewInnerLoopCtx>
struct session_loop_ctx_rebind_inner<VendorCtx<V, InnerLoopCtx>, NewInnerLoopCtx> {
    using type = VendorCtx<V, typename session_loop_ctx_rebind_inner<InnerLoopCtx, NewInnerLoopCtx>::type>;
};

template <typename LoopCtx>
struct loop_ctx_traits {
    using inner_loop_ctx = session_loop_ctx_inner_t<LoopCtx>;
    static constexpr VendorBackend vendor_backend = VendorBackend::Portable;
    static constexpr bool explicit_vendor = false;
    static constexpr bool explicit_epoch = session_loop_ctx_has_explicit_epoch_v<LoopCtx>;
    static constexpr std::uint64_t current_epoch = session_loop_ctx_epoch_v<LoopCtx>;
    static constexpr std::uint64_t current_generation = session_loop_ctx_generation_v<LoopCtx>;
};

template <std::uint64_t CurrentEpoch, std::uint64_t CurrentGeneration, typename InnerLoopCtx>
struct loop_ctx_traits<EpochCtx<CurrentEpoch, CurrentGeneration, InnerLoopCtx>> {
    using inner_loop_ctx = typename loop_ctx_traits<InnerLoopCtx>::inner_loop_ctx;
    static constexpr VendorBackend vendor_backend = loop_ctx_traits<InnerLoopCtx>::vendor_backend;
    static constexpr bool explicit_vendor = loop_ctx_traits<InnerLoopCtx>::explicit_vendor;
    static constexpr bool explicit_epoch = true;
    static constexpr std::uint64_t current_epoch = CurrentEpoch;
    static constexpr std::uint64_t current_generation = CurrentGeneration;
};

template <VendorBackend V, typename InnerLoopCtx>
struct loop_ctx_traits<VendorCtx<V, InnerLoopCtx>> {
    using inner_loop_ctx = typename loop_ctx_traits<InnerLoopCtx>::inner_loop_ctx;
    static constexpr VendorBackend vendor_backend = V;
    static constexpr bool explicit_vendor = true;
    static constexpr bool explicit_epoch = loop_ctx_traits<InnerLoopCtx>::explicit_epoch;
    static constexpr std::uint64_t current_epoch = loop_ctx_traits<InnerLoopCtx>::current_epoch;
    static constexpr std::uint64_t current_generation = loop_ctx_traits<InnerLoopCtx>::current_generation;
};

template <typename LoopCtx>
using loop_ctx_inner_t = typename loop_ctx_traits<LoopCtx>::inner_loop_ctx;

template <typename LoopCtx>
inline constexpr VendorBackend loop_ctx_vendor_v = loop_ctx_traits<LoopCtx>::vendor_backend;

template <typename LoopCtx>
inline constexpr bool loop_ctx_has_explicit_vendor_v = loop_ctx_traits<LoopCtx>::explicit_vendor;

template <typename LoopCtx>
inline constexpr bool loop_ctx_has_explicit_epoch_v = loop_ctx_traits<LoopCtx>::explicit_epoch;

template <typename LoopCtx>
inline constexpr std::uint64_t loop_ctx_epoch_v = loop_ctx_traits<LoopCtx>::current_epoch;

template <typename LoopCtx>
inline constexpr std::uint64_t loop_ctx_generation_v = loop_ctx_traits<LoopCtx>::current_generation;

template <typename LoopCtx, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
inline constexpr bool loop_ctx_epoch_satisfies_v =
    loop_ctx_has_explicit_epoch_v<LoopCtx> && MinEpoch <= loop_ctx_epoch_v<LoopCtx>
    && MinGeneration <= loop_ctx_generation_v<LoopCtx>;

template <typename LoopCtx>
using loop_ctx_as_vendor_ctx_t = VendorCtx<loop_ctx_vendor_v<LoopCtx>, loop_ctx_inner_t<LoopCtx>>;

template <typename LoopCtx, typename NewInnerLoopCtx>
struct loop_ctx_rebind_inner {
    using type = NewInnerLoopCtx;
};

template <VendorBackend V, typename InnerLoopCtx, typename NewInnerLoopCtx>
struct loop_ctx_rebind_inner<VendorCtx<V, InnerLoopCtx>, NewInnerLoopCtx> {
    using type = VendorCtx<V, typename loop_ctx_rebind_inner<InnerLoopCtx, NewInnerLoopCtx>::type>;
};

template <std::uint64_t CurrentEpoch, std::uint64_t CurrentGeneration, typename InnerLoopCtx, typename NewInnerLoopCtx>
struct loop_ctx_rebind_inner<EpochCtx<CurrentEpoch, CurrentGeneration, InnerLoopCtx>, NewInnerLoopCtx> {
    using type =
        EpochCtx<CurrentEpoch, CurrentGeneration, typename loop_ctx_rebind_inner<InnerLoopCtx, NewInnerLoopCtx>::type>;
};

template <typename LoopCtx, typename NewInnerLoopCtx>
using loop_ctx_rebind_inner_t = typename loop_ctx_rebind_inner<LoopCtx, NewInnerLoopCtx>::type;

template <VendorBackend Provider, VendorBackend Consumer>
inline constexpr bool session_vendor_satisfies_v =
    Provider != VendorBackend::None && Consumer != VendorBackend::None && VendorLattice::leq(Consumer, Provider);

template <typename ProviderLoopCtx, typename ConsumerLoopCtx>
inline constexpr bool loop_ctx_vendor_satisfies_v =
    session_vendor_satisfies_v<loop_ctx_vendor_v<ProviderLoopCtx>, loop_ctx_vendor_v<ConsumerLoopCtx>>;

template <typename Proto, typename PS, typename Resource, typename LoopCtx = void>
class PermissionedSessionHandle;

namespace detail {

struct permissioned_session_construct_key {
private:
    constexpr permissioned_session_construct_key() noexcept = default;

    template <typename R, typename PS2, typename Res, typename L>
    friend constexpr auto step_to_next_permissioned(Res, std::source_location) noexcept;

    template <typename Proto, typename InitialPS, typename Res, typename L>
    friend constexpr auto permissioned_session_with_loc_(Res, std::source_location) noexcept;

    template <typename P, typename PS2, typename Res, typename L>
    friend class ::crucible::safety::proto::PermissionedSessionHandle;
};

}  // namespace detail

template <typename ProviderHandle, typename ConsumerHandle>
inline constexpr bool permissioned_session_vendor_compatible_v =
    loop_ctx_vendor_satisfies_v<typename ProviderHandle::loop_ctx, typename ConsumerHandle::loop_ctx>;

template <typename ProviderHandle, typename ConsumerHandle>
consteval void assert_permissioned_session_vendor_compatible() {
    static_assert(permissioned_session_vendor_compatible_v<ProviderHandle, ConsumerHandle>,
                  "crucible::session::diagnostic [VendorCtx_Mismatch]: "
                  "PermissionedSessionHandle vendor composition rejected.  "
                  "VendorLattice::leq(consumer_vendor, provider_vendor) is false "
                  "or one side is VendorCtx<None>.  Portable providers may satisfy "
                  "vendor-specific consumers; distinct vendor-specific providers "
                  "such as NV and AMD are intentionally incomparable.");
}

// DelegatedSession is a payload, not a protocol head.  It names the inner
// protocol together with the permission set that travels with that endpoint.
template <typename InnerProto, typename InnerPS, typename K, typename LoopCtx>
struct is_well_formed<Delegate<DelegatedSession<InnerProto, InnerPS>, K>, LoopCtx>
    : std::bool_constant<is_well_formed<InnerProto, void>::value && is_well_formed<K, LoopCtx>::value> {};

template <typename InnerProto, typename InnerPS, typename K, typename LoopCtx>
struct is_well_formed<Accept<DelegatedSession<InnerProto, InnerPS>, K>, LoopCtx>
    : std::bool_constant<is_well_formed<InnerProto, void>::value && is_well_formed<K, LoopCtx>::value> {};

template <typename InnerProto, typename InnerPS, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          typename LoopCtx>
struct is_well_formed<EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>, LoopCtx>
    : std::bool_constant<session_epoch_threshold_valid_v<LoopCtx, MinEpoch, MinGeneration>
                         && is_well_formed<InnerProto, void>::value && is_well_formed<K, LoopCtx>::value
                         && (!session_loop_ctx_has_explicit_epoch_v<LoopCtx>
                             || session_loop_ctx_epoch_matches_v<LoopCtx, MinEpoch, MinGeneration>)> {};

template <typename InnerProto, typename InnerPS, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          typename LoopCtx>
struct is_well_formed<EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>, LoopCtx>
    : std::bool_constant<session_epoch_threshold_valid_v<LoopCtx, MinEpoch, MinGeneration>
                         && is_well_formed<InnerProto, void>::value && is_well_formed<K, LoopCtx>::value
                         && session_loop_ctx_epoch_satisfies_v<LoopCtx, MinEpoch, MinGeneration>> {};

namespace detail {

template <typename R, typename PS, typename Resource, typename LoopCtx>
[[nodiscard]] constexpr auto step_to_next_permissioned(Resource r, std::source_location loc) noexcept {
    if constexpr (std::is_same_v<R, Continue>) {
        using ActiveLoopCtx = loop_ctx_inner_t<LoopCtx>;

        // The base framework already rejects a Continue with no enclosing
        // Loop.  This check repeats it so the diagnostic names this handle.
        static_assert(!std::is_void_v<ActiveLoopCtx>, "crucible::session::diagnostic [Continue_Without_Loop]: "
                                                      "PermissionedSessionHandle: Continue appears outside any "
                                                      "enclosing Loop.  Wrap the protocol prefix containing "
                                                      "Continue in Loop<Body>, or replace Continue with End to "
                                                      "make the protocol one-shot.");

        using LoopBody = typename ActiveLoopCtx::body;
        using LoopEntryPS = typename ActiveLoopCtx::entry_perm_set;

        static_assert(perm_set_equal_v<PS, LoopEntryPS>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                         "PermissionedSessionHandle: Loop body's terminal PermSet "
                                                         "differs from the Loop entry PermSet — the iteration's "
                                                         "permission flow does not balance.  Each iteration of a "
                                                         "Loop must leave the PermSet exactly as it entered "
                                                         "(otherwise iteration N+1 would start in a state different "
                                                         "from iteration N, violating the invariant).  Either "
                                                         "surrender the leftover Transferable permissions before "
                                                         "Continue (via send<Returned<...>>), or restructure the "
                                                         "loop body to receive matching Returned permissions on "
                                                         "each iteration so the net PS evolution is zero.");

        return PermissionedSessionHandle<LoopBody, LoopEntryPS, Resource, LoopCtx>{permissioned_session_construct_key{},
                                                                                   std::forward<Resource>(r), loc};
    } else if constexpr (is_loop_v<R>) {
        using InnerBody = typename R::body;
        using InnerCtx = loop_ctx_rebind_inner_t<LoopCtx, LoopContext<InnerBody, PS>>;
        return PermissionedSessionHandle<InnerBody, PS, Resource, InnerCtx>{permissioned_session_construct_key{},
                                                                            std::forward<Resource>(r), loc};
    } else {
        return PermissionedSessionHandle<R, PS, Resource, LoopCtx>{permissioned_session_construct_key{},
                                                                   std::forward<Resource>(r), loc};
    }
}

template <typename R, typename PS, typename Resource, typename LoopCtx>
[[nodiscard]] constexpr auto step_to_next_permissioned(Resource r) noexcept {
    return step_to_next_permissioned<R, PS, Resource, LoopCtx>(std::forward<Resource>(r),
                                                               std::source_location::current());
}

template <typename PS>
inline void emit_leaked_permissions_debug() noexcept {
#ifndef NDEBUG
    if constexpr (PS::size > 0) {
        constexpr auto name = perm_set_name<PS>();
        std::fprintf(stderr,
                     "─────────────────────────────────────────────────────────────────────\n"
                     "[PermissionedSessionHandle] LEAKED PERMISSIONS (PS::size = %zu):\n"
                     "  %.*s\n"
                     "Each tag in the PermSet was acquired via Recv<Transferable<...>>\n"
                     "or Recv<Returned<...>> but never surrendered before the handle\n"
                     "was abandoned.  Surrender via Send<Returned<...>> back to the\n"
                     "origin or close the protocol with EmptyPermSet at End/Stop.\n"
                     "─────────────────────────────────────────────────────────────────────\n",
                     PS::size, static_cast<int>(name.size()), name.data());
    }
#endif
}

}  // namespace detail

template <typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<End, PS, Resource, LoopCtx>
    : public SessionHandleBase<End, PermissionedSessionHandle<End, PS, Resource, LoopCtx>> {
    Resource resource_;
    [[no_unique_address]] PS perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename R, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

public:
    using protocol = End;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_loop_ctx = loop_ctx_inner_t<LoopCtx>;
    using vendor_ctx = loop_ctx_as_vendor_ctx_t<LoopCtx>;
    static constexpr VendorBackend vendor_backend = loop_ctx_vendor_v<LoopCtx>;

    constexpr explicit PermissionedSessionHandle(
        detail::permissioned_session_construct_key, Resource r,
        std::source_location loc =
            std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<End, PermissionedSessionHandle<End, PS, Resource, LoopCtx>>{loc},
          resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    // The base exempts a terminal state from its own abandonment check, so
    // this destructor is the only place that reports a handle abandoned at
    // End while its permission set was still non-empty.
    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_()) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    [[nodiscard]] constexpr Resource close() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(perm_set_equal_v<PS, EmptyPermSet>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                          "PermissionedSessionHandle: reached End with a non-empty "
                                                          "PermSet — every permission acquired through the protocol "
                                                          "must be surrendered before close().  Either Send<Returned"
                                                          "<...>> the remaining permissions back to their origin, or "
                                                          "(if the protocol is genuinely one-shot consumption of the "
                                                          "permission) extend the protocol to surrender via send "
                                                          "before End.  Reaching End with leftover authority is "
                                                          "structurally a permission leak.");
        this->mark_consumed_();
        return std::forward<Resource>(resource_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <CrashClass C, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Stop_g<C>, PermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx>> {
    Resource resource_;
    [[no_unique_address]] PS perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename R, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

public:
    using protocol = Stop_g<C>;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_loop_ctx = loop_ctx_inner_t<LoopCtx>;
    using vendor_ctx = loop_ctx_as_vendor_ctx_t<LoopCtx>;
    static constexpr CrashClass crash_class = C;
    static constexpr VendorBackend vendor_backend = loop_ctx_vendor_v<LoopCtx>;

    constexpr explicit PermissionedSessionHandle(
        detail::permissioned_session_construct_key, Resource r,
        std::source_location loc =
            std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Stop_g<C>, PermissionedSessionHandle<Stop_g<C>, PS, Resource, LoopCtx>>{loc},
          resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_()) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    [[nodiscard]] constexpr Resource close() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(perm_set_equal_v<PS, EmptyPermSet>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                          "PermissionedSessionHandle<Stop>: reached Stop with a "
                                                          "non-empty PermSet.  The crash-stop discipline drops "
                                                          "permissions on the floor at Stop.  If that is the "
                                                          "intended behaviour, surrender the permissions "
                                                          "explicitly before Stop rather than relying on close to "
                                                          "do it implicitly.");
        this->mark_consumed_();
        return std::forward<Resource>(resource_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename T, typename R, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Send<T, R>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Send<T, R>, PermissionedSessionHandle<Send<T, R>, PS, Resource, LoopCtx>> {
    Resource resource_;
    [[no_unique_address]] PS perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

public:
    using protocol = Send<T, R>;
    using payload = T;
    using continuation = R;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_loop_ctx = loop_ctx_inner_t<LoopCtx>;
    using vendor_ctx = loop_ctx_as_vendor_ctx_t<LoopCtx>;
    static constexpr VendorBackend vendor_backend = loop_ctx_vendor_v<LoopCtx>;

    constexpr explicit PermissionedSessionHandle(
        detail::permissioned_session_construct_key, Resource r,
        std::source_location loc =
            std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Send<T, R>, PermissionedSessionHandle<Send<T, R>, PS, Resource, LoopCtx>>{loc},
          resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Send<T, R>>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    // The payload permission check is a body static_assert rather than a
    // requires clause.  A requires clause reports only that constraints were
    // not satisfied, losing the message that names the missing tag.  Transport
    // invocability stays in the requires clause, because a mismatch there is a
    // signature error rather than a permission-flow error.
    template <typename U = T, typename Transport>
        requires is_subsort_v<std::remove_cvref_t<U>, T> && std::is_invocable_v<Transport, Resource&, U&&>
    [[nodiscard]] constexpr auto
    send(U value, Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, U&&>
                                                   && std::is_nothrow_move_constructible_v<Resource>
                                                   && std::is_nothrow_move_constructible_v<U>) {
        static_assert(SendablePayload<T, PS>, "crucible::session::diagnostic [PermissionImbalance]: "
                                              "PermissionedSessionHandle::send: payload type T requires "
                                              "a permission tag the handle's PermSet does not contain.  "
                                              "Cases:\n"
                                              "  * Send<Transferable<T, X>, K>: sender must hold X "
                                              "(X must be in PS).\n"
                                              "  * Send<Returned<T, X>, K>: sender must hold X "
                                              "(X was previously borrowed and is being returned).\n"
                                              "  * Send<Borrowed<T, X>, K> and Send<Plain T, K>: "
                                              "always sendable (no permission demand).\n"
                                              "Verify the handle was minted (or evolved) with the "
                                              "permission you're trying to transfer.  If the protocol "
                                              "intends to lend (not transfer), wrap the payload in "
                                              "Borrowed<T, X> instead of Transferable<T, X>.");
        std::invoke(transport, resource_, std::move(value));
        this->mark_consumed_();
        using NextPS = compute_perm_set_after_send_t<PS, T>;
        return detail::step_to_next_permissioned<R, NextPS, Resource, LoopCtx>(std::forward<Resource>(resource_));
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename T, typename R, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Recv<T, R>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Recv<T, R>, PermissionedSessionHandle<Recv<T, R>, PS, Resource, LoopCtx>> {
    Resource resource_;
    [[no_unique_address]] PS perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

public:
    using protocol = Recv<T, R>;
    using payload = T;
    using continuation = R;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_loop_ctx = loop_ctx_inner_t<LoopCtx>;
    using vendor_ctx = loop_ctx_as_vendor_ctx_t<LoopCtx>;
    static constexpr VendorBackend vendor_backend = loop_ctx_vendor_v<LoopCtx>;

    constexpr explicit PermissionedSessionHandle(
        detail::permissioned_session_construct_key, Resource r,
        std::source_location loc =
            std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Recv<T, R>, PermissionedSessionHandle<Recv<T, R>, PS, Resource, LoopCtx>>{loc},
          resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Recv<T, R>>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    // The received value carries whatever permission tokens the sender bundled
    // into the payload.  The type-level set grows to match them.
    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto
    recv(Transport transport) && noexcept(std::is_nothrow_invocable_r_v<T, Transport, Resource&>
                                          && std::is_nothrow_move_constructible_v<Resource>
                                          && std::is_nothrow_move_constructible_v<T>) {
        T value = std::invoke(transport, resource_);
        this->mark_consumed_();
        using NextPS = compute_perm_set_after_recv_t<PS, T>;
        auto next = detail::step_to_next_permissioned<R, NextPS, Resource, LoopCtx>(std::forward<Resource>(resource_));
        return std::pair{std::move(value), std::move(next)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

// The carrier never absorbs the inner permission set.  The delegated endpoint
// owns those tokens, which is why the carrier's own set must stay disjoint
// from them.

template <typename InnerProto, typename InnerPS, typename K, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Delegate<DelegatedSession<InnerProto, InnerPS>, K>, PS, Resource, LoopCtx>
    : public SessionHandleBase<
          Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
          PermissionedSessionHandle<Delegate<DelegatedSession<InnerProto, InnerPS>, K>, PS, Resource, LoopCtx>> {
    using Protocol = Delegate<DelegatedSession<InnerProto, InnerPS>, K>;

    Resource resource_;
    [[no_unique_address]] PS perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

public:
    using protocol = Protocol;
    using delegated_proto = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set = InnerPS;
    using continuation = K;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_loop_ctx = loop_ctx_inner_t<LoopCtx>;
    using vendor_ctx = loop_ctx_as_vendor_ctx_t<LoopCtx>;
    static constexpr VendorBackend vendor_backend = loop_ctx_vendor_v<LoopCtx>;

    constexpr explicit PermissionedSessionHandle(
        detail::permissioned_session_construct_key, Resource r,
        std::source_location loc =
            std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Protocol, PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>>{loc},
          resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Protocol>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires(!is_stop_v<InnerProto> && std::is_invocable_v<Transport, Resource&, DelegatedResource &&>)
    [[nodiscard]] constexpr auto
    delegate(PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&& delegated,
             Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, DelegatedResource&&>
                                              && std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(perm_set_equal_v<ActualInnerPS, InnerPS>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::delegate: PermSet does not "
                      "contain inner_ps tokens declared by DelegatedSession<P, "
                      "InnerPS>.  The delegated handle's ActualInnerPS must "
                      "match InnerPS exactly; otherwise the handoff would either "
                      "fabricate missing authority or drop extra authority.");
        static_assert(perm_set_disjoint_v<PS, InnerPS>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                        "PermissionedSessionHandle::delegate: PermSet conflict -- "
                                                        "inner_ps already owned by the carrier handle.  The "
                                                        "delegated endpoint must be the unique owner of InnerPS "
                                                        "before handoff.");
        static_assert(!is_terminal_state_v<InnerProto> || perm_set_equal_v<InnerPS, EmptyPermSet>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::delegate: "
                      "is_permission_balanced_v<InnerProto> against InnerPS "
                      "rejects.  A terminal delegated protocol cannot carry a "
                      "non-empty inner_ps; close or rebalance the inner endpoint "
                      "before handoff.");

        std::invoke(transport, resource_, std::move(delegated.resource_));
        delegated.mark_consumed_();
        this->mark_consumed_();
        return detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
    }

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires is_stop_v<InnerProto>
    void delegate(PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&&,
                  Transport) && = delete("[DelegateStop_NoContinuation] PermissionedSessionHandle<"
                                         "Delegate<DelegatedSession<Stop, InnerPS>, K>> cannot "
                                         "delegate an already-crashed endpoint and continue as K.  "
                                         "Handle Stop/crash before this permissioned handoff point.");

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx>
        requires(!is_stop_v<InnerProto>)
    [[nodiscard]] constexpr auto
    delegate_local(PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&&
                       delegated) && noexcept(std::is_nothrow_move_constructible_v<Resource>
                                              && std::is_nothrow_destructible_v<DelegatedResource>) {
        static_assert(perm_set_equal_v<ActualInnerPS, InnerPS>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::delegate_local: PermSet does "
                      "not contain inner_ps tokens declared by DelegatedSession"
                      "<P, InnerPS>.");
        static_assert(perm_set_disjoint_v<PS, InnerPS>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                        "PermissionedSessionHandle::delegate_local: PermSet "
                                                        "conflict -- inner_ps already owned by the carrier "
                                                        "handle.");
        static_assert(!is_terminal_state_v<InnerProto> || perm_set_equal_v<InnerPS, EmptyPermSet>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::delegate_local: "
                      "is_permission_balanced_v<InnerProto> against InnerPS "
                      "rejects.");
        delegated.mark_consumed_();
        (void)std::move(delegated);
        this->mark_consumed_();
        return detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
    }

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx>
    void delegate(PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&&) && =
        delete("[Wire_Variant_Required] PermissionedSessionHandle<Delegate<"
               "DelegatedSession<P, InnerPS>, K>>::delegate(handle) without "
               "a transport is not allowed.  Choose delegate(handle, "
               "transport) for wire handoff or delegate_local(handle) for "
               "explicit in-memory tests.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

// The sender side demands an exact epoch and generation match, not a minimum.
// A sender free to declare a minimum below the epoch it actually runs at could
// weaken the handoff until stale peers satisfied it.  The recipient side, by
// contrast, admits any epoch at or above the minimum.

template <typename InnerProto, typename InnerPS, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
PermissionedSessionHandle<EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>, PS,
                          Resource, LoopCtx>
    : public SessionHandleBase<
          EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>,
          PermissionedSessionHandle<EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>,
                                    PS, Resource, LoopCtx>> {
    using Protocol = EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>;

    Resource resource_;
    [[no_unique_address]] PS perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

    static_assert(session_loop_ctx_epoch_matches_v<LoopCtx, MinEpoch, MinGeneration>,
                  "crucible::session::diagnostic [EpochCtx_StaleSender]: "
                  "PermissionedSessionHandle<EpochedDelegate<...>> requires "
                  "LoopCtx = EpochCtx<CurrentEpoch, CurrentGeneration, ...> with "
                  "CurrentEpoch == MinEpoch and CurrentGeneration == MinGeneration. "
                  "A sender cannot mint a reshard handoff from a stale or weakened "
                  "epoch context.");

public:
    using protocol = Protocol;
    using delegated_proto = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set = InnerPS;
    using continuation = K;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_loop_ctx = loop_ctx_inner_t<LoopCtx>;
    using vendor_ctx = loop_ctx_as_vendor_ctx_t<LoopCtx>;
    static constexpr VendorBackend vendor_backend = loop_ctx_vendor_v<LoopCtx>;
    static constexpr std::uint64_t min_epoch = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;

    constexpr explicit PermissionedSessionHandle(
        detail::permissioned_session_construct_key, Resource r,
        std::source_location loc =
            std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Protocol, PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>>{loc},
          resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Protocol>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires(!is_stop_v<InnerProto> && std::is_invocable_v<Transport, Resource&, DelegatedResource &&>)
    [[nodiscard]] constexpr auto
    delegate(PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&& delegated,
             Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, DelegatedResource&&>
                                              && std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(perm_set_equal_v<ActualInnerPS, InnerPS>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::delegate: PermSet does not "
                      "contain inner_ps tokens declared by DelegatedSession<P, "
                      "InnerPS>.");
        static_assert(perm_set_disjoint_v<PS, InnerPS>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                        "PermissionedSessionHandle::delegate: PermSet conflict -- "
                                                        "inner_ps already owned by the carrier handle.");
        static_assert(!is_terminal_state_v<InnerProto> || perm_set_equal_v<InnerPS, EmptyPermSet>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::delegate: "
                      "is_permission_balanced_v<InnerProto> against InnerPS "
                      "rejects.");

        std::invoke(transport, resource_, std::move(delegated.resource_));
        delegated.mark_consumed_();
        this->mark_consumed_();
        return detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
    }

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires is_stop_v<InnerProto>
    void delegate(PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&&,
                  Transport) && = delete("[DelegateStop_NoContinuation] PermissionedSessionHandle<"
                                         "EpochedDelegate<DelegatedSession<Stop, InnerPS>, K, "
                                         "MinEpoch, MinGeneration>> cannot delegate an already-crashed "
                                         "endpoint and continue as K.  Handle Stop/crash before this "
                                         "epoch-versioned permissioned handoff point.");

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx>
        requires(!is_stop_v<InnerProto>)
    [[nodiscard]] constexpr auto
    delegate_local(PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&&
                       delegated) && noexcept(std::is_nothrow_move_constructible_v<Resource>
                                              && std::is_nothrow_destructible_v<DelegatedResource>) {
        static_assert(perm_set_equal_v<ActualInnerPS, InnerPS>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::delegate_local: PermSet does "
                      "not contain inner_ps tokens declared by DelegatedSession"
                      "<P, InnerPS>.");
        static_assert(perm_set_disjoint_v<PS, InnerPS>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                        "PermissionedSessionHandle::delegate_local: PermSet "
                                                        "conflict -- inner_ps already owned by the carrier handle.");
        static_assert(!is_terminal_state_v<InnerProto> || perm_set_equal_v<InnerPS, EmptyPermSet>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::delegate_local: "
                      "is_permission_balanced_v<InnerProto> against InnerPS "
                      "rejects.");

        delegated.mark_consumed_();
        (void)std::move(delegated);
        this->mark_consumed_();
        return detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
    }

    template <typename ActualInnerPS, typename DelegatedResource, typename DelegatedLoopCtx>
    void delegate(PermissionedSessionHandle<InnerProto, ActualInnerPS, DelegatedResource, DelegatedLoopCtx>&&) && =
        delete("[Wire_Variant_Required] PermissionedSessionHandle<EpochedDelegate<"
               "DelegatedSession<P, InnerPS>, K, MinEpoch, MinGeneration>>::"
               "delegate(handle) without a transport is not allowed.  Choose "
               "delegate(handle, transport) for wire handoff or delegate_local"
               "(handle) for explicit in-memory tests.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename InnerProto, typename InnerPS, typename K, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Accept<DelegatedSession<InnerProto, InnerPS>, K>, PS, Resource, LoopCtx>
    : public SessionHandleBase<
          Accept<DelegatedSession<InnerProto, InnerPS>, K>,
          PermissionedSessionHandle<Accept<DelegatedSession<InnerProto, InnerPS>, K>, PS, Resource, LoopCtx>> {
    using Protocol = Accept<DelegatedSession<InnerProto, InnerPS>, K>;

    Resource resource_;
    [[no_unique_address]] PS perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

public:
    using protocol = Protocol;
    using delegated_proto = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set = InnerPS;
    using continuation = K;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_loop_ctx = loop_ctx_inner_t<LoopCtx>;
    using vendor_ctx = loop_ctx_as_vendor_ctx_t<LoopCtx>;
    static constexpr VendorBackend vendor_backend = loop_ctx_vendor_v<LoopCtx>;

    constexpr explicit PermissionedSessionHandle(
        detail::permissioned_session_construct_key, Resource r,
        std::source_location loc =
            std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Protocol, PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>>{loc},
          resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Protocol>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    template <typename Transport, typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto
    accept(Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&>
                                            && std::is_nothrow_move_constructible_v<Resource>
                                            && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        static_assert(perm_set_disjoint_v<PS, InnerPS>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                        "PermissionedSessionHandle::accept: PermSet conflict -- "
                                                        "inner_ps already owned by the carrier handle.  Accepting "
                                                        "DelegatedSession<P, InnerPS> would duplicate a CSL "
                                                        "permission token; split the authority into distinct tags "
                                                        "or remove the overlapping carrier permission before "
                                                        "accepting the endpoint.");
        static_assert(!is_terminal_state_v<InnerProto> || perm_set_equal_v<InnerPS, EmptyPermSet>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::accept: "
                      "is_permission_balanced_v<InnerProto> against InnerPS "
                      "rejects.  A terminal delegated protocol cannot carry a "
                      "non-empty inner_ps.");

        DelegatedResource delegated_res = std::invoke(transport, resource_);
        this->mark_consumed_();
        PermissionedSessionHandle<InnerProto, InnerPS, DelegatedResource, void> delegated_handle{
            detail::permissioned_session_construct_key{}, std::move(delegated_res)};
        auto continuation_handle =
            detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
        return std::pair{std::move(delegated_handle), std::move(continuation_handle)};
    }

    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto accept_with(DelegatedResource delegated_res) && noexcept(
        std::is_nothrow_move_constructible_v<Resource> && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        static_assert(perm_set_disjoint_v<PS, InnerPS>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                        "PermissionedSessionHandle::accept_with: PermSet conflict "
                                                        "-- inner_ps already owned by the carrier handle.");
        static_assert(!is_terminal_state_v<InnerProto> || perm_set_equal_v<InnerPS, EmptyPermSet>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::accept_with: "
                      "is_permission_balanced_v<InnerProto> against InnerPS "
                      "rejects.");

        this->mark_consumed_();
        PermissionedSessionHandle<InnerProto, InnerPS, DelegatedResource, void> delegated_handle{
            detail::permissioned_session_construct_key{}, std::move(delegated_res)};
        auto continuation_handle =
            detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
        return std::pair{std::move(delegated_handle), std::move(continuation_handle)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename InnerProto, typename InnerPS, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]]
PermissionedSessionHandle<EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>, PS,
                          Resource, LoopCtx>
    : public SessionHandleBase<
          EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>,
          PermissionedSessionHandle<EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>,
                                    PS, Resource, LoopCtx>> {
    using Protocol = EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>;

    Resource resource_;
    [[no_unique_address]] PS perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

    static_assert(session_loop_ctx_epoch_satisfies_v<LoopCtx, MinEpoch, MinGeneration>,
                  "crucible::session::diagnostic [EpochCtx_StaleRecipient]: "
                  "PermissionedSessionHandle<EpochedAccept<...>> requires "
                  "LoopCtx = EpochCtx<CurrentEpoch, CurrentGeneration, ...> with "
                  "CurrentEpoch >= MinEpoch and CurrentGeneration >= MinGeneration. "
                  "A stale or unannotated recipient cannot accept this delegated "
                  "reshard endpoint.");

public:
    using protocol = Protocol;
    using delegated_proto = InnerProto;
    using delegated_payload = DelegatedSession<InnerProto, InnerPS>;
    using inner_perm_set = InnerPS;
    using continuation = K;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_loop_ctx = loop_ctx_inner_t<LoopCtx>;
    using vendor_ctx = loop_ctx_as_vendor_ctx_t<LoopCtx>;
    static constexpr VendorBackend vendor_backend = loop_ctx_vendor_v<LoopCtx>;
    static constexpr std::uint64_t min_epoch = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;

    constexpr explicit PermissionedSessionHandle(
        detail::permissioned_session_construct_key, Resource r,
        std::source_location loc =
            std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Protocol, PermissionedSessionHandle<Protocol, PS, Resource, LoopCtx>>{loc},
          resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Protocol>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    template <typename Transport, typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto
    accept(Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&>
                                            && std::is_nothrow_move_constructible_v<Resource>
                                            && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        static_assert(perm_set_disjoint_v<PS, InnerPS>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                        "PermissionedSessionHandle::accept: PermSet conflict -- "
                                                        "inner_ps already owned by the carrier handle.  Accepting "
                                                        "DelegatedSession<P, InnerPS> would duplicate a CSL "
                                                        "permission token.");
        static_assert(!is_terminal_state_v<InnerProto> || perm_set_equal_v<InnerPS, EmptyPermSet>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::accept: "
                      "is_permission_balanced_v<InnerProto> against InnerPS "
                      "rejects.  A terminal delegated protocol cannot carry a "
                      "non-empty inner_ps.");

        DelegatedResource delegated_res = std::invoke(transport, resource_);
        this->mark_consumed_();
        PermissionedSessionHandle<InnerProto, InnerPS, DelegatedResource, void> delegated_handle{
            detail::permissioned_session_construct_key{}, std::move(delegated_res)};
        auto continuation_handle =
            detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
        return std::pair{std::move(delegated_handle), std::move(continuation_handle)};
    }

    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto accept_with(DelegatedResource delegated_res) && noexcept(
        std::is_nothrow_move_constructible_v<Resource> && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        static_assert(perm_set_disjoint_v<PS, InnerPS>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                        "PermissionedSessionHandle::accept_with: PermSet conflict "
                                                        "-- inner_ps already owned by the carrier handle.");
        static_assert(!is_terminal_state_v<InnerProto> || perm_set_equal_v<InnerPS, EmptyPermSet>,
                      "crucible::session::diagnostic [PermissionImbalance]: "
                      "PermissionedSessionHandle::accept_with: "
                      "is_permission_balanced_v<InnerProto> against InnerPS "
                      "rejects.");

        this->mark_consumed_();
        PermissionedSessionHandle<InnerProto, InnerPS, DelegatedResource, void> delegated_handle{
            detail::permissioned_session_construct_key{}, std::move(delegated_res)};
        auto continuation_handle =
            detail::step_to_next_permissioned<K, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
        return std::pair{std::move(delegated_handle), std::move(continuation_handle)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename ProtoBase, typename ProtoRollback, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, PS, Resource, LoopCtx>
    : public SessionHandleBase<
          CheckpointedSession<ProtoBase, ProtoRollback>,
          PermissionedSessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, PS, Resource, LoopCtx>> {
    Resource resource_;
    [[no_unique_address]] PS perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

public:
    using protocol = CheckpointedSession<ProtoBase, ProtoRollback>;
    using base_protocol = ProtoBase;
    using rollback_protocol = ProtoRollback;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_loop_ctx = loop_ctx_inner_t<LoopCtx>;
    using vendor_ctx = loop_ctx_as_vendor_ctx_t<LoopCtx>;
    static constexpr VendorBackend vendor_backend = loop_ctx_vendor_v<LoopCtx>;

    constexpr explicit PermissionedSessionHandle(
        detail::permissioned_session_construct_key, Resource r,
        std::source_location loc =
            std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<
              CheckpointedSession<ProtoBase, ProtoRollback>,
              PermissionedSessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, PS, Resource, LoopCtx>>{loc},
          resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_()) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    [[nodiscard]] constexpr auto base() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->mark_consumed_();
        return detail::step_to_next_permissioned<ProtoBase, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
    }

    [[nodiscard]] constexpr auto rollback() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->mark_consumed_();
        return detail::step_to_next_permissioned<ProtoRollback, PS, Resource, LoopCtx>(
            std::forward<Resource>(resource_));
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

// Branch convergence needs no check of its own.  Every branch ends at a
// terminal head that already constrains the permission set: close demands an
// empty set, and Continue demands the loop entry set.  Branches meeting at the
// same terminal agree by construction.  A convergence metafunction
// would have to be extended for every branch added, while the terminal heads
// absorb new branches unchanged.

template <typename... Branches, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Select<Branches...>,
                               PermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx>> {
    Resource resource_;
    [[no_unique_address]] PS perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

public:
    using protocol = Select<Branches...>;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_loop_ctx = loop_ctx_inner_t<LoopCtx>;
    using vendor_ctx = loop_ctx_as_vendor_ctx_t<LoopCtx>;
    static constexpr VendorBackend vendor_backend = loop_ctx_vendor_v<LoopCtx>;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    static_assert(branch_count > 0, "crucible::session::diagnostic [Empty_Choice_Combinator]: "
                                    "PermissionedSessionHandle<Select<>>: cannot construct a "
                                    "runnable handle on Select<> with zero branches.");

    constexpr explicit PermissionedSessionHandle(
        detail::permissioned_session_construct_key, Resource r,
        std::source_location loc =
            std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Select<Branches...>, PermissionedSessionHandle<Select<Branches...>, PS, Resource, LoopCtx>>{
              loc},
          resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Select<Branches...>>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    template <std::size_t I, typename Transport>
        requires std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto
    select(Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, std::size_t>
                                            && std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(I < sizeof...(Branches), "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                               "PermissionedSessionHandle<Select<...>>::select<I>(transport): "
                                               "branch index I is out of range.");
        std::invoke(transport, resource_, I);
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next_permissioned<Chosen, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
    }

    template <std::size_t I>
    [[nodiscard]] constexpr auto select_local() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(I < sizeof...(Branches), "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                               "PermissionedSessionHandle<Select<...>>::select_local<I>(): "
                                               "branch index I is out of range.");
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next_permissioned<Chosen, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
    }

    template <std::size_t I>
    void select() && = delete("[Wire_Variant_Required] PermissionedSessionHandle<Select<...>>"
                              "::select<I>() without arguments is not allowed.  Choose "
                              "select<I>(transport) for wire-based sessions or "
                              "select_local<I>() for in-memory channels.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

// Branch convergence holds for the same reason it holds on the sending side:
// every branch ends at a terminal head that constrains the permission set.

template <typename... Branches, typename PS, typename Resource, typename LoopCtx>
class [[nodiscard]] PermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx>
    : public SessionHandleBase<Offer<Branches...>,
                               PermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx>> {
    Resource resource_;
    [[no_unique_address]] PS perm_set_;

    template <typename P, typename PS2, typename R2, typename L2>
    friend class PermissionedSessionHandle;

    template <typename U, typename PS2, typename Res, typename L>
    friend constexpr auto detail::step_to_next_permissioned(Res, std::source_location) noexcept;

public:
    using protocol = Offer<Branches...>;
    using perm_set = PS;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    using inner_loop_ctx = loop_ctx_inner_t<LoopCtx>;
    using vendor_ctx = loop_ctx_as_vendor_ctx_t<LoopCtx>;
    static constexpr VendorBackend vendor_backend = loop_ctx_vendor_v<LoopCtx>;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    static_assert(branch_count > 0, "crucible::session::diagnostic [Empty_Choice_Combinator]: "
                                    "PermissionedSessionHandle<Offer<>>: cannot construct a "
                                    "runnable handle on Offer<> with zero branches.");

    constexpr explicit PermissionedSessionHandle(
        detail::permissioned_session_construct_key, Resource r,
        std::source_location loc =
            std::source_location::current()) noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Offer<Branches...>, PermissionedSessionHandle<Offer<Branches...>, PS, Resource, LoopCtx>>{
              loc},
          resource_{std::forward<Resource>(r)} {}

    constexpr PermissionedSessionHandle(PermissionedSessionHandle&&) noexcept = default;
    constexpr PermissionedSessionHandle& operator=(PermissionedSessionHandle&&) noexcept = default;

    ~PermissionedSessionHandle() {
#ifndef NDEBUG
        if (!this->is_consumed_() && !is_terminal_state_v<Offer<Branches...>>) {
            detail::emit_leaked_permissions_debug<PS>();
        }
#endif
    }

    template <typename Transport, typename Handler>
        requires std::is_invocable_r_v<std::size_t, Transport, Resource&>
    constexpr auto branch(Transport transport, Handler handler) && {
        const std::size_t idx = std::invoke(transport, resource_);
        this->mark_consumed_();
        return dispatch_branch_(idx, std::forward<Resource>(resource_), std::move(handler),
                                std::make_index_sequence<sizeof...(Branches)>{});
    }

    template <std::size_t I>
    [[nodiscard]] constexpr auto pick_local() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        static_assert(I < sizeof...(Branches), "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
                                               "PermissionedSessionHandle<Offer<...>>::pick_local<I>(): "
                                               "branch index I is out of range.");
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next_permissioned<Chosen, PS, Resource, LoopCtx>(std::forward<Resource>(resource_));
    }

    template <std::size_t I>
    void pick() && = delete("[Wire_Variant_Required] PermissionedSessionHandle<Offer<...>>"
                            "::pick<I>() without arguments is not allowed.  Use "
                            "pick_local<I>() for in-memory channels or "
                            "branch(transport, handler) for wire-based sessions.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }

private:
    template <std::size_t I>
    static constexpr auto make_branch_handle_(Resource r) {
        using B = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next_permissioned<B, PS, Resource, LoopCtx>(std::forward<Resource>(r));
    }

    template <std::size_t... Is, typename Handler>
    static constexpr auto dispatch_branch_(std::size_t idx, Resource res, Handler handler, std::index_sequence<Is...>) {
        if (idx >= sizeof...(Branches)) [[unlikely]] {
            std::abort();
        }

        using FirstHandle = decltype(make_branch_handle_<0>(std::declval<Resource>()));
        using Result = std::invoke_result_t<Handler&&, FirstHandle>;

        if constexpr (std::is_void_v<Result>) {
            bool dispatched = false;
            (
                [&]() {
                    if (!dispatched && idx == Is) {
                        std::invoke(std::move(handler), make_branch_handle_<Is>(std::forward<Resource>(res)));
                        dispatched = true;
                    }
                }(),
                ...);
        } else {
            std::optional<Result> result;
            bool dispatched = false;
            (
                [&]() {
                    if (!dispatched && idx == Is) {
                        result.emplace(
                            std::invoke(std::move(handler), make_branch_handle_<Is>(std::forward<Resource>(res))));
                        dispatched = true;
                    }
                }(),
                ...);
            if (!result) [[unlikely]]
                std::abort();
            return std::move(*result);
        }
    }
};

// The public construction surface is the ctx-bound mint.  It consumes the
// caller's Permission tokens and passes the resulting set here as a type.
// This layer takes no tokens, so it mints authority it cannot check, which is
// why it stays in detail.

namespace detail {

template <typename Proto, typename InitialPS, typename Resource, typename LoopCtx>
[[nodiscard]] constexpr auto permissioned_session_with_loc_(Resource r, std::source_location loc) noexcept {
    static_assert(is_well_formed<Proto, LoopCtx>::value, "crucible::session::diagnostic [Protocol_Ill_Formed]: "
                                                         "mint_permissioned_session<Proto>: protocol is ill-formed.");
    static_assert(!is_empty_choice_v<Proto>, "crucible::session::diagnostic [Empty_Choice_Combinator]: "
                                             "mint_permissioned_session<Proto>: cannot construct a runnable "
                                             "handle on Proto with a reachable empty Select<> / Offer<> / "
                                             "Offer<Sender<R>> (top-level or nested under Send/Recv/Loop/"
                                             "branch/Delegate/Accept).  The is_empty_choice trait walks "
                                             "recursively, so empty choices anywhere in the protocol tree "
                                             "are caught at mint time, not at the eventual .pick<I>() / "
                                             ".recv() that hits the dead-end.");
    static_assert(SessionResource<Resource>, "crucible::session::diagnostic [SessionResource_NotPinned]: "
                                             "mint_permissioned_session<Proto, Resource>: Resource fails the "
                                             "pin-discipline required by the SessionResource concept.");

    if constexpr (is_vendor_pinned_v<Proto>) {
        using InnerProto = protocol_inner_t<Proto>;
        using VendorLoopCtx = VendorCtx<protocol_vendor_v<Proto>, loop_ctx_inner_t<LoopCtx>>;
        return permissioned_session_with_loc_<InnerProto, InitialPS, Resource, VendorLoopCtx>(std::forward<Resource>(r),
                                                                                              loc);
    } else if constexpr (is_loop_v<Proto>) {
        using Body = typename Proto::body;
        using Ctx = loop_ctx_rebind_inner_t<LoopCtx, LoopContext<Body, InitialPS>>;
        return step_to_next_permissioned<Body, InitialPS, Resource, Ctx>(std::forward<Resource>(r), loc);
    } else {
        static_assert(!std::is_same_v<Proto, Continue>, "crucible::session::diagnostic [Continue_Without_Loop]: "
                                                        "mint_permissioned_session<Continue>: Continue cannot be the "
                                                        "top-level protocol.");
        return PermissionedSessionHandle<Proto, InitialPS, Resource, LoopCtx>{permissioned_session_construct_key{},
                                                                              std::forward<Resource>(r), loc};
    }
}

template <typename Proto, typename InitialPS, typename Resource>
[[nodiscard]] constexpr auto permissioned_session_with_loc_(Resource r, std::source_location loc) noexcept {
    return permissioned_session_with_loc_<Proto, InitialPS, Resource, void>(std::forward<Resource>(r), loc);
}

}  // namespace detail

// Detaching on a crash drops the type-level permission set.  Permissions are
// not recovered from a crashed peer, so every tag the set still held is gone
// and the value-level tokens destruct with the handle.  The resource goes with
// the handle too, so a caller that wants the channel back must hold its own
// reference to it.
//
// The peek is relaxed rather than a full acquiring read.  That is sound
// because a crash flag is monotonic: once set it is never cleared for the rest
// of the session, so a stale read can only be early, never wrong.

template <typename PSH, typename Body>
    requires std::is_invocable_v<Body, PSH&&>
[[nodiscard]] constexpr auto with_crash_check_or_detach(PSH&& h, ::crucible::safety::OneShotFlag& flag,
                                                        Body&& body) noexcept(std::is_nothrow_invocable_v<Body, PSH&&>)
    -> std::optional<std::invoke_result_t<Body, PSH&&>> {
    if (flag.peek()) [[unlikely]] {
        // The fence pairs with the release store in the flag's signal, so
        // state the producer mutated before signalling is visible here.
        std::atomic_thread_fence(std::memory_order_acquire);
        std::move(h).detach(detach_reason::TransportClosedOutOfBand{});
        return std::nullopt;
    }
    return std::optional{std::forward<Body>(body)(std::forward<PSH>(h))};
}

// Spawns one thread per role, each running that role's projection of the
// global type with the role's permission as its initial set, and rebuilds the
// whole permission once every role has joined.
//
// Each role body must drive its projected protocol to a terminal state.  It
// either surrenders its role permission through the protocol before close, or
// detaches to drop the set without the close-time surrender check.  A role
// whose projection never sends the permission back over the wire uses the
// second form: there the permission is proof of participation, and the type
// system checks only that the role held it for the duration of its session.
//
// The shared channel is taken by reference, so every role runs in this
// process.

namespace detail {

// session_fork has already checked the construction, so the role endpoint
// comes from the detail factory rather than from the ctx-bound mint, whose
// header this one cannot include without a cycle.
template <typename G, typename Role, typename SharedChannel, typename Body>
[[nodiscard]] constexpr auto session_fork_role_lambda(SharedChannel& ch, Body&& body) noexcept {
    return [&ch, body = std::forward<Body>(body)](Permission<Role>&& role_perm, auto const&) mutable noexcept {
        using LocalProto = typename Project<G, Role>::type;
        // The role token is dropped here.  Its authority is re-expressed as
        // the endpoint's type-level permission set.
        static_cast<void>(role_perm);
        auto handle = permissioned_session_with_loc_<LocalProto, PermSet<Role>, SharedChannel&, void>(
            ch, std::source_location::current());
        std::move(body)(std::move(handle));
    };
}

}  // namespace detail

template <typename G, typename Whole, typename... RolePerms, typename SharedChannel, typename... Bodies>
[[nodiscard]] Permission<Whole> session_fork(SharedChannel& ch, Permission<Whole>&& whole_perm,
                                             Bodies&&... bodies) noexcept {
    static_assert(is_global_well_formed_v<G>, "crucible::session::diagnostic [Protocol_Ill_Formed]: "
                                              "session_fork<G, ...>: global type G is ill-formed.");
    static_assert(sizeof...(RolePerms) == sizeof...(Bodies),
                  "crucible::session::diagnostic [PermissionImbalance]: "
                  "session_fork: number of RolePerms template arguments must "
                  "match number of body callables.");
    static_assert(splits_into_pack_v<Whole, RolePerms...>,
                  "crucible::session::diagnostic [PermissionImbalance]: "
                  "session_fork<G, Whole, RolePerms...>: requires "
                  "splits_into_pack<Whole, RolePerms...>::value true.  Declare "
                  "the manifest in the same TU as the Whole and Role tags so "
                  "reviewers see the entire region tree at one glance.");
    static_assert(SessionResource<SharedChannel&>, "crucible::session::diagnostic [SessionResource_NotPinned]: "
                                                   "session_fork: the SharedChannel must be Pinned (its address "
                                                   "must be stable across the spawned threads' lifetimes).  "
                                                   "Derive your channel from safety::Pinned<ChannelType>.");

    return mint_permission_fork<RolePerms...>(
        PermissionForkSpawnCtx{}, std::move(whole_perm),
        detail::session_fork_role_lambda<G, RolePerms, SharedChannel, Bodies>(ch, std::forward<Bodies>(bodies))...);
}

}  // namespace crucible::safety::proto

namespace crucible::safety::proto::detail::permissioned_session_smoke {

struct WorkPerm {};
struct HotPerm {};

// A value resource does not have to be pinned.
struct FakeChannel {
    int last_sent = 0;
};

// The size assertions demand equality rather than an upper bound.  Equality is
// what catches a permission set or a loop context that gains a non-empty
// member, or a dropped [[no_unique_address]].

static_assert(std::is_empty_v<EmptyPermSet>);
static_assert(std::is_empty_v<PermSet<WorkPerm>>);
static_assert(std::is_empty_v<PermSet<WorkPerm, HotPerm>>);

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, FakeChannel>)
              == sizeof(SessionHandle<End, FakeChannel>));

static_assert(sizeof(PermissionedSessionHandle<End, PermSet<WorkPerm, HotPerm>, FakeChannel>)
              == sizeof(SessionHandle<End, FakeChannel>));

static_assert(sizeof(PermissionedSessionHandle<Send<int, End>, EmptyPermSet, FakeChannel>)
              == sizeof(SessionHandle<Send<int, End>, FakeChannel>));

using WorkChannel = FakeChannel;
using SendProto = Send<Transferable<int, WorkPerm>, End>;
using PSWith = PermSet<WorkPerm>;
using PSWithout = EmptyPermSet;
using DelegatedPayload = DelegatedSession<SendProto, PSWith>;
using DelegateProto = Delegate<DelegatedPayload, End>;
using AcceptProto = Accept<DelegatedPayload, End>;

static_assert(perm_set_equal_v<compute_perm_set_after_send_t<PSWith, Transferable<int, WorkPerm>>, PSWithout>);

static_assert(
    perm_set_equal_v<compute_perm_set_after_recv_t<EmptyPermSet, Transferable<int, HotPerm>>, PermSet<HotPerm>>);

static_assert(is_well_formed_v<DelegateProto>);
static_assert(is_well_formed_v<AcceptProto>);
static_assert(std::is_same_v<dual_of_t<DelegateProto>, AcceptProto>);
static_assert(std::is_same_v<
              typename PermissionedSessionHandle<DelegateProto, EmptyPermSet, FakeChannel>::inner_perm_set, PSWith>);
static_assert(sizeof(PermissionedSessionHandle<DelegateProto, EmptyPermSet, FakeChannel>)
              == sizeof(SessionHandle<DelegateProto, FakeChannel>));
static_assert(sizeof(PermissionedSessionHandle<AcceptProto, EmptyPermSet, FakeChannel>)
              == sizeof(SessionHandle<AcceptProto, FakeChannel>));

using NvBareCtx = VendorCtx<VendorBackend::NV>;
using AmdBareCtx = VendorCtx<VendorBackend::AMD>;
using EpochBareCtx = EpochCtx<5, 3>;
using NvEpochCtx = VendorCtx<VendorBackend::NV, EpochBareCtx>;
using EpochNvCtx = EpochCtx<5, 3, NvBareCtx>;
using NvLoopCtx = VendorCtx<VendorBackend::NV, LoopContext<Send<int, Continue>, EmptyPermSet>>;
using NvEndHandle = PermissionedSessionHandle<End, EmptyPermSet, FakeChannel, NvBareCtx>;
using AmdEndHandle = PermissionedSessionHandle<End, EmptyPermSet, FakeChannel, AmdBareCtx>;
using RawEndHandle = PermissionedSessionHandle<End, EmptyPermSet, FakeChannel>;

static_assert(std::is_empty_v<NvBareCtx>);
static_assert(std::is_empty_v<EpochBareCtx>);
static_assert(std::is_empty_v<NvEpochCtx>);
static_assert(std::is_empty_v<NvLoopCtx>);
static_assert(loop_ctx_vendor_v<void> == VendorBackend::Portable);
static_assert(loop_ctx_vendor_v<LoopContext<Send<int, Continue>, EmptyPermSet>> == VendorBackend::Portable);
static_assert(loop_ctx_vendor_v<NvBareCtx> == VendorBackend::NV);
static_assert(std::is_same_v<loop_ctx_inner_t<NvBareCtx>, void>);
static_assert(std::is_same_v<loop_ctx_inner_t<NvLoopCtx>, LoopContext<Send<int, Continue>, EmptyPermSet>>);
static_assert(loop_ctx_has_explicit_epoch_v<EpochBareCtx>);
static_assert(loop_ctx_epoch_v<EpochBareCtx> == 5);
static_assert(loop_ctx_generation_v<EpochBareCtx> == 3);
static_assert(loop_ctx_epoch_satisfies_v<EpochBareCtx, 5, 3>);
static_assert(!loop_ctx_epoch_satisfies_v<EpochBareCtx, 6, 3>);
static_assert(loop_ctx_vendor_v<NvEpochCtx> == VendorBackend::NV);
static_assert(loop_ctx_has_explicit_epoch_v<NvEpochCtx>);
static_assert(session_loop_ctx_epoch_satisfies_v<NvEpochCtx, 5, 3>);
static_assert(loop_ctx_vendor_v<EpochNvCtx> == VendorBackend::NV);
static_assert(loop_ctx_has_explicit_epoch_v<EpochNvCtx>);
static_assert(session_loop_ctx_epoch_satisfies_v<EpochNvCtx, 5, 3>);
static_assert(!session_loop_ctx_epoch_satisfies_v<NvEpochCtx, 6, 3>);
static_assert(std::is_same_v<loop_ctx_rebind_inner_t<NvBareCtx, LoopContext<Recv<int, Continue>, EmptyPermSet>>,
                             VendorCtx<VendorBackend::NV, LoopContext<Recv<int, Continue>, EmptyPermSet>>>);
static_assert(
    std::is_same_v<loop_ctx_rebind_inner_t<NvEpochCtx, LoopContext<Recv<int, Continue>, EmptyPermSet>>,
                   VendorCtx<VendorBackend::NV, EpochCtx<5, 3, LoopContext<Recv<int, Continue>, EmptyPermSet>>>>);
static_assert(std::is_same_v<session_loop_ctx_rebind_inner_t<NvEpochCtx, Loop<Recv<int, Continue>>>,
                             VendorCtx<VendorBackend::NV, EpochCtx<5, 3, Loop<Recv<int, Continue>>>>>);

static_assert(std::is_same_v<typename RawEndHandle::loop_ctx, void>);
static_assert(std::is_same_v<typename RawEndHandle::vendor_ctx, VendorCtx<VendorBackend::Portable, void>>);
static_assert(RawEndHandle::vendor_backend == VendorBackend::Portable);
static_assert(NvEndHandle::vendor_backend == VendorBackend::NV);
static_assert(sizeof(NvEndHandle) == sizeof(SessionHandle<End, FakeChannel>));

static_assert(permissioned_session_vendor_compatible_v<RawEndHandle, NvEndHandle>);
static_assert(permissioned_session_vendor_compatible_v<NvEndHandle, NvEndHandle>);
static_assert(!permissioned_session_vendor_compatible_v<NvEndHandle, AmdEndHandle>);
static_assert(!permissioned_session_vendor_compatible_v<AmdEndHandle, NvEndHandle>);
static_assert(!permissioned_session_vendor_compatible_v<NvEndHandle, RawEndHandle>);

inline void runtime_smoke_test() noexcept {
    {
        FakeChannel ch{42};
        auto h =
            detail::permissioned_session_with_loc_<End, EmptyPermSet, FakeChannel>(ch, std::source_location::current());
        FakeChannel out = std::move(h).close();
        if (out.last_sent != 42) std::abort();
    }

    // The public mint lives in a header this one cannot include without a
    // cycle, so the smoke test reaches for the detail factory instead.
    {
        auto perm = ::crucible::safety::mint_permission_root<WorkPerm>();
        ::crucible::safety::permission_drop(std::move(perm));

        auto handle = detail::permissioned_session_with_loc_<End, EmptyPermSet, FakeChannel>(
            FakeChannel{7}, std::source_location::current());
        FakeChannel out = std::move(handle).close();
        if (out.last_sent != 7) std::abort();
    }

    {
        using PSHSend = PermissionedSessionHandle<SendProto, PSWith, FakeChannel>;
        using PSHEnd = PermissionedSessionHandle<End, EmptyPermSet, FakeChannel>;
        static_assert(std::is_same_v<typename PSHSend::perm_set, PSWith>);
        static_assert(std::is_same_v<typename PSHEnd::perm_set, EmptyPermSet>);
        static_assert(std::is_same_v<typename PSHSend::protocol, SendProto>);
        static_assert(std::is_same_v<typename PSHEnd::protocol, End>);
    }

    {
        using Ctx = LoopContext<Send<int, Continue>, EmptyPermSet>;
        static_assert(std::is_same_v<typename Ctx::body, Send<int, Continue>>);
        static_assert(std::is_same_v<typename Ctx::entry_perm_set, EmptyPermSet>);
        using PinnedCtx = VendorCtx<VendorBackend::NV, Ctx>;
        static_assert(std::is_same_v<loop_ctx_inner_t<PinnedCtx>, Ctx>);
    }

    {
        using LoopProto = Loop<Send<int, Continue>>;
        using LoopHandle = decltype(detail::permissioned_session_with_loc_<LoopProto, EmptyPermSet, FakeChannel>(
            FakeChannel{}, std::source_location::current()));
        static_assert(std::is_same_v<typename LoopHandle::protocol, Send<int, Continue>>);
        static_assert(std::is_same_v<typename LoopHandle::perm_set, EmptyPermSet>);
        static_assert(std::is_same_v<typename LoopHandle::loop_ctx, LoopContext<Send<int, Continue>, EmptyPermSet>>);
    }

    {
        using NextEnd =
            decltype(detail::step_to_next_permissioned<End, EmptyPermSet, FakeChannel, void>(FakeChannel{}));
        static_assert(std::is_same_v<NextEnd, PermissionedSessionHandle<End, EmptyPermSet, FakeChannel, void>>);
    }

    {
        using NextLoop =
            decltype(detail::step_to_next_permissioned<Loop<Send<int, Continue>>, PermSet<WorkPerm>, FakeChannel, void>(
                FakeChannel{}));
        static_assert(std::is_same_v<typename NextLoop::protocol, Send<int, Continue>>);
        static_assert(std::is_same_v<typename NextLoop::perm_set, PermSet<WorkPerm>>);
        static_assert(std::is_same_v<typename NextLoop::loop_ctx, LoopContext<Send<int, Continue>, PermSet<WorkPerm>>>);
    }

    {
        using PinnedLoop =
            decltype(detail::step_to_next_permissioned<Loop<Send<int, Continue>>, EmptyPermSet, FakeChannel,
                                                       VendorCtx<VendorBackend::NV>>(FakeChannel{}));
        static_assert(std::is_same_v<typename PinnedLoop::loop_ctx,
                                     VendorCtx<VendorBackend::NV, LoopContext<Send<int, Continue>, EmptyPermSet>>>);
        static_assert(PinnedLoop::vendor_backend == VendorBackend::NV);
    }

    detail::emit_leaked_permissions_debug<EmptyPermSet>();
}

}  // namespace crucible::safety::proto::detail::permissioned_session_smoke
