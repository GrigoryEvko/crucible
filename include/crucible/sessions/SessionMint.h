#pragma once

// ── crucible::safety::proto::mint_session ──────────────────────────
//
// Eager whole-protocol ctx-check at session-mint time.  Walks Proto
// recursively, asserts every Send/Recv payload's effect-row is
// admitted by Ctx::row, and returns the existing unchanged
// SessionHandle<Proto, Resource>.  Subsequent send/recv operations
// run at full speed with no per-op check — the whole protocol was
// certified at construction.
//
// This is the canonical session-side instance of the universal mint
// pattern shipped across Tier 1: `mint_X(ctx, args...) → X`
// constrained on `CtxFitsX<X, Ctx>`.  Symmetric to
// effects::mint_cap, effects::mint_from_ctx, and the upcoming
// concurrent::mint_endpoint / stages::mint_stage / mint_pipeline.
//
//   Axiom coverage: TypeSafe — proto_row_admitted_by walks the tree
//                   at template-instantiation; mismatches surface
//                   as concept-violation diagnostics at the mint
//                   call site.
//                   InitSafe — pure metafunction during walk.
//                   DetSafe — consteval throughout.
//   Runtime cost:   zero.  The walker resolves at template
//                   substitution; the returned SessionHandle is the
//                   existing unchanged Session.h primitive.
//
// ── Concept and factory ─────────────────────────────────────────────
//
//   proto_row_admitted_by<Proto, Ctx>::value — recursive walker
//   CtxFitsProtocol<Proto, Ctx>              — concept (= the walker)
//   mint_session<Proto>(ctx, resource)        — factory; returns SessionHandle
//
// ── Walk shape ──────────────────────────────────────────────────────
//
//   End                       → true (terminal)
//   Continue                  → true (loop closes back; payloads checked at Loop)
//   Send<T, K>                → payload_row_t<T> ⊆ Ctx::row  ∧  walk(K)
//   Recv<T, K>                → payload_row_t<T> ⊆ Ctx::row  ∧  walk(K)
//   Loop<B>                   → walk(B)
//   Select<Branches...>       → ∀ branch. walk(branch)
//   Offer<Branches...>        → ∀ branch. walk(branch)
//   Offer<Sender<R>, Bs...>   → ∀ branch. walk(branch)
//
// Composed payloads (Refined<P, Linear<Computation<R, T>>>) unwrap
// transparently via payload_row_t — see SessionRowExtraction.h.
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
// The handle type is identical; only the construction boundary
// changes.  The whole protocol's effect surface is now certified
// against the surrounding Ctx.

#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>          // Stop terminator
#include <crucible/sessions/SessionDelegate.h>       // Delegate / Accept
#include <crucible/sessions/SessionRowExtraction.h>

#include <type_traits>

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

// Send<T, K>: payload_row_t<T> must be a Subrow of Ctx's row, AND
// the continuation K must be admitted.
template <class T, class K, class Ctx>
struct proto_row_admitted_by<Send<T, K>, Ctx>
    : std::bool_constant<
          ::crucible::effects::is_subrow_v<
              payload_row_t<T>,
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
              payload_row_t<T>,
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

// ── mint_session<Proto>(ctx, resource) ─────────────────────────────
//
// Factory.  Requires CtxFitsProtocol<Proto, Ctx>; returns the
// existing mint_session_handle<Proto>(resource) result.  The
// session machinery (Session.h's static_asserts on Proto well-
// formedness, SessionResource pin discipline, etc.) all run as
// before.  The added value is the protocol-vs-ctx check — every
// Send/Recv payload in Proto is now certified against ctx's row.

template <class Proto, ::crucible::effects::IsExecCtx Ctx, class Resource>
    requires CtxFitsProtocol<Proto, Ctx>
[[nodiscard]] constexpr auto mint_session(
    Ctx const&,
    Resource&& resource,
    std::source_location loc = std::source_location::current()) noexcept
{
    return mint_session_handle<Proto>(std::forward<Resource>(resource), loc);
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
