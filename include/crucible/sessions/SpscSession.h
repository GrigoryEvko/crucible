#pragma once

// ═══════════════════════════════════════════════════════════════════
// SpscSession.h — typed-session wrapper for PermissionedSpscChannel
//
// First production-shaped wiring of FOUND-C v1's PermissionedSession-
// Handle stack onto the existing concurrent/PermissionedSpscChannel
// primitive.  Closes the K-series POC angle of SAFEINT-R31 / SEPLOG-
// INT-1 (#384, #413) WITHOUT touching the existing TraceRing hot
// path: this header demonstrates the framework's "zero overhead in
// production" claim on the channel shape that TraceRing, MetaLog,
// CNTP wire bytes, and the bg-pipeline drain-channels all share —
// SPSC streaming with linear Producer + Consumer Permissions.
//
// ─── What this layer ADDS over PermissionedSpscChannel ─────────────
//
// The bare PermissionedSpscChannel<T, N, UserTag>::ProducerHandle
// already gives:
//   * Compile-time role discrimination (try_push only, no try_pop)
//   * Permission<Producer<UserTag>> linearity (one producer per
//     channel, enforced by the linear-token discipline)
//
// What it does NOT give:
//   * Protocol-shape typing.  The handle is "any caller may try_push
//     anytime"; there is no compile-time encoding that the channel
//     is a STREAMING session that loops sends until shutdown.
//
// SpscSession adds exactly that.  Wrapping a ProducerHandle as a
// PermissionedSessionHandle<ProducerProto<T>, EmptyPermSet,
// ProducerHandle*> gives:
//   * Loop<Send<T, Continue>> protocol-shape typing — the type
//     system knows this is a streaming channel that never reaches
//     End (an explicit detach is required at shutdown — Loop
//     without exit branch is the documented infinite-loop pattern).
//   * Loop body permission-balance enforcement (vacuously true for
//     EmptyPermSet but compiler-checked at every Continue).
//   * Branch-terminal PS convergence (vacuously true; no Select/
//     Offer in this protocol shape).
//   * Debug-mode abandonment-tracker enrichment if the handle is
//     dropped without detach (zero release-mode cost).
//
// ─── Why the Resource is a POINTER ─────────────────────────────────
//
// PermissionedSpscChannel::ProducerHandle is move-CONSTRUCTIBLE (for
// return-by-value from channel.producer()) but move-ASSIGNMENT is
// DELETED (rebinding would orphan the original Permission).  And it
// does NOT derive Pinned (Pinned deletes move-ctor, breaking the
// factory).  Both T-by-value (PSH's loop pattern needs move-assign)
// and T& (SessionResource Pinned-constraint) are therefore rejected.
//
// Pointer Resource is the framework's blessed escape hatch (Session.h
// :2010-2018, "caller's manual lifetime contract").  The pointee
// outlives the PSH by construction — typically a stack-allocated
// ProducerHandle bound to its enclosing scope's lifetime.  Move-
// assigning the PSH variable simply rebinds a pointer.
//
// The factories below take handle BY REFERENCE for ergonomic call
// sites (no caller-side `&` taking-address-of); they internally
// take the address and forward as Resource = Handle*.  The lifetime
// contract is the SAME as if the caller wrote &handle directly.
//
// ─── Worked example ────────────────────────────────────────────────
//
//   struct TraceRingTag {};
//   crucible::concurrent::PermissionedSpscChannel<int, 1024, TraceRingTag> ch;
//
//   namespace ses = crucible::safety::proto::spsc_session;
//
//   auto whole = crucible::safety::mint_permission_root<
//       crucible::concurrent::spsc_tag::Whole<TraceRingTag>>();
//   auto [pp, cp] = crucible::safety::mint_permission_split<
//       crucible::concurrent::spsc_tag::Producer<TraceRingTag>,
//       crucible::concurrent::spsc_tag::Consumer<TraceRingTag>>(
//           std::move(whole));
//
//   auto prod = ch.producer(std::move(pp));
//   auto cons = ch.consumer(std::move(cp));
//
//   std::jthread producer{[&prod](auto) mutable {
//       auto psh = ses::mint_producer_session<decltype(ch)>(prod);
//       for (int i = 0; i < 1000; ++i) {
//           auto next = std::move(psh).send(i, ses::blocking_push);
//           psh = std::move(next);
//       }
//       std::move(psh).detach(
//           crucible::safety::proto::detach_reason::TestInstrumentation{});
//   }};
//
//   std::jthread consumer{[&cons](auto) mutable {
//       auto psh = ses::mint_consumer_session<decltype(ch)>(cons);
//       for (int i = 0; i < 1000; ++i) {
//           auto [v, next] = std::move(psh).recv(ses::blocking_pop);
//           (void)v;
//           psh = std::move(next);
//       }
//       std::move(psh).detach(
//           crucible::safety::proto::detach_reason::TestInstrumentation{});
//   }};
//
// ─── Zero-cost claim ───────────────────────────────────────────────
//
// PSH wrapping a Handle* is purely compile-time: pointer-Resource (8
// bytes) + EmptyPermSet (0 bytes EBO) + abandonment tracker (0 bytes
// in release EBO).  Release-mode send/recv inlines through PSH →
// transport lambda → handle method to byte-identical machine code
// vs the bare handle's try_push/try_pop.  Validated by
// `bench/bench_spsc_session.cpp` head-to-head against bare handles;
// see also bench_permissioned_session_handle.cpp for the canonical
// asm-identical witness pattern.
//
// To re-verify the asm-identical claim manually:
//   cmake --preset default -DCRUCIBLE_DUMP_ASM=ON
//   cmake --build --preset default --target bench_spsc_session
//   # Inspect build/asm-dump/bench_spsc_session.s — the bare and
//   # typed bench bodies are inlined into bench::Run::measure
//   # instantiations; the inner SpscRing acquire-load + release-
//   # store sequences are identical for both API shapes.
//
// ─── What this wiring DOES NOT demonstrate ─────────────────────────
//
// EMPTY PERMSET BY DESIGN.  This wiring uses EmptyPermSet throughout
// and the protocol shape Loop<Send<T, Continue>> carries plain payloads
// — no Transferable<T, Tag> / Borrowed<T, Tag> / Returned<T, Tag>
// markers.  TraceRing / MetaLog / CNTP-shape SPSC channels stream
// values, not permissions; permissions stay with the role-typed
// handles (one Producer<UserTag>, one Consumer<UserTag>) and never
// transit the wire.  This is the CORRECT shape for the production
// callers this wiring targets — but it means the framework's PermSet
// evolution path is NOT exercised here.
//
// The PermSet evolution path is exercised in
// test/test_permissioned_session_handle.cpp (FOUND-C v1's own
// integration test) via Transferable<int, WorkPerm> +
// Returned<int, WorkPerm> payload tests that watch PS shrink + grow
// at compile time.  This wiring composes orthogonally with that
// machinery — production callers needing wire-permission transfer
// can swap their payload type from `T` to `Transferable<T, MyTag>`
// without changing the rest of the wiring.
//
// FIXED PROTOCOL SHAPE.  Loop<Send<T, Continue>> is the simplest
// protocol the framework supports — no Select, no Offer, no Stop,
// no payload markers, no branch convergence.  Shutdown is via
// detach (Loop without exit branch — the documented infinite-loop
// pattern).  Production callers wanting richer protocols (e.g.
// Loop<Choice<Send<T, Continue>, Stop>> for graceful shutdown,
// Loop<Send<Req, Recv<Resp, Continue>>> for request-response over
// SPSC) define their own ProducerProto / ConsumerProto and use
// mint_permissioned_session directly — this header is the canonical
// streaming-SPSC factory, not an exhaustive protocol library.
//
// FIRST PRODUCTION-SHAPED EXERCISE.  This is the framework's first
// wired-in production-shape exercise (closes Part IX.2 of
// session_types.md "no production callers" critique) — but it
// targets the SIMPLEST production shape.  Richer wirings (Cipher
// hot→warm tier promotion via Returned<DurabilityAck, HotEntry>,
// CNTP Raft via session_fork) demonstrate broader framework
// capabilities; track them via the K-series tasks (#355-#358).
//
// ─── References ────────────────────────────────────────────────────
//
//   misc/27_04_csl_permission_session_wiring.md §17 — closing
//     paragraph names this wiring as "the next PR after FOUND-C".
//   misc/24_04_2026_safety_integration.md §31 — SAFEINT-R31 spec.
//   misc/CRUCIBLE.md §IV.2 — TraceRing as the canonical SPSC user.
//   concurrent/PermissionedSpscChannel.h — the underlying primitive.
//   sessions/PermissionedSession.h — the FOUND-C v1 framework.
//   bench/bench_permissioned_session_handle.cpp — the asm-identical
//     witness pattern that this wiring's bench mirrors.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>

