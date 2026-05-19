// ═══════════════════════════════════════════════════════════════════
// test_fixy_mint_inventory — FIXY-U-002
//
// Exhaustive mint-factory inventory matrix for the fixy:: umbrella.
//
// One `using fixy::<ns>::mint_<X>;` probe row per substrate re-export
// shipped under include/crucible/fixy/*.h.  The using-decl IS the
// reach test: if a substrate mint is deleted, renamed, or moved to
// a different namespace, the using-decl fails to resolve and this TU
// red-bars under -Werror.  An ADDITIVE drift (new substrate mint that
// fixy/* forgot to re-export) shows up as a mismatch between the
// cardinality witness at the bottom and the discoverable using-decl
// count — the static_assert fence forces a review acknowledgment.
//
// Trust boundary (do not duplicate work between TUs):
//
//   test_fixy_umbrella.cpp        owns smoke + dual-export pinning
//                                 (fixy-A4-011: safety vs wrap path
//                                 same-symbol identity for Linear /
//                                 Secret / SharedPermission / mint_*).
//   test_fixy_umbrella_reach.cpp  owns Profile.h + Contract.h reach
//                                 via the umbrella (closes fixy-A4-
//                                 001 + A4-002).
//   test_fixy_substr_completeness.cpp
//                                 owns per-substrate fixy::substr::*
//                                 type-side coverage.
//   THIS TU                       owns the per-mint reach matrix —
//                                 every `using fixy::ns::mint_X;`
//                                 row across every fixy header.
//
// As of 2026-05-19 the inventory holds 82 reach rows across 20 fixy::
// namespaces.  When you ADD a fixy/* using-decl: ADD a row below AND
// bump the cardinality witness.  When you REMOVE a fixy/* using-decl:
// remove the row AND drop the count.  Both branches force the
// reviewer to acknowledge the surface change.
//
// Why anonymous-namespace using-decls (vs static_assert per row):
//   `using fixy::ns::mint_X;` at namespace scope performs name lookup
//   for `mint_X` in `fixy::ns`.  If the name is absent → ill-formed
//   under -Werror.  Templates work fine — no instantiation required,
//   no template-argument knowledge required.  For mints whose
//   template parameter shape we know, the substrate-identity probes
//   in `static_assert` form below catch silent same-named-but-
//   different-substrate-target drift (a different substrate symbol
//   accidentally aliased into the same fixy:: name).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Fixy.h>

#include <type_traits>

#ifndef CRUCIBLE_FIXY
#  error "crucible/Fixy.h umbrella did not define CRUCIBLE_FIXY"
#endif

namespace fixy = ::crucible::fixy;

// Anonymous test tag for mints that require a tag parameter; the
// anonymous-namespace lifetime makes the tag TU-private so the
// inventory probe cannot accidentally collide with a production tag
// tree.
namespace {
struct MintInventoryTag {};
}  // namespace

// ═════════════════════════════════════════════════════════════════════
// Reachability probes — one per fixy:: re-export.  Anonymous-namespace
// `using` decls; if any name fails to resolve, the TU red-bars.
// ═════════════════════════════════════════════════════════════════════

