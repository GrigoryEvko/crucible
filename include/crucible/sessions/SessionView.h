#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — non-consuming protocol-position views
//                            (SAFEINT-B16, #405,
//                            misc/24_04_2026_safety_integration.md §16)
//
// External code sometimes needs to *inspect* a SessionHandle without
// consuming or advancing it: an Augur metrics broadcast enumerates
// every active session and reports each one's protocol position; a
// debug renderer wants to print "this handle is at a Send to peer X"
// before any state change; the test harness wants to assert "this
// handle is in Offer state" without paying the destructor's
// abandonment-check cost or leaking the handle entirely.
//
// The only options before this header were:
//
//   (a) protocol_name() — a std::string_view rendering of the
//       compile-time Proto.  Useful for logging, but loses the
//       discriminated structure (Send vs Recv vs Offer).
//
//   (b) Move-out / move-in — destructive consumption.  Not
//       acceptable for inspection.
//
//   (c) Decltype + template metaprogramming at the call site.
//       Works but bleeds protocol structure into every consumer.
//
// This header introduces the FOURTH option: position-tagged
// `ScopedView<Handle, At*>` borrows.  The Carrier IS the handle;
// the Tag names which combinator-head the handle is currently at.
// Mint via `mint_session_view<AtSend>(handle)`; pay zero runtime
// cost (the position is determined entirely by the handle's
// compile-time Proto); reject wrong-position requests at compile
// time via the `HandleIsAt` concept.
//
// ─── The position tags ─────────────────────────────────────────────
//
// One tag per SessionHandle specialisation, plus AtTerminal as the
// terminal-state category covering End ∪ Stop:
//
//     AtSend          handle's Proto is Send<T, R>
//     AtRecv                            Recv<T, R>
//     AtSelect                          Select<Bs...>
//     AtOffer                           Offer<Bs...>
//     AtEnd                             End
//     AtStop                            Stop      (from SessionCrash.h)
//     AtTerminal                        End or Stop
//     AtCheckpointed                    CheckpointedSession<B, R>
//     AtDelegate                        Delegate<T, R> or
//                                       EpochedDelegate<T, R, E, G>
//     AtAccept                          Accept<T, R> or
//                                       EpochedAccept<T, R, E, G>
//
// Per-spec tags exist for callers who need to dispatch on the exact
// combinator; AtTerminal exists for callers who only care that the
// handle has reached a destruction-safe state.
//
// ─── Example: typed Augur metrics callback ─────────────────────────
//
//     // Metrics collector — only callable when handle is at Recv.
//     // Compile-time enforced via the view's tag.
//     template <typename Handle>
//         requires HandleIsAt<Handle, AtRecv>
//     void report_pending_recv(ScopedView<Handle, AtRecv> view) {
//         augur::record_pending_recv(view->protocol_name());
//     }
//
//     // Caller mints the view; compile error if handle is at Send.
//     auto view = mint_session_view<AtRecv>(handle);
//     report_pending_recv(view);
//     // handle still alive, may continue protocol below.
//     auto [msg, next] = std::move(handle).recv(transport);
//
// ─── Lifetime + cost ───────────────────────────────────────────────
//
// ScopedView<H, At*> stores a `H const*` only.  sizeof equals
// sizeof(void*).  CRUCIBLE_LIFETIMEBOUND on the constructor argument
// makes -Wdangling-reference fire when a view of a local handle is
// returned from the function that minted it; ScopedView's other
// disciplines (no heap allocation, no field storage, no assignment)
// apply automatically.
//
// Concurrent views on the same handle are SAFE — they're all
// const-only borrows.  But they don't compose with the handle's
// own `&&`-qualified consumers: if you mint a view, then move-from
// the handle, the view dangles.  ScopedView's lifetime-bound
// attribute catches the obvious "return view of local" case;
// long-distance handoff still requires caller discipline (same
// rule as for any reference borrow).
//
// ─── Why position tags rather than handle aliases ──────────────────
//
// We could expose `using SendView<H> = ScopedView<H, AtSend>` shapes
// to tighten the surface, but that's syntactic sugar over the same
// underlying type.  The tag-as-marker pattern matches every other
// ScopedView use in Crucible (Vigil's pending_region/pending_activation,
// Graph's Sealed/Building, IterationDetector's signature stages),
// keeping the framework idiom uniform.
//
// ─── References ───────────────────────────────────────────────────
//
//   misc/24_04_2026_safety_integration.md §16 — the design.
//   safety/ScopedView.h — the underlying typestate-borrow primitive.
//   safety/Session.h — SessionHandle and the combinator types.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/ScopedView.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════════
// ── Position tags ──────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Pure type markers; no runtime state.  Each tag corresponds to a
// SessionHandle specialisation (or, for AtTerminal, a union of two).

