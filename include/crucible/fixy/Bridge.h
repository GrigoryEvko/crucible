#pragma once

// ── crucible::fixy::bridge — Bridge minters under fixy:: ───────────
//
// Re-export per misc/16_05_2026_fixy.md.  Surfaces the
// failure-recovery / persistence / mode-bridge wrap factories under
// `fixy::bridge::` so callers who include only the fixy umbrella
// never have to descend into the bridges/ tree to wrap a handle.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: every re-export
// preserves the substrate's wrap-shape concept gate (the handle
// parameter dictates whether the wrap is permitted, surfaced via
// SFINAE/concept overload resolution), the `[[nodiscard]]
// constexpr noexcept` qualifiers, and the bridge composition
// algebra (Recording over Crash-watched over PermissionedSession).
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::proto::mint_recording_session(handle, ...)
//   safety::proto::mint_crash_watched_session(handle, ...)
//   safety::proto::mint_persisted_session(ctx, handle, cipher, ...)
//   bridges::mint_recording_endpoint(handle, ...)
//   bridges::mint_crash_watched_endpoint(handle, ...)
//   crucible::mint_vigil_mode_bridge(vigil)
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports introduce no new state path.
//   TypeSafe — using-declarations preserve the substrate's concept
//              gates (IsSessionHandle, PeerTag deduction, etc.).
//   NullSafe — wrapped handles inherit substrate's pointer discipline.
//   MemSafe  — allocation discipline differs per minter (see below);
//              every alloc is owned via RAII whose destructor runs.
//   BorrowSafe — wrap algebra preserves linearity of inner handle.
//   ThreadSafe — log/flag pointers obey substrate's atomic discipline.
//   LeakSafe — wrappers' destructor runs inner handle's destructor;
//              for the allocating minter (mint_persisted_session)
//              this transitively closes the SessionPersistenceState
//              owned via std::unique_ptr.
//   DetSafe  — bit-exact composition; pure wrap.
//
// ── Allocation discipline (fixy-A4-010) ───────────────────────────
//
// Three classes of mint behavior in this header — `grep make_unique`
// in bridges/SessionPersistence.h returns the canonical heap sites:
//
//   1. Pure-wrap minters (zero heap, EBO-collapsible):
//      mint_recording_session, mint_crash_watched_session,
//      mint_recording_endpoint, mint_crash_watched_endpoint,
//      mint_vigil_mode_bridge.
//      Compose handles via using-declarations + struct-by-value;
//      sizeof(result) == sizeof(inner) + O(byte) for the wrap tag.
//
//   2. Allocating minter (one heap alloc per mint, RAII-owned):
//      mint_persisted_session.
//      Performs `std::make_unique<SessionPersistenceState<CallerRow>>`
//      at the mint boundary (bridges/SessionPersistence.h:701/755/812)
//      because the persistence state owns a SessionEventLog drain ring
//      whose lifetime must exceed the inner RecordingSessionHandle's.
//      The unique_ptr is moved into PersistedSessionHandle; the
//      handle's destructor runs the state's destructor (flush + close).
//      MemSafe via LeakSafe-RAII, NOT via "no heap".
//
// Hot-path callers (per-op record / per-op recv) MUST use class 1.
// Class 2 is a Cipher cold-tier roll-forward mint — appropriate at
// session-establish time, not in the per-message loop.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Class 1: zero. using-declarations are pure name-lookup directives.
// Class 2: one heap alloc per mint_persisted_session call (plus
//          whatever SessionPersistenceState's ctor does — typically
//          one ring-buffer allocation + Cipher::OpenView capture).

