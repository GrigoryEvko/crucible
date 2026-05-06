#pragma once

// ── crucible::safety::proto::mint_session ──────────────────────────
//
// Eager whole-protocol ctx-check at session-mint time.  Walks Proto
// recursively, asserts every Send/Recv payload's effect-row is admitted
// by Ctx::row, and returns a PermissionedSessionHandle with a concrete
// PermSet.  Subsequent send/recv operations run at full speed with no
// per-op ctx check — the whole protocol was certified at construction.
//
// This is the canonical session-side instance of the universal mint
// pattern shipped across Tier 1: `mint_X(ctx, args...) → X`
// constrained on `CtxFitsX<X, Ctx>`.  Symmetric to
// effects::mint_cap, effects::mint_from_ctx, concurrent::mint_endpoint,
// concurrent::mint_stage, and concurrent::mint_pipeline.
//
//   Axiom coverage: TypeSafe — proto_row_admitted_by walks the tree
//                   at template-instantiation; mismatches surface
//                   as concept-violation diagnostics at the mint
//                   call site.
//                   InitSafe — pure metafunction during walk.
//                   DetSafe — consteval throughout.
//   Runtime cost:   zero.  The walkers resolve at template
//                   substitution; the returned PSH is the existing
//                   PermissionedSession.h primitive.
//
// ── Concept and factory ─────────────────────────────────────────────
//
//   proto_row_admitted_by<Proto, Ctx>::value — recursive row walker
//   CtxFitsProtocol<Proto, Ctx>              — row-admission concept
//   ProtocolVendorAdmittedByLoopCtx<Proto, L>
//                                           — VendorPinned / Vendor<T>
//                                             admission against LoopCtx
//   CtxFitsPermissionedProtocol<Proto, Ctx,
//                               PS, L>      — row + local PS closure +
//                                             vendor admission
//   mint_permissioned_session<Proto>(ctx,
//       resource, perms...)                  — factory; returns PSH
//   mint_session<Proto>(ctx, resource)        — empty-PS shim
//
// ── Walk shape ──────────────────────────────────────────────────────
//
//   End                       → true (terminal)
//   Continue                  → true (loop closes back; payloads checked at Loop)
//   Send<T, K>                → payload_effect_row_t<T> ⊆ Ctx::row ∧ walk(K)
//   Recv<T, K>                → payload_effect_row_t<T> ⊆ Ctx::row ∧ walk(K)
//   Loop<B>                   → walk(B)
//   Select<Branches...>       → ∀ branch. walk(branch)
//   Offer<Branches...>        → ∀ branch. walk(branch)
//   Offer<Sender<R>, Bs...>   → ∀ branch. walk(branch)
//
// Composed payloads (Refined<P, Linear<Computation<R, T>>>) unwrap
// transparently via payload_effect_row_t while payload_row_t preserves
// non-effect grades such as NumericalTier — see SessionRowExtraction.h.
// Vendor<T> payloads are different: their grade is a wire-placement
// obligation, not an effect row.  GAPS-068 admits them only when the
// surrounding session has an explicit VendorCtx and
// VendorLattice::leq(payload_vendor, session_vendor) holds.
//
// ── Why eager whole-protocol ────────────────────────────────────────
//
// Alternatives (per-operation ctx parameter, ctx as PSH template
// parameter, decorator wrapper) all require either modifying
// existing session machinery (~10,000 LOC, 102+ tests) or adding
// runtime overhead.  Eager whole-protocol mint validates ONCE at
// construction; the resulting handle is the existing unchanged
// primitive.  Zero session-side disturbance.
//
// ── Migration ───────────────────────────────────────────────────────
//
// Production code that calls mint_session_handle<Proto>(resource)
// directly today opts in by replacing one line:
//
//     // Before:
//     auto h = mint_session_handle<Proto>(resource);
//     // After:
//     auto h = mint_session<Proto>(ctx, resource);
//
// The session surface is intentionally compatible for ordinary Send /
// Recv / Select / Offer protocols, but the returned handle now carries
// EmptyPermSet rather than being bare.  Production code that needs an
// initial CSL token uses the ctx-bound mint_permissioned_session overload.

#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/IsVendor.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>          // Stop terminator
#include <crucible/sessions/SessionDelegate.h>       // Delegate / Accept
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/SessionRowExtraction.h>