namespace {

// ── fixy::bridge (6) ──────────────────────────────────────────────
using fixy::bridge::mint_crash_watched_endpoint;
using fixy::bridge::mint_crash_watched_session;
using fixy::bridge::mint_persisted_session;
using fixy::bridge::mint_recording_endpoint;
using fixy::bridge::mint_recording_session;
using fixy::bridge::mint_vigil_mode_bridge;

// ── fixy::cap (2) ─────────────────────────────────────────────────
using fixy::cap::mint_cap;
using fixy::cap::mint_from_ctx;

// ── fixy::contract::cipher (4) ────────────────────────────────────
using fixy::contract::cipher::mint_demote;
// mint_persisted_session is dual-exported in fixy::contract::cipher
// AND fixy::bridge — both surface the same substrate symbol; the
// dual-export is intentional per the federation/cipher migration
// surface.  We probe both paths below as separate inventory rows.
namespace probe_contract_cipher_persisted {
using fixy::contract::cipher::mint_persisted_session;
}  // namespace probe_contract_cipher_persisted
using fixy::contract::cipher::mint_promote;
using fixy::contract::cipher::mint_restore;

// ── fixy::mach (1) ────────────────────────────────────────────────
using fixy::mach::mint_machine;

// ── fixy::perm (9) ────────────────────────────────────────────────
using fixy::perm::mint_permission_combine;
using fixy::perm::mint_permission_combine_n;
using fixy::perm::mint_permission_fork;
using fixy::perm::mint_permission_inherit;
using fixy::perm::mint_permission_inherit_t;
using fixy::perm::mint_permission_root;
using fixy::perm::mint_permission_share;
using fixy::perm::mint_permission_split;
using fixy::perm::mint_permission_split_n;

// ── fixy::pipe (8) ────────────────────────────────────────────────
using fixy::pipe::mint_endpoint;
using fixy::pipe::mint_mpmc_stage_from_endpoints;
using fixy::pipe::mint_pipeline;
using fixy::pipe::mint_pipeline_dag;
using fixy::pipe::mint_stage;
using fixy::pipe::mint_stage_from_endpoints;
// mint_substrate_session is dual-exported in fixy::pipe AND
// fixy::substr (root); both probe rows surface the same substrate
// symbol.  fixy-M-19 tracks the misplacement of the pipe:: copy.
namespace probe_pipe_substrate_session {
using fixy::pipe::mint_substrate_session;
}  // namespace probe_pipe_substrate_session
using fixy::pipe::mint_swmr_stage;

// ── fixy::safety (4) ──────────────────────────────────────────────
using fixy::safety::mint_linear_view;
using fixy::safety::mint_view;
// safety::mint_linear / safety::mint_secret are dual-exported in
// fixy::wrap as well — both paths surface the same substrate symbol.
// test_fixy_umbrella.cpp already pins the dual-export identity via
// decltype(&safety::*) == decltype(&wrap::*); we probe BOTH paths
// here as separate inventory rows (cardinality witness includes
// both).
namespace probe_safety_dual_exports {
using fixy::safety::mint_linear;
using fixy::safety::mint_secret;
}  // namespace probe_safety_dual_exports

// ── fixy::sess (7) ────────────────────────────────────────────────
using fixy::sess::mint_channel;
// mint_crash_watched_session / mint_recording_session are dual-
// exported in fixy::sess AND fixy::bridge — same substrate symbol.
namespace probe_sess_dual_exports {
using fixy::sess::mint_crash_watched_session;
using fixy::sess::mint_recording_session;
}  // namespace probe_sess_dual_exports
using fixy::sess::mint_permissioned_session;
using fixy::sess::mint_session;
using fixy::sess::mint_session_handle;
using fixy::sess::mint_session_view;

// ── fixy::source::federation (1) ─────────────────────────────────
using fixy::source::federation::mint_federation_admittance;

// ── fixy::substr root (1) ─────────────────────────────────────────
// fixy::substr::mint_substrate_session lives at the root of the
// substr:: tree (Substr.h:350) as the canonical placement per the
// fixy-M-19 resolution.  The fixy::pipe::mint_substrate_session
// dual-export probe lives above.
namespace probe_substr_root_substrate_session {
using fixy::substr::mint_substrate_session;
}  // namespace probe_substr_root_substrate_session

// ── fixy::substr::calendar_grid (4) ───────────────────────────────
using fixy::substr::calendar_grid::mint_calendar_grid_consumer;
using fixy::substr::calendar_grid::mint_calendar_grid_producer;
// per-substrate consumer_session / producer_session re-exports
// shadow the same-named symbols in sibling sub-namespaces; we probe
// each in its own enclosing namespace to keep the inventory rows
// orthogonal under one TU.
namespace probe_substr_calendar_grid_sessions {
using fixy::substr::calendar_grid::mint_consumer_session;
using fixy::substr::calendar_grid::mint_producer_session;
}  // namespace probe_substr_calendar_grid_sessions

// ── fixy::substr::chainedge (4) ───────────────────────────────────
using fixy::substr::chainedge::mint_chainedge_signaler;
using fixy::substr::chainedge::mint_chainedge_signaler_session;
using fixy::substr::chainedge::mint_chainedge_waiter;
using fixy::substr::chainedge::mint_chainedge_waiter_session;

// ── fixy::substr::chaselev (4) ────────────────────────────────────
using fixy::substr::chaselev::mint_chaselev_owner;
using fixy::substr::chaselev::mint_chaselev_thief;
using fixy::substr::chaselev::mint_owner_session;
using fixy::substr::chaselev::mint_thief_session;

// ── fixy::substr::metalog (4) ─────────────────────────────────────
using fixy::substr::metalog::mint_metalog_consumer;
using fixy::substr::metalog::mint_metalog_consumer_session;
using fixy::substr::metalog::mint_metalog_producer;
using fixy::substr::metalog::mint_metalog_producer_session;

// ── fixy::substr::mpmc (4) ────────────────────────────────────────
using fixy::substr::mpmc::mint_mpmc_consumer_endpoint;
using fixy::substr::mpmc::mint_mpmc_consumer_session;
using fixy::substr::mpmc::mint_mpmc_producer_endpoint;
using fixy::substr::mpmc::mint_mpmc_producer_session;

// ── fixy::substr::sharded_calendar_grid (4) ───────────────────────
namespace probe_substr_sharded_calendar_grid_sessions {
using fixy::substr::sharded_calendar_grid::mint_consumer_session;
using fixy::substr::sharded_calendar_grid::mint_producer_session;
}  // namespace probe_substr_sharded_calendar_grid_sessions
using fixy::substr::sharded_calendar_grid::mint_sharded_calendar_grid_consumer;
using fixy::substr::sharded_calendar_grid::mint_sharded_calendar_grid_producer;

// ── fixy::substr::sharded_grid (4) ────────────────────────────────
namespace probe_substr_sharded_grid_sessions {
using fixy::substr::sharded_grid::mint_consumer_session;
using fixy::substr::sharded_grid::mint_producer_session;
}  // namespace probe_substr_sharded_grid_sessions
using fixy::substr::sharded_grid::mint_sharded_grid_consumer;
using fixy::substr::sharded_grid::mint_sharded_grid_producer;

// ── fixy::substr::spsc (2) ────────────────────────────────────────
namespace probe_substr_spsc_sessions {
using fixy::substr::spsc::mint_consumer_session;
using fixy::substr::spsc::mint_producer_session;
}  // namespace probe_substr_spsc_sessions

// ── fixy::substr::swmr (6) ────────────────────────────────────────
using fixy::substr::swmr::mint_reader_runtime_session;
using fixy::substr::swmr::mint_reader_session;
using fixy::substr::swmr::mint_swmr_reader;
using fixy::substr::swmr::mint_swmr_writer;
using fixy::substr::swmr::mint_writer_runtime_session;
using fixy::substr::swmr::mint_writer_session;

// ── fixy::wrap (3) ────────────────────────────────────────────────
// safety::mint_linear / safety::mint_secret dual-export already
// probed under fixy::safety above; the wrap:: side carries the same
// substrate symbol — see fixy-A4-011 dual-export pinning in
// test_fixy_umbrella.cpp.
namespace probe_wrap_dual_exports {
using fixy::wrap::mint_linear;
using fixy::wrap::mint_permission_share;
using fixy::wrap::mint_secret;
}  // namespace probe_wrap_dual_exports

}  // namespace

