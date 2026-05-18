#pragma once

// ── crucible::sessions::payload_row<T> — extract a payload's row ────
//
// `payload_row_t<T>` projects a session-payload type to the
// effects::Row it "carries".  Used by SessionMint.h's
// CtxFitsProtocol concept to walk a session-protocol tree and
// verify that every Send<T, K>'s payload is row-admitted by the
// surrounding Ctx; reused by Tier 3's Stage to validate body
// input/output payloads against the stage's Ctx.
//
// Default: bare T carries no effects (Row<>).  Specialisations:
//
//   Computation<R, T>      → R                      (canonical row carrier)
//   Capability<E, S>       → Row<E>                 (sending a cap conveys E)
//   Refined<P, T>          → payload_row<T>          (transparent unwrap)
//   SealedRefined<P, T>    → payload_row<T>          (transparent unwrap)
//   Tagged<T, S>           → payload_row<T>          (transparent unwrap)
//   Linear<T>              → payload_row<T>          (transparent unwrap)
//   Stale<T>               → payload_row<T>          (transparent unwrap)
//   NumericalTier<V, T>    → NumericalPayloadRow<V,
//                              payload_row<T>>       (preserve grade)
//   Transferable<T, X>     → payload_row<T>          (transparent unwrap)
//   Borrowed<T, X>         → payload_row<T>          (transparent unwrap)
//   Returned<T, X>         → payload_row<T>          (transparent unwrap)
//   DelegatedSession<P,
//                    InnerPS>
//                          → protocol_effect_row<P>  (higher-order capability)
//
// Composed payloads (Refined<P, Linear<Tagged<Computation<R, T>, S>>>)
// unwrap transparently — every value-level wrapper that "passes
// through" effects specialises to recurse on its element type.  The
// base case (bare T) yields Row<>.  Consumers that need only the
// effects::Row use payload_effect_row_t<T>; consumers that audit the
// full payload contract use payload_row_t<T> and keep wrapper grades
// such as NumericalTier's tolerance axis visible.
//
//   Axiom coverage: TypeSafe — pure metafunction; mismatches surface
//                   at template-substitution.
//                   InitSafe — no construction at this layer.
//                   DetSafe — consteval throughout.
//   Runtime cost:   zero.
//
// ── Why this lives in sessions/ ─────────────────────────────────────
//
// The trait is consumed by SessionMint's protocol walker AND by
// Tier 3's Stage body-row check.  Putting it in sessions/ keeps it
// near the SessionMint consumer; Tier 3 includes it from
// sessions/SessionRowExtraction.h directly.  Putting it in effects/
// would invert the layering — effects/ is the row vocabulary, not
// the row-extraction infrastructure.
//
// ── Extending payload_row<> for new wrappers ────────────────────────
//
// User code that ships its own value-level wrapper (e.g., a custom
// Refined-style wrapper) can specialise payload_row<MyWrapper<T>> in
// the user's own translation unit.  The default-Row<> base case
// covers any unrecognised type.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Capability.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
// Session protocol primitives — needed by `protocol_effect_row<Proto>`, the
// recursive walker that powers `payload_row<DelegatedSession<P, IPS>>`.
// Without these, sending a DelegatedSession<HighIO_Proto, ...> across a
// channel would fall through to the primary `payload_row<>` and yield
// Row<>, silently hiding every effect that running the inner protocol
// would carry.  Closes fixy-A2-010.
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
// Value-level wrappers — every wrapper that can appear in a Send/Recv
// payload position must specialize payload_row<>.  Per CLAUDE.md §XXI's
// AUDIT-2 closure: missing specializations are SOUNDNESS BUGS — the
// walker silently undercounts effects when an unrecognized wrapper
// hides a Computation<R, T> inside it.  All shipped graded wrappers
// from safety/Safety.h are specialized below.
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/RecipeSpec.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Wait.h>
#include <crucible/sessions/SessionContentAddressed.h>
#include <crucible/sessions/SessionPermPayloads.h>

#include <type_traits>