// ── Include discipline (FIXY-AUDIT-C5 + fixy-H-23) ─────────────────
//
// All substrate headers are pulled in directly, per CLAUDE.md §XV
// "Self-contained. Every header compiles standalone. Add required
// includes directly; never rely on transitive pull-in."
//
// fixy-H-23 (2026-05-19): the `mint_vigil_mode_bridge` re-export
// below used to require `<crucible/Vigil.h>` because the substrate
// signature returned the nested type `Vigil::ModeSessionHandle`.
// That dragged in BackgroundThread + MerkleDag + Cipher (warm tier)
// + perf::Senses + warden::DeadlineWatchdog — every fixy::bridge::
// consumer paid the full 1.1 KLoC Vigil hub cost just to wrap a
// mode-cell session.  The mode machinery is now carved out into a
// standalone `<crucible/bridges/VigilModeHandle.h>` (~140 LOC) that
// pulls only MachineSessionBridge + sessions/Session.  The umbrella
// re-export resolves to the ModeCell-taking primary mint surface;
// callers that already hold a Vigil reach the convenience overload
// `mint_vigil_mode_bridge(const Vigil&)` only after directly
// including `<crucible/Vigil.h>` (e.g. via `<crucible/Fixy.h>`).
//
// The four `bridges/*` headers (CrashTransport, EndpointMint,
// RecordingSessionHandle, SessionPersistence) are each required
// directly because they declare distinct mint factories surfaced
// under the fixy::bridge:: namespace — none subsumes another.
// fixy-A2-014: SessionPersistence.h no longer transitively pulls Cipher.h;
// fixy::bridge:: re-exports mint_persisted_session and its companions,
// so the umbrella restores the convenience pull.
#include <crucible/Cipher.h>
#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/EndpointMint.h>
#include <crucible/bridges/MachineSessionBridge.h>  // FIXY-U-117: mint_atomic_session
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/bridges/SessionPersistence.h>
#include <crucible/bridges/VigilModeHandle.h>
#include <crucible/fixy/Handle.h>                    // FIXY-V-216 — cross-tier identity sentinel
#include <crucible/permissions/PermissionInherit.h>  // FIXY-U-070 sentinel
                                                     //   uses survivor_registry
#include <crucible/safety/EpochVersioned.h>          // FIXY-V-216 — dual-export at bridge tier

#include <cstdint>                                    // FIXY-V-216 sentinel uses std::uint64_t
#include <type_traits>                                // FIXY-U-070 sentinel

