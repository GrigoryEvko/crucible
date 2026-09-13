#pragma once

// The whole protocol is certified once, when the session is minted, so no send
// or receive carries a context check.  Three alternatives were rejected: a
// per-operation context parameter and a context template parameter on the
// handle both change every existing session operation, and a decorator wrapper
// adds an indirection the eager walk does not need.  The handle returned here
// is the unmodified session primitive.
//
// A vendor grade on a payload is a wire-placement obligation rather than an
// effect row, so it is admitted on its own axis instead of through the row
// check.

#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/IsVendor.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/SessionRowExtraction.h>

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

template <std::uint64_t CurrentEpoch, std::uint64_t CurrentGeneration, ::crucible::effects::IsExecCtx InnerCtx>
struct [[nodiscard]] EpochExecCtx {
    using inner_ctx = InnerCtx;
    using cap_type = typename InnerCtx::cap_type;
    using numa_policy = typename InnerCtx::numa_policy;
    using alloc_class = typename InnerCtx::alloc_class;
    using hot_path_tier = typename InnerCtx::hot_path_tier;
    using residency = typename InnerCtx::residency;
    using row_type = typename InnerCtx::row_type;
    using workload_hint = typename InnerCtx::workload_hint;

    static constexpr std::uint64_t current_epoch = CurrentEpoch;
    static constexpr std::uint64_t current_generation = CurrentGeneration;

    [[no_unique_address]] InnerCtx inner{};
};

template <std::uint64_t CurrentEpoch, std::uint64_t CurrentGeneration, ::crucible::effects::IsExecCtx InnerCtx>
[[nodiscard]] consteval auto with_session_epoch(InnerCtx const&) noexcept
    -> EpochExecCtx<CurrentEpoch, CurrentGeneration, InnerCtx> {
    return {};
}

}  // namespace crucible::safety::proto

namespace crucible::effects {

template <std::uint64_t CurrentEpoch, std::uint64_t CurrentGeneration, class InnerCtx>
struct is_exec_ctx<::crucible::safety::proto::EpochExecCtx<CurrentEpoch, CurrentGeneration, InnerCtx>>
    : is_exec_ctx<InnerCtx> {};

}  // namespace crucible::effects

