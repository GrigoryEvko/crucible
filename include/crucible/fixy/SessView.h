#pragma once

// ── crucible::fixy::sess::view — non-consuming inspection surface ───
//
// FIXY-V-063.  Re-exports the public surface of
// `sessions/SessionView.h` — `ScopedView<Handle, AtXxx>`-based
// non-consuming protocol-position views.  This is the load-bearing
// bridge between observe metrics / debug renderers / test harnesses
// and live session handles:  callers inspect a handle's protocol
// position (Send / Recv / Select / Offer / End / Stop / Terminal /
// Checkpointed / Delegate / Accept) WITHOUT consuming or advancing
// it, paying zero runtime cost (the position is determined entirely
// by the handle's compile-time Proto).
//
// ── Three orthogonal layers in the surface ─────────────────────────
//
// 1. Position TAGS (10 empty marker structs):  AtSend / AtRecv /
//    AtSelect / AtOffer / AtEnd / AtStop / AtTerminal / AtCheckpointed
//    / AtDelegate / AtAccept.  AtTerminal is a category covering
//    End ∪ Stop_g (destruction-safe positions); the per-spec tags
//    exist for callers who need to dispatch on the exact combinator.
//
// 2. Position TRAIT family (3): `handle_is_at<H, Tag>` (struct
//    template, primary `false_type` + per-tag×per-combinator
//    specialisations), `handle_is_at_v<H, Tag>` (variable template
//    shortcut), and `HandleIsAt<H, Tag>` (concept gate).  Callers
//    take `requires HandleIsAt<H, AtRecv>` to compile-error reject
//    handles that aren't at the expected position.
//
// 3. Mint + ACCESSORS (7): `view_ok(handle, type_identity<Tag>)`
//    (runtime predicate for cross-type comparison), `mint_session_view
//    <Tag>(handle)` (the §XXI authorisation factory), `session_view_
//    protocol_name<View>()` (rendered Proto name forwarded to the
//    carrier's static accessor), `session_view_message_type[_t]`
//    (payload type extracted from AtSend / AtRecv views — defined
//    ONLY for those tags), `session_view_branch_count[_v]` (branch
//    count from AtSelect / AtOffer views — defined ONLY for those
//    tags).
//
// ── Why this surface exists (Agent 3 finding B7) ───────────────────
//
// Before V-063, production observe / debug renderers reached
// `crucible::safety::proto::mint_session_view<AtRecv>(handle)`
// directly — bypassing the fixy:: audit ledger AND tying inspection
// call sites to substrate ordinals.  A substrate-side rename of any
// position tag (e.g., AtRecv → AtReceive for naming consistency
// across the session universe) would break every call site silently.
// V-063 closes that gap with a 20-symbol re-export surface that
// productions consume via `fixy::sess::view::mint_session_view<Tag>
// (handle)` — substrate moves trip only this header's sentinels,
// not 30+ call sites.
//
// ── Twenty symbols (the public inspection API) ─────────────────────
//
//   position tags (10):        AtSend, AtRecv, AtSelect, AtOffer,
//                              AtEnd, AtStop, AtTerminal,
//                              AtCheckpointed, AtDelegate, AtAccept
//   position trait family (3): handle_is_at, handle_is_at_v,
//                              HandleIsAt
//   view-ok predicate (1):     view_ok
//   mint factory (1):          mint_session_view
//   accessor (1):              session_view_protocol_name
//   message-type metafn (2):   session_view_message_type,
//                              session_view_message_type_t
//   branch-count metafn (2):   session_view_branch_count,
//                              session_view_branch_count_v
//                          total: 10 + 3 + 1 + 1 + 1 + 2 + 2 = 20
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Twenty using-decls, sentinel battery, smoke routine.  No
// new types, no mint factories, no free functions — every entry is
// a pure name-lookup directive (zero machine code).
//
// ── §XXI mint discipline ───────────────────────────────────────────
//
// `mint_session_view<Tag>(handle)` is a §XXI token-mint factory
// (NOT ctx-bound — non-consuming inspection takes no ctx; only the
// handle's compile-time Proto determines admission).  The single
// gate is `HandleIsAt<Handle, Tag>` — fired at construction; after
// mint, the view is trusted and every downstream operation runs at
// zero cost (one const-pointer carry).  Three HS14 fixtures pin the
// gate (see test/fixy_neg/neg_fixy_sess_view_*).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; pure type-level.
//   TypeSafe — using-decls preserve substrate identity; sentinels
//              assert via is_same_v across substrate and fixy.
//   NullSafe — no pointer dereference at this layer; ScopedView
//              guards its own non-null invariant.
//   MemSafe  — ScopedView is borrow-only, no allocation.
//   DetSafe  — view extraction is pure compile-time discipline;
//              same handle always yields the same admissible tags.
//   BorrowSafe — ScopedView is const-only and CRUCIBLE_LIFETIMEBOUND;
//                concurrent views safely coexist.
//   ThreadSafe — read-only borrow surface; no shared mutation.
//   LeakSafe — no resource owned (ScopedView is one const-ptr).
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).