namespace crucible::safety::proto {

// ── Payload-row wrappers ────────────────────────────────────────────
//
// Most payload rows are directly effects::Row<...>.  NumericalTier is
// different: dropping its tolerance grade at the wire boundary loses
// the MIMIC §41 numerical contract.  NumericalPayloadRow pairs the
// tolerance grade with the inner payload row while payload_effect_row_t
// still exposes the effects::Row needed by CtxFitsProtocol and Stage.

template <::crucible::safety::Tolerance Tier, class InnerRow>
struct NumericalPayloadRow {
    using effect_row = InnerRow;
    static constexpr ::crucible::safety::Tolerance tolerance = Tier;
};

template <class PayloadRow>
struct payload_row_effect {
    using type = PayloadRow;
};

template <::crucible::safety::Tolerance Tier, class InnerRow>
struct payload_row_effect<NumericalPayloadRow<Tier, InnerRow>>
    : payload_row_effect<InnerRow> {};

template <class PayloadRow>
using payload_row_effect_t = typename payload_row_effect<PayloadRow>::type;

// ── payload_row<T> — primary trait + canonical specialisations ──────

// Default: a bare T carries no effects.  Bare ints, doubles, POD
// structs, raw pointers — all yield the empty row.  This is the
// "base of the recursion" for transparent-unwrap specialisations.
template <class T>
struct payload_row {
    using type = ::crucible::effects::Row<>;
};

// Computation<R, T> is THE canonical row carrier.  Sending a
// Computation<R, T> means "this payload was produced under row R";
// the receiver inherits the obligation that R was authorized.
template <class R, class T>
struct payload_row<::crucible::effects::Computation<R, T>> {
    using type = R;
};

// Capability<E, S>: sending the cap CONVEYS its effect.  The
// receiver gains authority to perform E.  Source S is informational
// at the row level — the row carries only E.
template <::crucible::effects::Effect E, class S>
struct payload_row<::crucible::effects::Capability<E, S>> {
    using type = ::crucible::effects::Row<E>;
};

// ── Transparent-unwrap specialisations ──────────────────────────────
//
// Each value-level wrapper that "passes through" effects (Refined,
// SealedRefined, Tagged, Linear, Stale) specialises payload_row to
// recurse on its element type.  The default-base-case handles bare
// T at the bottom of the chain.

template <auto Pred, class T>
struct payload_row<::crucible::safety::Refined<Pred, T>>
    : payload_row<T> {};

template <auto Pred, class T>
struct payload_row<::crucible::safety::SealedRefined<Pred, T>>
    : payload_row<T> {};

template <class T, class Tag>
struct payload_row<::crucible::safety::Tagged<T, Tag>>
    : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::Linear<T>>
    : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::Stale<T>>
    : payload_row<T> {};

// ContentAddressed<T> is a session-level payload marker that quotients
// payloads by content hash (Appendix D.5).  It carries no effects of
// its own — the underlying T is what carries the row.  Transparent
// unwrap.
template <class T>
struct payload_row<ContentAddressed<T>>
    : payload_row<T> {};

// Permission-flow payload markers are transparent for row accounting:
// the wrapper moves or borrows CSL authority, while the carried value T
// is still the thing whose effect row must be admitted by the ctx.
template <class T, class Tag>
struct payload_row<Transferable<T, Tag>>
    : payload_row<T> {};

template <class T, class Tag>
struct payload_row<Borrowed<T, Tag>>
    : payload_row<T> {};

template <class T, class Tag>
struct payload_row<Returned<T, Tag>>
    : payload_row<T> {};

// ── Single-axis policy wrappers (chain-lattice family) ─────────────
//
// Each of these wrappers carries a compile-time POLICY axis (HotPath
// tier, DetSafe tier, AllocClass tag, etc.) but NO effect of its own.
// The payload's effect row lives entirely in the underlying T.
// Transparent unwrap so payload_row sees through the policy layer to
// the inner Computation/Capability/etc.
//
// AUDIT-2 closure (CLAUDE.md §XXI): missing specializations are
// SOUNDNESS BUGS — without these specs, a `Send<HotPath<Hot,
// Computation<Row<Bg>, T>>, End>` would silently fall to the primary
// template's Row<> default, hiding the Bg effect from the walker and
// admitting the protocol on a HotFgCtx (Row<>) that should reject it.

template <auto V, class T>
struct payload_row<::crucible::safety::HotPath<V, T>>        : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::DetSafe<V, T>>        : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::AllocClass<V, T>>     : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::ResidencyHeat<V, T>>  : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::CipherTier<V, T>>     : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::MemOrder<V, T>>       : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::Wait<V, T>>           : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::Progress<V, T>>       : payload_row<T> {};

template <::crucible::safety::Tolerance V, class T>
struct payload_row<::crucible::safety::NumericalTier<V, T>> {
    using type = NumericalPayloadRow<V, typename payload_row<T>::type>;
};

template <auto V, class T>
struct payload_row<::crucible::safety::Vendor<V, T>>         : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::Crash<V, T>>          : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::Consistency<V, T>>    : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::OpaqueLifetime<V, T>> : payload_row<T> {};

// ── Single-T policy wrappers (no axis parameter) ───────────────────
//
// These wrappers carry their grade INTERNALLY (per-instance), with no
// type-level axis parameter.  The payload's effect row still lives in
// the underlying T.

template <class T>
struct payload_row<::crucible::safety::Secret<T>>            : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::Budgeted<T>>          : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::EpochVersioned<T>>    : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::NumaPlacement<T>>     : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::RecipeSpec<T>>        : payload_row<T> {};

// ── Mutation family — state-holder wrappers ────────────────────────
//
// Monotonic / AppendOnly / WriteOnce / etc. wrap a value or a
// container.  The payload's effect row lives in the element type T.
// AppendOnly's Storage<T> container is opaque at the row level — only
// T matters for effect propagation.

template <class T, class Cmp>
struct payload_row<::crucible::safety::Monotonic<T, Cmp>>    : payload_row<T> {};

template <class T, auto Max, class Cmp>
struct payload_row<::crucible::safety::BoundedMonotonic<T, Max, Cmp>>
    : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::WriteOnce<T>>         : payload_row<T> {};

template <class T, class Cmp>
    requires std::is_trivially_copyable_v<T>
struct payload_row<::crucible::safety::AtomicMonotonic<T, Cmp>>
    : payload_row<T> {};

template <class T, template <class...> class Storage>
struct payload_row<::crucible::safety::AppendOnly<T, Storage>>
    : payload_row<T> {};

// ── TimeOrdered — happens-before lattice wrapper ───────────────────
//
// T carries the payload effect; N is the happens-before capacity, Tag
// is identity.  Transparent unwrap to T.

template <class T, std::size_t N, class Tag>
struct payload_row<::crucible::safety::TimeOrdered<T, N, Tag>>
    : payload_row<T> {};

// ── protocol_effect_row<Proto> — higher-order capability walker ────
//
// Closes fixy-A2-010.  When a session `Send<DelegatedSession<P, IPS>, K>`
// transfers an endpoint of protocol P to the receiver, the receiver
// gains the capability to RUN P.  By the same principle that gives
// `payload_row<Capability<E, S>>` = `Row<E>` (sending a cap conveys its
// effect), sending a DelegatedSession conveys every effect P's
// operations could trigger.  The outer Ctx must therefore admit the
// union of P's per-step effect rows, just as if the sender had to
// possess those effects to legitimately transfer the authority.
//
// Without this spec, `payload_row<DelegatedSession<HighIO_Proto, IPS>>`
// falls through to the primary template's `Row<>` default — a Fg
// empty-row Ctx silently admits a session that contains
// `Send<DelegatedSession<IO_heavy_proto, ...>, ...>`, defeating the
// fundamental row-admission discipline that `mint_permissioned_session`'s
// `CtxFitsProtocol` concept is supposed to enforce.
//
// Walk shape (mirrors `proto_row_admitted_by` in SessionMint.h):
//
//   End                         → Row<>
//   Stop_g<C>                   → Row<>
//   Continue                    → Row<>
//   Send<T, K>                  → row_union(payload_effect_row<T>,
//                                            protocol_effect_row<K>)
//   Recv<T, K>                  → same as Send
//   Loop<B>                     → protocol_effect_row<B>
//   VendorPinned<V, P>          → protocol_effect_row<P>
//   Select<Bs...>               → union over branches
//   Offer<Bs...>                → union over branches
//   Offer<Sender<R>, Bs...>     → union over branches
//   CheckpointedSession<B, R>   → row_union(walk(B), walk(R))
//   Delegate<T, K>              → protocol_effect_row<K>
//                                  (T is recipient's-of-the-Delegate
//                                   responsibility per existing
//                                   `proto_row_admitted_by<Delegate>`
//                                   semantics — sender of the Delegate
//                                   does not execute T; same discipline
//                                   carries through nested delegation)
//   Accept<T, K>                → protocol_effect_row<K>  (symmetric)
//   EpochedDelegate<T, K, ...>  → protocol_effect_row<Delegate<T, K>>
//   EpochedAccept<T, K, ...>    → protocol_effect_row<Accept<T, K>>
//
//   Axiom coverage: TypeSafe / InitSafe / DetSafe — pure metafunction.
//   Runtime cost:   zero.

template <class Proto>
struct protocol_effect_row;

// End: terminal; carries no effects.
template <>
struct protocol_effect_row<End> {
    using type = ::crucible::effects::Row<>;
};

// Continue: closes back to enclosing Loop; payloads already accounted
// for when Loop was visited.
template <>
struct protocol_effect_row<Continue> {
    using type = ::crucible::effects::Row<>;
};

// Stop_g<CrashClass>: crash-stop terminator (covers `Stop`, which is
// the `Stop_g<CrashClass::Abort>` alias).  Same row semantics as End.
template <CrashClass C>
struct protocol_effect_row<Stop_g<C>> {
    using type = ::crucible::effects::Row<>;
};

// Send<T, K>: union of payload's row with continuation's row.
// Uses the explicit `payload_row_effect_t<payload_row<T>::type>`
// expansion because the alias `payload_effect_row_t` is defined later
// in the file (after the `payload_row_t` alias); the inlined form here
// depends only on the primary trait and the wrapper-row projector,
// both of which are visible above.
template <class T, class K>
struct protocol_effect_row<Send<T, K>> {
    using type = ::crucible::effects::row_union_t<
        payload_row_effect_t<typename payload_row<T>::type>,
        typename protocol_effect_row<K>::type>;
};

// Recv<T, K>: symmetric to Send.
template <class T, class K>
struct protocol_effect_row<Recv<T, K>> {
    using type = ::crucible::effects::row_union_t<
        payload_row_effect_t<typename payload_row<T>::type>,
        typename protocol_effect_row<K>::type>;
};

// Loop<B>: walk the body.
template <class B>
struct protocol_effect_row<Loop<B>>
    : protocol_effect_row<B> {};

// VendorPinned<V, P>: walk the inner protocol; vendor-axis is non-effect.
template <VendorBackend V, class P>
struct protocol_effect_row<VendorPinned<V, P>>
    : protocol_effect_row<P> {};

namespace detail::protocol_effect_row_fold {

template <class... Rows>
struct row_union_pack;

template <>
struct row_union_pack<> {
    using type = ::crucible::effects::Row<>;
};

template <class R>
struct row_union_pack<R> {
    using type = R;
};

template <class R1, class R2, class... Rest>
struct row_union_pack<R1, R2, Rest...> {
    using type = typename row_union_pack<
        ::crucible::effects::row_union_t<R1, R2>, Rest...>::type;
};

template <class... Rows>
using row_union_pack_t = typename row_union_pack<Rows...>::type;

}  // namespace detail::protocol_effect_row_fold

// Select<Branches...>: union over all branches (the proposer may pick
// any, so the surrounding Ctx must admit the worst case).
template <class... Branches>
struct protocol_effect_row<Select<Branches...>> {
    using type = detail::protocol_effect_row_fold::row_union_pack_t<
        typename protocol_effect_row<Branches>::type...>;
};

// Offer<Branches...>: symmetric — the offerer must support every branch.
template <class... Branches>
struct protocol_effect_row<Offer<Branches...>> {
    using type = detail::protocol_effect_row_fold::row_union_pack_t<
        typename protocol_effect_row<Branches>::type...>;
};

// Offer<Sender<Role>, Branches...>: sender-typed Offer; Sender wrapper
// carries no payload, walk the same branch pack.
template <class Role, class... Branches>
struct protocol_effect_row<Offer<Sender<Role>, Branches...>> {
    using type = detail::protocol_effect_row_fold::row_union_pack_t<
        typename protocol_effect_row<Branches>::type...>;
};

// CheckpointedSession<Base, Rollback>: BOTH branches are reachable.
template <class Base, class Rollback>
struct protocol_effect_row<CheckpointedSession<Base, Rollback>> {
    using type = ::crucible::effects::row_union_t<
        typename protocol_effect_row<Base>::type,
        typename protocol_effect_row<Rollback>::type>;
};

// Delegate<T, K>: mirror `proto_row_admitted_by<Delegate, Ctx>`'s
// discipline — the SENDER of the Delegate doesn't execute T; the
// recipient is responsible for T's effects under their own Ctx.  Walk
// only K (the sender's continuation).  Nested DelegatedSession payloads
// inside K still surface via the payload_row<DelegatedSession<...>>
// specialisation below.
template <class T, class K>
struct protocol_effect_row<Delegate<T, K>>
    : protocol_effect_row<K> {};

// fixy-A2-028 bottom-preservation: Delegate<Stop_g<C>, K> ALIGNS WITH
// `compose<Delegate<Stop_g<C>, K>, Q> = Stop_g<C>` (SessionDelegate.h
// :711-713) — the carrier already crashed at handoff, so K is
// unreachable.  Without this specialisation, the standalone protocol
// `Delegate<Stop_g<C>, K>` reports `protocol_effect_row<K>`, but
// after composing with ANY Q the resulting `Stop_g<C>` reports
// `Row<>` (SessionRowExtraction.h:390).  That asymmetry is ROW
// NARROWING UNDER COMPOSITION: a user composing the protocol gets
// strictly weaker Ctx admission than the original.  Specialising on
// `T = Stop_g<C>` makes the trait agree with the compose semantics:
// the carrier is bottom either way, K's effects are unreachable
// either way, the row is `Row<>` either way.  Bottom-preservation is
// uniform across compose, subtyping (SessionDelegate.h:809-820), AND
// the row trait.
template <CrashClass C, class K>
struct protocol_effect_row<Delegate<Stop_g<C>, K>> {
    using type = ::crucible::effects::Row<>;
};

// Accept<T, K>: symmetric to Delegate.
template <class T, class K>
struct protocol_effect_row<Accept<T, K>>
    : protocol_effect_row<K> {};

// fixy-A2-028 bottom-preservation: Accept<Stop_g<C>, K> mirrors the
// Delegate-side rule for the same reason — the recipient accepted an
// already-crashed delegated endpoint, so K is unreachable.  Compose
// (SessionDelegate.h:731-734) and subtyping (A2-002) already enforce
// bottom-preservation on the Accept arm; the row trait now matches.
template <CrashClass C, class K>
struct protocol_effect_row<Accept<Stop_g<C>, K>> {
    using type = ::crucible::effects::Row<>;
};

// EpochedDelegate / EpochedAccept: forward to the un-epoched variant.
// The bottom-preservation specs above propagate transparently — when
// T = Stop_g<C>, this inherits from `protocol_effect_row<Delegate<
// Stop_g<C>, K>>` which yields `Row<>`.
template <class T, class K,
          std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct protocol_effect_row<
    EpochedDelegate<T, K, MinEpoch, MinGeneration>>
    : protocol_effect_row<Delegate<T, K>> {};

template <class T, class K,
          std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct protocol_effect_row<
    EpochedAccept<T, K, MinEpoch, MinGeneration>>
    : protocol_effect_row<Accept<T, K>> {};

template <class Proto>
using protocol_effect_row_t = typename protocol_effect_row<Proto>::type;

// ── DelegatedSession<P, InnerPS> ───────────────────────────────────
//
// fixy-A2-010 fix: higher-order capability transfer.  Conveys the union
// of every effect row P's Send/Recv operations carry, by symmetry with
// `payload_row<Capability<E, S>>` = `Row<E>`.  The InnerPS axis is
// permission-token bookkeeping handled by SessionPermPayloads.h's
// compute_perm_set_after_send/_recv specialisations — it is NOT a
// row-admission concern, so InnerPS does not contribute here.

template <class InnerProto, class InnerPS>
struct payload_row<DelegatedSession<InnerProto, InnerPS>> {
    using type = protocol_effect_row_t<InnerProto>;
};

// ── User alias ──────────────────────────────────────────────────────

template <class T>
using payload_row_t = typename payload_row<T>::type;

template <class T>
using payload_effect_row_t = payload_row_effect_t<payload_row_t<T>>;

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::payload_row_self_test {

namespace eff = ::crucible::effects;
namespace saf = ::crucible::safety;

// ── Default: bare types yield Row<> ────────────────────────────────
static_assert(std::is_same_v<payload_row_t<int>,    eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<double>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<void*>,  eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<char>,   eff::Row<>>);

struct UserPod { int x; double y; };
static_assert(std::is_same_v<payload_row_t<UserPod>, eff::Row<>>);

// ── Computation<R, T> yields R ─────────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<eff::Computation<eff::Row<>, int>>,
    eff::Row<>>);

static_assert(std::is_same_v<
    payload_row_t<eff::Computation<eff::Row<eff::Effect::Bg>, int>>,
    eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<
    payload_row_t<eff::Computation<eff::Row<eff::Effect::Bg, eff::Effect::Alloc>, double>>,
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);

// ── Capability<E, S> yields Row<E> ─────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<eff::Capability<eff::Effect::Alloc, eff::Bg>>,
    eff::Row<eff::Effect::Alloc>>);