struct AtSend         {};
struct AtRecv         {};
struct AtSelect       {};
struct AtOffer        {};
struct AtEnd          {};
struct AtStop         {};
struct AtTerminal     {};   // End ∪ Stop — destruction-safe positions
struct AtCheckpointed {};
struct AtDelegate     {};
struct AtAccept       {};

// ═════════════════════════════════════════════════════════════════════
// ── handle_is_at<Handle, Tag> — compile-time position predicate ────
// ═════════════════════════════════════════════════════════════════════
//
// Primary template: false (the catch-all rejection).  Specialisations
// below match each (SessionHandle<Proto, R, L>, Tag) shape that we
// want to admit as a valid mint position.

template <typename Handle, typename Tag>
struct handle_is_at : std::false_type {};

// ─── AtSend ───────────────────────────────────────────────────────

template <typename T, typename R, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Send<T, R>, Resource, LoopCtx>, AtSend>
    : std::true_type {};

// ─── AtRecv ───────────────────────────────────────────────────────

template <typename T, typename R, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Recv<T, R>, Resource, LoopCtx>, AtRecv>
    : std::true_type {};

// ─── AtSelect ─────────────────────────────────────────────────────

template <typename... Bs, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Select<Bs...>, Resource, LoopCtx>, AtSelect>
    : std::true_type {};

// ─── AtOffer ──────────────────────────────────────────────────────

template <typename... Bs, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Offer<Bs...>, Resource, LoopCtx>, AtOffer>
    : std::true_type {};

// ─── AtEnd ────────────────────────────────────────────────────────

template <typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<End, Resource, LoopCtx>, AtEnd>
    : std::true_type {};

// ─── AtStop ───────────────────────────────────────────────────────

template <CrashClass C, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Stop_g<C>, Resource, LoopCtx>, AtStop>
    : std::true_type {};

// ─── AtTerminal — End OR Stop ──────────────────────────────────────
//
// Convenience tag for callers that only care about reaching a
// destruction-safe position.  Both End and Stop are terminal from
// the abandonment-check's viewpoint; the union admits either.

template <typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<End,  Resource, LoopCtx>, AtTerminal>
    : std::true_type {};

template <CrashClass C, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Stop_g<C>, Resource, LoopCtx>, AtTerminal>
    : std::true_type {};

// ─── AtCheckpointed ───────────────────────────────────────────────

template <typename B, typename R, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<CheckpointedSession<B, R>, Resource, LoopCtx>,
                     AtCheckpointed>
    : std::true_type {};

// ─── AtDelegate ───────────────────────────────────────────────────

template <typename T, typename R, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Delegate<T, R>, Resource, LoopCtx>, AtDelegate>
    : std::true_type {};

template <typename T, typename R,
          std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          typename Resource, typename LoopCtx>
struct handle_is_at<
    SessionHandle<EpochedDelegate<T, R, MinEpoch, MinGeneration>,
                  Resource, LoopCtx>,
    AtDelegate>
    : std::true_type {};

// ─── AtAccept ─────────────────────────────────────────────────────

template <typename T, typename R, typename Resource, typename LoopCtx>
struct handle_is_at<SessionHandle<Accept<T, R>, Resource, LoopCtx>, AtAccept>
    : std::true_type {};

template <typename T, typename R,
          std::uint64_t MinEpoch, std::uint64_t MinGeneration,
          typename Resource, typename LoopCtx>
struct handle_is_at<
    SessionHandle<EpochedAccept<T, R, MinEpoch, MinGeneration>,
                  Resource, LoopCtx>,
    AtAccept>
    : std::true_type {};

// ─── Trait + concept aliases ──────────────────────────────────────

template <typename Handle, typename Tag>
inline constexpr bool handle_is_at_v = handle_is_at<Handle, Tag>::value;

template <typename Handle, typename Tag>
concept HandleIsAt = handle_is_at_v<Handle, Tag>;