namespace crucible::fixy::bridge {

// ═════════════════════════════════════════════════════════════════════
// ── Recording session — wrap with SessionEventLog emission ─────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_recording_session(handle, log, self, peer)` returns a
// RecordingSessionHandle<Proto, Resource, Log> that emits structured
// events on every Send/Recv for replay / debugging.

using ::crucible::safety::proto::mint_recording_session;
using ::crucible::safety::proto::RecordingSessionHandle;

// ═════════════════════════════════════════════════════════════════════
// ── Crash-watched session — wrap with Stop_g<C> peer-crash gate ────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_crash_watched_session<PeerTag>(handle, flag)` returns a
// CrashWatchedHandle<Proto, Resource, PeerTag, LoopCtx> that
// observes peer.crashed() before every Recv and surfaces Stop_g<C>
// when set.

using ::crucible::safety::proto::mint_crash_watched_session;
using ::crucible::safety::proto::CrashWatchedHandle;

// ═════════════════════════════════════════════════════════════════════
// ── Crash-event surface (FIXY-U-070 / GAPS-045) ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The crash-watched session wrap (above) produces a `Stop_g<C>` on
// peer.crashed() and routes recovery through the substrate's
// `wrap_crash_return` friend factory.  That factory bundles the
// inner protocol's would-have-been return value with the surviving
// PermSet into a `CrashEvent<PeerTag, Resource, SurvivorTags...>` —
// a structured-return type that callers pattern-match on at the
// stop boundary to drive Cipher roll-forward / Canopy peer-replace.
//
// Pre-U-070, every consumer that wanted to write a stop-handler had
// to descend into `<crucible/bridges/CrashTransport.h>` directly,
// because `fixy::bridge::` surfaced ONLY the `CrashWatchedHandle`
// wrap and the `mint_crash_watched_session` factory — never the
// `CrashEvent` family itself.  GAPS-045 was filed when the
// observability sweep noticed that the umbrella's "fixy:: covers
// the bridge tree" promise was vacuous for the stop-handler side.
//
// The six items below close the surface gap:
//   wrap_crash_return            — friend factory authoritative for
//                                  CrashEvent construction
//   CrashEvent                   — structured-return carrier
//   crash_event_from_survivors   — helper trait projecting an
//                                  inheritance_list onto CrashEvent
//   crash_event_for_t            — canonical type alias for the
//                                  PeerTag's CrashEvent
//   crash_event_matches_survivors      — predicate trait
//   crash_event_matches_survivors_v    — predicate variable template
//
// These are pure substrate-identity using-declarations; the
// `CrashEvent` ctor remains friend-gated to `wrap_crash_return`, so
// the surface gain is "you can name the type without crucible/bridges/
// include," NOT "you can construct it from any call site."  Hot-path
// stop-handler is type-name-only:
//
//   auto evt_or = ::crucible::fixy::bridge::wrap_crash_return(
//                     handle.detach(), recovery_return_value);
//   if (auto* evt = std::get_if<fixy::bridge::crash_event_for_t<
//                                  PeerTag, MyResource>>(&evt_or))
//   { /* roll-forward via evt->survivors() */ }
//
// Discipline (FIXY-A4-011 dual-export): the substrate identity is
// witnessed inline below (self_test block) AND in test_fixy_bridge.cpp
// to defend against silent substrate rename — same recipe as the
// fixy::handle:: + fixy::diag:: surfaces.

using ::crucible::safety::proto::wrap_crash_return;
using ::crucible::safety::proto::CrashEvent;
// The next four live in `safety::proto::detail::` in the substrate.
// They are nonetheless the documented stop-handler pattern (see
// CrashTransport.h doc-block on CrashEvent line 178+); surfacing them
// here promotes the documented-public-but-detail-namespaced helpers
// to consumer-discoverable position WITHOUT requiring a substrate
// refactor.  If the substrate later promotes them out of detail::,
// these using-decls will adapt seamlessly (using-decl preserves the
// substrate path on each re-resolution).
using ::crucible::safety::proto::detail::crash_event_from_survivors;
using ::crucible::safety::proto::detail::crash_event_for_t;
using ::crucible::safety::proto::detail::crash_event_matches_survivors;
using ::crucible::safety::proto::detail::crash_event_matches_survivors_v;

// ═════════════════════════════════════════════════════════════════════
// ── Persisted session — Cipher cold-tier roll-forward wrapper ──────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_persisted_session(ctx, handle, cipher, ...)` composes
// RecordingSessionHandle with Cipher::OpenView so every recorded
// event is persisted for crash-recovery replay across the
// reincarnation boundary.  Bare overloads without (ctx, cipher) are
// `=delete`d with diagnostics.

using ::crucible::safety::proto::mint_persisted_session;

// ═════════════════════════════════════════════════════════════════════
// ── Endpoint-side wraps (bridges/EndpointMint.h) ───────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Sister factories that wrap the underlying Endpoint's handle rather
// than a free SessionHandle.  Useful when the substrate is owned by
// a Pipeline stage that already minted Endpoints — wrap once at the
// endpoint construction site rather than at every Stage body entry.

using ::crucible::bridges::mint_recording_endpoint;
using ::crucible::bridges::mint_crash_watched_endpoint;

// ═════════════════════════════════════════════════════════════════════
// ── Vigil mode bridge (Vigil.h) ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_vigil_mode_bridge(vigil)` exposes the Vigil's mode-tracking
// SessionHandle for cold observers (Keeper telemetry, Canopy
// recovery probes).  Documents the read-only access discipline at
// the bridge level; production hot path never calls this.

using ::crucible::mint_vigil_mode_bridge;

// ═════════════════════════════════════════════════════════════════════
// ── Atomic-machine session — Machine<State> + SessionHandle bridge ─
// ═════════════════════════════════════════════════════════════════════
//
// FIXY-U-117: `mint_atomic_session<Proto, Cell>(cell)` mints a
// SessionHandle that observes a Cell's atomic state-machine through
// the Send/Recv protocol surface.  The substrate function lives in
// `crucible::safety::` (NOT `crucible::bridges::` despite the file
// path — see `bridges/MachineSessionBridge.h:107` `namespace
// crucible::safety { ... }`), so the using-decl resolves the name
// from the safety namespace.
//
// Concept gate `AtomicMachineCell<Cell> && safety::proto::is_well_formed_v<Proto>`
// is preserved through the alias (using-decl is name-lookup-only).
// Pointer-identity reach proof lives in test_fixy_bridge.cpp where
// probe types AtomicProbeCell + Send<int, End> instantiate the
// template at a concrete argument pair.

using ::crucible::safety::mint_atomic_session;

// ═════════════════════════════════════════════════════════════════════
// ── EpochVersioned<T> — fleet-epoch + per-Relay generation wrapper ─
// ═════════════════════════════════════════════════════════════════════
//
// FIXY-V-216 (Agent 8 Part 4 #2 + Part 10 #8): dual-export of the
// `safety/EpochVersioned.h` substrate wrapper that ALREADY backs
// SessionPersistence's checkpoint headers, RecordingSessionHandle's
// event-log emission, and the Cipher cold-tier roll-forward index.
// The substrate ships at `crucible::safety::EpochVersioned<T>` and is
// also re-exported at `fixy::handle::EpochVersioned` for handle-tier
// consumers (TraceRing wrappers, PublishSlot publishers).  Bridges
// consume the same wrapper to tag checkpoint values with the
// (committed_epoch, local_generation) pair at which they were produced
// — without this `using`, a band-3 bridge author had to reach into
// `safety::` directly, bypassing the fixy:: discipline that catches
// substrate-identity drift at the umbrella boundary.
//
// The Epoch + Generation strong-typed newtypes are re-exported too so
// admission-gate sites can spell the axes via `fixy::bridge::Epoch{...}`
// without descending into safety::.  Cross-axis confusion (passing a
// Generation where an Epoch is expected) is a substrate-level compile
// error preserved through the alias — verified by neg-fixture #1.

using ::crucible::safety::Epoch;
using ::crucible::safety::Generation;
using ::crucible::safety::EpochVersioned;

}  // namespace crucible::fixy::bridge