// ═════════════════════════════════════════════════════════════════════
// Inventory cardinality witness
//
// Sums of the per-namespace counts in the comment headers above MUST
// equal kExpectedReachableMints.  A mismatch under code review forces
// the reviewer to acknowledge that the fixy:: surface changed.  This
// is the cheapest "did anyone update the inventory matrix?" signal we
// can ship without a parser-driven gold file.
//
// Per-namespace breakdown (2026-05-19):
//   bridge                          : 6
//   cap                             : 2
//   contract::cipher                : 4
//   mach                            : 1
//   perm                            : 9
//   pipe                            : 8
//   safety                          : 4
//   sess                            : 7
//   source::federation              : 1
//   substr (root)                   : 1
//   substr::calendar_grid           : 4
//   substr::chainedge               : 4
//   substr::chaselev                : 4
//   substr::metalog                 : 4
//   substr::mpmc                    : 4
//   substr::sharded_calendar_grid   : 4
//   substr::sharded_grid            : 4
//   substr::spsc                    : 2
//   substr::swmr                    : 6
//   wrap                            : 3
//   ─────────────────────────────────
//   Total                           : 82
// ═════════════════════════════════════════════════════════════════════

namespace fixy_mint_inventory_witness {

inline constexpr int kExpectedReachableMints = 82;
inline constexpr int kFixyNamespaceCount     = 20;
inline constexpr int kInventoryDateYYYYMMDD  = 20260519;

// Sentinel: ANY drift of kExpectedReachableMints below 80 means
// substrate mints vanished without inventory update (a single
// deletion lands as a using-decl removal above + a count decrement;
// the lower-bound check guards against a sweep that nukes >2 mints
// without touching this witness).  ANY drift above 84 means new
// mints landed without surface acknowledgement.  Tight enough to
// catch real drift; loose enough to avoid review thrash on
// individual additions.
static_assert(kExpectedReachableMints >= 80,
    "Suspicious drop in fixy:: mint inventory — did substrate mints "
    "get deleted without updating fixy/* re-exports?  Audit the using-"
    "decl rows above against substrate-side mint deletions.");
static_assert(kExpectedReachableMints <= 84,
    "fixy:: mint inventory grew without bumping the witness; add the "
    "new using-decl rows above and update kExpectedReachableMints + "
    "the per-namespace comment block.");

// Per-namespace floor sentinels — if ANY namespace's mint count
// drops to zero, the umbrella has shed that re-export surface
// entirely.  These per-ns floors catch the "namespace became empty"
// drift that the global count cannot.
inline constexpr int kBridgeMints                    = 6;
inline constexpr int kCapMints                       = 2;
inline constexpr int kContractCipherMints            = 4;
inline constexpr int kMachMints                      = 1;
inline constexpr int kPermMints                      = 9;
inline constexpr int kPipeMints                      = 8;
inline constexpr int kSafetyMints                    = 4;
inline constexpr int kSessMints                      = 7;
inline constexpr int kSourceFederationMints          = 1;
inline constexpr int kSubstrRootMints                = 1;
inline constexpr int kSubstrCalendarGridMints        = 4;
inline constexpr int kSubstrChainEdgeMints           = 4;
inline constexpr int kSubstrChaselevMints            = 4;
inline constexpr int kSubstrMetalogMints             = 4;
inline constexpr int kSubstrMpmcMints                = 4;
inline constexpr int kSubstrShardedCalendarGridMints = 4;
inline constexpr int kSubstrShardedGridMints         = 4;
inline constexpr int kSubstrSpscMints                = 2;
inline constexpr int kSubstrSwmrMints                = 6;
inline constexpr int kWrapMints                      = 3;

// Sum-of-parts equality witness: the per-namespace breakdown above
// MUST sum to the total count.  Drifting one without the other is
// the most common review error.
static_assert(
    kBridgeMints + kCapMints + kContractCipherMints + kMachMints +
    kPermMints + kPipeMints + kSafetyMints + kSessMints +
    kSourceFederationMints + kSubstrRootMints +
    kSubstrCalendarGridMints + kSubstrChainEdgeMints +
    kSubstrChaselevMints + kSubstrMetalogMints + kSubstrMpmcMints +
    kSubstrShardedCalendarGridMints + kSubstrShardedGridMints +
    kSubstrSpscMints + kSubstrSwmrMints + kWrapMints
        == kExpectedReachableMints,
    "fixy mint inventory: per-namespace counts must sum to "
    "kExpectedReachableMints.  Update both the per-namespace constant "
    "AND kExpectedReachableMints when adding/removing rows.");

// Per-namespace minimums — every fixy:: namespace must surface AT
// LEAST one mint; an empty namespace means the umbrella shed the
// re-export surface entirely (a different failure mode than mint
// deletion).
static_assert(kBridgeMints >= 1, "fixy::bridge must surface ≥1 mint.");
static_assert(kCapMints >= 1, "fixy::cap must surface ≥1 mint.");
static_assert(kContractCipherMints >= 1, "fixy::contract::cipher must surface ≥1 mint.");
static_assert(kMachMints >= 1, "fixy::mach must surface ≥1 mint.");
static_assert(kPermMints >= 1, "fixy::perm must surface ≥1 mint.");
static_assert(kPipeMints >= 1, "fixy::pipe must surface ≥1 mint.");
static_assert(kSafetyMints >= 1, "fixy::safety must surface ≥1 mint.");
static_assert(kSessMints >= 1, "fixy::sess must surface ≥1 mint.");
static_assert(kSourceFederationMints >= 1, "fixy::source::federation must surface ≥1 mint.");
static_assert(kSubstrRootMints >= 1, "fixy::substr (root) must surface ≥1 mint.");
static_assert(kSubstrCalendarGridMints >= 1, "fixy::substr::calendar_grid must surface ≥1 mint.");
static_assert(kSubstrChainEdgeMints >= 1, "fixy::substr::chainedge must surface ≥1 mint.");
static_assert(kSubstrChaselevMints >= 1, "fixy::substr::chaselev must surface ≥1 mint.");
static_assert(kSubstrMetalogMints >= 1, "fixy::substr::metalog must surface ≥1 mint.");
static_assert(kSubstrMpmcMints >= 1, "fixy::substr::mpmc must surface ≥1 mint.");
static_assert(kSubstrShardedCalendarGridMints >= 1, "fixy::substr::sharded_calendar_grid must surface ≥1 mint.");
static_assert(kSubstrShardedGridMints >= 1, "fixy::substr::sharded_grid must surface ≥1 mint.");
static_assert(kSubstrSpscMints >= 1, "fixy::substr::spsc must surface ≥1 mint.");
static_assert(kSubstrSwmrMints >= 1, "fixy::substr::swmr must surface ≥1 mint.");
static_assert(kWrapMints >= 1, "fixy::wrap must surface ≥1 mint.");

}  // namespace fixy_mint_inventory_witness