// ═════════════════════════════════════════════════════════════════════
// ── view_ok ADL hook for SessionHandle ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// safety::mint_view<Tag>(c) calls `view_ok(c, type_identity<Tag>{})`
// via ADL as a contract precondition.  For SessionHandle, the
// precondition is purely compile-time (the handle's Proto IS the
// position) — view_ok evaluates handle_is_at_v at constant time and
// returns the bool.  Under contract semantic=enforce this becomes a
// trivial `pre(true)` / `pre(false)` check; the compile-time
// rejection sits one layer up in mint_session_view's requires-clause.

template <typename Proto, typename Resource, typename LoopCtx, typename Tag>
constexpr bool view_ok(
    SessionHandle<Proto, Resource, LoopCtx> const& /*h*/,
    std::type_identity<Tag>) noexcept
{
    return handle_is_at_v<SessionHandle<Proto, Resource, LoopCtx>, Tag>;
}

// ═════════════════════════════════════════════════════════════════════
// ── mint_session_view<Tag>(handle) — the factory ───────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Mint a non-consuming view of `handle` at the requested protocol
// position.  Compile error if the handle is not at that position
// (HandleIsAt concept rejects).  Lifetime-bound — the view borrows
// the handle's address, and -Wdangling-reference catches the obvious
// "return view of local" case.
//
// Returns ScopedView<Handle, Tag> by value; the view is copyable
// but not assignable (ScopedView's discipline) — multiple read-only
// witnesses can coexist, none can replace another.

template <typename Tag, typename Handle>
    requires HandleIsAt<Handle, Tag>
[[nodiscard]] constexpr auto mint_session_view(
    Handle const& handle CRUCIBLE_LIFETIMEBOUND) noexcept
    -> safety::ScopedView<Handle, Tag>
{
    return safety::mint_view<Tag>(handle);
}

// ═════════════════════════════════════════════════════════════════════
// ── Convenience accessors on the view ───────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Common metadata extractions that callers would otherwise reach for
// via `view.carrier_type::xxx` boilerplate.  All const, all zero-cost,
// all forwarded directly to the underlying handle's compile-time
// type or const-method.

// session_view_protocol_name<View>() — the handle's Proto's rendered
// name.  Forwards to the carrier's protocol_name() static accessor.
// Same cross-TU constexpr-capture caveat as protocol_name() itself
// (see SessionFromMachine's documentation): treat as a runtime
// helper, not a constexpr literal.
template <typename View>
[[nodiscard]] constexpr std::string_view session_view_protocol_name() noexcept {
    using Handle = typename View::carrier_type;
    return Handle::protocol_name();
}

// session_view_message_type — the message_type of a Send / Recv view.
// Defined only for AtSend and AtRecv views; instantiation on any
// other tag is ill-formed (no matching specialisation).
//
// Useful for typed metrics: `report_recv<message_type<View>>(...)`.

template <typename View>
struct session_view_message_type;

template <typename T, typename R, typename Resource, typename LoopCtx>
struct session_view_message_type<
    safety::ScopedView<SessionHandle<Send<T, R>, Resource, LoopCtx>, AtSend>>
{
    using type = T;
};

template <typename T, typename R, typename Resource, typename LoopCtx>
struct session_view_message_type<
    safety::ScopedView<SessionHandle<Recv<T, R>, Resource, LoopCtx>, AtRecv>>
{
    using type = T;
};

template <typename View>
using session_view_message_type_t =
    typename session_view_message_type<View>::type;

// session_view_branch_count — the branch count of a Select / Offer
// view.  Defined only for AtSelect and AtOffer views.

template <typename View>
struct session_view_branch_count;

template <typename... Bs, typename Resource, typename LoopCtx>
struct session_view_branch_count<
    safety::ScopedView<SessionHandle<Select<Bs...>, Resource, LoopCtx>, AtSelect>>
    : std::integral_constant<std::size_t, sizeof...(Bs)> {};

template <typename... Bs, typename Resource, typename LoopCtx>
struct session_view_branch_count<
    safety::ScopedView<SessionHandle<Offer<Bs...>, Resource, LoopCtx>, AtOffer>>
    : std::integral_constant<std::size_t, sizeof...(Bs)> {};

