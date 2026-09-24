// One probe row per mint the fixy umbrella re-exports.  The using-decl
// is itself the test: namespace-scope name lookup fails to resolve if a
// substrate mint is deleted, renamed, or moved to another namespace, and
// the translation unit stops compiling.
//
// A using-decl is preferable to a static_assert per row because it needs
// no instantiation and no knowledge of the mint's template parameters,
// which means a row costs one line and never has to be revisited when
// the mint's signature changes.  It cannot, however, detect a name that
// resolves to the wrong substrate symbol, so the identity probes at the
// bottom cover that case for the mints whose parameter shape is known.
//
// Additive drift — a new substrate mint the umbrella forgot to
// re-export — is invisible to name lookup, so the cardinality witness
// below is what forces a reviewer to acknowledge a surface change.

#include <crucible/Fixy.h>

#include <type_traits>

#ifndef CRUCIBLE_FIXY
#error "crucible/Fixy.h umbrella did not define CRUCIBLE_FIXY"
#endif

namespace crucible_fixy = ::crucible::fixy;

// A tag for the mints that take one.  Keeping it private to this
// translation unit stops the probe from colliding with a production tag
// tree.
namespace {
struct MintInventoryTag {};
}  // namespace

namespace {

using crucible_fixy::bridge::mint_crash_watched_endpoint;
using crucible_fixy::bridge::mint_crash_watched_session;
using crucible_fixy::bridge::mint_persisted_session;
using crucible_fixy::bridge::mint_recording_endpoint;
using crucible_fixy::bridge::mint_recording_session;
using crucible_fixy::bridge::mint_vigil_mode_bridge;

using crucible_fixy::cap::mint_cap;
using crucible_fixy::cap::mint_from_ctx;

using crucible_fixy::contract::cipher::mint_demote;
// Deliberately dual-exported.  Two using-decls for one name cannot sit
// in the same scope, so each reachable path gets its own enclosing
// namespace and stays a separate inventory row.
namespace probe_contract_cipher_persisted {
using crucible_fixy::contract::cipher::mint_persisted_session;
}  // namespace probe_contract_cipher_persisted
using crucible_fixy::contract::cipher::mint_promote;
using crucible_fixy::contract::cipher::mint_restore;

using crucible_fixy::mach::mint_machine;

using crucible_fixy::perm::mint_permission_combine;
using crucible_fixy::perm::mint_permission_combine_n;
using crucible_fixy::perm::mint_permission_fork;
using crucible_fixy::perm::mint_permission_inherit;
using crucible_fixy::perm::mint_permission_inherit_t;
using crucible_fixy::perm::mint_permission_root;
using crucible_fixy::perm::mint_permission_share;
using crucible_fixy::perm::mint_permission_split;
using crucible_fixy::perm::mint_permission_split_n;

using crucible_fixy::pipe::mint_endpoint;
using crucible_fixy::pipe::mint_mpmc_stage_from_endpoints;
using crucible_fixy::pipe::mint_pipeline;
using crucible_fixy::pipe::mint_pipeline_dag;
using crucible_fixy::pipe::mint_stage;
using crucible_fixy::pipe::mint_stage_from_endpoints;
using crucible_fixy::pipe::mint_swmr_stage;

using crucible_fixy::safety::mint_linear_view;
using crucible_fixy::safety::mint_view;
namespace probe_safety_dual_exports {
using crucible_fixy::safety::mint_linear;
using crucible_fixy::safety::mint_secret;
}  // namespace probe_safety_dual_exports

using crucible_fixy::sess::mint_channel;
namespace probe_sess_dual_exports {
using crucible_fixy::sess::mint_crash_watched_session;
using crucible_fixy::sess::mint_recording_session;
}  // namespace probe_sess_dual_exports
using crucible_fixy::sess::mint_permissioned_session;
using crucible_fixy::sess::mint_session;
using crucible_fixy::sess::mint_session_handle;
using crucible_fixy::sess::mint_session_view;
namespace probe_sess_substrate_session {
using crucible_fixy::sess::mint_substrate_session;
}  // namespace probe_sess_substrate_session

using crucible_fixy::source::federation::mint_federation_admittance;

namespace probe_substr_root_substrate_session {
using crucible_fixy::substr::mint_substrate_session;
}  // namespace probe_substr_root_substrate_session

using crucible_fixy::substr::calendar_grid::mint_calendar_grid_consumer;
using crucible_fixy::substr::calendar_grid::mint_calendar_grid_producer;
// Every substrate re-exports a consumer_session and a producer_session
// under the same two names, so each substrate's pair needs its own
// enclosing namespace to stay a distinct inventory row.
namespace probe_substr_calendar_grid_sessions {
using crucible_fixy::substr::calendar_grid::mint_consumer_session;
using crucible_fixy::substr::calendar_grid::mint_producer_session;
}  // namespace probe_substr_calendar_grid_sessions

using crucible_fixy::substr::chainedge::mint_chainedge_signaler;
using crucible_fixy::substr::chainedge::mint_chainedge_signaler_session;
using crucible_fixy::substr::chainedge::mint_chainedge_waiter;
using crucible_fixy::substr::chainedge::mint_chainedge_waiter_session;

using crucible_fixy::substr::chaselev::mint_chaselev_owner;
using crucible_fixy::substr::chaselev::mint_chaselev_thief;
using crucible_fixy::substr::chaselev::mint_owner_session;
using crucible_fixy::substr::chaselev::mint_thief_session;

using crucible_fixy::substr::mpmc::mint_mpmc_consumer_endpoint;
using crucible_fixy::substr::mpmc::mint_mpmc_consumer_session;
using crucible_fixy::substr::mpmc::mint_mpmc_producer_endpoint;
using crucible_fixy::substr::mpmc::mint_mpmc_producer_session;

namespace probe_substr_sharded_calendar_grid_sessions {
using crucible_fixy::substr::sharded_calendar_grid::mint_consumer_session;
using crucible_fixy::substr::sharded_calendar_grid::mint_producer_session;
}  // namespace probe_substr_sharded_calendar_grid_sessions
using crucible_fixy::substr::sharded_calendar_grid::mint_sharded_calendar_grid_consumer;
using crucible_fixy::substr::sharded_calendar_grid::mint_sharded_calendar_grid_producer;

namespace probe_substr_sharded_grid_sessions {
using crucible_fixy::substr::sharded_grid::mint_consumer_session;
using crucible_fixy::substr::sharded_grid::mint_producer_session;
}  // namespace probe_substr_sharded_grid_sessions
using crucible_fixy::substr::sharded_grid::mint_sharded_grid_consumer;
using crucible_fixy::substr::sharded_grid::mint_sharded_grid_producer;

namespace probe_substr_spsc_sessions {
using crucible_fixy::substr::spsc::mint_consumer_session;
using crucible_fixy::substr::spsc::mint_producer_session;
}  // namespace probe_substr_spsc_sessions

using crucible_fixy::substr::swmr::mint_reader_runtime_session;
using crucible_fixy::substr::swmr::mint_reader_session;
using crucible_fixy::substr::swmr::mint_swmr_reader;
using crucible_fixy::substr::swmr::mint_swmr_writer;
using crucible_fixy::substr::swmr::mint_writer_runtime_session;
using crucible_fixy::substr::swmr::mint_writer_session;

namespace probe_wrap_dual_exports {
using crucible_fixy::wrap::mint_linear;
using crucible_fixy::wrap::mint_permission_share;
using crucible_fixy::wrap::mint_secret;
}  // namespace probe_wrap_dual_exports

}  // namespace