#include <thread>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::spsc_session {

// ── Protocol shapes ─────────────────────────────────────────────────
//
// Both protocols are infinite Loops: SPSC channels in production stream
// indefinitely until shutdown (no explicit End in the wire protocol).
// Loop without an exit branch is the documented infinite-loop pattern;
// callers MUST detach the terminal handle at shutdown (any
// detach_reason::* is fine; TestInstrumentation is the convention for
// fixtures, ProducerStop / ConsumerStop or similar for production).

template <typename T>
using ProducerProto = Loop<Send<T, Continue>>;

template <typename T>
using ConsumerProto = Loop<Recv<T, Continue>>;

// ── Establishment factories ─────────────────────────────────────────
//
// Take handle BY REFERENCE; internally take the address and forward
// as Resource = Handle*.  This is a pure ergonomic improvement over
// the pointer-taking form — same lifetime contract, no caller-side
// `&` taking-address-of.  The handle MUST outlive the returned PSH
// (typically: handle is stack-bound to its enclosing scope; PSH is
// scoped tighter or moves into a jthread joined before scope exit).
//
// EmptyPermSet at session start: the ProducerHandle's embedded
// Permission<Producer<UserTag>> already enforces single-producer
// linearity.  Adding Transferable / Borrowed / Returned payloads
// to this protocol would compose orthogonally via SessionPermPayloads.h
// but is not the shape SPSC streaming channels need (no permissions
// to transfer through the wire).