static_assert(std::is_same_v<
    payload_row_t<eff::Capability<eff::Effect::IO, eff::Init>>,
    eff::Row<eff::Effect::IO>>);

static_assert(std::is_same_v<
    payload_row_t<eff::Capability<eff::Effect::Block, eff::Test>>,
    eff::Row<eff::Effect::Block>>);

// ── Refined<P, T> unwraps ──────────────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<saf::Refined<saf::positive, int>>,
    eff::Row<>>);

static_assert(std::is_same_v<
    payload_row_t<saf::Refined<saf::positive,
                                eff::Computation<eff::Row<eff::Effect::Bg>, int>>>,
    eff::Row<eff::Effect::Bg>>);

// ── SealedRefined unwraps similarly ────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<saf::SealedRefined<saf::positive,
                                      eff::Computation<eff::Row<eff::Effect::IO>, int>>>,
    eff::Row<eff::Effect::IO>>);

// ── Linear<T> unwraps ──────────────────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<saf::Linear<eff::Computation<eff::Row<eff::Effect::IO>, int>>>,
    eff::Row<eff::Effect::IO>>);

static_assert(std::is_same_v<
    payload_row_t<saf::Linear<int>>,
    eff::Row<>>);

// ── Tagged<T, Tag> unwraps ─────────────────────────────────────────
struct ProvTag {};
static_assert(std::is_same_v<
    payload_row_t<saf::Tagged<eff::Computation<eff::Row<eff::Effect::Bg>, int>, ProvTag>>,
    eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<
    payload_row_t<saf::Tagged<int, ProvTag>>,
    eff::Row<>>);