namespace crucible::safety::proto {

template <class Proto, class Ctx>
struct proto_row_admitted_by : std::false_type {};

template <class Ctx>
struct proto_row_admitted_by<End, Ctx> : std::true_type {};

template <class Ctx>
struct proto_row_admitted_by<Stop, Ctx> : std::true_type {};

// Continue closes back to the enclosing Loop, whose body this walk already
// visited, so admitting it unconditionally does not skip a payload.
template <class Ctx>
struct proto_row_admitted_by<Continue, Ctx> : std::true_type {};

template <class T, class K, class Ctx>
struct proto_row_admitted_by<Send<T, K>, Ctx>
    : std::bool_constant<::crucible::effects::is_subrow_v<payload_effect_row_t<T>, typename Ctx::row_type>
                         && proto_row_admitted_by<K, Ctx>::value> {};

// A receiver must itself hold authority for the row its payload carries, so
// the check runs in the same direction as the sender's rather than inverting.
template <class T, class K, class Ctx>
struct proto_row_admitted_by<Recv<T, K>, Ctx>
    : std::bool_constant<::crucible::effects::is_subrow_v<payload_effect_row_t<T>, typename Ctx::row_type>
                         && proto_row_admitted_by<K, Ctx>::value> {};

template <class B, class Ctx>
struct proto_row_admitted_by<Loop<B>, Ctx> : proto_row_admitted_by<B, Ctx> {};

template <VendorBackend V, class P, class Ctx>
struct proto_row_admitted_by<VendorPinned<V, P>, Ctx> : proto_row_admitted_by<P, Ctx> {};

// Which branch runs is a runtime choice on one side or the other, so every
// branch has to be admitted rather than just one.
template <class... Branches, class Ctx>
struct proto_row_admitted_by<Select<Branches...>, Ctx>
    : std::bool_constant<(proto_row_admitted_by<Branches, Ctx>::value && ...)> {};

template <class... Branches, class Ctx>
struct proto_row_admitted_by<Offer<Branches...>, Ctx>
    : std::bool_constant<(proto_row_admitted_by<Branches, Ctx>::value && ...)> {};

// The leading Sender annotation names the peer and carries no payload, so only
// the branches after it are walked.
template <class Role, class... Branches, class Ctx>
struct proto_row_admitted_by<Offer<Sender<Role>, Branches...>, Ctx>
    : std::bool_constant<(proto_row_admitted_by<Branches, Ctx>::value && ...)> {};

// The rollback arm is reachable whenever the checkpoint fails, so its row
// counts as much as the base arm's.
template <class Base, class Rollback, class Ctx>
struct proto_row_admitted_by<CheckpointedSession<Base, Rollback>, Ctx>
    : std::bool_constant<proto_row_admitted_by<Base, Ctx>::value && proto_row_admitted_by<Rollback, Ctx>::value> {};

// The delegated protocol T runs under the recipient's context, not this one,
// so only the continuation K is walked here.
template <class T, class K, class Ctx>
struct proto_row_admitted_by<Delegate<T, K>, Ctx> : proto_row_admitted_by<K, Ctx> {};

// Accepting an endpoint does not run its protocol, so T is skipped here too.
// A recipient that wants to run T mints a session for it, and that mint
// re-runs the row check against the recipient's own context.
template <class T, class K, class Ctx>
struct proto_row_admitted_by<Accept<T, K>, Ctx> : proto_row_admitted_by<K, Ctx> {};

template <class T, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, class Ctx>
struct proto_row_admitted_by<EpochedDelegate<T, K, MinEpoch, MinGeneration>, Ctx>
    : proto_row_admitted_by<Delegate<T, K>, Ctx> {};

template <class T, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, class Ctx>
struct proto_row_admitted_by<EpochedAccept<T, K, MinEpoch, MinGeneration>, Ctx>
    : proto_row_admitted_by<Accept<T, K>, Ctx> {};

template <class Proto, class Ctx>
inline constexpr bool proto_row_admitted_by_v = proto_row_admitted_by<Proto, Ctx>::value;

template <class Proto, class Ctx>
concept CtxFitsProtocol = ::crucible::effects::IsExecCtx<Ctx> && proto_row_admitted_by_v<Proto, Ctx>;

namespace detail::session_mint {

template <class Proto, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx;

template <class Payload, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx : std::true_type {};

template <VendorBackend PayloadVendor, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Vendor<PayloadVendor, T>, LoopCtx>
    : std::bool_constant<loop_ctx_has_explicit_vendor_v<LoopCtx>
                         && session_vendor_satisfies_v<loop_ctx_vendor_v<LoopCtx>, PayloadVendor>> {};

template <class T, class Tag, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<Transferable<T, Tag>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class Tag, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<Borrowed<T, Tag>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class Tag, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<Returned<T, Tag>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class InnerProto, class InnerPS, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<DelegatedSession<InnerProto, InnerPS>, LoopCtx>
    : protocol_vendor_admitted_by_loop_ctx<InnerProto, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<ContentAddressed<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto Pred, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Refined<Pred, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto Pred, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::SealedRefined<Pred, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class Tag, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Tagged<T, Tag>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Linear<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Stale<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::HotPath<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::DetSafe<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::AllocClass<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::ResidencyHeat<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::CipherTier<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::MemOrder<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Wait<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Progress<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <::crucible::safety::Tolerance V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::NumericalTier<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Crash<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Consistency<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::OpaqueLifetime<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Secret<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Budgeted<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::EpochVersioned<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::NumaPlacement<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::RecipeSpec<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class Cmp, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::Monotonic<T, Cmp>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, auto Max, class Cmp, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::BoundedMonotonic<T, Max, Cmp>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::WriteOnce<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class Cmp, class LoopCtx>
    requires std::is_trivially_copyable_v<T>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::AtomicMonotonic<T, Cmp>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, template <class...> class Storage, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::AppendOnly<T, Storage>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, std::size_t N, class Tag, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<::crucible::safety::TimeOrdered<T, N, Tag>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class Proto, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx : std::false_type {};

template <class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<End, LoopCtx> : std::true_type {};

template <class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Continue, LoopCtx> : std::true_type {};

template <class T, class K, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Send<T, K>, LoopCtx>
    : std::bool_constant<payload_vendor_admitted_by_loop_ctx<T, LoopCtx>::value
                         && protocol_vendor_admitted_by_loop_ctx<K, LoopCtx>::value> {};

template <class T, class K, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Recv<T, K>, LoopCtx>
    : std::bool_constant<payload_vendor_admitted_by_loop_ctx<T, LoopCtx>::value
                         && protocol_vendor_admitted_by_loop_ctx<K, LoopCtx>::value> {};

template <class Body, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Loop<Body>, LoopCtx> : protocol_vendor_admitted_by_loop_ctx<Body, LoopCtx> {
};

template <class... Branches, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Select<Branches...>, LoopCtx>
    : std::bool_constant<(protocol_vendor_admitted_by_loop_ctx<Branches, LoopCtx>::value && ...)> {};

template <class... Branches, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Offer<Branches...>, LoopCtx>
    : std::bool_constant<(protocol_vendor_admitted_by_loop_ctx<Branches, LoopCtx>::value && ...)> {};

template <class Role, class... Branches, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Offer<Sender<Role>, Branches...>, LoopCtx>
    : std::bool_constant<(protocol_vendor_admitted_by_loop_ctx<Branches, LoopCtx>::value && ...)> {};

template <class Base, class Rollback, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<CheckpointedSession<Base, Rollback>, LoopCtx>
    : std::bool_constant<protocol_vendor_admitted_by_loop_ctx<Base, LoopCtx>::value
                         && protocol_vendor_admitted_by_loop_ctx<Rollback, LoopCtx>::value> {};

template <class InnerProto, class InnerPS, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<DelegatedSession<InnerProto, InnerPS>, LoopCtx>
    : protocol_vendor_admitted_by_loop_ctx<InnerProto, LoopCtx> {};

template <class T, class K, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Delegate<T, K>, LoopCtx>
    : std::bool_constant<(!is_vendor_pinned_v<T>
                          || (loop_ctx_has_explicit_vendor_v<LoopCtx>
                              && session_vendor_satisfies_v<loop_ctx_vendor_v<LoopCtx>, protocol_vendor_v<T>>))
                         && protocol_vendor_admitted_by_loop_ctx<T, LoopCtx>::value
                         && protocol_vendor_admitted_by_loop_ctx<K, LoopCtx>::value> {};

template <class T, class K, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Accept<T, K>, LoopCtx>
    : protocol_vendor_admitted_by_loop_ctx<Delegate<T, K>, LoopCtx> {};

template <class T, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<EpochedDelegate<T, K, MinEpoch, MinGeneration>, LoopCtx>
    : protocol_vendor_admitted_by_loop_ctx<Delegate<T, K>, LoopCtx> {};

template <class T, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<EpochedAccept<T, K, MinEpoch, MinGeneration>, LoopCtx>
    : protocol_vendor_admitted_by_loop_ctx<Accept<T, K>, LoopCtx> {};

template <VendorBackend V, class P, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<VendorPinned<V, P>, LoopCtx>
    : std::bool_constant<V != VendorBackend::None
                         && (!loop_ctx_has_explicit_vendor_v<LoopCtx>
                             || session_vendor_satisfies_v<loop_ctx_vendor_v<LoopCtx>, V>)
                         && protocol_vendor_admitted_by_loop_ctx<P, VendorCtx<V, loop_ctx_inner_t<LoopCtx>>>::value> {};

template <class Proto, class LoopCtx>
inline constexpr bool protocol_vendor_admitted_by_loop_ctx_v =
    protocol_vendor_admitted_by_loop_ctx<Proto, LoopCtx>::value;

template <class Ctx>
struct loop_ctx_from_exec_ctx {
    using type = void;
};

template <std::uint64_t CurrentEpoch, std::uint64_t CurrentGeneration, class InnerCtx>
struct loop_ctx_from_exec_ctx<EpochExecCtx<CurrentEpoch, CurrentGeneration, InnerCtx>> {
    using type = EpochCtx<CurrentEpoch, CurrentGeneration>;
};

template <class Ctx>
using loop_ctx_from_exec_ctx_t = typename loop_ctx_from_exec_ctx<std::remove_cvref_t<Ctx>>::type;

template <class Proto, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx : std::false_type {};

template <class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<End, LoopCtx> : std::true_type {};

template <CrashClass C, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<Stop_g<C>, LoopCtx> : std::true_type {};

template <class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<Continue, LoopCtx> : std::true_type {};

template <class T, class K, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<Send<T, K>, LoopCtx> : protocol_epoch_admitted_by_loop_ctx<K, LoopCtx> {};

template <class T, class K, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<Recv<T, K>, LoopCtx> : protocol_epoch_admitted_by_loop_ctx<K, LoopCtx> {};

template <class Body, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<Loop<Body>, LoopCtx>
    : protocol_epoch_admitted_by_loop_ctx<Body, loop_ctx_rebind_inner_t<LoopCtx, Loop<Body>>> {};

template <class... Branches, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<Select<Branches...>, LoopCtx>
    : std::bool_constant<(protocol_epoch_admitted_by_loop_ctx<Branches, LoopCtx>::value && ...)> {};

template <class... Branches, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<Offer<Branches...>, LoopCtx>
    : std::bool_constant<(protocol_epoch_admitted_by_loop_ctx<Branches, LoopCtx>::value && ...)> {};

template <class Role, class... Branches, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<Offer<Sender<Role>, Branches...>, LoopCtx>
    : std::bool_constant<(protocol_epoch_admitted_by_loop_ctx<Branches, LoopCtx>::value && ...)> {};

template <class Base, class Rollback, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<CheckpointedSession<Base, Rollback>, LoopCtx>
    : std::bool_constant<protocol_epoch_admitted_by_loop_ctx<Base, LoopCtx>::value
                         && protocol_epoch_admitted_by_loop_ctx<Rollback, LoopCtx>::value> {};

template <class InnerProto, class InnerPS, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<DelegatedSession<InnerProto, InnerPS>, LoopCtx>
    : protocol_epoch_admitted_by_loop_ctx<InnerProto, LoopCtx> {};

template <class T, class K, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<Delegate<T, K>, LoopCtx> : protocol_epoch_admitted_by_loop_ctx<K, LoopCtx> {
};

template <class T, class K, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<Accept<T, K>, LoopCtx> : protocol_epoch_admitted_by_loop_ctx<K, LoopCtx> {};

// The two sides of a handoff are deliberately asymmetric.  A sender must match
// the declared epoch and generation exactly, so it cannot mint a handoff at a
// threshold weaker than the one it is running at.  A recipient only has to be
// at or above the threshold, so a context that has already moved on may still
// accept.
template <class T, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<EpochedDelegate<T, K, MinEpoch, MinGeneration>, LoopCtx>
    : std::bool_constant<session_loop_ctx_epoch_matches_v<LoopCtx, MinEpoch, MinGeneration>
                         && protocol_epoch_admitted_by_loop_ctx<K, LoopCtx>::value> {};

template <class T, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<EpochedAccept<T, K, MinEpoch, MinGeneration>, LoopCtx>
    : std::bool_constant<session_loop_ctx_epoch_satisfies_v<LoopCtx, MinEpoch, MinGeneration>
                         && protocol_epoch_admitted_by_loop_ctx<K, LoopCtx>::value> {};

template <VendorBackend V, class P, class LoopCtx>
struct protocol_epoch_admitted_by_loop_ctx<VendorPinned<V, P>, LoopCtx>
    : protocol_epoch_admitted_by_loop_ctx<P, LoopCtx> {};

template <class Proto, class LoopCtx>
inline constexpr bool protocol_epoch_admitted_by_loop_ctx_v =
    protocol_epoch_admitted_by_loop_ctx<Proto, LoopCtx>::value;

template <class Proto, class PS, class LoopCtx = void>
struct permission_flow_closes : std::false_type {};

template <class PS, class LoopCtx>
struct permission_flow_closes<End, PS, LoopCtx> : std::bool_constant<perm_set_equal_v<PS, EmptyPermSet>> {};

template <CrashClass C, class PS, class LoopCtx>
struct permission_flow_closes<Stop_g<C>, PS, LoopCtx> : std::bool_constant<perm_set_equal_v<PS, EmptyPermSet>> {};

template <class PS, class LoopCtx, bool InLoop = !std::is_void_v<loop_ctx_inner_t<LoopCtx>>>
struct continue_permission_flow_branch : std::false_type {};

template <class PS, class LoopCtx>
struct continue_permission_flow_branch<PS, LoopCtx, true>
    : std::bool_constant<perm_set_equal_v<PS, typename loop_ctx_inner_t<LoopCtx>::entry_perm_set>> {};

template <class PS, class LoopCtx>
struct permission_flow_closes<Continue, PS, LoopCtx> : continue_permission_flow_branch<PS, LoopCtx> {};

template <class Proto, class PS, class LoopCtx, bool Sendable>
struct send_permission_flow_branch : std::false_type {};

template <class T, class K, class PS, class LoopCtx>
struct send_permission_flow_branch<Send<T, K>, PS, LoopCtx, true>
    : permission_flow_closes<K, compute_perm_set_after_send_t<PS, T>, LoopCtx> {};

template <class T, class K, class PS, class LoopCtx>
struct permission_flow_closes<Send<T, K>, PS, LoopCtx>
    : send_permission_flow_branch<Send<T, K>, PS, LoopCtx, SendablePayload<T, PS>> {};

template <class T, class K, class PS, class LoopCtx>
struct permission_flow_closes<Recv<T, K>, PS, LoopCtx>
    : permission_flow_closes<K, compute_perm_set_after_recv_t<PS, T>, LoopCtx> {};

template <class Body, class PS, class LoopCtx>
struct permission_flow_closes<Loop<Body>, PS, LoopCtx>
    : permission_flow_closes<Body, PS, loop_ctx_rebind_inner_t<LoopCtx, LoopContext<Body, PS>>> {};

template <class... Branches, class PS, class LoopCtx>
struct permission_flow_closes<Select<Branches...>, PS, LoopCtx>
    : std::bool_constant<(permission_flow_closes<Branches, PS, LoopCtx>::value && ...)> {};

template <class... Branches, class PS, class LoopCtx>
struct permission_flow_closes<Offer<Branches...>, PS, LoopCtx>
    : std::bool_constant<(permission_flow_closes<Branches, PS, LoopCtx>::value && ...)> {};

template <class Role, class... Branches, class PS, class LoopCtx>
struct permission_flow_closes<Offer<Sender<Role>, Branches...>, PS, LoopCtx>
    : std::bool_constant<(permission_flow_closes<Branches, PS, LoopCtx>::value && ...)> {};

template <class Base, class Rollback, class PS, class LoopCtx>
struct permission_flow_closes<CheckpointedSession<Base, Rollback>, PS, LoopCtx>
    : std::bool_constant<permission_flow_closes<Base, PS, LoopCtx>::value
                         && permission_flow_closes<Rollback, PS, LoopCtx>::value> {};

template <class T, class K, class PS, class LoopCtx>
struct permission_flow_closes<Delegate<T, K>, PS, LoopCtx> : permission_flow_closes<K, PS, LoopCtx> {};

template <class T, class K, class PS, class LoopCtx>
struct permission_flow_closes<Accept<T, K>, PS, LoopCtx> : permission_flow_closes<K, PS, LoopCtx> {};

template <class T, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, class PS, class LoopCtx>
struct permission_flow_closes<EpochedDelegate<T, K, MinEpoch, MinGeneration>, PS, LoopCtx>
    : permission_flow_closes<K, PS, LoopCtx> {};

template <class T, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, class PS, class LoopCtx>
struct permission_flow_closes<EpochedAccept<T, K, MinEpoch, MinGeneration>, PS, LoopCtx>
    : permission_flow_closes<K, PS, LoopCtx> {};

template <VendorBackend V, class P, class PS, class LoopCtx>
struct permission_flow_closes<VendorPinned<V, P>, PS, LoopCtx>
    : permission_flow_closes<P, PS, VendorCtx<V, loop_ctx_inner_t<LoopCtx>>> {};

template <class Proto, class PS, class LoopCtx = void>
inline constexpr bool permission_flow_closes_v = permission_flow_closes<Proto, PS, LoopCtx>::value;

template <class Proto>
struct protocol_permissioned_runnable : std::false_type {};

template <>
struct protocol_permissioned_runnable<End> : std::true_type {};

template <CrashClass C>
struct protocol_permissioned_runnable<Stop_g<C>> : std::true_type {};

template <>
struct protocol_permissioned_runnable<Continue> : std::true_type {};

template <class T, class K>
struct protocol_permissioned_runnable<Send<T, K>> : protocol_permissioned_runnable<K> {};

template <class T, class K>
struct protocol_permissioned_runnable<Recv<T, K>> : protocol_permissioned_runnable<K> {};

template <class B>
struct protocol_permissioned_runnable<Loop<B>> : protocol_permissioned_runnable<B> {};

template <VendorBackend V, class P>
struct protocol_permissioned_runnable<VendorPinned<V, P>> : protocol_permissioned_runnable<P> {};

// A choice with no branches offers nothing to run.  The variadic fold below
// would report it runnable, because the identity of && over an empty pack is
// true, so the zero-branch shapes need their own specializations.
template <>
struct protocol_permissioned_runnable<Select<>> : std::false_type {};

template <>
struct protocol_permissioned_runnable<Offer<>> : std::false_type {};

template <class Role>
struct protocol_permissioned_runnable<Offer<Sender<Role>>> : std::false_type {};

template <class... Branches>
struct protocol_permissioned_runnable<Select<Branches...>>
    : std::bool_constant<(protocol_permissioned_runnable<Branches>::value && ...)> {};

template <class... Branches>
struct protocol_permissioned_runnable<Offer<Branches...>>
    : std::bool_constant<(protocol_permissioned_runnable<Branches>::value && ...)> {};

template <class Role, class... Branches>
struct protocol_permissioned_runnable<Offer<Sender<Role>, Branches...>>
    : std::bool_constant<(protocol_permissioned_runnable<Branches>::value && ...)> {};

template <class Base, class Rollback>
struct protocol_permissioned_runnable<CheckpointedSession<Base, Rollback>>
    : std::bool_constant<protocol_permissioned_runnable<Base>::value
                         && protocol_permissioned_runnable<Rollback>::value> {};

template <class InnerProto, class InnerPS, class K>
struct protocol_permissioned_runnable<Delegate<DelegatedSession<InnerProto, InnerPS>, K>>
    : std::bool_constant<protocol_permissioned_runnable<InnerProto>::value
                         && protocol_permissioned_runnable<K>::value> {};

template <class InnerProto, class InnerPS, class K>
struct protocol_permissioned_runnable<Accept<DelegatedSession<InnerProto, InnerPS>, K>>
    : std::bool_constant<protocol_permissioned_runnable<InnerProto>::value
                         && protocol_permissioned_runnable<K>::value> {};

template <class InnerProto, class InnerPS, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct protocol_permissioned_runnable<
    EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>>
    : std::bool_constant<protocol_permissioned_runnable<InnerProto>::value
                         && protocol_permissioned_runnable<K>::value> {};

template <class InnerProto, class InnerPS, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct protocol_permissioned_runnable<EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K, MinEpoch, MinGeneration>>
    : std::bool_constant<protocol_permissioned_runnable<InnerProto>::value
                         && protocol_permissioned_runnable<K>::value> {};

template <class Proto>
inline constexpr bool protocol_permissioned_runnable_v = protocol_permissioned_runnable<Proto>::value;

}  // namespace detail::session_mint

template <class Proto, class LoopCtx>
concept ProtocolVendorAdmittedByLoopCtx = detail::session_mint::protocol_vendor_admitted_by_loop_ctx_v<Proto, LoopCtx>;

template <class Proto, class LoopCtx>
concept ProtocolEpochAdmittedByLoopCtx = detail::session_mint::protocol_epoch_admitted_by_loop_ctx_v<Proto, LoopCtx>;

template <class Proto>
concept ProtocolPermissionedRunnable = detail::session_mint::protocol_permissioned_runnable_v<Proto>;

template <class Proto, class Ctx, class InitialPS, class LoopCtx = detail::session_mint::loop_ctx_from_exec_ctx_t<Ctx>>
concept CtxFitsPermissionedProtocol =
    CtxFitsProtocol<Proto, Ctx> && detail::session_mint::permission_flow_closes_v<Proto, InitialPS, LoopCtx>
    && ProtocolVendorAdmittedByLoopCtx<Proto, LoopCtx> && ProtocolEpochAdmittedByLoopCtx<Proto, LoopCtx>;

// A channel has two local protocols: one endpoint runs Proto and the other its
// dual.  Admission has to hold on both sides.  Checking only the sender would
// let it transmit a payload the receiver's context has no authority to hold.

template <class Proto, class CtxA, class CtxB>
concept CtxFitsChannel =
    ::crucible::effects::IsExecCtx<CtxA> && ::crucible::effects::IsExecCtx<CtxB> && ProtocolPermissionedRunnable<Proto>
    && ProtocolPermissionedRunnable<dual_of_t<Proto>> && CtxFitsPermissionedProtocol<Proto, CtxA, EmptyPermSet>
    && CtxFitsPermissionedProtocol<dual_of_t<Proto>, CtxB, EmptyPermSet>;

template <class Proto, ::crucible::effects::IsExecCtx Ctx, class Resource, class... InitPerms>
    requires CtxFitsPermissionedProtocol<Proto, Ctx, PermSet<InitPerms...>>
[[nodiscard]] constexpr auto mint_permissioned_session(Ctx const&, Resource&& resource,
                                                       ::crucible::safety::Permission<InitPerms>&&... perms) noexcept {
    using InitialPS = PermSet<InitPerms...>;
    using LoopCtx = detail::session_mint::loop_ctx_from_exec_ctx_t<Ctx>;
    ((void)perms, ...);

    return detail::permissioned_session_with_loc_<Proto, InitialPS, Resource, LoopCtx>(std::forward<Resource>(resource),
                                                                                       std::source_location::current());
}

// These deletions exist so that a call written in the older spelling fails
// against a direct message instead of unrelated overload-resolution text.

template <class Proto, ::crucible::effects::IsExecCtx Ctx, class Resource>
void mint_session(Ctx const&, Resource&&, std::source_location = std::source_location::current()) noexcept =
    delete("mint_session<Proto>(ctx, resource) is removed; use "
           "mint_permissioned_session<Proto>(ctx, resource, perms...) — "
           "structured diagnostic: fixy::sess::diag::FixyMintSessionRemoved");

template <class Proto, class Resource>
void mint_session(Resource&&) noexcept = delete("mint_session<Proto>(resource) is removed; use "
                                                "mint_permissioned_session<Proto>(ctx, resource, perms...) — "
                                                "structured diagnostic: fixy::sess::diag::FixyMintSessionRemoved");

template <class Proto, ::crucible::effects::IsExecCtx CtxA, ::crucible::effects::IsExecCtx CtxB, class ResourceA,
          class ResourceB>
    requires CtxFitsChannel<Proto, CtxA, CtxB>
[[nodiscard]] constexpr auto mint_channel(CtxA const& ctx_a, CtxB const& ctx_b, ResourceA&& resource_a,
                                          ResourceB&& resource_b,
                                          std::source_location loc = std::source_location::current()) noexcept {
    static_cast<void>(ctx_a);
    static_cast<void>(ctx_b);

    using StoredResourceA = std::remove_cvref_t<ResourceA>;
    using StoredResourceB = std::remove_cvref_t<ResourceB>;
    using LoopCtxA = detail::session_mint::loop_ctx_from_exec_ctx_t<CtxA>;
    using LoopCtxB = detail::session_mint::loop_ctx_from_exec_ctx_t<CtxB>;

    return std::pair{detail::permissioned_session_with_loc_<Proto, EmptyPermSet, StoredResourceA, LoopCtxA>(
                         std::forward<ResourceA>(resource_a), loc),
                     detail::permissioned_session_with_loc_<dual_of_t<Proto>, EmptyPermSet, StoredResourceB, LoopCtxB>(
                         std::forward<ResourceB>(resource_b), loc)};
}

namespace detail::session_mint_self_test {

namespace eff = ::crucible::effects;

static_assert(proto_row_admitted_by_v<End, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<End, eff::BgDrainCtx>);
static_assert(proto_row_admitted_by_v<Continue, eff::HotFgCtx>);

using SendInt = Send<int, End>;
static_assert(proto_row_admitted_by_v<SendInt, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<SendInt, eff::BgDrainCtx>);
static_assert(proto_row_admitted_by_v<SendInt, eff::ColdInitCtx>);

using SendBgComp = Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>;
static_assert(!proto_row_admitted_by_v<SendBgComp, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<SendBgComp, eff::BgDrainCtx>);
static_assert(proto_row_admitted_by_v<SendBgComp, eff::BgCompileCtx>);

using SendChain = Send<int, Send<eff::Computation<eff::Row<eff::Effect::Alloc>, int>, End>>;
static_assert(!proto_row_admitted_by_v<SendChain, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<SendChain, eff::BgDrainCtx>);

using LoopSendBg = Loop<Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, Continue>>;
static_assert(!proto_row_admitted_by_v<LoopSendBg, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<LoopSendBg, eff::BgDrainCtx>);

using LoopRecvBg = Loop<Recv<eff::Computation<eff::Row<eff::Effect::Bg>, int>, Continue>>;
static_assert(!proto_row_admitted_by_v<LoopRecvBg, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<LoopRecvBg, eff::BgDrainCtx>);

using SendCap = Send<eff::Capability<eff::Effect::Alloc, eff::Bg>, End>;
static_assert(!proto_row_admitted_by_v<SendCap, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<SendCap, eff::BgDrainCtx>);

using SelectMix = Select<Send<int, End>, Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>>;
static_assert(!proto_row_admitted_by_v<SelectMix, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<SelectMix, eff::BgDrainCtx>);

static_assert(CtxFitsProtocol<End, eff::HotFgCtx>);
static_assert(CtxFitsProtocol<SendInt, eff::HotFgCtx>);
static_assert(!CtxFitsProtocol<SendBgComp, eff::HotFgCtx>);
static_assert(CtxFitsProtocol<SendBgComp, eff::BgDrainCtx>);
static_assert(!CtxFitsProtocol<int, eff::HotFgCtx>);

static_assert(CtxFitsChannel<SendInt, eff::HotFgCtx, eff::HotFgCtx>);
static_assert(!CtxFitsChannel<SendBgComp, eff::HotFgCtx, eff::BgDrainCtx>);
static_assert(!CtxFitsChannel<SendBgComp, eff::BgDrainCtx, eff::HotFgCtx>);
static_assert(CtxFitsChannel<SendBgComp, eff::BgDrainCtx, eff::BgDrainCtx>);

struct WorkPerm {};
struct FakeResource {};

using SendTransfer = Send<Transferable<int, WorkPerm>, End>;
static_assert(detail::session_mint::permission_flow_closes_v<SendTransfer, PermSet<WorkPerm>>);
static_assert(!detail::session_mint::permission_flow_closes_v<SendTransfer, EmptyPermSet>);
static_assert(!detail::session_mint::permission_flow_closes_v<End, PermSet<WorkPerm>>);
static_assert(ProtocolPermissionedRunnable<SendInt>);
static_assert(!ProtocolPermissionedRunnable<Delegate<SendInt, End>>);
static_assert(ProtocolPermissionedRunnable<Delegate<DelegatedSession<SendInt, EmptyPermSet>, End>>);

static_assert(!ProtocolPermissionedRunnable<Select<>>);
static_assert(!ProtocolPermissionedRunnable<Offer<>>);
namespace fixy_cr15_sender_role_tag {
struct Probe {};
}  // namespace fixy_cr15_sender_role_tag
static_assert(!ProtocolPermissionedRunnable<Offer<Sender<fixy_cr15_sender_role_tag::Probe>>>);
static_assert(ProtocolPermissionedRunnable<Select<End>>);
static_assert(ProtocolPermissionedRunnable<Offer<End>>);
static_assert(ProtocolPermissionedRunnable<Offer<Sender<fixy_cr15_sender_role_tag::Probe>, End>>);

using RecvThenReturn = Recv<Transferable<int, WorkPerm>, Send<Returned<int, WorkPerm>, End>>;
static_assert(detail::session_mint::permission_flow_closes_v<RecvThenReturn, EmptyPermSet>);

using TransferBgComp = Send<Transferable<eff::Computation<eff::Row<eff::Effect::Bg>, int>, WorkPerm>, End>;
static_assert(!CtxFitsProtocol<TransferBgComp, eff::HotFgCtx>);
static_assert(CtxFitsProtocol<TransferBgComp, eff::BgDrainCtx>);
static_assert(CtxFitsPermissionedProtocol<TransferBgComp, eff::BgDrainCtx, PermSet<WorkPerm>>);
static_assert(!CtxFitsPermissionedProtocol<TransferBgComp, eff::HotFgCtx, PermSet<WorkPerm>>);

using IoComp = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using TransferIoComp = Send<Transferable<IoComp, WorkPerm>, End>;
using BorrowedIoComp = Send<Borrowed<IoComp, WorkPerm>, End>;
using ReturnedIoComp = Send<Returned<IoComp, WorkPerm>, End>;
static_assert(!CtxFitsProtocol<TransferIoComp, eff::HotFgCtx>);
static_assert(!CtxFitsProtocol<BorrowedIoComp, eff::HotFgCtx>);
static_assert(!CtxFitsProtocol<ReturnedIoComp, eff::HotFgCtx>);
static_assert(CtxFitsProtocol<TransferIoComp, eff::BgCompileCtx>);
static_assert(CtxFitsProtocol<BorrowedIoComp, eff::BgCompileCtx>);
static_assert(CtxFitsProtocol<ReturnedIoComp, eff::BgCompileCtx>);
static_assert(CtxFitsPermissionedProtocol<TransferIoComp, eff::BgCompileCtx, PermSet<WorkPerm>>);
static_assert(CtxFitsPermissionedProtocol<BorrowedIoComp, eff::BgCompileCtx, EmptyPermSet>);
static_assert(CtxFitsPermissionedProtocol<ReturnedIoComp, eff::BgCompileCtx, PermSet<WorkPerm>>);

using SendIoComp = Send<IoComp, End>;
using SendIoCap = Send<eff::Capability<eff::Effect::IO, eff::Bg>, End>;
static_assert(CtxFitsChannel<SendIoComp, eff::BgCompileCtx, eff::BgCompileCtx>);
static_assert(!CtxFitsChannel<SendIoComp, eff::BgCompileCtx, eff::HotFgCtx>);
static_assert(!CtxFitsChannel<SendIoComp, eff::BgDrainCtx, eff::BgCompileCtx>);
static_assert(CtxFitsChannel<SendIoCap, eff::BgCompileCtx, eff::BgCompileCtx>);
static_assert(!CtxFitsChannel<SendIoCap, eff::BgCompileCtx, eff::HotFgCtx>);

using NvIntPayload = ::crucible::safety::Vendor<VendorBackend::NV, int>;
using AmdIntPayload = ::crucible::safety::Vendor<VendorBackend::AMD, int>;
using PortableIntPayload = ::crucible::safety::Vendor<VendorBackend::Portable, int>;
using SendNvPayload = Send<NvIntPayload, End>;
using RecvAmdPayload = Recv<AmdIntPayload, End>;
using WrappedNvPayload =
    Transferable<::crucible::safety::NumericalTier<::crucible::safety::Tolerance::BITEXACT, NvIntPayload>, WorkPerm>;

static_assert(!ProtocolVendorAdmittedByLoopCtx<SendNvPayload, void>);
static_assert(ProtocolVendorAdmittedByLoopCtx<SendNvPayload, VendorCtx<VendorBackend::NV>>);
static_assert(ProtocolVendorAdmittedByLoopCtx<SendNvPayload, VendorCtx<VendorBackend::Portable>>);
static_assert(!ProtocolVendorAdmittedByLoopCtx<SendNvPayload, VendorCtx<VendorBackend::AMD>>);
static_assert(!ProtocolVendorAdmittedByLoopCtx<RecvAmdPayload, VendorCtx<VendorBackend::NV>>);
static_assert(!ProtocolVendorAdmittedByLoopCtx<Send<PortableIntPayload, End>, VendorCtx<VendorBackend::NV>>);
static_assert(!ProtocolVendorAdmittedByLoopCtx<Send<WrappedNvPayload, End>, void>);
static_assert(ProtocolVendorAdmittedByLoopCtx<Send<WrappedNvPayload, End>, VendorCtx<VendorBackend::NV>>);

using PinnedNvSend = VendorPinned<VendorBackend::NV, SendNvPayload>;
static_assert(CtxFitsPermissionedProtocol<PinnedNvSend, eff::HotFgCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<SendNvPayload, eff::HotFgCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<VendorPinned<VendorBackend::None, End>, eff::HotFgCtx, EmptyPermSet>);
static_assert(CtxFitsPermissionedProtocol<SendNvPayload, eff::HotFgCtx, EmptyPermSet, VendorCtx<VendorBackend::NV>>);
static_assert(!CtxFitsPermissionedProtocol<RecvAmdPayload, eff::HotFgCtx, EmptyPermSet, VendorCtx<VendorBackend::NV>>);

using NvCarrierDelegatesNv = VendorPinned<VendorBackend::NV, Delegate<VendorPinned<VendorBackend::NV, End>, End>>;
using NvCarrierDelegatesAmd = VendorPinned<VendorBackend::NV, Delegate<VendorPinned<VendorBackend::AMD, End>, End>>;
static_assert(CtxFitsPermissionedProtocol<NvCarrierDelegatesNv, eff::HotFgCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<NvCarrierDelegatesAmd, eff::HotFgCtx, EmptyPermSet>);
static_assert(
    !CtxFitsPermissionedProtocol<VendorPinned<VendorBackend::NV, Select<VendorPinned<VendorBackend::AMD, End>, End>>,
                                 eff::HotFgCtx, EmptyPermSet>);

using CtxBoundPsh =
    decltype(mint_permissioned_session<SendTransfer>(std::declval<eff::HotFgCtx const&>(), std::declval<FakeResource>(),
                                                     std::declval<::crucible::safety::Permission<WorkPerm>&&>()));
static_assert(std::is_same_v<typename CtxBoundPsh::protocol, SendTransfer>);
static_assert(std::is_same_v<typename CtxBoundPsh::perm_set, PermSet<WorkPerm>>);

using EmptyMint =
    decltype(mint_permissioned_session<SendInt>(std::declval<eff::HotFgCtx const&>(), std::declval<FakeResource>()));
static_assert(std::is_same_v<typename EmptyMint::protocol, SendInt>);
static_assert(std::is_same_v<typename EmptyMint::perm_set, EmptyPermSet>);

using CtxBoundChannel =
    decltype(mint_channel<SendInt>(std::declval<eff::HotFgCtx const&>(), std::declval<eff::HotFgCtx const&>(),
                                   std::declval<FakeResource>(), std::declval<FakeResource>()));
static_assert(std::is_same_v<typename CtxBoundChannel::first_type::protocol, SendInt>);
static_assert(std::is_same_v<typename CtxBoundChannel::second_type::protocol, dual_of_t<SendInt>>);
static_assert(std::is_same_v<typename CtxBoundChannel::first_type::perm_set, EmptyPermSet>);
static_assert(std::is_same_v<typename CtxBoundChannel::second_type::perm_set, EmptyPermSet>);

using EpochFgCtx = EpochExecCtx<5, 3, eff::HotFgCtx>;
using FreshEpochDelegate = EpochedDelegate<DelegatedSession<End, EmptyPermSet>, End, 5, 3>;
using WeakenedGenerationDelegate = EpochedDelegate<DelegatedSession<End, EmptyPermSet>, End, 5, 2>;
static_assert(::crucible::effects::IsExecCtx<EpochFgCtx>);
static_assert(sizeof(EpochFgCtx) == sizeof(eff::HotFgCtx));
static_assert(ProtocolEpochAdmittedByLoopCtx<FreshEpochDelegate, EpochCtx<5, 3>>);
static_assert(!ProtocolEpochAdmittedByLoopCtx<FreshEpochDelegate, EpochCtx<4, 3>>);
static_assert(!ProtocolEpochAdmittedByLoopCtx<WeakenedGenerationDelegate, EpochCtx<5, 3>>);
static_assert(CtxFitsPermissionedProtocol<FreshEpochDelegate, EpochFgCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<FreshEpochDelegate, eff::HotFgCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<WeakenedGenerationDelegate, EpochFgCtx, EmptyPermSet>);

using EpochMint = decltype(mint_permissioned_session<FreshEpochDelegate>(std::declval<EpochFgCtx const&>(),
                                                                         std::declval<FakeResource>()));
static_assert(std::is_same_v<typename EpochMint::protocol, FreshEpochDelegate>);
static_assert(std::is_same_v<typename EpochMint::loop_ctx, EpochCtx<5, 3>>);

static_assert(proto_row_admitted_by_v<Stop, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<Stop, eff::BgDrainCtx>);
using SendThenStop = Send<int, Stop>;
static_assert(proto_row_admitted_by_v<SendThenStop, eff::HotFgCtx>);
using SendBgThenStop = Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, Stop>;
static_assert(!proto_row_admitted_by_v<SendBgThenStop, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<SendBgThenStop, eff::BgDrainCtx>);

using CkptSafe = CheckpointedSession<Send<int, End>, Recv<int, End>>;
static_assert(proto_row_admitted_by_v<CkptSafe, eff::HotFgCtx>);

using CkptBgRollback = CheckpointedSession<Send<int, End>, Recv<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>>;
static_assert(!proto_row_admitted_by_v<CkptBgRollback, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<CkptBgRollback, eff::BgDrainCtx>);

using DelegateBgChannel = Delegate<Loop<Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, Continue>>, End>;
static_assert(proto_row_admitted_by_v<DelegateBgChannel, eff::HotFgCtx>);
using DelegateBgChannelBgK =
    Delegate<Loop<Recv<int, Continue>>, Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>>;
static_assert(!proto_row_admitted_by_v<DelegateBgChannelBgK, eff::HotFgCtx>);

using AcceptThenSendBg = Accept<Send<int, End>, Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>>;
static_assert(!proto_row_admitted_by_v<AcceptThenSendBg, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<AcceptThenSendBg, eff::BgDrainCtx>);

using SendCa = Send<ContentAddressed<int>, End>;
static_assert(proto_row_admitted_by_v<SendCa, eff::HotFgCtx>);

using SendCaBg = Send<ContentAddressed<eff::Computation<eff::Row<eff::Effect::Bg>, int>>, End>;
static_assert(!proto_row_admitted_by_v<SendCaBg, eff::HotFgCtx>);
static_assert(proto_row_admitted_by_v<SendCaBg, eff::BgDrainCtx>);

}  // namespace detail::session_mint_self_test

[[gnu::cold]] inline void runtime_smoke_test_session_mint() noexcept {
    namespace eff = ::crucible::effects;

    // No session is instantiated here, because a real resource would have to be
    // a pinned channel.  The assertions establish only that the mint is
    // callable behind its concept gate.

    using PureLoop = Loop<Send<int, Continue>>;
    eff::HotFgCtx fg;
    static_cast<void>(fg);
    static_assert(CtxFitsProtocol<PureLoop, eff::HotFgCtx>);
    static_assert(CtxFitsProtocol<PureLoop, eff::BgDrainCtx>);

    using BgLoop = Loop<Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, Continue>>;
    static_assert(!CtxFitsProtocol<BgLoop, eff::HotFgCtx>);
    static_assert(CtxFitsProtocol<BgLoop, eff::BgDrainCtx>);
}

}  // namespace crucible::safety::proto