#include <cstddef>
#include <source_location>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// ── proto_row_admitted_by<Proto, Ctx> — recursive protocol walker ──
//
// Primary template: false_type (unrecognized protocol shape).
// Specializations cover every Session.h combinator.

template <class Proto, class Ctx>
struct proto_row_admitted_by : std::false_type {};

// End: terminal; vacuously admitted.
template <class Ctx>
struct proto_row_admitted_by<End, Ctx> : std::true_type {};

// Stop: BSYZ22 crash-stop terminator (`sessions/SessionCrash.h`).
// Same semantics as End for the row walker — the protocol stops here,
// nothing more to admit.
template <class Ctx>
struct proto_row_admitted_by<Stop, Ctx> : std::true_type {};

// Continue: closes back to enclosing Loop; the loop body's payloads
// were already checked when Loop was visited.  Vacuously admitted.
template <class Ctx>
struct proto_row_admitted_by<Continue, Ctx> : std::true_type {};

// Send<T, K>: payload_effect_row_t<T> must be a Subrow of Ctx's row, AND
// the continuation K must be admitted.
template <class T, class K, class Ctx>
struct proto_row_admitted_by<Send<T, K>, Ctx>
    : std::bool_constant<
          ::crucible::effects::is_subrow_v<
              payload_effect_row_t<T>,
              typename Ctx::row_type>
       && proto_row_admitted_by<K, Ctx>::value>
{};

// Recv<T, K>: symmetric to Send.  Receiving a payload that carries
// row R obliges the receiver to authorize R in its surrounding
// context.
template <class T, class K, class Ctx>
struct proto_row_admitted_by<Recv<T, K>, Ctx>
    : std::bool_constant<
          ::crucible::effects::is_subrow_v<
              payload_effect_row_t<T>,
              typename Ctx::row_type>
       && proto_row_admitted_by<K, Ctx>::value>
{};

// Loop<B>: walk the body.  The body may contain Continue, which
// closes the loop — handled by the Continue specialization above
// (vacuously admitted, since the body's payloads were already
// validated on this Loop walk).
template <class B, class Ctx>
struct proto_row_admitted_by<Loop<B>, Ctx>
    : proto_row_admitted_by<B, Ctx>
{};

template <VendorBackend V, class P, class Ctx>
struct proto_row_admitted_by<VendorPinned<V, P>, Ctx>
    : proto_row_admitted_by<P, Ctx>
{};

// Select<Branches...>: every branch must be admitted (the proposer
// may pick any of them, so all must fit).
template <class... Branches, class Ctx>
struct proto_row_admitted_by<Select<Branches...>, Ctx>
    : std::bool_constant<(proto_row_admitted_by<Branches, Ctx>::value && ...)>
{};

// Offer<Branches...>: symmetric.  The offerer must support every
// branch the peer might pick.
template <class... Branches, class Ctx>
struct proto_row_admitted_by<Offer<Branches...>, Ctx>
    : std::bool_constant<(proto_row_admitted_by<Branches, Ctx>::value && ...)>
{};

// Offer<Sender<Role>, Bs...>: sender-typed Offer; same per-branch
// walk as untagged Offer (the Sender wrapper carries no payload).
template <class Role, class... Branches, class Ctx>
struct proto_row_admitted_by<Offer<Sender<Role>, Branches...>, Ctx>
    : std::bool_constant<(proto_row_admitted_by<Branches, Ctx>::value && ...)>
{};

// CheckpointedSession<Base, Rollback>: BOTH branches are reachable
// (Base when checkpoint succeeds, Rollback when it doesn't).  Both
// must be admitted by the surrounding Ctx.
template <class Base, class Rollback, class Ctx>
struct proto_row_admitted_by<CheckpointedSession<Base, Rollback>, Ctx>
    : std::bool_constant<
          proto_row_admitted_by<Base,     Ctx>::value
       && proto_row_admitted_by<Rollback, Ctx>::value>
{};

// Delegate<T, K>: send my endpoint of a T-typed channel; continue as
// K.  The DELEGATED protocol T is the recipient's responsibility —
// they will execute it under their own Ctx.  We only walk K (our own
// continuation).  This mirrors the crash-walker's discipline (see
// SessionDelegate.h:124-141) which also bypasses T.
template <class T, class K, class Ctx>
struct proto_row_admitted_by<Delegate<T, K>, Ctx>
    : proto_row_admitted_by<K, Ctx>
{};

