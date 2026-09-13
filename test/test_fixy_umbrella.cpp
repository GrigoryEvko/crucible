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

namespace fixy = crucible::fixy;

static_assert(
    std::is_same_v<
        decltype(&fixy::cap::mint_cap<::crucible::effects::Effect::Alloc, ::crucible::effects::ctx_cap::Bg>),
        decltype(&::crucible::effects::mint_cap<::crucible::effects::Effect::Alloc, ::crucible::effects::ctx_cap::Bg>)>,
    "fixy::cap::mint_cap must be reachable via the umbrella.");

// The value wrappers and their mint factories are deliberately exported
// on two paths: the by-feature carve-outs and the one-stop
// value-wrapping directory.  Both name one substrate symbol, which makes
// the second export redundant rather than ambiguous.  A patch that points
// one path at a different symbol breaks the build here, instead of
// handing callers two types that both spell Linear<int>.

static_assert(std::is_same_v<fixy::safety::Linear<int>, fixy::wrap::Linear<int>>,
              "fixy::safety::Linear and fixy::wrap::Linear must "
              "resolve to the same substrate symbol.");

static_assert(std::is_same_v<fixy::safety::Secret<int>, fixy::wrap::Secret<int>>,
              "fixy::safety::Secret and fixy::wrap::Secret must "
              "resolve to the same substrate symbol.");

static_assert(
    std::is_same_v<decltype(&fixy::safety::mint_linear<int, int>), decltype(&fixy::wrap::mint_linear<int, int>)>,
    "fixy::safety::mint_linear and fixy::wrap::mint_linear "
    "must resolve to the same substrate symbol.");

static_assert(
    std::is_same_v<decltype(&fixy::safety::mint_secret<int, int>), decltype(&fixy::wrap::mint_secret<int, int>)>,
    "fixy::safety::mint_secret and fixy::wrap::mint_secret "
    "must resolve to the same substrate symbol.");

// The probe tag lives in an anonymous namespace so it cannot collide
// with another translation unit's tag tree.  Nothing here mints from it,
// so neither the permission nor its pool is instantiated.
namespace {
struct A4_011_TestTag {};
}  // namespace

static_assert(
    std::is_same_v<fixy::perm::SharedPermission<A4_011_TestTag>, fixy::wrap::SharedPermission<A4_011_TestTag>>,
    "fixy::perm::SharedPermission and fixy::wrap::SharedPermission "
    "must resolve to the same substrate symbol.");

// The discard has two overloads, one per ownership wrapper, so each name
// resolves to an overload set.  The cast picks one arm, which is what
// lets the identity check name a single symbol.
static_assert(std::is_same_v<
                  decltype(static_cast<void (*)(::crucible::safety::Linear<int>&&) noexcept>(&fixy::safety::drop<int>)),
                  decltype(static_cast<void (*)(::crucible::safety::Linear<int>&&) noexcept>(&fixy::wrap::drop<int>))>,
              "fixy::safety::drop<Linear<int>> and fixy::wrap::drop must "
              "resolve to the same substrate function template (Linear arm).");

static_assert(std::is_same_v<
                  decltype(static_cast<void (*)(::crucible::safety::Affine<int>&&) noexcept>(&fixy::safety::drop<int>)),
                  decltype(static_cast<void (*)(::crucible::safety::Affine<int>&&) noexcept>(&fixy::wrap::drop<int>))>,
              "fixy::safety::drop<Affine<int>> and fixy::wrap::drop must "
              "resolve to the same substrate function template (Affine arm).");

// The probe tag has no effect-row specialization, so it takes the empty
// row.  That admits the overload without a context parameter, since the
// context-bound one demands a row-bearing tag.
static_assert(std::is_same_v<decltype(static_cast<::crucible::safety::SharedPermission<A4_011_TestTag> (*)(
                                          ::crucible::safety::Permission<A4_011_TestTag>&&) noexcept>(
                                 &fixy::perm::mint_permission_share<A4_011_TestTag>)),
                             decltype(static_cast<::crucible::safety::SharedPermission<A4_011_TestTag> (*)(
                                          ::crucible::safety::Permission<A4_011_TestTag>&&) noexcept>(
                                 &fixy::wrap::mint_permission_share<A4_011_TestTag>))>,
              "fixy::perm::mint_permission_share and "
              "fixy::wrap::mint_permission_share must resolve to the same substrate "
              "function template.");

// Each assertion below pins one re-exported path to its substrate
// symbol, so a rename on either side reddens here rather than at every
// call site downstream.
//
// The tag namespace exports namespace aliases rather than concrete
// types, so one type per axis is enough to show the alias landed in the
// right substrate scope.