// ─── Dual-export sentinel — FIXY-U-070 ─────────────────────────────
//
// Header-internal identity sentinels for the crash-event surface.
// Every alias resolves to its substrate type, not a shadowed local.
// Same recipe as fixy/Handle.h::self_test + fixy/Diag.h::self_test —
// drift surfaces here at every consumer's include time, NOT only in
// test_fixy_bridge.cpp.  Cardinality witness at the tail catches a
// future contributor adding (or removing) a crash-event item without
// updating both the using-decl block AND this sentinel.

namespace crucible::fixy::bridge::self_test {

// Probe peer tag with a declared `survivors_t` projection so
// crash_event_for_t can resolve in the sentinel context.  Real
// production peer tags would specialize at the tag declaration site.
struct BridgeProbePeer {};
struct BridgeProbeResource {};
struct BridgeProbeSurvivorA {};
struct BridgeProbeSurvivorB {};

}  // namespace crucible::fixy::bridge::self_test

namespace crucible::permissions {

// Specialize survivor_registry for the probe peer so survivors_t
// (used by crash_event_for_t) resolves to a non-empty list at
// sentinel-evaluation time.  Production peer tags carry analogous
// specializations next to their `struct PeerTag {};` declaration.
template <>
struct survivor_registry<
    ::crucible::fixy::bridge::self_test::BridgeProbePeer>
{
    using type = inheritance_list<
        ::crucible::fixy::bridge::self_test::BridgeProbeSurvivorA,
        ::crucible::fixy::bridge::self_test::BridgeProbeSurvivorB>;
};

}  // namespace crucible::permissions