// Accept<T, K>: symmetric to Delegate.  We receive an endpoint and
// the SENDER had to validate T against their own Ctx.  We walk only
// K because we don't execute T's protocol — we hand it off further
// or store it.  If the receiver actually wants to RUN the accepted
// session, they call mint_session<T>(ctx, accepted_resource) at that
// point, which re-runs the row check against their Ctx.
template <class T, class K, class Ctx>
struct proto_row_admitted_by<Accept<T, K>, Ctx>
    : proto_row_admitted_by<K, Ctx>
{};

template <class Proto, class Ctx>
inline constexpr bool proto_row_admitted_by_v =
    proto_row_admitted_by<Proto, Ctx>::value;

// ── CtxFitsProtocol<Proto, Ctx> ────────────────────────────────────
//
// User-facing concept: `mint_session<Proto>(ctx, res) requires
// CtxFitsProtocol<Proto, Ctx>`.  The constraint also gates Tier 3
// Stage's body-row check (which composes pipelined sessions).

template <class Proto, class Ctx>
concept CtxFitsProtocol = ::crucible::effects::IsExecCtx<Ctx>
                       && proto_row_admitted_by_v<Proto, Ctx>;

namespace detail::session_mint {

template <class Proto, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx;

template <class Payload, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx : std::true_type {};

template <VendorBackend PayloadVendor, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Vendor<PayloadVendor, T>, LoopCtx>
    : std::bool_constant<
          loop_ctx_has_explicit_vendor_v<LoopCtx> &&
          session_vendor_satisfies_v<loop_ctx_vendor_v<LoopCtx>,
                                     PayloadVendor>
      > {};

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
struct payload_vendor_admitted_by_loop_ctx<
    DelegatedSession<InnerProto, InnerPS>, LoopCtx>
    : protocol_vendor_admitted_by_loop_ctx<InnerProto, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<ContentAddressed<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto Pred, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Refined<Pred, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto Pred, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::SealedRefined<Pred, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class Tag, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Tagged<T, Tag>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Linear<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Stale<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::HotPath<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::DetSafe<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::AllocClass<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::ResidencyHeat<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::CipherTier<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::MemOrder<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Wait<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Progress<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <::crucible::safety::Tolerance V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::NumericalTier<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Crash<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Consistency<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <auto V, class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::OpaqueLifetime<V, T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Secret<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Budgeted<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::EpochVersioned<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::NumaPlacement<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::RecipeSpec<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class Cmp, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::Monotonic<T, Cmp>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, auto Max, class Cmp, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::BoundedMonotonic<T, Max, Cmp>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::WriteOnce<T>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, class Cmp, class LoopCtx>
    requires std::is_trivially_copyable_v<T>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::AtomicMonotonic<T, Cmp>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, template <class...> class Storage, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::AppendOnly<T, Storage>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class T, std::size_t N, class Tag, class LoopCtx>
struct payload_vendor_admitted_by_loop_ctx<
    ::crucible::safety::TimeOrdered<T, N, Tag>, LoopCtx>
    : payload_vendor_admitted_by_loop_ctx<T, LoopCtx> {};

template <class Proto, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx : std::false_type {};

template <class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<End, LoopCtx> : std::true_type {};

template <class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Continue, LoopCtx> : std::true_type {};

template <class T, class K, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Send<T, K>, LoopCtx>
    : std::bool_constant<
          payload_vendor_admitted_by_loop_ctx<T, LoopCtx>::value &&
          protocol_vendor_admitted_by_loop_ctx<K, LoopCtx>::value
      > {};

template <class T, class K, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Recv<T, K>, LoopCtx>
    : std::bool_constant<
          payload_vendor_admitted_by_loop_ctx<T, LoopCtx>::value &&
          protocol_vendor_admitted_by_loop_ctx<K, LoopCtx>::value
      > {};

template <class Body, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Loop<Body>, LoopCtx>
    : protocol_vendor_admitted_by_loop_ctx<Body, LoopCtx> {};

template <class... Branches, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Select<Branches...>, LoopCtx>
    : std::bool_constant<
          (protocol_vendor_admitted_by_loop_ctx<Branches, LoopCtx>::value && ...)
      > {};

template <class... Branches, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Offer<Branches...>, LoopCtx>
    : std::bool_constant<
          (protocol_vendor_admitted_by_loop_ctx<Branches, LoopCtx>::value && ...)
      > {};

template <class Role, class... Branches, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<
    Offer<Sender<Role>, Branches...>, LoopCtx>
    : std::bool_constant<
          (protocol_vendor_admitted_by_loop_ctx<Branches, LoopCtx>::value && ...)
      > {};

template <class Base, class Rollback, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<
    CheckpointedSession<Base, Rollback>, LoopCtx>
    : std::bool_constant<
          protocol_vendor_admitted_by_loop_ctx<Base, LoopCtx>::value &&
          protocol_vendor_admitted_by_loop_ctx<Rollback, LoopCtx>::value
      > {};

template <class T, class K, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Delegate<T, K>, LoopCtx>
    : std::bool_constant<
          (!is_vendor_pinned_v<T> ||
           (loop_ctx_has_explicit_vendor_v<LoopCtx> &&
            session_vendor_satisfies_v<loop_ctx_vendor_v<LoopCtx>,
                                       protocol_vendor_v<T>>)) &&
          protocol_vendor_admitted_by_loop_ctx<T, LoopCtx>::value &&
          protocol_vendor_admitted_by_loop_ctx<K, LoopCtx>::value
      > {};

template <class T, class K, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<Accept<T, K>, LoopCtx>
    : protocol_vendor_admitted_by_loop_ctx<Delegate<T, K>, LoopCtx> {};

template <VendorBackend V, class P, class LoopCtx>
struct protocol_vendor_admitted_by_loop_ctx<VendorPinned<V, P>, LoopCtx>
    : std::bool_constant<
          V != VendorBackend::None &&
          (!loop_ctx_has_explicit_vendor_v<LoopCtx> ||
           session_vendor_satisfies_v<loop_ctx_vendor_v<LoopCtx>, V>) &&
          protocol_vendor_admitted_by_loop_ctx<
              P, VendorCtx<V, loop_ctx_inner_t<LoopCtx>>>::value
      > {};

template <class Proto, class LoopCtx>
inline constexpr bool protocol_vendor_admitted_by_loop_ctx_v =
    protocol_vendor_admitted_by_loop_ctx<Proto, LoopCtx>::value;

template <class Proto, class PS, class LoopCtx = void>
struct permission_flow_closes : std::false_type {};

template <class PS, class LoopCtx>
struct permission_flow_closes<End, PS, LoopCtx>
    : std::bool_constant<perm_set_equal_v<PS, EmptyPermSet>> {};

template <CrashClass C, class PS, class LoopCtx>
struct permission_flow_closes<Stop_g<C>, PS, LoopCtx>
    : std::bool_constant<perm_set_equal_v<PS, EmptyPermSet>> {};

template <class PS, class LoopCtx,
          bool InLoop = !std::is_void_v<loop_ctx_inner_t<LoopCtx>>>
struct continue_permission_flow_branch : std::false_type {};

template <class PS, class LoopCtx>
struct continue_permission_flow_branch<PS, LoopCtx, true>
    : std::bool_constant<
          perm_set_equal_v<PS, typename loop_ctx_inner_t<LoopCtx>::entry_perm_set>
      > {};

template <class PS, class LoopCtx>
struct permission_flow_closes<Continue, PS, LoopCtx>
    : continue_permission_flow_branch<PS, LoopCtx> {};

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
    : permission_flow_closes<
          Body, PS, loop_ctx_rebind_inner_t<LoopCtx, LoopContext<Body, PS>>> {};

template <class... Branches, class PS, class LoopCtx>
struct permission_flow_closes<Select<Branches...>, PS, LoopCtx>
    : std::bool_constant<
          (permission_flow_closes<Branches, PS, LoopCtx>::value && ...)> {};

template <class... Branches, class PS, class LoopCtx>
struct permission_flow_closes<Offer<Branches...>, PS, LoopCtx>
    : std::bool_constant<
          (permission_flow_closes<Branches, PS, LoopCtx>::value && ...)> {};

template <class Role, class... Branches, class PS, class LoopCtx>
struct permission_flow_closes<Offer<Sender<Role>, Branches...>, PS, LoopCtx>
    : std::bool_constant<
          (permission_flow_closes<Branches, PS, LoopCtx>::value && ...)> {};

template <class Base, class Rollback, class PS, class LoopCtx>
struct permission_flow_closes<CheckpointedSession<Base, Rollback>, PS, LoopCtx>
    : std::bool_constant<
          permission_flow_closes<Base,     PS, LoopCtx>::value
       && permission_flow_closes<Rollback, PS, LoopCtx>::value> {};

template <class T, class K, class PS, class LoopCtx>
struct permission_flow_closes<Delegate<T, K>, PS, LoopCtx>
    : permission_flow_closes<K, PS, LoopCtx> {};

template <class T, class K, class PS, class LoopCtx>
struct permission_flow_closes<Accept<T, K>, PS, LoopCtx>
    : permission_flow_closes<K, PS, LoopCtx> {};

template <VendorBackend V, class P, class PS, class LoopCtx>
struct permission_flow_closes<VendorPinned<V, P>, PS, LoopCtx>
    : permission_flow_closes<P, PS,
          VendorCtx<V, loop_ctx_inner_t<LoopCtx>>> {};

template <class Proto, class PS, class LoopCtx = void>
inline constexpr bool permission_flow_closes_v =
    permission_flow_closes<Proto, PS, LoopCtx>::value;

}  // namespace detail::session_mint

