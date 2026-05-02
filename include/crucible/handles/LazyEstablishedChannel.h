#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety — LazyEstablishedChannel<Proto, Resource>
//                    (SAFEINT-B14, #403,
//                    misc/24_04_2026_safety_integration.md §14)
//
// Synchronous session establishment via `mint_channel<Proto>()`
// constructs both endpoint handles in one call.  Several Crucible
// patterns need ASYNCHRONOUS establishment: Vessel startup publishes
// a channel before the dispatch path tries to observe it; Cipher
// hot-tier registration publishes a peer's QP before the local
// Keeper attempts a fetch; the schema-cache thread publishes its
// catalog before any worker queries it.
//
// The conventional pattern looks like:
//
//     std::atomic<bool> initialized{false};
//     ChannelStorage   storage;
//
//     // Init thread:
//     storage.init(...);
//     initialized.store(true, std::memory_order_release);
//
//     // Worker thread:
//     while (!initialized.load(std::memory_order_acquire)) { ... }
//     // now use &storage
//
// Three subtle bugs hide here.  (1) The release/acquire pair must
// pair the storage WRITES to the storage READS — easy to get wrong
// if the bool is used before the storage is fully initialised.
// (2) "Already initialised" detection requires a separate guard.
// (3) Observers that arrive before init must either spin or fall
// back to a slow path; the inline conditional checks bleed into
// every dispatch site.
//
// LazyEstablishedChannel removes the discipline:
//
//   * `PublishOnce<Resource>` provides the single-publish primitive
//     and the release/acquire pairing.  Second `establish()` fires
//     the PublishOnce contract (no silent overwrite).
//   * `observe()` returns `std::optional<SessionHandle<Proto, R*>>`
//     — explicit "session present yet?" return rather than UB on
//     uninitialised access.
//   * The channel is `Pinned` because the atomic IS the channel
//     identity (same discipline as PublishOnce, MpmcRing, etc.).
//
// ─── Resource shape ────────────────────────────────────────────────
//
// The Resource template parameter is the POINTEE type.  Internally
// we store `PublishOnce<Resource>` (which contains
// `std::atomic<Resource*>`).  observe() yields a SessionHandle whose
// Resource template parameter is `Resource*` — a pointer is a value
// type, so the SessionResource concept (#406) admits it without
// requiring Resource to derive Pinned.  This matches the pattern
// MachineSessionBridge already uses for its session_view().
//
// If the publisher's storage IS Pinned (typical for stable channels),
// callers can additionally constrain `Resource` via a concept; this
// header does not require Pinned to keep the surface small.  The
// caller's contract is "the Resource* passed to establish() outlives
// every successful observe()."
//
// ─── Worked example: Vessel startup ────────────────────────────────
//
//     struct VesselChannel {
//         // ... whatever the channel's runtime state is
//     };
//
//     using DispatchProto = Loop<Send<DispatchRequest, Recv<MockHandle, Continue>>>;
//
//     LazyEstablishedChannel<DispatchProto, VesselChannel> g_dispatch;
//     VesselChannel g_storage;  // long-lived; address stable
//
//     // Vessel init thread (runs once at startup):
//     init_channel(g_storage);
//     g_dispatch.establish(&g_storage);
//
//     // Dispatch path (runs concurrently from any worker):
//     auto h = g_dispatch.observe();
//     if (!h) {
//         // Init not finished yet; fall back to eager dispatch path
//         eager_dispatch(req);
//         return;
//     }
//     // Drive the protocol normally on *h.
//     auto next = std::move(*h).send(req, transport);
//
// No conditional on `bool initialized`, no manual mutex around init,
// no order-of-publication subtlety.  PublishOnce's release/acquire
// guarantees that the channel's contents are visible to any observer
// whose `observe()` returned non-empty.
//
// ─── Resolves ───────────────────────────────────────────────────────
//
//   * #160 (thread_local schema_cache WriteOnce) — the schema cache
//     becomes a LazyEstablishedChannel<SchemaQueryProto, SchemaCache>.
//   * #87  (register_externals_from_region_ Session protocol) — the
//     external-region registration becomes a LazyEstablishedChannel.
//
// Production refactors (those tasks) compose this header.  This
// commit ships the INFRASTRUCTURE only — no production code changes.
//
// ─── Cost ───────────────────────────────────────────────────────────
//
//   sizeof(LazyEstablishedChannel<Proto, Resource>)
//       == sizeof(PublishOnce<Resource>)
//       == sizeof(std::atomic<Resource*>)
//       == sizeof(void*)  on every supported platform.
//
// publish/observe are exactly the cost of PublishOnce's atomic
// store-release / load-acquire pair plus the (zero-cost in release)
// SessionHandle move into std::optional.
//
// ─── References ────────────────────────────────────────────────────
//
//   misc/24_04_2026_safety_integration.md §14 — design.
//   safety/PublishOnce.h — the lock-free pointer-handoff primitive.
//   safety/Session.h — SessionHandle + mint_session_handle.
//   safety/Pinned.h — address-stability discipline.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Pinned.h>
#include <crucible/handles/PublishOnce.h>
#include <crucible/sessions/Session.h>