// ── Stale<T> unwraps ───────────────────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<saf::Stale<eff::Computation<eff::Row<eff::Effect::Alloc>, int>>>,
    eff::Row<eff::Effect::Alloc>>);

// ── Composed unwrap chains ─────────────────────────────────────────
//
// The payload_row<> chain handles arbitrary nesting transparently:
// Refined<P, Linear<Tagged<Computation<R, T>, Tag>>> → R.

using ComposedT =
    saf::Refined<saf::positive,
        saf::Linear<
            saf::Tagged<
                eff::Computation<eff::Row<eff::Effect::Bg, eff::Effect::Alloc>, int>,
                ProvTag>>>;
static_assert(std::is_same_v<
    payload_row_t<ComposedT>,
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);

using FourLayerT =
    saf::Linear<
        saf::Refined<saf::positive,
            saf::Tagged<
                saf::Stale<eff::Computation<eff::Row<eff::Effect::IO>, int>>,
                ProvTag>>>;
static_assert(std::is_same_v<
    payload_row_t<FourLayerT>,
    eff::Row<eff::Effect::IO>>);

// ── Composed unwrap that bottoms out at bare T → Row<> ─────────────
using BarelyComposedT =
    saf::Refined<saf::positive,
        saf::Linear<
            saf::Tagged<int, ProvTag>>>;