// The witness below is the cheapest signal that anyone updated the
// inventory, short of a parser-driven gold file.  A reviewer who changes
// the surface has to touch a per-namespace constant and the total in the
// same commit, and the sum check refuses anything else.

namespace fixy_mint_inventory_witness {

inline constexpr int kExpectedReachableMints = 78;
inline constexpr int kFixyNamespaceCount = 20;
inline constexpr int kInventoryDateYYYYMMDD = 20260519;

// The window is two either side of the current total.  A single
// addition or removal moves the count by one and passes, which keeps
// routine changes out of review thrash, while a sweep that adds or
// deletes three at once trips the fence.
static_assert(kExpectedReachableMints >= 76, "suspicious drop in the fixy mint inventory.  Audit the using-decl "
                                             "rows against substrate-side mint deletions before lowering this "
                                             "bound.");
static_assert(kExpectedReachableMints <= 80, "the fixy mint inventory grew without the witness being bumped.  "
                                             "Add the new using-decl rows and update kExpectedReachableMints "
                                             "together with the per-namespace constants.");

inline constexpr int kBridgeMints = 6;
inline constexpr int kCapMints = 2;
inline constexpr int kContractCipherMints = 4;
inline constexpr int kMachMints = 1;
inline constexpr int kPermMints = 9;
inline constexpr int kPipeMints = 8;
inline constexpr int kSafetyMints = 4;
inline constexpr int kSessMints = 7;
inline constexpr int kSourceFederationMints = 1;
inline constexpr int kSubstrRootMints = 1;
inline constexpr int kSubstrCalendarGridMints = 4;
inline constexpr int kSubstrChainEdgeMints = 4;
inline constexpr int kSubstrChaselevMints = 4;
inline constexpr int kSubstrMpmcMints = 4;
inline constexpr int kSubstrShardedCalendarGridMints = 4;
inline constexpr int kSubstrShardedGridMints = 4;
inline constexpr int kSubstrSpscMints = 2;
inline constexpr int kSubstrSwmrMints = 6;
inline constexpr int kWrapMints = 3;

// Changing one constant without the other is the commonest review
// error, so the two are pinned against each other.
static_assert(kBridgeMints + kCapMints + kContractCipherMints + kMachMints + kPermMints + kPipeMints + kSafetyMints
                      + kSessMints + kSourceFederationMints + kSubstrRootMints + kSubstrCalendarGridMints
                      + kSubstrChainEdgeMints + kSubstrChaselevMints + kSubstrMpmcMints
                      + kSubstrShardedCalendarGridMints + kSubstrShardedGridMints + kSubstrSpscMints + kSubstrSwmrMints
                      + kWrapMints
                  == kExpectedReachableMints,
              "the per-namespace counts must sum to kExpectedReachableMints.  "
              "Update the per-namespace constant and the total together when "
              "adding or removing rows.");

// A namespace that falls to zero mints has shed its re-export surface
// entirely, which the total alone cannot see: another namespace growing
// by the same amount keeps the sum intact.
static_assert(kBridgeMints >= 1, "crucible_fixy::bridge must surface ≥1 mint.");
static_assert(kCapMints >= 1, "crucible_fixy::cap must surface ≥1 mint.");
static_assert(kContractCipherMints >= 1, "crucible_fixy::contract::cipher must surface ≥1 mint.");
static_assert(kMachMints >= 1, "crucible_fixy::mach must surface ≥1 mint.");
static_assert(kPermMints >= 1, "crucible_fixy::perm must surface ≥1 mint.");
static_assert(kPipeMints >= 1, "crucible_fixy::pipe must surface ≥1 mint.");
static_assert(kSafetyMints >= 1, "crucible_fixy::safety must surface ≥1 mint.");
static_assert(kSessMints >= 1, "crucible_fixy::sess must surface ≥1 mint.");
static_assert(kSourceFederationMints >= 1, "crucible_fixy::source::federation must surface ≥1 mint.");
static_assert(kSubstrRootMints >= 1, "crucible_fixy::substr (root) must surface ≥1 mint.");
static_assert(kSubstrCalendarGridMints >= 1, "crucible_fixy::substr::calendar_grid must surface ≥1 mint.");
static_assert(kSubstrChainEdgeMints >= 1, "crucible_fixy::substr::chainedge must surface ≥1 mint.");
static_assert(kSubstrChaselevMints >= 1, "crucible_fixy::substr::chaselev must surface ≥1 mint.");
static_assert(kSubstrMpmcMints >= 1, "crucible_fixy::substr::mpmc must surface ≥1 mint.");
static_assert(kSubstrShardedCalendarGridMints >= 1, "crucible_fixy::substr::sharded_calendar_grid must surface ≥1 mint.");
static_assert(kSubstrShardedGridMints >= 1, "crucible_fixy::substr::sharded_grid must surface ≥1 mint.");
static_assert(kSubstrSpscMints >= 1, "crucible_fixy::substr::spsc must surface ≥1 mint.");
static_assert(kSubstrSwmrMints >= 1, "crucible_fixy::substr::swmr must surface ≥1 mint.");
static_assert(kWrapMints >= 1, "crucible_fixy::wrap must surface ≥1 mint.");

}  // namespace fixy_mint_inventory_witness