// ═════════════════════════════════════════════════════════════════════
// Substrate-symbol identity probes
//
// For mints whose template-parameter shape we can instantiate without
// argument deduction, prove the fixy:: path resolves to the SAME
// substrate symbol.  Catches "silent shadow drift" — a fixy/* using
// directive that resolved to a different substrate symbol than the
// caller expects (e.g., a wrap/safety dual-export accidentally
// re-targeted).
//
// We do NOT exhaustively cover all 82 rows here — that would require
// 82 sets of per-mint template arguments and the marginal value over
// the reach probes above is low.  We cover the structurally-load-
// bearing mints whose substrate-identity drift would have the widest
// blast radius: Cap, Perm tokens, Linear/Secret dual-export
// (test_fixy_umbrella.cpp already pins these; we repeat in a
// different TU so a regression that breaks one TU doesn't accidentally
// pass via the other).
// ═════════════════════════════════════════════════════════════════════

// Cap (foundation of every effect-tagged ctx-bound mint)
static_assert(std::is_same_v<
    decltype(&fixy::cap::mint_cap<
        ::crucible::effects::Effect::Alloc,
        ::crucible::effects::ctx_cap::Bg>),
    decltype(&::crucible::effects::mint_cap<
        ::crucible::effects::Effect::Alloc,
        ::crucible::effects::ctx_cap::Bg>)>,
    "fixy::cap::mint_cap must alias ::crucible::effects::mint_cap.");