static_assert(std::is_same_v<
    payload_row_t<BarelyComposedT>,
    eff::Row<>>);

// ── ContentAddressed<T> unwraps ────────────────────────────────────
//
// CA is a session-level quotient marker; the underlying T's row is
// preserved.  Composes with the other unwrap chains.

static_assert(std::is_same_v<
    payload_row_t<ContentAddressed<int>>,
    eff::Row<>>);

static_assert(std::is_same_v<
    payload_row_t<ContentAddressed<eff::Computation<eff::Row<eff::Effect::Bg>, int>>>,
    eff::Row<eff::Effect::Bg>>);

// Composed: ContentAddressed<Refined<P, Computation<R, T>>> → R.
using CaRefinedT =
    ContentAddressed<saf::Refined<saf::positive,
        eff::Computation<eff::Row<eff::Effect::IO>, int>>>;
static_assert(std::is_same_v<
    payload_row_t<CaRefinedT>,
    eff::Row<eff::Effect::IO>>);

// Permission-flow markers are row-transparent: they move or lend CSL
// authority, but they must not hide the carried value's effect row from
// the protocol admission walker.
struct WirePerm {};
using IoComp = eff::Computation<eff::Row<eff::Effect::IO>, int>;
static_assert(std::is_same_v<
    payload_row_t<Transferable<IoComp, WirePerm>>,
    eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<
    payload_row_t<Borrowed<IoComp, WirePerm>>,
    eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<
    payload_row_t<Returned<IoComp, WirePerm>>,
    eff::Row<eff::Effect::IO>>);