template <typename Channel>
[[nodiscard]] constexpr auto
mint_producer_session(typename Channel::ProducerHandle& handle) noexcept
{
    using T = typename Channel::value_type;
    return mint_permissioned_session<ProducerProto<T>,
                                   typename Channel::ProducerHandle*>(&handle);
}

template <typename Channel>
[[nodiscard]] constexpr auto
mint_consumer_session(typename Channel::ConsumerHandle& handle) noexcept
{
    using T = typename Channel::value_type;
    return mint_permissioned_session<ConsumerProto<T>,
                                   typename Channel::ConsumerHandle*>(&handle);
}

// ── Transport helpers ───────────────────────────────────────────────
//
// Generic lambdas — call sites need NO template args.  The first
// parameter (`Handle*&`) is deduced from PSH's invocation context:
// PSH calls `tx(resource_, std::move(value))` where resource_ is the
// stored Handle* lvalue.  std::forward preserves the rvalue-ness of
// `value` so 64-byte payloads (canonical TraceEntry shape) are NOT
// copied a second time inside the helper — the only mandatory copy
// is PSH::send's by-value parameter.
//
// blocking_push: yield-on-full retry loop.  Matches the canonical
// TraceRing recording fast path (CRUCIBLE.md §IV.2.3).
//
// blocking_pop:  yield-on-empty retry loop.  Matches the canonical
// bg-drain pattern.  optional<T>::operator* returns T&; the auto
// return type strips the reference, returning T by value (move where
// possible).
//
// Callers wanting different blocking strategies (sleep, spin, busy-
// wait, deadline-aware) write their own transport closures — these
// helpers are conveniences for the common case, not the only option.

