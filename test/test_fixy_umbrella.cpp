// One translation unit that pulls the whole umbrella under the project
// warning flags.  A header that adds a using-declaration colliding with
// another surfaces here, so any addition to the umbrella has to keep this
// file green.  A caller who includes the umbrella must never have to
// descend into a sub-header, so every namespace is reached from here.

#include <crucible/Fixy.h>

#include <type_traits>

#ifndef CRUCIBLE_FIXY
#error "crucible/Fixy.h umbrella did not define CRUCIBLE_FIXY"
#endif

static_assert(CRUCIBLE_FIXY == 1, "crucible/Fixy.h umbrella must set CRUCIBLE_FIXY=1.");

namespace crucible_fixy = crucible::fixy;

static_assert(
    std::is_same_v<
        decltype(&crucible_fixy::cap::mint_cap<::crucible::effects::Effect::Alloc, ::crucible::effects::ctx_cap::Bg>),
        decltype(&::crucible::effects::mint_cap<::crucible::effects::Effect::Alloc, ::crucible::effects::ctx_cap::Bg>)>,
    "crucible_fixy::cap::mint_cap must be reachable via the umbrella.");

// The value wrappers and their mint factories are deliberately exported
// on two paths: the by-feature carve-outs and the one-stop
// value-wrapping directory.  Both name one substrate symbol, which makes
// the second export redundant rather than ambiguous.  A patch that points
// one path at a different symbol breaks the build here, instead of
// handing callers two types that both spell Linear<int>.

static_assert(std::is_same_v<crucible_fixy::safety::Linear<int>, crucible_fixy::wrap::Linear<int>>,
              "crucible_fixy::safety::Linear and crucible_fixy::wrap::Linear must "
              "resolve to the same substrate symbol.");

static_assert(std::is_same_v<crucible_fixy::safety::Secret<int>, crucible_fixy::wrap::Secret<int>>,
              "crucible_fixy::safety::Secret and crucible_fixy::wrap::Secret must "
              "resolve to the same substrate symbol.");

static_assert(
    std::is_same_v<decltype(&crucible_fixy::safety::mint_linear<int, int>), decltype(&crucible_fixy::wrap::mint_linear<int, int>)>,
    "crucible_fixy::safety::mint_linear and crucible_fixy::wrap::mint_linear "
    "must resolve to the same substrate symbol.");

static_assert(
    std::is_same_v<decltype(&crucible_fixy::safety::mint_secret<int, int>), decltype(&crucible_fixy::wrap::mint_secret<int, int>)>,
    "crucible_fixy::safety::mint_secret and crucible_fixy::wrap::mint_secret "
    "must resolve to the same substrate symbol.");

// The probe tag lives in an anonymous namespace so it cannot collide
// with another translation unit's tag tree.  Nothing here mints from it,
// so neither the permission nor its pool is instantiated.
namespace {
struct A4_011_TestTag {};
}  // namespace

static_assert(
    std::is_same_v<crucible_fixy::perm::SharedPermission<A4_011_TestTag>, crucible_fixy::wrap::SharedPermission<A4_011_TestTag>>,
    "crucible_fixy::perm::SharedPermission and crucible_fixy::wrap::SharedPermission "
    "must resolve to the same substrate symbol.");

// The discard has two overloads, one per ownership wrapper, so each name
// resolves to an overload set.  The cast picks one arm, which is what
// lets the identity check name a single symbol.
static_assert(std::is_same_v<
                  decltype(static_cast<void (*)(::crucible::safety::Linear<int>&&) noexcept>(&crucible_fixy::safety::drop<int>)),
                  decltype(static_cast<void (*)(::crucible::safety::Linear<int>&&) noexcept>(&crucible_fixy::wrap::drop<int>))>,
              "crucible_fixy::safety::drop<Linear<int>> and crucible_fixy::wrap::drop must "
              "resolve to the same substrate function template (Linear arm).");