// ── AUDIT-2: cross-wrapper soundness — every shipped graded wrapper
//            propagates inner effect rows transparently ────────────
//
// For each wrapper W shipped in safety/Safety.h, verify:
//   (a) bare W<T>            → Row<>         (T has no effects)
//   (b) W<Computation<R, T>> → R             (transparent unwrap)
//
// Without these specs the walker silently undercounts effects — a
// HotPath<Hot, Computation<Row<Bg>, T>> sent to a Fg ctx would PASS
// the proto walker (false-positive admission) and be admitted into
// HotFgCtx, which is unsound.

using BgComp = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using BareInt = int;

// Single-axis policy wrappers — verify Bg-effect propagation through
// each.  (Axis values picked to be valid for each enum.)
static_assert(std::is_same_v<payload_row_t<saf::HotPath<saf::HotPathTier_v::Hot, BgComp>>,
                              eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<saf::HotPath<saf::HotPathTier_v::Hot, BareInt>>,
                              eff::Row<>>);

static_assert(std::is_same_v<payload_row_t<saf::DetSafe<saf::DetSafeTier_v::Pure, BgComp>>,
                              eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<saf::DetSafe<saf::DetSafeTier_v::Pure, BareInt>>,
                              eff::Row<>>);

static_assert(std::is_same_v<payload_row_t<saf::AllocClass<saf::AllocClassTag_v::Arena, BgComp>>,
                              eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<saf::ResidencyHeat<saf::ResidencyHeatTag_v::Hot, BgComp>>,
                              eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<saf::CipherTier<saf::CipherTierTag_v::Hot, BgComp>>,
                              eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<saf::MemOrder<saf::MemOrderTag_v::Acquire, BgComp>>,
                              eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<saf::Wait<saf::WaitStrategy_v::SpinPause, BgComp>>,
                              eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<saf::Progress<saf::ProgressClass_v::Terminating, BgComp>>,
                              eff::Row<eff::Effect::Bg>>);

using BitexactBgRow =
    payload_row_t<saf::NumericalTier<
        ::crucible::algebra::lattices::Tolerance::BITEXACT, BgComp>>;
static_assert(BitexactBgRow::tolerance
              == ::crucible::algebra::lattices::Tolerance::BITEXACT);
static_assert(std::is_same_v<typename BitexactBgRow::effect_row,
                              eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<
    payload_effect_row_t<saf::NumericalTier<
        ::crucible::algebra::lattices::Tolerance::BITEXACT, BgComp>>,
    eff::Row<eff::Effect::Bg>>);
static_assert(!std::is_same_v<BitexactBgRow, eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<saf::Vendor<saf::VendorBackend_v::CPU, BgComp>>,
                              eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<saf::Crash<saf::CrashClass_v::NoThrow, BgComp>>,
                              eff::Row<eff::Effect::Bg>>);

// Single-T policy wrappers — verify Bg propagation.
static_assert(std::is_same_v<payload_row_t<saf::Secret<BgComp>>,
                              eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<saf::Secret<BareInt>>,
                              eff::Row<>>);

// Mutation family — verify Bg propagation.
static_assert(std::is_same_v<payload_row_t<saf::Monotonic<BgComp>>,
                              eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<saf::WriteOnce<BgComp>>,
                              eff::Row<eff::Effect::Bg>>);

// TimeOrdered — verify Bg propagation.
struct TimeTag {};
static_assert(std::is_same_v<payload_row_t<saf::TimeOrdered<BgComp, 4, TimeTag>>,
                              eff::Row<eff::Effect::Bg>>);

// ── Cross-axis nesting: HotPath<Hot, Refined<P, Computation<R, T>>> ─
//
// The classic composed wrapper stack from CLAUDE.md §XVI canonical-
// nesting-order.  Bg row should propagate through ALL outer layers.

// NumericalTier uses bare `Tolerance` (no `_v` alias) — fully qualify.
using DeepStack =
    saf::HotPath<saf::HotPathTier_v::Hot,
        saf::DetSafe<saf::DetSafeTier_v::Pure,
            saf::NumericalTier<::crucible::algebra::lattices::Tolerance::BITEXACT,
                saf::Refined<saf::positive,
                    eff::Computation<eff::Row<eff::Effect::Bg, eff::Effect::Alloc>, int>>>>>;