namespace crucible::fixy::bridge::self_test {

// 1. Function-template identity — `wrap_crash_return` is a free
//    function template; reaching it via fixy::bridge:: at concrete
//    template args is witnessed in test_fixy_bridge.cpp (where the
//    full type context is available).  The using-decl above is the
//    structural change; the test TU is the runtime witness.

// 2. CrashEvent template identity.
static_assert(std::is_same_v<
    ::crucible::fixy::bridge::CrashEvent<
        BridgeProbePeer, BridgeProbeResource,
        BridgeProbeSurvivorA, BridgeProbeSurvivorB>,
    ::crucible::safety::proto::CrashEvent<
        BridgeProbePeer, BridgeProbeResource,
        BridgeProbeSurvivorA, BridgeProbeSurvivorB>>,
    "fixy::bridge::CrashEvent must alias safety::proto::CrashEvent");

// 3. crash_event_from_survivors helper identity.  Substrate lives in
//    proto::detail::; the fixy::bridge:: alias resolves to the same.
static_assert(std::is_same_v<
    typename ::crucible::fixy::bridge::crash_event_from_survivors<
        BridgeProbePeer, BridgeProbeResource,
        ::crucible::permissions::inheritance_list<
            BridgeProbeSurvivorA, BridgeProbeSurvivorB>>::type,
    typename ::crucible::safety::proto::detail::crash_event_from_survivors<
        BridgeProbePeer, BridgeProbeResource,
        ::crucible::permissions::inheritance_list<
            BridgeProbeSurvivorA, BridgeProbeSurvivorB>>::type>,
    "fixy::bridge::crash_event_from_survivors must alias substrate");

// 4. crash_event_for_t resolves through the alias.
static_assert(std::is_same_v<
    ::crucible::fixy::bridge::crash_event_for_t<
        BridgeProbePeer, BridgeProbeResource>,
    ::crucible::safety::proto::detail::crash_event_for_t<
        BridgeProbePeer, BridgeProbeResource>>,
    "fixy::bridge::crash_event_for_t must alias substrate");

// 5. crash_event_matches_survivors predicate true on canonical shape.
using ProbeCanonicalEvent = ::crucible::fixy::bridge::crash_event_for_t<
    BridgeProbePeer, BridgeProbeResource>;
static_assert(::crucible::fixy::bridge::crash_event_matches_survivors<
    ProbeCanonicalEvent>::value,
    "predicate must fire true on the canonical CrashEvent for the peer");
static_assert(::crucible::fixy::bridge::crash_event_matches_survivors_v<
    ProbeCanonicalEvent>,
    "variable template must fire true on the canonical CrashEvent");

// 6. Predicate false on a manually-constructed mismatched event.
using ProbeMismatchEvent = ::crucible::fixy::bridge::CrashEvent<
    BridgeProbePeer, BridgeProbeResource,
    BridgeProbeSurvivorA>;  // only one survivor, registry has two
static_assert(!::crucible::fixy::bridge::crash_event_matches_survivors_v<
    ProbeMismatchEvent>,
    "predicate must reject events whose survivor list doesn't match "
    "the peer's registered survivors_t");

// Cardinality witness: 6 items surfaced (wrap_crash_return /
// CrashEvent / crash_event_from_survivors / crash_event_for_t /
// crash_event_matches_survivors / crash_event_matches_survivors_v).
// A future addition / removal MUST bump this number AND extend the
// sentinel block above.
constexpr int crash_event_surface_cardinality = 6;
static_assert(crash_event_surface_cardinality == 6,
    "fixy::bridge:: crash-event surface cardinality drifted — update "
    "Bridge.h sentinel block to match the substrate.");

// FIXY-V-216 — EpochVersioned dual-export sentinels.  The wrapper is
// also re-exported in fixy::handle::; the bridge-tier alias MUST
// resolve to the IDENTICAL substrate type so a value attached at
// handle tier (e.g. a TraceRing entry) and a value attached at
// bridge tier (e.g. a checkpoint header) share representation when
// they flow through the same Cipher cold-tier index.
//
// 1. Strong-typed axis identity preserved through alias.

struct BridgeProbeEpochT {};

static_assert(std::is_same_v<
    ::crucible::fixy::bridge::Epoch,
    ::crucible::safety::Epoch>,
    "fixy::bridge::Epoch must alias safety::Epoch — Canopy fleet-"
    "epoch identity drift would break checkpoint admission gates "
    "at bridge tier.");

static_assert(std::is_same_v<
    ::crucible::fixy::bridge::Generation,
    ::crucible::safety::Generation>,
    "fixy::bridge::Generation must alias safety::Generation — "
    "per-Relay restart counter identity drift would silently equate "
    "Generation and Epoch at the bridge-tier admission gates.");

// 2. Wrapper template identity preserved through alias.
static_assert(std::is_same_v<
    ::crucible::fixy::bridge::EpochVersioned<BridgeProbeEpochT>,
    ::crucible::safety::EpochVersioned<BridgeProbeEpochT>>,
    "fixy::bridge::EpochVersioned<T> must alias safety::EpochVersioned<T> "
    "— bridge-tier checkpoint values flow through Cipher cold-tier as "
    "the same byte-representation handle-tier consumers see; drift "
    "here breaks the federation cache key (FOUND-G68 row_hash).");

// 3. Cross-tier representation identity — handle and bridge aliases
//    MUST resolve to one substrate type.  Otherwise a value attached
//    at handle tier cannot be admission-gated at bridge tier without
//    a retag (which would force a fresh row_hash and miss the
//    federation cache).
static_assert(std::is_same_v<
    ::crucible::fixy::bridge::EpochVersioned<BridgeProbeEpochT>,
    ::crucible::fixy::handle::EpochVersioned<BridgeProbeEpochT>>,
    "fixy::bridge::EpochVersioned<T> and fixy::handle::EpochVersioned<T> "
    "MUST share substrate identity — dual-export discipline; drift would "
    "force a retag at every handle→bridge crossing and invalidate the "
    "FOUND-G68 row_hash federation cache key.");

// 4. REGIME-4 storage contract — same as handle-tier sentinel, but
//    surfaced at the bridge boundary too so a contributor reading
//    Bridge.h doesn't have to grep into Handle.h to verify the
//    contract.
static_assert(
    sizeof(::crucible::fixy::bridge::EpochVersioned<std::uint8_t>) >=
        sizeof(std::uint64_t) * 2 + sizeof(std::uint8_t),
    "fixy::bridge::EpochVersioned<T> must carry at least 16 bytes of "
    "grade (Epoch + Generation) — REGIME-4 storage contract; drift "
    "would silently shrink checkpoint headers below the (epoch, gen) "
    "pair required by Cipher cold-tier roll-forward.");

// FIXY-V-216 cardinality witness — 3 EpochVersioned-axis aliases
// surfaced at fixy::bridge:: (Epoch, Generation, EpochVersioned<T>).
constexpr int epoch_versioned_surface_cardinality = 3;
static_assert(epoch_versioned_surface_cardinality == 3,
    "fixy::bridge:: EpochVersioned surface cardinality drifted — "
    "update Bridge.h sentinel block (FIXY-V-216) to track the "
    "substrate EpochVersioned axis surface.");

}  // namespace crucible::fixy::bridge::self_test