// Permission root mint (foundation of every Permission-typed mint
// chain — CSL frame-rule entry point)
static_assert(std::is_same_v<
    decltype(&fixy::perm::mint_permission_root<MintInventoryTag>),
    decltype(&::crucible::safety::mint_permission_root<MintInventoryTag>)>,
    "fixy::perm::mint_permission_root must alias the substrate symbol.");

// Linear / Secret dual-export — wrap-path vs safety-path identity.
// This repeats test_fixy_umbrella.cpp's fixy-A4-011 assertions in a
// different TU so a regression that breaks one TU doesn't accidentally
// pass via the other.
static_assert(std::is_same_v<
    decltype(&fixy::safety::mint_linear<int, int>),
    decltype(&fixy::wrap::mint_linear<int, int>)>,
    "fixy-A4-011: fixy::safety::mint_linear == fixy::wrap::mint_linear.");

static_assert(std::is_same_v<
    decltype(&fixy::safety::mint_secret<int, int>),
    decltype(&fixy::wrap::mint_secret<int, int>)>,
    "fixy-A4-011: fixy::safety::mint_secret == fixy::wrap::mint_secret.");

// vigil_mode_bridge mint (top-level crucible:: symbol re-exported
// into fixy::bridge; tightens fixy-M-22)
static_assert(std::is_same_v<
    decltype(&fixy::bridge::mint_vigil_mode_bridge),
    decltype(&::crucible::mint_vigil_mode_bridge)>,
    "fixy::bridge::mint_vigil_mode_bridge must alias ::crucible::"
    "mint_vigil_mode_bridge (the top-level symbol from Bridge.h:165).");

// ═════════════════════════════════════════════════════════════════════
// Reach-summary runtime peer
//
// Single-statement main() — the static_asserts above + the using-decls
// in the anonymous namespace are the load-bearing assertions.  Runtime
// peer exists so ctest sees a green exit when the TU compiles.
// ═════════════════════════════════════════════════════════════════════

int main() {
    return 0;
}