static_assert(payload_row_t<DeepStack>::tolerance
              == ::crucible::algebra::lattices::Tolerance::BITEXACT);
static_assert(std::is_same_v<typename payload_row_t<DeepStack>::effect_row,
                              eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);
static_assert(std::is_same_v<payload_effect_row_t<DeepStack>,
                              eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);

// ── fixy-A2-010: DelegatedSession higher-order capability ──────────
//
// `payload_row<DelegatedSession<P, IPS>>` walks `P` for the union of
// effect rows in P's Send/Recv payloads.  Without this, sending an
// endpoint of P silently hides P's effects from the outer Ctx admission
// check.  These tests pin the closure of the soundness gap.

struct DSPermTag {};

// (a) DelegatedSession<End, EmptyPermSet> — inner protocol has no
//     operations, so no effects flow.
using IPS_empty = ::crucible::safety::proto::EmptyPermSet;
static_assert(std::is_same_v<
    payload_row_t<DelegatedSession<End, IPS_empty>>,
    eff::Row<>>);
static_assert(std::is_same_v<
    protocol_effect_row_t<End>,
    eff::Row<>>);

// (b) DelegatedSession<Send<Computation<Row<IO>, int>, End>, ...> —
//     inner protocol has a Send carrying an IO payload, so the outer
//     payload_row must surface IO.
using InnerIo =
    Send<eff::Computation<eff::Row<eff::Effect::IO>, int>, End>;
static_assert(std::is_same_v<
    protocol_effect_row_t<InnerIo>,
    eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<
    payload_row_t<DelegatedSession<InnerIo, IPS_empty>>,
    eff::Row<eff::Effect::IO>>);

// (c) Deeper inner protocol with Bg + Alloc payloads on chained Send
//     and Recv — union accumulates correctly.
using InnerBgAlloc =
    Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>,
    Recv<eff::Computation<eff::Row<eff::Effect::Alloc>, int>,
    End>>;
static_assert(::crucible::effects::is_subrow_v<
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc>,
    protocol_effect_row_t<InnerBgAlloc>>);
static_assert(::crucible::effects::is_subrow_v<
    protocol_effect_row_t<InnerBgAlloc>,
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);
static_assert(::crucible::effects::is_subrow_v<
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc>,
    payload_row_t<DelegatedSession<InnerBgAlloc, IPS_empty>>>);

// (d) Loop<Send<IO, Continue>> — Loop transparently unwraps.
using InnerLoopIo =
    Loop<Send<eff::Computation<eff::Row<eff::Effect::IO>, int>, Continue>>;
static_assert(std::is_same_v<
    payload_row_t<DelegatedSession<InnerLoopIo, IPS_empty>>,
    eff::Row<eff::Effect::IO>>);

// (e) Select<End, Send<IO, End>>: branch walker takes the union of
//     reachable branch effects.
using InnerSelectIo =
    Select<End,
           Send<eff::Computation<eff::Row<eff::Effect::IO>, int>, End>>;
static_assert(std::is_same_v<
    protocol_effect_row_t<InnerSelectIo>,
    eff::Row<eff::Effect::IO>>);

// (f) Delegate<X, K> nested inside InnerProto: by the existing
//     proto_row_admitted_by<Delegate> discipline, T is bypassed at the
//     sender's row gate.  Effect comes from K alone.
using InnerDelegate =
    Delegate<Send<eff::Computation<eff::Row<eff::Effect::Block>, int>, End>,
             Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>>;
static_assert(std::is_same_v<
    protocol_effect_row_t<InnerDelegate>,
    eff::Row<eff::Effect::Bg>>);

// (g) Stop_g<C> is a terminal — no effects.
static_assert(std::is_same_v<
    protocol_effect_row_t<Stop_g<CrashClass::Abort>>,
    eff::Row<>>);
static_assert(std::is_same_v<
    protocol_effect_row_t<Stop>,
    eff::Row<>>);

// (g.1) fixy-A2-028 — Delegate<Stop_g<C>, K> with K carrying Bg.
//       Before the bottom-preservation specialisation the trait
//       inherited from the general Delegate<T, K> rule and reported
//       K's effects; after, it agrees with compose's bottom-
//       preservation and reports Row<>.  Pin BOTH ENDS — the
//       standalone protocol AND the composed protocol — so the row
//       no longer narrows across the composition boundary.
using InnerBgRecv =
    Recv<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>;
using DelegateStopWithBg = Delegate<Stop_g<CrashClass::Abort>, InnerBgRecv>;

static_assert(std::is_same_v<
    protocol_effect_row_t<DelegateStopWithBg>,
    eff::Row<>>,
    "fixy-A2-028: Delegate<Stop_g<C>, K> must carry empty row "
    "(carrier is bottom, K unreachable, aligns with compose rule)");

using DelegateStopComposed =
    ::crucible::safety::proto::compose_t<DelegateStopWithBg, End>;
static_assert(std::is_same_v<
    DelegateStopComposed, Stop_g<CrashClass::Abort>>);
