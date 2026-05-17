// ── test_fixy_source — sentinel TU for fixy/Source.h ───────────────
//
// Pulls fixy/Source.h into a TU compiled under project warning flags
// so the header's static_asserts execute under enforcement.  Witnesses:
//
//   1. Every source::* / trust::* / access::* / version::* /
//      vessel_trust::* / secret_policy::* / hash_family::* tag is
//      reachable through fixy::tags::* AND is the SAME TYPE as the
//      substrate tag (template identity preserved).
//   2. Tags can be used as Tagged<T, Tag> template parameters via the
//      alias and produce the same Tagged specialization as direct use.
//   3. Cross-axis mismatches at compile time reject (the substrate's
//      overload-resolution diagnostic carries through).
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/
//       neg_fixy_source_*.cpp.

#include <crucible/fixy/Source.h>
#include <crucible/safety/Tagged.h>

#include <type_traits>

namespace ft     = crucible::fixy::tags;
namespace cs     = crucible::safety::source;
namespace ctr    = crucible::safety::trust;
namespace cac    = crucible::safety::access;
namespace cv     = crucible::safety::version;
namespace cvt    = crucible::safety::vessel_trust;
namespace csp    = crucible::safety::secret_policy;
namespace chf    = crucible::hash_family;

// ─── 1. source::* tag identity (sample broadly) ───────────────────

static_assert(std::is_same_v<ft::source::FromUser,        cs::FromUser>);
static_assert(std::is_same_v<ft::source::FromDb,          cs::FromDb>);
static_assert(std::is_same_v<ft::source::FromConfig,      cs::FromConfig>);
static_assert(std::is_same_v<ft::source::FromInternal,    cs::FromInternal>);
static_assert(std::is_same_v<ft::source::External,        cs::External>);
static_assert(std::is_same_v<ft::source::ABIBoundary,     cs::ABIBoundary>);
static_assert(std::is_same_v<ft::source::Sanitized,       cs::Sanitized>);
static_assert(std::is_same_v<ft::source::FormatVersion,   cs::FormatVersion>);
static_assert(std::is_same_v<ft::source::Loaded,          cs::Loaded>);
static_assert(std::is_same_v<ft::source::Interned,        cs::Interned>);
static_assert(std::is_same_v<ft::source::Arena,           cs::Arena>);
static_assert(std::is_same_v<ft::source::Singleton,       cs::Singleton>);
static_assert(std::is_same_v<ft::source::Recorded,        cs::Recorded>);
static_assert(std::is_same_v<ft::source::Replayed,        cs::Replayed>);
static_assert(std::is_same_v<ft::source::Durable,         cs::Durable>);
static_assert(std::is_same_v<ft::source::Computed,        cs::Computed>);
static_assert(std::is_same_v<ft::source::Vendor,          cs::Vendor>);
static_assert(std::is_same_v<ft::source::Calibrated,      cs::Calibrated>);
static_assert(std::is_same_v<ft::source::Hlc,             cs::Hlc>);
static_assert(std::is_same_v<ft::source::Local,           cs::Local>);
static_assert(std::is_same_v<ft::source::Gossiped,        cs::Gossiped>);
static_assert(std::is_same_v<ft::source::IntegrityVerified, cs::IntegrityVerified>);
static_assert(std::is_same_v<ft::source::JsonRegistry,    cs::JsonRegistry>);
static_assert(std::is_same_v<ft::source::Ir001,           cs::Ir001>);
static_assert(std::is_same_v<ft::source::TcpInfo,         cs::TcpInfo>);

// ─── 2. trust::* tag identity ─────────────────────────────────────

static_assert(std::is_same_v<ft::trust::Verified,   ctr::Verified>);
static_assert(std::is_same_v<ft::trust::Tested,     ctr::Tested>);
static_assert(std::is_same_v<ft::trust::Unverified, ctr::Unverified>);
static_assert(std::is_same_v<ft::trust::Assumed,    ctr::Assumed>);
static_assert(std::is_same_v<ft::trust::External,   ctr::External>);

// ─── 3. access::* tag identity ────────────────────────────────────

static_assert(std::is_same_v<ft::access::RW,            cac::RW>);
static_assert(std::is_same_v<ft::access::RO,            cac::RO>);
static_assert(std::is_same_v<ft::access::WO,            cac::WO>);
static_assert(std::is_same_v<ft::access::W1C,           cac::W1C>);
static_assert(std::is_same_v<ft::access::W1S,           cac::W1S>);
static_assert(std::is_same_v<ft::access::WriteOnce,     cac::WriteOnce>);
static_assert(std::is_same_v<ft::access::AppendOnly,    cac::AppendOnly>);
static_assert(std::is_same_v<ft::access::Unique,        cac::Unique>);
static_assert(std::is_same_v<ft::access::AutoIncrement, cac::AutoIncrement>);
static_assert(std::is_same_v<ft::access::Deprecated,    cac::Deprecated>);

// ─── 4. version::V<N> identity ────────────────────────────────────

static_assert(std::is_same_v<ft::version::V<1>, cv::V<1>>);
static_assert(std::is_same_v<ft::version::V<3>, cv::V<3>>);
static_assert(std::is_same_v<ft::version::V<99>, cv::V<99>>);

static_assert(ft::version::V<3>::number == 3);

// ─── 5. vessel_trust::* identity ──────────────────────────────────

static_assert(std::is_same_v<ft::vessel_trust::FromPytorch, cvt::FromPytorch>);
static_assert(std::is_same_v<ft::vessel_trust::Validated,   cvt::Validated>);

// ─── 6. secret_policy::* identity ─────────────────────────────────

static_assert(std::is_same_v<ft::secret_policy::AuditedLogging, csp::AuditedLogging>);
static_assert(std::is_same_v<ft::secret_policy::WireSerialize,  csp::WireSerialize>);
static_assert(std::is_same_v<ft::secret_policy::HashForCompare, csp::HashForCompare>);
static_assert(std::is_same_v<ft::secret_policy::LengthOnly,     csp::LengthOnly>);
static_assert(std::is_same_v<ft::secret_policy::UserDisplay,    csp::UserDisplay>);

// ─── 7. hash_family::* identity ───────────────────────────────────

static_assert(std::is_same_v<ft::hash_family::FamilyA, chf::FamilyA>);
static_assert(std::is_same_v<ft::hash_family::FamilyB, chf::FamilyB>);

// ─── 8. Tags compose with Tagged<T, Tag> ──────────────────────────
//
// Building Tagged<int, fixy::tags::source::FromUser> must be the same
// specialization as Tagged<int, safety::source::FromUser>.

using TaggedAlias = crucible::safety::Tagged<int, ft::source::FromUser>;
using TaggedDirect = crucible::safety::Tagged<int, cs::FromUser>;
static_assert(std::is_same_v<TaggedAlias, TaggedDirect>,
    "Tagged<T, fixy::tags::source::X> must alias Tagged<T, safety::source::X>");

// Layout still bit-exact sizeof(T).
static_assert(sizeof(TaggedAlias) == sizeof(int),
    "Tagged via fixy alias must preserve EBO sizeof(T) invariant");

// ─── 9. Runtime sanity ────────────────────────────────────────────

int main() {
    crucible::safety::Tagged<int, ft::source::FromUser> t{42};
    (void)t.value();
    return 0;
}
