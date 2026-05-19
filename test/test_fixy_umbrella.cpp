// ── test_fixy_umbrella — sentinel TU for crucible/Fixy.h ───────────
//
// Pulls the entire fixy:: surface via the umbrella header into one
// TU compiled under project warning flags.  This proves:
//
//   1. Every fixy header compiles cleanly together (no ODR clash,
//      no using-declaration ambiguity, no header-order dependency).
//   2. CRUCIBLE_FIXY=1 is defined after the include.
//   3. The umbrella exposes every minter family — Cap, Perm, Sess,
//      Pipe, Bridge, Substr (per-substrate variants), Mach, Safety,
//      plus Fn — through one well-known include path.
//   4. fixy-M-27: every non-Phase-A/B/C namespace reachable through
//      the umbrella — fixy::wrap (Wrap.h), fixy::tags (Source.h),
//      fixy::source::federation (Source.h), fixy::contract::cipher
//      (Contract.h).  Each has a dedicated test/test_fixy_*.cpp TU,
//      but the UMBRELLA path must also reach them so a caller who
//      does `#include <crucible/Fixy.h>` never has to descend into a
//      sub-header.  Identity sentinels below pin the substrate path
//      so a rename on either side reddens HERE.
//
// Failure mode: if a future fixy/*.h adds a `using` that collides
// with another fixy/*.h, this TU surfaces the collision under
// -Werror.  Any future addition to fixy/ MUST keep this TU green.

#include <crucible/Fixy.h>

#include <type_traits>

#ifndef CRUCIBLE_FIXY
#  error "crucible/Fixy.h umbrella did not define CRUCIBLE_FIXY"
#endif

static_assert(CRUCIBLE_FIXY == 1,
    "crucible/Fixy.h umbrella must set CRUCIBLE_FIXY=1.");

// Minter-family reachability via the umbrella.  Each line below
// asserts that the named function template is reachable through
// `crucible::fixy::<sub>::*` after a single `#include <crucible/Fixy.h>`.

namespace fixy = crucible::fixy;

// Cap
static_assert(std::is_same_v<
    decltype(&fixy::cap::mint_cap<
                 ::crucible::effects::Effect::Alloc,
                 ::crucible::effects::ctx_cap::Bg>),
    decltype(&::crucible::effects::mint_cap<
                 ::crucible::effects::Effect::Alloc,
                 ::crucible::effects::ctx_cap::Bg>)>,
    "fixy::cap::mint_cap must be reachable via the umbrella.");

// ── fixy-A4-011 / fixy-M-28 dual-path drift detector ──────────────
//
// Linear / Secret / SharedPermission (and their mint factories) are
// re-exported on TWO namespace paths intentionally — by-feature
// carve-outs (fixy::safety::, fixy::perm::) AND the one-stop
// value-wrapping directory (fixy::wrap::).  Today both paths name
// the SAME substrate symbol via using-declarations, so the dual
// re-export is structurally redundant rather than ambiguous.  These
// static_asserts pin that fact at compile time: if a future patch
// changes one path's `using` to point at a divergent symbol, the
// build breaks here rather than silently giving callers two
// different "Linear<int>" types.  See Safety.h header docs +
// Wrap.h "Dual-export discipline" block for the policy.

static_assert(std::is_same_v<
    fixy::safety::Linear<int>,
    fixy::wrap::Linear<int>>,
    "fixy-A4-011: fixy::safety::Linear and fixy::wrap::Linear must "
    "resolve to the same substrate symbol.");

static_assert(std::is_same_v<
    fixy::safety::Secret<int>,
    fixy::wrap::Secret<int>>,
    "fixy-A4-011: fixy::safety::Secret and fixy::wrap::Secret must "
    "resolve to the same substrate symbol.");

static_assert(std::is_same_v<
    decltype(&fixy::safety::mint_linear<int, int>),
    decltype(&fixy::wrap::mint_linear<int, int>)>,
    "fixy-A4-011: fixy::safety::mint_linear and fixy::wrap::mint_linear "
    "must resolve to the same substrate symbol.");

static_assert(std::is_same_v<
    decltype(&fixy::safety::mint_secret<int, int>),
    decltype(&fixy::wrap::mint_secret<int, int>)>,
    "fixy-A4-011: fixy::safety::mint_secret and fixy::wrap::mint_secret "
    "must resolve to the same substrate symbol.");

// Tag for the SharedPermission identity assertion below.  Lives in
// an anonymous namespace so it can't collide with any other TU's
// tag tree; the per-tag root mint is gated by SharedPermissionPool
// at runtime so the test compiles without instantiating either.
namespace {
struct A4_011_TestTag {};
}  // namespace

static_assert(std::is_same_v<
    fixy::perm::SharedPermission<A4_011_TestTag>,
    fixy::wrap::SharedPermission<A4_011_TestTag>>,
    "fixy-A4-011: fixy::perm::SharedPermission and fixy::wrap::SharedPermission "
    "must resolve to the same substrate symbol.");