static_assert(std::is_same_v<
    protocol_effect_row_t<DelegateStopComposed>,
    eff::Row<>>,
    "fixy-A2-028: composing Delegate<Stop_g<C>, K> ⊕ Q yields "
    "Stop_g<C> with empty row — must match pre-composition row");

static_assert(std::is_same_v<
    protocol_effect_row_t<DelegateStopWithBg>,
    protocol_effect_row_t<DelegateStopComposed>>,
    "fixy-A2-028: NO row narrowing under composition");

// (g.2) Symmetric for Accept-of-Stop.
using AcceptStopWithBg = Accept<Stop_g<CrashClass::Abort>, InnerBgRecv>;
static_assert(std::is_same_v<
    protocol_effect_row_t<AcceptStopWithBg>,
    eff::Row<>>);
using AcceptStopComposed =
    ::crucible::safety::proto::compose_t<AcceptStopWithBg, End>;
static_assert(std::is_same_v<
    AcceptStopComposed, Stop_g<CrashClass::Abort>>);
static_assert(std::is_same_v<
    protocol_effect_row_t<AcceptStopComposed>,
    eff::Row<>>);
static_assert(std::is_same_v<
    protocol_effect_row_t<AcceptStopWithBg>,
    protocol_effect_row_t<AcceptStopComposed>>);

// (g.3) Epoched variants inherit through the un-epoched specs —
//       EpochedDelegate<Stop_g<C>, K, E, G> walks to
//       Delegate<Stop_g<C>, K>'s bottom-preservation rule and reports
//       Row<>.  Pin against the compose result.
using EpochedDelegateStopWithBg =
    EpochedDelegate<Stop_g<CrashClass::Abort>, InnerBgRecv, 1, 1>;
static_assert(std::is_same_v<
    protocol_effect_row_t<EpochedDelegateStopWithBg>,
    eff::Row<>>);
using EpochedDelegateStopComposed = ::crucible::safety::proto::compose_t<
    EpochedDelegateStopWithBg, End>;
static_assert(std::is_same_v<
    EpochedDelegateStopComposed, Stop_g<CrashClass::Abort>>);
static_assert(std::is_same_v<
    protocol_effect_row_t<EpochedDelegateStopComposed>,
    eff::Row<>>);

using EpochedAcceptStopWithBg =
    EpochedAccept<Stop_g<CrashClass::Abort>, InnerBgRecv, 1, 1>;
static_assert(std::is_same_v<
    protocol_effect_row_t<EpochedAcceptStopWithBg>,
    eff::Row<>>);
using EpochedAcceptStopComposed = ::crucible::safety::proto::compose_t<
    EpochedAcceptStopWithBg, End>;
static_assert(std::is_same_v<
    EpochedAcceptStopComposed, Stop_g<CrashClass::Abort>>);
static_assert(std::is_same_v<
    protocol_effect_row_t<EpochedAcceptStopComposed>,
    eff::Row<>>);

// (g.4) Non-crashed Delegate<T, K> with K carrying Bg STILL reports
//       K's row — the bottom-preservation rule is narrowly scoped to
//       T = Stop_g<C>.  This pins that we did not over-collapse.
using DelegateLiveWithBg = Delegate<End, InnerBgRecv>;
static_assert(std::is_same_v<
    protocol_effect_row_t<DelegateLiveWithBg>,
    eff::Row<eff::Effect::Bg>>,
    "fixy-A2-028: non-crashed Delegate<T, K> must still walk K");

// (h) The whole point of the gap-closure: a `Send<DelegatedSession,
//     End>` previously yielded Row<> on the outer Send walker (the
//     primary payload_row<> fallback).  After the spec it correctly
//     surfaces every effect the inner protocol carries.
using OuterSendCarryingDelegated =
    Send<DelegatedSession<InnerIo, IPS_empty>, End>;
static_assert(std::is_same_v<
    protocol_effect_row_t<OuterSendCarryingDelegated>,
    eff::Row<eff::Effect::IO>>);

}  // namespace detail::payload_row_self_test

// ── Runtime smoke test ──────────────────────────────────────────────

[[gnu::cold]] inline void runtime_smoke_test_payload_row() noexcept {
    namespace eff = ::crucible::effects;
    namespace saf = ::crucible::safety;

    // Bare T → Row<>.  Compile-time check on a runtime-context type.
    static_assert(std::is_same_v<payload_row_t<int>, eff::Row<>>);

    // Capability minted at runtime; payload_row recovers the effect.
    eff::Bg bg;
    auto cap = eff::mint_cap<eff::Effect::Alloc>(bg);
    static_assert(std::is_same_v<payload_row_t<decltype(cap)>,
                                  eff::Row<eff::Effect::Alloc>>);
    static_cast<void>(cap);

    // Composed payload at runtime (just type-level, no construction —
    // the composed type uses Trusted refined construction below).
    using ComposedT =
        saf::Refined<saf::positive,
            eff::Computation<eff::Row<eff::Effect::Bg>, int>>;
    static_assert(std::is_same_v<payload_row_t<ComposedT>,
                                  eff::Row<eff::Effect::Bg>>);
}

}  // namespace crucible::safety::proto