template <typename View>
inline constexpr std::size_t session_view_branch_count_v =
    session_view_branch_count<View>::value;

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Verify the position-tag specialisations cover every SessionHandle
// shape and reject all wrong-position requests.  Runs at header-
// inclusion time; regressions fail at the first TU that pulls us in.

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::sv_self_test {

struct FakeRes {};
struct Msg     {};
struct Other   {};

// ─── Per-tag positive cases ───────────────────────────────────────

static_assert( handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtSend>);
static_assert( handle_is_at_v<SessionHandle<Recv<Msg, End>, FakeRes>, AtRecv>);
static_assert( handle_is_at_v<SessionHandle<Select<Send<Msg, End>>, FakeRes>, AtSelect>);
static_assert( handle_is_at_v<SessionHandle<Offer<Recv<Msg, End>>,  FakeRes>, AtOffer>);
static_assert( handle_is_at_v<SessionHandle<End,  FakeRes>,                   AtEnd>);
static_assert( handle_is_at_v<SessionHandle<Stop, FakeRes>,                   AtStop>);
static_assert( handle_is_at_v<
    SessionHandle<Stop_g<CrashClass::NoThrow>, FakeRes>, AtStop>);
static_assert( handle_is_at_v<SessionHandle<End,  FakeRes>,                   AtTerminal>);
static_assert( handle_is_at_v<SessionHandle<Stop, FakeRes>,                   AtTerminal>);
static_assert( handle_is_at_v<
    SessionHandle<Stop_g<CrashClass::NoThrow>, FakeRes>, AtTerminal>);
static_assert( handle_is_at_v<SessionHandle<Delegate<Send<Msg, End>, End>, FakeRes>,
                              AtDelegate>);
static_assert( handle_is_at_v<SessionHandle<Accept<Send<Msg, End>,   End>, FakeRes>,
                              AtAccept>);
static_assert( handle_is_at_v<SessionHandle<CheckpointedSession<End, End>, FakeRes>,
                              AtCheckpointed>);

// ─── Negative cases — wrong tag, wrong shape, wrong both ─────────

static_assert(!handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtRecv>);
static_assert(!handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtSelect>);
static_assert(!handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtTerminal>);
static_assert(!handle_is_at_v<SessionHandle<End,  FakeRes>,           AtSend>);
static_assert(!handle_is_at_v<SessionHandle<End,  FakeRes>,           AtStop>);
static_assert(!handle_is_at_v<SessionHandle<Stop, FakeRes>,           AtEnd>);
static_assert(!handle_is_at_v<SessionHandle<Recv<Msg, End>, FakeRes>, AtCheckpointed>);
static_assert(!handle_is_at_v<SessionHandle<Offer<Recv<Msg, End>>, FakeRes>, AtSelect>);
static_assert(!handle_is_at_v<SessionHandle<Select<Send<Msg, End>>, FakeRes>, AtOffer>);

// Non-SessionHandle types are rejected for every tag.
static_assert(!handle_is_at_v<int,        AtSend>);
static_assert(!handle_is_at_v<FakeRes,    AtTerminal>);
static_assert(!handle_is_at_v<Send<Msg, End>, AtSend>);  // combinator, not handle

// ─── view_ok / handle_is_at_v equivalence ────────────────────────
//
// view_ok's ADL hook delegates to handle_is_at_v; we don't separately
// instantiate constexpr SessionHandle samples here because doing so
// would create namespace-scope static instances whose destructors
// fire the abandonment check on Send-state handles at program exit
// (the constexpr modifier covers the initializer, not the
// destructor).  Coverage of the runtime evaluation lives in
// test/test_session_view.cpp via stack-local handles consumed before
// scope exit.

// ─── Concept rejection at the factory boundary ────────────────────
//
// HandleIsAt<H, Tag> compiles iff the handle is at the position;
// otherwise mint_session_view's requires-clause excludes it from
// overload resolution and the call site fails to find a match.

template <typename H, typename Tag>
concept can_mint_session_view = requires (H const& h) {
    mint_session_view<Tag>(h);
};

static_assert( can_mint_session_view<SessionHandle<Send<Msg, End>, FakeRes>, AtSend>);
static_assert(!can_mint_session_view<SessionHandle<Send<Msg, End>, FakeRes>, AtRecv>);
static_assert(!can_mint_session_view<SessionHandle<End,            FakeRes>, AtSend>);
static_assert( can_mint_session_view<SessionHandle<End,            FakeRes>, AtTerminal>);

}  // namespace detail::sv_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