// drop(Linear<T>&&) — explicit discard. Dual-exported in safety + wrap.
static_assert(std::is_same_v<
    decltype(&fixy::safety::drop<int>),
    decltype(&fixy::wrap::drop<int>)>,
    "fixy-A4-011: fixy::safety::drop and fixy::wrap::drop must resolve to "
    "the same substrate function template.");

// mint_permission_share — non-ctx overload. Dual-exported in perm + wrap.
// A4_011_TestTag has no permission_row<> specialization so it defaults to
// Row<>, admitting the non-ctx overload (the ctx-bound overload requires
// a row-bearing tag).
static_assert(std::is_same_v<
    decltype(static_cast<
        ::crucible::safety::SharedPermission<A4_011_TestTag>(*)(
            ::crucible::safety::Permission<A4_011_TestTag>&&) noexcept>(
        &fixy::perm::mint_permission_share<A4_011_TestTag>)),
    decltype(static_cast<
        ::crucible::safety::SharedPermission<A4_011_TestTag>(*)(
            ::crucible::safety::Permission<A4_011_TestTag>&&) noexcept>(
        &fixy::wrap::mint_permission_share<A4_011_TestTag>))>,
    "fixy-A4-011: fixy::perm::mint_permission_share and "
    "fixy::wrap::mint_permission_share must resolve to the same substrate "
    "function template.");

// ═════════════════════════════════════════════════════════════════════
// ── fixy-M-27 identity sentinels (umbrella reach for tags / source ──
//                                  ::federation / contract::cipher) ──
// ═════════════════════════════════════════════════════════════════════
//
// Pin substrate identity for each of the four namespaces the M-27
// premise calls out.  `fixy::wrap` already had drift detectors in the
// fixy-A4-011 block above (Linear / Secret / SharedPermission +
// mint_linear / mint_secret / drop / mint_permission_share).  The
// three remaining namespaces are pinned here.  Each sentinel asserts
// that the fixy:: path resolves to the canonical substrate symbol;
// a future rename on either side reddens HERE rather than at every
// downstream call-site.

// ── fixy::tags (Source.h) — one witness per re-exported axis ──────
//
// `fixy::tags` exports namespace aliases (source / trust / access /
// version / vessel_trust / secret_policy / hash_family) rather than
// concrete types.  One canonical type witness per axis confirms the
// alias resolved to the right substrate scope.

static_assert(std::is_same_v<
    fixy::tags::source::FromUser,
    ::crucible::safety::source::FromUser>,
    "fixy-M-27: fixy::tags::source::FromUser must alias the substrate "
    "safety::source::FromUser via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::tags::trust::Verified,
    ::crucible::safety::trust::Verified>,
    "fixy-M-27: fixy::tags::trust::Verified must alias the substrate "
    "safety::trust::Verified via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::tags::access::RW,
    ::crucible::safety::access::RW>,
    "fixy-M-27: fixy::tags::access::RW must alias the substrate "
    "safety::access::RW via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::tags::version::V<3>,
    ::crucible::safety::version::V<3>>,
    "fixy-M-27: fixy::tags::version::V<N> must alias the substrate "
    "safety::version::V<N> via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::tags::vessel_trust::Validated,
    ::crucible::safety::vessel_trust::Validated>,
    "fixy-M-27: fixy::tags::vessel_trust::Validated must alias the "
    "substrate safety::vessel_trust::Validated via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::tags::secret_policy::AuditedLogging,
    ::crucible::safety::secret_policy::AuditedLogging>,
    "fixy-M-27: fixy::tags::secret_policy::AuditedLogging must alias "
    "the substrate safety::secret_policy::AuditedLogging via the Fixy.h "
    "umbrella.");

static_assert(std::is_same_v<
    fixy::tags::hash_family::FamilyA,
    ::crucible::hash_family::FamilyA>,
    "fixy-M-27: fixy::tags::hash_family::FamilyA must alias the "
    "substrate hash_family::FamilyA via the Fixy.h umbrella.");

// ── fixy::source::federation (Source.h) — federation surface ───────
//
// FederationPermission.h ships the Cipher federation admission
// boundary.  Pin the load-bearing handshake / tag / mint identities
// so a substrate rename in permissions/FederationPermission.h reddens
// in the umbrella TU.  Probe org tag scoped to anon namespace so it
// can't collide with any other TU's tag tree.

namespace {
struct M27_FederationProbeOrg {};
}  // namespace