// A using-decl resolves a name but says nothing about which symbol the
// name reached, so a re-export that quietly re-targets a different
// substrate symbol passes every probe above.  These checks close that
// gap for the mints whose template parameters can be supplied without
// deduction.
//
// Covering all of them would mean writing a parameter pack for every
// row, and the mints below are the ones whose drift would reach
// furthest: the capability mint every context-bound mint builds on, the
// permission root every ownership chain starts from, and the two
// wrappers that are reachable by two paths at once.

static_assert(
    std::is_same_v<
        decltype(&crucible_fixy::cap::mint_cap<::crucible::effects::Effect::Alloc, ::crucible::effects::ctx_cap::Bg>),
        decltype(&::crucible::effects::mint_cap<::crucible::effects::Effect::Alloc, ::crucible::effects::ctx_cap::Bg>)>,
    "crucible_fixy::cap::mint_cap must alias ::crucible::effects::mint_cap.");

static_assert(std::is_same_v<decltype(&crucible_fixy::perm::mint_permission_root<MintInventoryTag>),
                             decltype(&::crucible::safety::mint_permission_root<MintInventoryTag>)>,
              "crucible_fixy::perm::mint_permission_root must alias the substrate symbol.");

static_assert(
    std::is_same_v<decltype(&crucible_fixy::safety::mint_linear<int, int>), decltype(&crucible_fixy::wrap::mint_linear<int, int>)>,
    "crucible_fixy::safety::mint_linear and crucible_fixy::wrap::mint_linear must be the "
    "same symbol.");

static_assert(
    std::is_same_v<decltype(&crucible_fixy::safety::mint_secret<int, int>), decltype(&crucible_fixy::wrap::mint_secret<int, int>)>,
    "crucible_fixy::safety::mint_secret and crucible_fixy::wrap::mint_secret must be the "
    "same symbol.");

// The mint is a template gated on a concept, so identity has to be
// taken on a concrete instantiation rather than on the template name.
static_assert(std::is_same_v<decltype(&crucible_fixy::bridge::mint_vigil_mode_bridge<::crucible::vigil_mode::ModeCell>),
                             decltype(&::crucible::mint_vigil_mode_bridge<::crucible::vigil_mode::ModeCell>)>,
              "crucible_fixy::bridge::mint_vigil_mode_bridge must alias the top-level "
              "::crucible::mint_vigil_mode_bridge.");

// The using-decls and the static_asserts are the whole test, and both
// run at compile time.  main exists only so that a successful build
// reports a passing run.
int main() { return 0; }