template <class Proto, class LoopCtx>
concept ProtocolVendorAdmittedByLoopCtx =
    detail::session_mint::protocol_vendor_admitted_by_loop_ctx_v<
        Proto, LoopCtx>;

template <class Proto, class Ctx, class InitialPS, class LoopCtx = void>
concept CtxFitsPermissionedProtocol =
    CtxFitsProtocol<Proto, Ctx>
    && detail::session_mint::permission_flow_closes_v<Proto, InitialPS, LoopCtx>
    && ProtocolVendorAdmittedByLoopCtx<Proto, LoopCtx>;

// ── mint_permissioned_session<Proto>(ctx, resource, perms...) ───────
//
// Ctx-bound factory.  Requires row admission AND local permission-flow
// closure at the construction boundary, then returns the concrete PSH.
// The rvalue Permission parameters are consumed into InitialPS exactly
// like the legacy token mint in PermissionedSession.h; this overload
// adds the ctx gate and the local close-balance check.

template <class Proto,
          ::crucible::effects::IsExecCtx Ctx,
          class Resource,
          class... InitPerms>
    requires CtxFitsPermissionedProtocol<Proto, Ctx, PermSet<InitPerms...>>
[[nodiscard]] constexpr auto mint_permissioned_session(
    Ctx const&,
    Resource&& resource,
    ::crucible::safety::Permission<InitPerms>&&... perms) noexcept
{
    using InitialPS = PermSet<InitPerms...>;
    ((void)perms, ...);

    return detail::mint_permissioned_session_with_loc<Proto, InitialPS, Resource>(
        std::forward<Resource>(resource), std::source_location::current());
}