static_assert(std::is_same_v<
    fixy::source::federation::FederatedPeer<M27_FederationProbeOrg>,
    ::crucible::permissions::tag::FederatedPeer<M27_FederationProbeOrg>>,
    "fixy-M-27: fixy::source::federation::FederatedPeer<Org> must alias "
    "permissions::tag::FederatedPeer<Org> via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::source::federation::LocalCipherTag,
    ::crucible::permissions::tag::LocalCipherTag>,
    "fixy-M-27: fixy::source::federation::LocalCipherTag must alias "
    "permissions::tag::LocalCipherTag via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::source::federation::FederationHandshake,
    ::crucible::permissions::FederationHandshake>,
    "fixy-M-27: fixy::source::federation::FederationHandshake must "
    "alias permissions::FederationHandshake via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::source::federation::AdmittanceError,
    ::crucible::permissions::AdmittanceError>,
    "fixy-M-27: fixy::source::federation::AdmittanceError must alias "
    "permissions::AdmittanceError via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::source::federation::OrgId,
    ::crucible::permissions::OrgId>,
    "fixy-M-27: fixy::source::federation::OrgId must alias "
    "permissions::OrgId via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::source::federation::PeerKeyFingerprint,
    ::crucible::permissions::PeerKeyFingerprint>,
    "fixy-M-27: fixy::source::federation::PeerKeyFingerprint must alias "
    "permissions::PeerKeyFingerprint via the Fixy.h umbrella.");

// ── fixy::contract::cipher (Contract.h) — Cipher migration surface ─
//
// CipherTierPromotion.h ships the Hot / Warm / Cold tier handles and
// promote / demote / restore mints.  Pin the three handle aliases +
// the error surface + a mint signature so a rename in the substrate
// reddens here.

static_assert(std::is_same_v<
    fixy::contract::cipher::HotTierHandle<int>,
    ::crucible::cipher::HotTierHandle<int>>,
    "fixy-M-27: fixy::contract::cipher::HotTierHandle<T> must alias "
    "cipher::HotTierHandle<T> via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::contract::cipher::WarmTierHandle<int>,
    ::crucible::cipher::WarmTierHandle<int>>,
    "fixy-M-27: fixy::contract::cipher::WarmTierHandle<T> must alias "
    "cipher::WarmTierHandle<T> via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::contract::cipher::ColdTierHandle<int>,
    ::crucible::cipher::ColdTierHandle<int>>,
    "fixy-M-27: fixy::contract::cipher::ColdTierHandle<T> must alias "
    "cipher::ColdTierHandle<T> via the Fixy.h umbrella.");

static_assert(std::is_same_v<
    fixy::contract::cipher::RestoreError,
    ::crucible::cipher::RestoreError>,
    "fixy-M-27: fixy::contract::cipher::RestoreError must alias "
    "cipher::RestoreError via the Fixy.h umbrella.");

// CipherTier template signature is <CipherTierTag_v Tier, typename T>
// (tag first, payload second — see safety/CipherTier.h:163).
static_assert(std::is_same_v<
    fixy::contract::cipher::CipherTier<
        ::crucible::safety::CipherTierTag_v::Hot, int>,
    ::crucible::safety::CipherTier<
        ::crucible::safety::CipherTierTag_v::Hot, int>>,
    "fixy-M-27: fixy::contract::cipher::CipherTier<Tag, T> must alias "
    "safety::CipherTier<Tag, T> via the Fixy.h umbrella.");

// Per-namespace reachability checks (compile-only, no instantiation).
// Each `using namespace` block names a fixy:: sub-namespace.  If the
// umbrella failed to pull a header, the name would not exist and
// these blocks would emit "no namespace named" under -Werror.

namespace {

void reach_sub_namespaces() {
    using namespace fixy::cap;
    using namespace fixy::perm;
    using namespace fixy::sess;
    using namespace fixy::pipe;
    using namespace fixy::bridge;
    using namespace fixy::substr::spsc;
    using namespace fixy::substr::swmr;
    using namespace fixy::substr::chaselev;
    using namespace fixy::substr::metalog;
    using namespace fixy::substr::chainedge;
    using namespace fixy::substr::mpmc;
    using namespace fixy::substr::calendar_grid;
    using namespace fixy::substr::sharded_calendar_grid;
    using namespace fixy::substr::sharded_grid;
    using namespace fixy::mach;
    using namespace fixy::safety;
    using namespace fixy::wrap;    // fixy-A4-011 / fixy-M-27: wrap re-exports
    using namespace fixy::stance;
    using namespace fixy::grant;
    using namespace fixy::dim;
    (void)0;
}

// fixy-M-27 reach witnesses — separate functions per namespace so
// "no namespace named X" diagnostics localize to the offending axis
// rather than a single monolithic reach function.  Each one is the
// minimum-viable witness that `#include <crucible/Fixy.h>` exposes
// the namespace; type-identity is pinned by the static_asserts above.

void reach_fixy_tags() {
    using namespace fixy::tags;                  // fixy-M-27
    (void)0;
}

void reach_fixy_source_federation() {
    using namespace fixy::source::federation;    // fixy-M-27
    (void)0;
}

void reach_fixy_contract_cipher() {
    using namespace fixy::contract::cipher;      // fixy-M-27
    (void)0;
}

}  // namespace

int main() {
    reach_sub_namespaces();
    reach_fixy_tags();
    reach_fixy_source_federation();
    reach_fixy_contract_cipher();
    return 0;
}