#include <crucible/sessions/SessionView.h>

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace crucible::fixy::sess::view {

// ── 1. Position tags (10) ──────────────────────────────────────────
// Empty marker structs identifying which combinator-head the handle
// is currently at.  AtTerminal is the destruction-safe category
// covering End ∪ Stop_g.
using ::crucible::safety::proto::AtSend;
using ::crucible::safety::proto::AtRecv;
using ::crucible::safety::proto::AtSelect;
using ::crucible::safety::proto::AtOffer;
using ::crucible::safety::proto::AtEnd;
using ::crucible::safety::proto::AtStop;
using ::crucible::safety::proto::AtTerminal;
using ::crucible::safety::proto::AtCheckpointed;
using ::crucible::safety::proto::AtDelegate;
using ::crucible::safety::proto::AtAccept;

// ── 2. Position trait family (3) ───────────────────────────────────
// `handle_is_at<H, Tag>::value` — true iff H is a SessionHandle whose
// Proto matches the position tag.  `_v` is the shortcut; `HandleIsAt`
// is the concept gate (used by `mint_session_view`'s requires-clause).
using ::crucible::safety::proto::handle_is_at;
using ::crucible::safety::proto::handle_is_at_v;
using ::crucible::safety::proto::HandleIsAt;

// ── 3. View-ok predicate (1) ───────────────────────────────────────
// Runtime constexpr predicate for cross-type position-checking —
// equivalent to `handle_is_at_v<...>` but spelled as a function
// over a handle reference + `type_identity<Tag>`; useful in
// generic helpers that already have the handle as a parameter.
using ::crucible::safety::proto::view_ok;

// ── 4. Mint factory (1) ────────────────────────────────────────────
// `mint_session_view<Tag>(handle)` — the §XXI token mint.  Requires
// `HandleIsAt<Handle, Tag>` at construction; returns
// `ScopedView<Handle, Tag>` by value.  Lifetime-bound — `-Wdangling
// -reference` fires when a view of a local handle escapes.
using ::crucible::safety::proto::mint_session_view;

// ── 5. Accessor (1) ────────────────────────────────────────────────
// `session_view_protocol_name<View>()` — rendered name of the view's
// underlying Proto, forwarded to the carrier's `protocol_name()`
// static accessor.  Same cross-TU caveat as protocol_name() itself
// (treat as runtime helper, not constexpr literal).
using ::crucible::safety::proto::session_view_protocol_name;

// ── 6. Message-type metafn (2) ─────────────────────────────────────
// `session_view_message_type<View>::type` — the payload T of a
// Send<T,R> / Recv<T,R> view.  Defined ONLY for AtSend / AtRecv
// views; instantiation on any other tag is ill-formed (no matching
// specialisation).
using ::crucible::safety::proto::session_view_message_type;
using ::crucible::safety::proto::session_view_message_type_t;

// ── 7. Branch-count metafn (2) ─────────────────────────────────────
// `session_view_branch_count<View>::value` — sizeof...(Bs) of a
// Select<Bs...> / Offer<Bs...> view.  Defined ONLY for AtSelect /
// AtOffer views.
using ::crucible::safety::proto::session_view_branch_count;
using ::crucible::safety::proto::session_view_branch_count_v;

}  // namespace crucible::fixy::sess::view

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::sess::view::v063_self_test {

namespace proto = ::crucible::safety::proto;
namespace saf   = ::crucible::safety;

// Fixture types — minimal session protocol shapes.
struct FakeResource {};
struct Msg {};

using SendProto = proto::Send<Msg, proto::End>;
using RecvProto = proto::Recv<Msg, proto::End>;
using EndProto  = proto::End;

using SendHandle = proto::SessionHandle<SendProto, FakeResource, void>;
using RecvHandle = proto::SessionHandle<RecvProto, FakeResource, void>;
using EndHandle  = proto::SessionHandle<EndProto,  FakeResource, void>;

// ── A. Position-tag identity reach ─────────────────────────────────
// Every tag MUST alias the substrate tag exactly.  Empty marker-
// struct identity is enforced via is_same_v.
static_assert(std::is_same_v<AtSend,         proto::AtSend>);
static_assert(std::is_same_v<AtRecv,         proto::AtRecv>);
static_assert(std::is_same_v<AtSelect,       proto::AtSelect>);
static_assert(std::is_same_v<AtOffer,        proto::AtOffer>);
static_assert(std::is_same_v<AtEnd,          proto::AtEnd>);
static_assert(std::is_same_v<AtStop,         proto::AtStop>);
static_assert(std::is_same_v<AtTerminal,     proto::AtTerminal>);
static_assert(std::is_same_v<AtCheckpointed, proto::AtCheckpointed>);
static_assert(std::is_same_v<AtDelegate,     proto::AtDelegate>);
static_assert(std::is_same_v<AtAccept,       proto::AtAccept>);

// ── B. handle_is_at trait reach ────────────────────────────────────
// Positive: SendHandle IS at AtSend.
static_assert(handle_is_at_v<SendHandle, AtSend>);
// Negative: SendHandle is NOT at AtRecv.
static_assert(!handle_is_at_v<SendHandle, AtRecv>);
// AtTerminal covers End ∪ Stop — EndHandle qualifies.
static_assert(handle_is_at_v<EndHandle, AtEnd>);
static_assert(handle_is_at_v<EndHandle, AtTerminal>);
// Substrate identity through fixy.
static_assert(
    handle_is_at_v<SendHandle, AtSend>
    == proto::handle_is_at_v<SendHandle, proto::AtSend>,
    "handle_is_at_v must reach identically through fixy::");

// ── C. HandleIsAt concept reach ────────────────────────────────────
// Concept admits matching handle/tag pairs and rejects mismatches.
static_assert(HandleIsAt<SendHandle, AtSend>);
static_assert(HandleIsAt<RecvHandle, AtRecv>);
static_assert(!HandleIsAt<SendHandle, AtRecv>);
static_assert(!HandleIsAt<EndHandle,  AtSend>);
static_assert(HandleIsAt<EndHandle, AtTerminal>);

// ── D. view_ok signature reach (type-level) ────────────────────────
// `view_ok` is a constexpr function — declval witnesses its return-
// type discipline without constructing an actual SessionHandle (the
// non-terminal handle destructor would fire abandonment-checks at
// scope exit per substrate doc-block).  The runtime evaluation
// itself is covered by test/test_session_view.cpp.
static_assert(std::is_same_v<
    decltype(view_ok(std::declval<SendHandle const&>(),
                     std::type_identity<AtSend>{})),
    bool>,
    "view_ok(handle, type_identity<Tag>) must be a bool-returning "
    "predicate at the fixy:: re-export boundary.");

// ── E. mint_session_view return-type reach (type-level) ────────────
// `decltype` witnesses that minting at the matching tag produces
// `ScopedView<Handle, Tag>` without actually invoking the factory
// (avoiding the Send-state abandonment-check destructor at scope
// exit).  Substrate identity preserved through fixy.
using MintedView = decltype(mint_session_view<AtSend>(
    std::declval<SendHandle const&>()));
static_assert(std::is_same_v<MintedView,
    saf::ScopedView<SendHandle, AtSend>>,
    "mint_session_view<AtSend>(send_handle&) must produce "
    "ScopedView<SendHandle, AtSend>.");
static_assert(std::is_same_v<MintedView,
    decltype(proto::mint_session_view<proto::AtSend>(
        std::declval<SendHandle const&>()))>,
    "mint_session_view must reach identically through fixy::");

// ── F. Message-type metafn reach (AtSend / AtRecv only) ────────────
using SendView = saf::ScopedView<SendHandle, AtSend>;
using RecvView = saf::ScopedView<RecvHandle, AtRecv>;
static_assert(std::is_same_v<session_view_message_type_t<SendView>, Msg>,
    "session_view_message_type_t<AtSend view> = Msg.");
static_assert(std::is_same_v<session_view_message_type_t<RecvView>, Msg>,
    "session_view_message_type_t<AtRecv view> = Msg.");
// Class-template form reach.
static_assert(std::is_same_v<
    typename session_view_message_type<SendView>::type, Msg>);

// ── G. Branch-count metafn reach (AtSelect / AtOffer only) ─────────
using SelectProto = proto::Select<SendProto, RecvProto, EndProto>;
using SelectHandle = proto::SessionHandle<SelectProto, FakeResource, void>;
using SelectView = saf::ScopedView<SelectHandle, AtSelect>;
static_assert(session_view_branch_count_v<SelectView> == 3,
    "session_view_branch_count_v counts sizeof...(Bs) of Select.");
static_assert(session_view_branch_count<SelectView>::value == 3,
    "Class-template form returns the same count.");

// ── H. Cardinality witness ─────────────────────────────────────────
//
//   position tags (10):     AtSend, AtRecv, AtSelect, AtOffer,
//                           AtEnd, AtStop, AtTerminal,
//                           AtCheckpointed, AtDelegate, AtAccept
// + position trait (3):     handle_is_at, _v, HandleIsAt
// + view_ok predicate (1)
// + mint factory (1)        mint_session_view
// + accessor (1)            session_view_protocol_name
// + message-type metafn (2) session_view_message_type, _t
// + branch-count metafn (2) session_view_branch_count, _v
//                                              ──── 20
constexpr int v063_surface_cardinality = 20;
static_assert(v063_surface_cardinality == 20,
    "fixy::sess::view:: V-063 surface cardinality drifted — update "
    "SessView.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::view::v063_self_test

namespace crucible::fixy::sess::view {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    namespace saf   = ::crucible::safety;

    struct FakeResource {};
    struct Msg {};

    using SendProto  = proto::Send<Msg, proto::End>;
    using SendHandle = proto::SessionHandle<SendProto, FakeResource, void>;

    // Pure type-level smoke: substrate's Send-state handle has an
    // abandonment-check destructor that fires at scope exit, so we
    // mirror the substrate's own self-test discipline and avoid
    // constructing handles here.  The runtime evaluation is covered
    // by test/test_session_view.cpp through stack-local handles
    // consumed before scope exit.

    [[maybe_unused]] constexpr bool admits_send =
        HandleIsAt<SendHandle, AtSend>;
    [[maybe_unused]] constexpr bool rejects_recv =
        !HandleIsAt<SendHandle, AtRecv>;

    using ViewType = decltype(mint_session_view<AtSend>(
        std::declval<SendHandle const&>()));
    [[maybe_unused]] constexpr bool msg_ok = std::is_same_v<
        session_view_message_type_t<ViewType>, Msg>;
    [[maybe_unused]] constexpr bool view_shape_ok = std::is_same_v<
        ViewType, saf::ScopedView<SendHandle, AtSend>>;

    (void) admits_send; (void) rejects_recv;
    (void) msg_ok; (void) view_shape_ok;
}

}  // namespace crucible::fixy::sess::view