static_assert(std::is_same_v<
                  decltype(static_cast<void (*)(::crucible::safety::Affine<int>&&) noexcept>(&crucible_fixy::safety::drop<int>)),
                  decltype(static_cast<void (*)(::crucible::safety::Affine<int>&&) noexcept>(&crucible_fixy::wrap::drop<int>))>,
              "crucible_fixy::safety::drop<Affine<int>> and crucible_fixy::wrap::drop must "
              "resolve to the same substrate function template (Affine arm).");

// The probe tag has no effect-row specialization, so it takes the empty
// row.  That admits the overload without a context parameter, since the
// context-bound one demands a row-bearing tag.
static_assert(std::is_same_v<decltype(static_cast<::crucible::safety::SharedPermission<A4_011_TestTag> (*)(
                                          ::crucible::safety::Permission<A4_011_TestTag>&&) noexcept>(
                                 &crucible_fixy::perm::mint_permission_share<A4_011_TestTag>)),
                             decltype(static_cast<::crucible::safety::SharedPermission<A4_011_TestTag> (*)(
                                          ::crucible::safety::Permission<A4_011_TestTag>&&) noexcept>(
                                 &crucible_fixy::wrap::mint_permission_share<A4_011_TestTag>))>,
              "crucible_fixy::perm::mint_permission_share and "
              "crucible_fixy::wrap::mint_permission_share must resolve to the same substrate "
              "function template.");

// Each assertion below pins one re-exported path to its substrate
// symbol, so a rename on either side reddens here rather than at every
// call site downstream.
//
// The tag namespace exports namespace aliases rather than concrete
// types, so one type per axis is enough to show the alias landed in the
// right substrate scope.

static_assert(std::is_same_v<crucible_fixy::tags::source::FromUser, ::crucible::safety::source::FromUser>,
              "crucible_fixy::tags::source::FromUser must alias the substrate "
              "safety::source::FromUser via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::tags::trust::Verified, ::crucible::safety::trust::Verified>,
              "crucible_fixy::tags::trust::Verified must alias the substrate "
              "safety::trust::Verified via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::tags::access::RW, ::crucible::safety::access::RW>,
              "crucible_fixy::tags::access::RW must alias the substrate "
              "safety::access::RW via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::tags::version::V<3>, ::crucible::safety::version::V<3>>,
              "crucible_fixy::tags::version::V<N> must alias the substrate "
              "safety::version::V<N> via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::tags::vessel_trust::Validated, ::crucible::safety::vessel_trust::Validated>,
              "crucible_fixy::tags::vessel_trust::Validated must alias the "
              "substrate safety::vessel_trust::Validated via the Fixy.h umbrella.");

static_assert(
    std::is_same_v<crucible_fixy::tags::secret_policy::AuditedLogging, ::crucible::safety::secret_policy::AuditedLogging>,
    "crucible_fixy::tags::secret_policy::AuditedLogging must alias "
    "the substrate safety::secret_policy::AuditedLogging via the Fixy.h "
    "umbrella.");

static_assert(std::is_same_v<crucible_fixy::tags::hash_family::FamilyA, ::crucible::hash_family::FamilyA>,
              "crucible_fixy::tags::hash_family::FamilyA must alias the "
              "substrate hash_family::FamilyA via the Fixy.h umbrella.");

// The probe organization tag is scoped to an anonymous namespace so it
// cannot collide with another translation unit's tag tree.

namespace {
struct M27_FederationProbeOrg {};
}  // namespace

static_assert(std::is_same_v<crucible_fixy::source::federation::FederatedPeer<M27_FederationProbeOrg>,
                             ::crucible::permissions::tag::FederatedPeer<M27_FederationProbeOrg>>,
              "crucible_fixy::source::federation::FederatedPeer<Org> must alias "
              "permissions::tag::FederatedPeer<Org> via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::source::federation::LocalCipherTag, ::crucible::permissions::tag::LocalCipherTag>,
              "crucible_fixy::source::federation::LocalCipherTag must alias "
              "permissions::tag::LocalCipherTag via the Fixy.h umbrella.");