#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── LazyEstablishedChannel<Proto, Resource> ────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename Proto, typename Resource>
class [[nodiscard]] LazyEstablishedChannel
    : public Pinned<LazyEstablishedChannel<Proto, Resource>>
{
    static_assert(safety::proto::is_well_formed_v<Proto>,
        "crucible::session::diagnostic [Protocol_Ill_Formed]: "
        "LazyEstablishedChannel<Proto, Resource>: Proto must be "
        "well-formed (every Continue must have an enclosing Loop).");

    PublishOnce<Resource> resource_;

public:
    using protocol      = Proto;
    using resource_type = Resource;

    // The handle yielded by a successful observe().  Resource* is a
    // pointer (value type) — satisfies the SessionResource concept
    // unconditionally; the user does NOT need to make Resource Pinned
    // for the framework's discipline to apply.
    using session_handle_type =
        decltype(safety::proto::mint_session_handle<Proto>(
            std::declval<Resource*>()));

    // Default-construct: PublishOnce starts un-published.
    constexpr LazyEstablishedChannel() noexcept = default;

    // Pinned base deletes copy/move.
    ~LazyEstablishedChannel() = default;

    // ── Publish ──────────────────────────────────────────────────────
    //
    // Single-publisher lock-free handoff of `r` to all subsequent
    // observers.  Caller's contract: `r` outlives every session
    // handle that any successful observe() yields, AND `*r` is
    // fully initialised before establish() returns.  The
    // store-release semantics of PublishOnce guarantee that any
    // observer's load-acquire sees the fully-initialised state.
    //
    // Calling establish() a second time fires PublishOnce's
    // contract (compile-time enforced never-double-publish via the
    // CAS pattern + contract_assert).  This catches the bug class
    // "two init paths raced and one silently overwrote the other".

    void establish(Resource* r) noexcept {
        resource_.publish(r);
    }

    // ── Observe ──────────────────────────────────────────────────────
    //
    // Returns:
    //   * std::nullopt — establish() has not yet fired (or fired but
    //     this thread's load-acquire hasn't observed the publication
    //     yet, which is still a "not established" state from this
    //     observer's perspective).
    //   * std::optional containing a fresh SessionHandle<Proto,
    //     Resource*> bound to the published Resource — usable
    //     directly via the framework's per-state methods.
    //
    // observe() is non-mutating from the channel's perspective; many
    // threads may call it concurrently.  Each call mints a fresh
    // session handle pointing to the same Resource — concurrent
    // handle use must coordinate at the Resource level (the Resource
    // is presumably Pinned + has its own SPSC / SWMR / atomic
    // discipline).

    [[nodiscard]] std::optional<session_handle_type> observe() noexcept {
        Resource* r = resource_.observe();
        if (!r) return std::nullopt;
        return safety::proto::mint_session_handle<Proto>(r);
    }

    // ── Diagnostic ──────────────────────────────────────────────────
    //
    // Relaxed read of the publication state.  Useful for branch-
    // prediction hints and metrics; do NOT dereference based on this
    // — only observe() carries the acquire-release synchronization
    // for the published object's contents.

    [[nodiscard]] bool is_established() const noexcept {
        return resource_.is_published();
    }

    // Static accessor — protocol's compile-time-rendered name.  Same
    // cross-TU constexpr-capture caveat as protocol_name() itself
    // (treat as runtime helper for substring matching; reserve
    // constexpr capture for pure compile-time identity checks).

    [[nodiscard]] static constexpr std::string_view protocol_name() noexcept {
        return safety::proto::detail::type_name<Proto>();
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── Zero-cost size guarantees ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// LazyEstablishedChannel is exactly the size of its PublishOnce
// member, which is the size of one atomic pointer.  The Pinned base
// is empty + EBO-collapsed; Proto is a phantom; the SessionHandle is
// constructed on-demand by observe() and never stored in the channel.

namespace detail::lec_size_test {

struct AnyResource {};
using P1 = safety::proto::End;
using P2 = safety::proto::Loop<safety::proto::Send<int, safety::proto::Continue>>;

static_assert(sizeof(LazyEstablishedChannel<P1, AnyResource>)
              == sizeof(PublishOnce<AnyResource>),
    "LazyEstablishedChannel must add zero bytes beyond its PublishOnce.");

static_assert(sizeof(LazyEstablishedChannel<P2, AnyResource>)
              == sizeof(std::atomic<AnyResource*>),
    "LazyEstablishedChannel must collapse to one atomic pointer.");

}  // namespace detail::lec_size_test

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::lec_self_test {

struct DummyChannel { int sentinel = 0; };

using SimpleProto = safety::proto::Loop<
    safety::proto::Select<
        safety::proto::Send<int, safety::proto::Continue>,
        safety::proto::End>>;

using LEC = LazyEstablishedChannel<SimpleProto, DummyChannel>;

// Pinned discipline.
static_assert(!std::is_copy_constructible_v<LEC>);
static_assert(!std::is_move_constructible_v<LEC>);
static_assert(std::is_base_of_v<Pinned<LEC>, LEC>);

// Public typedefs wire correctly.
static_assert(std::is_same_v<typename LEC::protocol,      SimpleProto>);
static_assert(std::is_same_v<typename LEC::resource_type, DummyChannel>);

// session_handle_type is the SessionHandle specialisation produced
// by mint_session_handle<SimpleProto>(DummyChannel*).  Loop unrolls
// at construction, so the resulting handle's compile-time Proto is
// the loop body (a Select), with Loop<...> as LoopCtx.
using ExpectedSession = safety::proto::SessionHandle<
    safety::proto::Select<
        safety::proto::Send<int, safety::proto::Continue>,
        safety::proto::End>,
    DummyChannel*,
    SimpleProto>;
static_assert(std::is_same_v<typename LEC::session_handle_type,
                              ExpectedSession>);

}  // namespace detail::lec_self_test

}  // namespace crucible::safety