// ── mint_session<Proto>(ctx, resource) ──────────────────────────────
//
// Backward-compatible empty-PermSet shim.  The construction point now
// uses the same PSH family as the permissioned overload, so production
// code can migrate from row-only session mints to row+CSL mints without
// learning a second construction discipline.

template <class Proto, ::crucible::effects::IsExecCtx Ctx, class Resource>
    requires CtxFitsPermissionedProtocol<Proto, Ctx, EmptyPermSet>
[[nodiscard]] constexpr auto mint_session(
    Ctx const&,
    Resource&& resource,
    std::source_location loc = std::source_location::current()) noexcept
{
    using StoredResource = std::remove_cvref_t<Resource>;
    return detail::mint_permissioned_session_with_loc<Proto, EmptyPermSet, StoredResource>(
        std::forward<Resource>(resource), loc);
}

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::session_mint_self_test {

namespace eff = ::crucible::effects;

// ── End / Continue: vacuously admitted by any Ctx ──────────────────
static_assert( proto_row_admitted_by_v<End,      eff::HotFgCtx>);
static_assert( proto_row_admitted_by_v<End,      eff::BgDrainCtx>);
static_assert( proto_row_admitted_by_v<Continue, eff::HotFgCtx>);

// ── Send<T, End> with bare T (Row<>) admitted by any Ctx ───────────
using SendInt = Send<int, End>;
static_assert( proto_row_admitted_by_v<SendInt, eff::HotFgCtx>);
static_assert( proto_row_admitted_by_v<SendInt, eff::BgDrainCtx>);
static_assert( proto_row_admitted_by_v<SendInt, eff::ColdInitCtx>);

// ── Send<Computation<Row<Bg>, T>, End> ─────────────────────────────
//
// HotFgCtx (row = Row<>) does NOT admit a Bg-effect payload.
// BgDrainCtx (row = Row<Bg, Alloc>) DOES admit Bg-effect payload.
using SendBgComp = Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>;
static_assert(!proto_row_admitted_by_v<SendBgComp, eff::HotFgCtx>);
static_assert( proto_row_admitted_by_v<SendBgComp, eff::BgDrainCtx>);
static_assert( proto_row_admitted_by_v<SendBgComp, eff::BgCompileCtx>);

// ── Multi-step Send chain ──────────────────────────────────────────
using SendChain = Send<int, Send<eff::Computation<eff::Row<eff::Effect::Alloc>, int>, End>>;
static_assert(!proto_row_admitted_by_v<SendChain, eff::HotFgCtx>);     // Alloc not in Fg row
static_assert( proto_row_admitted_by_v<SendChain, eff::BgDrainCtx>);   // Alloc in Bg row

// ── Loop<Send<T, Continue>> — the canonical SPSC producer pattern ──
using LoopSendBg = Loop<Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, Continue>>;
static_assert(!proto_row_admitted_by_v<LoopSendBg, eff::HotFgCtx>);
static_assert( proto_row_admitted_by_v<LoopSendBg, eff::BgDrainCtx>);

// ── Loop<Recv<T, Continue>> — the canonical SPSC consumer pattern ──
using LoopRecvBg = Loop<Recv<eff::Computation<eff::Row<eff::Effect::Bg>, int>, Continue>>;
static_assert(!proto_row_admitted_by_v<LoopRecvBg, eff::HotFgCtx>);
static_assert( proto_row_admitted_by_v<LoopRecvBg, eff::BgDrainCtx>);

// ── Capability transmission ────────────────────────────────────────
//
// Send<Capability<Alloc, Bg>, End>: payload conveys Effect::Alloc.
// Admitted by BgDrainCtx (Alloc in row); not by HotFgCtx (empty row).
using SendCap = Send<eff::Capability<eff::Effect::Alloc, eff::Bg>, End>;
static_assert(!proto_row_admitted_by_v<SendCap, eff::HotFgCtx>);
static_assert( proto_row_admitted_by_v<SendCap, eff::BgDrainCtx>);

// ── Select / Offer fan-out ─────────────────────────────────────────
using SelectMix = Select<
    Send<int, End>,                                                 // Row<>
    Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>>;   // Row<Bg>
// HotFgCtx: branch 0 fits, branch 1 doesn't → entire Select fails.
static_assert(!proto_row_admitted_by_v<SelectMix, eff::HotFgCtx>);
static_assert( proto_row_admitted_by_v<SelectMix, eff::BgDrainCtx>);

// ── CtxFitsProtocol concept ────────────────────────────────────────
static_assert( CtxFitsProtocol<End,         eff::HotFgCtx>);
static_assert( CtxFitsProtocol<SendInt,     eff::HotFgCtx>);
static_assert(!CtxFitsProtocol<SendBgComp,  eff::HotFgCtx>);
static_assert( CtxFitsProtocol<SendBgComp,  eff::BgDrainCtx>);
static_assert(!CtxFitsProtocol<int,         eff::HotFgCtx>);  // int isn't a protocol → false_type primary

struct WorkPerm {};
struct FakeResource {};

using SendTransfer = Send<Transferable<int, WorkPerm>, End>;
static_assert( detail::session_mint::permission_flow_closes_v<
    SendTransfer, PermSet<WorkPerm>>);
static_assert(!detail::session_mint::permission_flow_closes_v<
    SendTransfer, EmptyPermSet>);
static_assert(!detail::session_mint::permission_flow_closes_v<
    End, PermSet<WorkPerm>>);

using RecvThenReturn = Recv<Transferable<int, WorkPerm>,
                            Send<Returned<int, WorkPerm>, End>>;
static_assert( detail::session_mint::permission_flow_closes_v<
    RecvThenReturn, EmptyPermSet>);

using TransferBgComp = Send<
    Transferable<eff::Computation<eff::Row<eff::Effect::Bg>, int>, WorkPerm>,
    End>;
static_assert(!CtxFitsProtocol<TransferBgComp, eff::HotFgCtx>);
static_assert( CtxFitsProtocol<TransferBgComp, eff::BgDrainCtx>);
static_assert( CtxFitsPermissionedProtocol<
    TransferBgComp, eff::BgDrainCtx, PermSet<WorkPerm>>);
static_assert(!CtxFitsPermissionedProtocol<
    TransferBgComp, eff::HotFgCtx, PermSet<WorkPerm>>);

using NvIntPayload =
    ::crucible::safety::Vendor<VendorBackend::NV, int>;
using AmdIntPayload =
    ::crucible::safety::Vendor<VendorBackend::AMD, int>;
using PortableIntPayload =
    ::crucible::safety::Vendor<VendorBackend::Portable, int>;
using SendNvPayload = Send<NvIntPayload, End>;
using RecvAmdPayload = Recv<AmdIntPayload, End>;
using WrappedNvPayload = Transferable<
    ::crucible::safety::NumericalTier<
        ::crucible::safety::Tolerance::BITEXACT, NvIntPayload>,
    WorkPerm>;

static_assert(!ProtocolVendorAdmittedByLoopCtx<SendNvPayload, void>);
static_assert( ProtocolVendorAdmittedByLoopCtx<
    SendNvPayload, VendorCtx<VendorBackend::NV>>);
static_assert( ProtocolVendorAdmittedByLoopCtx<
    SendNvPayload, VendorCtx<VendorBackend::Portable>>);
static_assert(!ProtocolVendorAdmittedByLoopCtx<
    SendNvPayload, VendorCtx<VendorBackend::AMD>>);
static_assert(!ProtocolVendorAdmittedByLoopCtx<
    RecvAmdPayload, VendorCtx<VendorBackend::NV>>);
static_assert(!ProtocolVendorAdmittedByLoopCtx<
    Send<PortableIntPayload, End>, VendorCtx<VendorBackend::NV>>);
static_assert(!ProtocolVendorAdmittedByLoopCtx<
    Send<WrappedNvPayload, End>, void>);
static_assert( ProtocolVendorAdmittedByLoopCtx<
    Send<WrappedNvPayload, End>, VendorCtx<VendorBackend::NV>>);

using PinnedNvSend = VendorPinned<VendorBackend::NV, SendNvPayload>;
static_assert( CtxFitsPermissionedProtocol<
    PinnedNvSend, eff::HotFgCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<
    SendNvPayload, eff::HotFgCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<
    VendorPinned<VendorBackend::None, End>, eff::HotFgCtx, EmptyPermSet>);
static_assert( CtxFitsPermissionedProtocol<
    SendNvPayload, eff::HotFgCtx, EmptyPermSet,
    VendorCtx<VendorBackend::NV>>);
static_assert(!CtxFitsPermissionedProtocol<
    RecvAmdPayload, eff::HotFgCtx, EmptyPermSet,
    VendorCtx<VendorBackend::NV>>);

using NvCarrierDelegatesNv =
    VendorPinned<VendorBackend::NV,
                 Delegate<VendorPinned<VendorBackend::NV, End>, End>>;
using NvCarrierDelegatesAmd =
    VendorPinned<VendorBackend::NV,
                 Delegate<VendorPinned<VendorBackend::AMD, End>, End>>;
static_assert( CtxFitsPermissionedProtocol<
    NvCarrierDelegatesNv, eff::HotFgCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<
    NvCarrierDelegatesAmd, eff::HotFgCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<
    VendorPinned<VendorBackend::NV,
                 Select<VendorPinned<VendorBackend::AMD, End>, End>>,
    eff::HotFgCtx, EmptyPermSet>);

using CtxBoundPsh = decltype(mint_permissioned_session<SendTransfer>(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<FakeResource>(),
    std::declval<::crucible::safety::Permission<WorkPerm>&&>()));
static_assert(std::is_same_v<typename CtxBoundPsh::protocol, SendTransfer>);
static_assert(std::is_same_v<typename CtxBoundPsh::perm_set,
                             PermSet<WorkPerm>>);

using EmptyShim = decltype(mint_session<SendInt>(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<FakeResource>()));
static_assert(std::is_same_v<typename EmptyShim::protocol, SendInt>);
static_assert(std::is_same_v<typename EmptyShim::perm_set, EmptyPermSet>);

// ── Stop terminator (BSYZ22 crash-stop) ────────────────────────────
static_assert( proto_row_admitted_by_v<Stop, eff::HotFgCtx>);
static_assert( proto_row_admitted_by_v<Stop, eff::BgDrainCtx>);
// Send<T, Stop> behaves like Send<T, End> for the row walker.
using SendThenStop = Send<int, Stop>;
static_assert( proto_row_admitted_by_v<SendThenStop, eff::HotFgCtx>);
using SendBgThenStop = Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, Stop>;
static_assert(!proto_row_admitted_by_v<SendBgThenStop, eff::HotFgCtx>);
static_assert( proto_row_admitted_by_v<SendBgThenStop, eff::BgDrainCtx>);

// ── CheckpointedSession<Base, Rollback> ────────────────────────────
//
// BOTH branches must fit.  If Base is row-admitted by Ctx but
// Rollback isn't, the whole thing fails — the rollback path is
// reachable on checkpoint failure, so its row counts.

using CkptSafe = CheckpointedSession<Send<int, End>, Recv<int, End>>;
static_assert( proto_row_admitted_by_v<CkptSafe, eff::HotFgCtx>);

using CkptBgRollback = CheckpointedSession<
    Send<int, End>,                                             // Row<>
    Recv<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>>;  // Row<Bg>
static_assert(!proto_row_admitted_by_v<CkptBgRollback, eff::HotFgCtx>);   // rollback row unfit
static_assert( proto_row_admitted_by_v<CkptBgRollback, eff::BgDrainCtx>); // both fit

// ── Delegate<T, K> / Accept<T, K> ──────────────────────────────────
//
// The delegated/accepted protocol T is the PEER'S problem.  Only the
// continuation K matters for our row-fit.  This means a Hot-fg ctx
// CAN delegate a Bg-effect channel — the recipient validates T at
// their own mint_session<T>(...) site.

using DelegateBgChannel = Delegate<
    Loop<Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, Continue>>,  // T (recipient's row)
    End>;                                                                     // K (our continuation)
static_assert( proto_row_admitted_by_v<DelegateBgChannel, eff::HotFgCtx>);
// But if our CONTINUATION K has Bg payload, that DOES fail Hot-fg.
using DelegateBgChannelBgK = Delegate<
    Loop<Recv<int, Continue>>,                                                // T (free)
    Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>>;             // K (Bg!)
static_assert(!proto_row_admitted_by_v<DelegateBgChannelBgK, eff::HotFgCtx>);

using AcceptThenSendBg = Accept<
    Send<int, End>,                                                            // T
    Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>>;              // K
static_assert(!proto_row_admitted_by_v<AcceptThenSendBg, eff::HotFgCtx>);
static_assert( proto_row_admitted_by_v<AcceptThenSendBg, eff::BgDrainCtx>);

// ── ContentAddressed payload via Send/Recv ─────────────────────────
//
// ContentAddressed<T> unwraps transparently in payload_row, so the
// protocol walker treats Send<ContentAddressed<T>, K> exactly like
// Send<T, K>.  Verifies the unwrap composes through Send/Recv.

using SendCa = Send<ContentAddressed<int>, End>;
static_assert( proto_row_admitted_by_v<SendCa, eff::HotFgCtx>);

using SendCaBg = Send<ContentAddressed<
    eff::Computation<eff::Row<eff::Effect::Bg>, int>>, End>;
static_assert(!proto_row_admitted_by_v<SendCaBg, eff::HotFgCtx>);
static_assert( proto_row_admitted_by_v<SendCaBg, eff::BgDrainCtx>);

}  // namespace detail::session_mint_self_test

// ── Runtime smoke test ──────────────────────────────────────────────

[[gnu::cold]] inline void runtime_smoke_test_session_mint() noexcept {
    namespace eff = ::crucible::effects;

    // ── mint_session against HotFgCtx for a bare-T protocol ─────────
    //
    // The protocol Loop<Send<int, Continue>> sends bare ints
    // (Row<>); admitted by any Ctx including HotFgCtx.  We don't
    // actually instantiate a real session here (the Resource type
    // would need a Pinned channel); the static_asserts confirm
    // mint_session is *callable* with the right concept gate.

    using PureLoop = Loop<Send<int, Continue>>;
    eff::HotFgCtx fg;
    static_cast<void>(fg);
    static_assert( CtxFitsProtocol<PureLoop, eff::HotFgCtx>);
    static_assert( CtxFitsProtocol<PureLoop, eff::BgDrainCtx>);

    // ── BgDrainCtx admits a Bg-effect protocol ─────────────────────
    using BgLoop = Loop<Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>,
                              Continue>>;
    static_assert(!CtxFitsProtocol<BgLoop, eff::HotFgCtx>);
    static_assert( CtxFitsProtocol<BgLoop, eff::BgDrainCtx>);
}

}  // namespace crucible::safety::proto