static_assert(
    std::is_same_v<crucible_fixy::source::federation::FederationHandshake, ::crucible::permissions::FederationHandshake>,
    "crucible_fixy::source::federation::FederationHandshake must "
    "alias permissions::FederationHandshake via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::source::federation::AdmittanceError, ::crucible::permissions::AdmittanceError>,
              "crucible_fixy::source::federation::AdmittanceError must alias "
              "permissions::AdmittanceError via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::source::federation::OrgId, ::crucible::permissions::OrgId>,
              "crucible_fixy::source::federation::OrgId must alias "
              "permissions::OrgId via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::source::federation::PeerKeyFingerprint, ::crucible::permissions::PeerKeyFingerprint>,
              "crucible_fixy::source::federation::PeerKeyFingerprint must alias "
              "permissions::PeerKeyFingerprint via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::contract::cipher::HotTierHandle<int>, ::crucible::cipher::HotTierHandle<int>>,
              "crucible_fixy::contract::cipher::HotTierHandle<T> must alias "
              "cipher::HotTierHandle<T> via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::contract::cipher::WarmTierHandle<int>, ::crucible::cipher::WarmTierHandle<int>>,
              "crucible_fixy::contract::cipher::WarmTierHandle<T> must alias "
              "cipher::WarmTierHandle<T> via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::contract::cipher::ColdTierHandle<int>, ::crucible::cipher::ColdTierHandle<int>>,
              "crucible_fixy::contract::cipher::ColdTierHandle<T> must alias "
              "cipher::ColdTierHandle<T> via the Fixy.h umbrella.");

static_assert(std::is_same_v<crucible_fixy::contract::cipher::RestoreError, ::crucible::cipher::RestoreError>,
              "crucible_fixy::contract::cipher::RestoreError must alias "
              "cipher::RestoreError via the Fixy.h umbrella.");

// The tier template takes the tag first and the payload second.
static_assert(std::is_same_v<crucible_fixy::contract::cipher::CipherTier<::crucible::safety::CipherTierTag_v::Hot, int>,
                             ::crucible::safety::CipherTier<::crucible::safety::CipherTierTag_v::Hot, int>>,
              "crucible_fixy::contract::cipher::CipherTier<Tag, T> must alias "
              "safety::CipherTier<Tag, T> via the Fixy.h umbrella.");

// These name every sub-namespace without instantiating anything.  A
// header the umbrella failed to pull leaves its namespace undeclared,
// and the directive below then fails to compile.

namespace {

void reach_sub_namespaces() {
    using namespace crucible_fixy::cap;
    using namespace crucible_fixy::perm;
    using namespace crucible_fixy::sess;
    using namespace crucible_fixy::pipe;
    using namespace crucible_fixy::bridge;
    using namespace crucible_fixy::substr::spsc;
    using namespace crucible_fixy::substr::swmr;
    using namespace crucible_fixy::substr::chaselev;
    using namespace crucible_fixy::substr::chainedge;
    using namespace crucible_fixy::substr::mpmc;
    using namespace crucible_fixy::substr::calendar_grid;
    using namespace crucible_fixy::substr::sharded_calendar_grid;
    using namespace crucible_fixy::substr::sharded_grid;
    using namespace crucible_fixy::mach;
    using namespace crucible_fixy::safety;
    using namespace crucible_fixy::wrap;
    using namespace crucible_fixy::stance;
    using namespace crucible_fixy::grant;
    using namespace crucible_fixy::dim;
    using namespace crucible_fixy::algebra::dim;
    (void)0;
}

// One function per namespace, so a missing-namespace diagnostic points
// at the offending axis rather than at one shared function.  Identity is
// pinned by the assertions above.  These only witness reach.

void reach_fixy_tags() {
    using namespace crucible_fixy::tags;
    (void)0;
}

void reach_fixy_source_federation() {
    using namespace crucible_fixy::source::federation;
    (void)0;
}

void reach_fixy_contract_cipher() {
    using namespace crucible_fixy::contract::cipher;
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