inline constexpr auto blocking_push = [](auto& hp, auto&& value) noexcept {
    while (!hp->try_push(std::forward<decltype(value)>(value))) {
        std::this_thread::yield();
    }
};

inline constexpr auto blocking_pop = [](auto& hp) noexcept {
    for (;;) {
        if (auto v = hp->try_pop()) return *v;
        std::this_thread::yield();
    }
};

}  // namespace crucible::safety::proto::spsc_session

// ═══════════════════════════════════════════════════════════════════
// Compile-time witnesses — fail-to-compile if the wiring drifts
// ═══════════════════════════════════════════════════════════════════

namespace crucible::safety::proto::spsc_session::detail::sizeof_witness {

// Small synthetic channel for size-equality assertions.  Picked to
// match the bare PermissionedSessionHandle bench's witness pattern
// (bench_permissioned_session_handle.cpp:202-210).
struct Tag {};
using SmallChannel = ::crucible::concurrent::PermissionedSpscChannel<int, 16, Tag>;
using ProdHandle   = SmallChannel::ProducerHandle;
using ConsHandle   = SmallChannel::ConsumerHandle;

// Sizeof-equality is asserted on the CONCRETE HEAD types that
// mint_permissioned_session<ProducerProto<int>>(...) actually returns
// after Loop unrolling, NOT on `Loop<...>` itself (which is a
// shape-only template with no SessionHandle / PSH specialisation —
// Loop unrolls at establishment to a per-head handle wrapping its
// body).  We mirror the canonical pattern from
// bench_permissioned_session_handle.cpp:202-210, which asserts on
// End and Send<T, End> rather than the Loop wrapper.
//
// What the witnesses prove:
//   1. `PSH<End, EmptyPermSet, Handle*>` is the same size as
//      `SessionHandle<End, Handle*>` — verifies EmptyPermSet
//      collapses via EBO and the abandonment tracker contributes
//      zero bytes in release (one byte in debug; equality holds in
//      both modes because both wrappers pay the same tracker cost).
//   2. `PSH<Send<int, End>, EmptyPermSet, Handle*>` is the same
//      size as the bare SessionHandle for the same head — verifies
//      the witness extends to non-terminal heads.
//   3. The PSH collapses to roughly pointer-sized (Resource +
//      tracker only), catching regressions where PS or LoopContext
//      accidentally gain a non-empty member.

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                ProdHandle*>)
              == sizeof(SessionHandle<End, ProdHandle*>),
              "spsc_session: PSH<End, EmptyPermSet, ProdHandle*> must be "
              "same size as bare SessionHandle<End, ProdHandle*> — if this "
              "fails, EBO collapse of EmptyPermSet has been broken or the "
              "abandonment tracker grew asymmetrically between PSH and bare.");

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                ConsHandle*>)
              == sizeof(SessionHandle<End, ConsHandle*>),
              "spsc_session: PSH<End, EmptyPermSet, ConsHandle*> must be "
              "same size as bare SessionHandle<End, ConsHandle*>.");

static_assert(sizeof(PermissionedSessionHandle<Send<int, End>, EmptyPermSet,
                                                ProdHandle*>)
              == sizeof(SessionHandle<Send<int, End>, ProdHandle*>),
              "spsc_session: PSH<Send<int, End>, EmptyPermSet, ProdHandle*> "
              "must be same size as bare SessionHandle for the same head.");

// Note: an absolute-size bound is intentionally NOT asserted here.
// In RELEASE the PSH collapses to exactly sizeof(Resource) (tracker
// is empty + EBO; PS is empty + EBO).  In DEBUG, SessionHandleBase's
// consumed_tracker carries `bool flag_ + std::source_location loc_`
// (~16 bytes after alignment on 64-bit targets).  Both shapes satisfy
// the size-EQUALITY claim above (because PSH and bare SH pay the same
// tracker cost).  An absolute upper bound that holds in both modes
// would have to encode the debug-mode tracker size, which the framework
// is free to evolve — pinning that here would break on the next
// SessionHandleBase enrichment without conveying any new information
// beyond what the size-equality witness already conveys.

}  // namespace