static_assert(std::is_same_v<fixy::tags::source::FromUser, ::crucible::safety::source::FromUser>,
              "fixy::tags::source::FromUser must alias the substrate "
              "safety::source::FromUser via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::tags::trust::Verified, ::crucible::safety::trust::Verified>,
              "fixy::tags::trust::Verified must alias the substrate "
              "safety::trust::Verified via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::tags::access::RW, ::crucible::safety::access::RW>,
              "fixy::tags::access::RW must alias the substrate "
              "safety::access::RW via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::tags::version::V<3>, ::crucible::safety::version::V<3>>,
              "fixy::tags::version::V<N> must alias the substrate "
              "safety::version::V<N> via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::tags::vessel_trust::Validated, ::crucible::safety::vessel_trust::Validated>,
              "fixy::tags::vessel_trust::Validated must alias the "
              "substrate safety::vessel_trust::Validated via the Fixy.h umbrella.");

static_assert(
    std::is_same_v<fixy::tags::secret_policy::AuditedLogging, ::crucible::safety::secret_policy::AuditedLogging>,
    "fixy::tags::secret_policy::AuditedLogging must alias "
    "the substrate safety::secret_policy::AuditedLogging via the Fixy.h "
    "umbrella.");

static_assert(std::is_same_v<fixy::tags::hash_family::FamilyA, ::crucible::hash_family::FamilyA>,
              "fixy::tags::hash_family::FamilyA must alias the "
              "substrate hash_family::FamilyA via the Fixy.h umbrella.");

// The probe organization tag is scoped to an anonymous namespace so it
// cannot collide with another translation unit's tag tree.

namespace {
struct M27_FederationProbeOrg {};
}  // namespace

static_assert(std::is_same_v<fixy::source::federation::FederatedPeer<M27_FederationProbeOrg>,
                             ::crucible::permissions::tag::FederatedPeer<M27_FederationProbeOrg>>,
              "fixy::source::federation::FederatedPeer<Org> must alias "
              "permissions::tag::FederatedPeer<Org> via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::source::federation::LocalCipherTag, ::crucible::permissions::tag::LocalCipherTag>,
              "fixy::source::federation::LocalCipherTag must alias "
              "permissions::tag::LocalCipherTag via the Fixy.h umbrella.");

static_assert(
    std::is_same_v<fixy::source::federation::FederationHandshake, ::crucible::permissions::FederationHandshake>,
    "fixy::source::federation::FederationHandshake must "
    "alias permissions::FederationHandshake via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::source::federation::AdmittanceError, ::crucible::permissions::AdmittanceError>,
              "fixy::source::federation::AdmittanceError must alias "
              "permissions::AdmittanceError via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::source::federation::OrgId, ::crucible::permissions::OrgId>,
              "fixy::source::federation::OrgId must alias "
              "permissions::OrgId via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::source::federation::PeerKeyFingerprint, ::crucible::permissions::PeerKeyFingerprint>,
              "fixy::source::federation::PeerKeyFingerprint must alias "
              "permissions::PeerKeyFingerprint via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::contract::cipher::HotTierHandle<int>, ::crucible::cipher::HotTierHandle<int>>,
              "fixy::contract::cipher::HotTierHandle<T> must alias "
              "cipher::HotTierHandle<T> via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::contract::cipher::WarmTierHandle<int>, ::crucible::cipher::WarmTierHandle<int>>,
              "fixy::contract::cipher::WarmTierHandle<T> must alias "
              "cipher::WarmTierHandle<T> via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::contract::cipher::ColdTierHandle<int>, ::crucible::cipher::ColdTierHandle<int>>,
              "fixy::contract::cipher::ColdTierHandle<T> must alias "
              "cipher::ColdTierHandle<T> via the Fixy.h umbrella.");

static_assert(std::is_same_v<fixy::contract::cipher::RestoreError, ::crucible::cipher::RestoreError>,
              "fixy::contract::cipher::RestoreError must alias "
              "cipher::RestoreError via the Fixy.h umbrella.");

// The tier template takes the tag first and the payload second.
static_assert(std::is_same_v<fixy::contract::cipher::CipherTier<::crucible::safety::CipherTierTag_v::Hot, int>,
                             ::crucible::safety::CipherTier<::crucible::safety::CipherTierTag_v::Hot, int>>,
              "fixy::contract::cipher::CipherTier<Tag, T> must alias "
              "safety::CipherTier<Tag, T> via the Fixy.h umbrella.");

// These name every sub-namespace without instantiating anything.  A
// header the umbrella failed to pull leaves its namespace undeclared,
// and the directive below then fails to compile.

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
    using namespace fixy::wrap;
    using namespace fixy::stance;
    using namespace fixy::grant;
    using namespace fixy::dim;
    using namespace fixy::algebra::dim;
    (void)0;
}

// One function per namespace, so a missing-namespace diagnostic points
// at the offending axis rather than at one shared function.  Identity is
// pinned by the assertions above.  These only witness reach.

void reach_fixy_tags() {
    using namespace fixy::tags;
    (void)0;
}

void reach_fixy_source_federation() {
    using namespace fixy::source::federation;
    (void)0;
}

void reach_fixy_contract_cipher() {
    using namespace fixy::contract::cipher;
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
