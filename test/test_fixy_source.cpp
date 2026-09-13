// A re-exported tag must be the same type as the substrate tag, not a
// look-alike declared in the re-exporting namespace.  Two distinct empty
// classes would satisfy every use below except these assertions.

#include <crucible/fixy/Source.h>
#include <crucible/safety/Tagged.h>

#include <type_traits>

namespace ft = crucible::fixy::tags;
namespace cs = crucible::safety::source;
namespace ctr = crucible::safety::trust;
namespace cac = crucible::safety::access;
namespace cv = crucible::safety::version;
namespace cvt = crucible::safety::vessel_trust;
namespace csp = crucible::safety::secret_policy;
namespace chf = crucible::hash_family;

static_assert(std::is_same_v<ft::source::FromUser, cs::FromUser>);
static_assert(std::is_same_v<ft::source::FromDb, cs::FromDb>);
static_assert(std::is_same_v<ft::source::FromConfig, cs::FromConfig>);
static_assert(std::is_same_v<ft::source::FromInternal, cs::FromInternal>);
static_assert(std::is_same_v<ft::source::External, cs::External>);
static_assert(std::is_same_v<ft::source::ABIBoundary, cs::ABIBoundary>);
static_assert(std::is_same_v<ft::source::Sanitized, cs::Sanitized>);
static_assert(std::is_same_v<ft::source::FormatVersion, cs::FormatVersion>);
static_assert(std::is_same_v<ft::source::Loaded, cs::Loaded>);
static_assert(std::is_same_v<ft::source::Interned, cs::Interned>);
static_assert(std::is_same_v<ft::source::Arena, cs::Arena>);
static_assert(std::is_same_v<ft::source::Singleton, cs::Singleton>);
static_assert(std::is_same_v<ft::source::Recorded, cs::Recorded>);
static_assert(std::is_same_v<ft::source::Replayed, cs::Replayed>);
static_assert(std::is_same_v<ft::source::Durable, cs::Durable>);
static_assert(std::is_same_v<ft::source::Computed, cs::Computed>);
static_assert(std::is_same_v<ft::source::Vendor, cs::Vendor>);
static_assert(std::is_same_v<ft::source::Calibrated, cs::Calibrated>);
static_assert(std::is_same_v<ft::source::Hlc, cs::Hlc>);
static_assert(std::is_same_v<ft::source::Local, cs::Local>);
static_assert(std::is_same_v<ft::source::Gossiped, cs::Gossiped>);
static_assert(std::is_same_v<ft::source::IntegrityVerified, cs::IntegrityVerified>);
static_assert(std::is_same_v<ft::source::JsonRegistry, cs::JsonRegistry>);
static_assert(std::is_same_v<ft::source::Ir001, cs::Ir001>);
static_assert(std::is_same_v<ft::source::TcpInfo, cs::TcpInfo>);

static_assert(std::is_same_v<ft::trust::Verified, ctr::Verified>);
static_assert(std::is_same_v<ft::trust::Tested, ctr::Tested>);
static_assert(std::is_same_v<ft::trust::Unverified, ctr::Unverified>);
static_assert(std::is_same_v<ft::trust::Assumed, ctr::Assumed>);
static_assert(std::is_same_v<ft::trust::External, ctr::External>);

static_assert(std::is_same_v<ft::access::RW, cac::RW>);
static_assert(std::is_same_v<ft::access::RO, cac::RO>);
static_assert(std::is_same_v<ft::access::WO, cac::WO>);
static_assert(std::is_same_v<ft::access::W1C, cac::W1C>);
static_assert(std::is_same_v<ft::access::W1S, cac::W1S>);
static_assert(std::is_same_v<ft::access::WriteOnce, cac::WriteOnce>);
static_assert(std::is_same_v<ft::access::AppendOnly, cac::AppendOnly>);
static_assert(std::is_same_v<ft::access::Unique, cac::Unique>);
static_assert(std::is_same_v<ft::access::AutoIncrement, cac::AutoIncrement>);
static_assert(std::is_same_v<ft::access::Deprecated, cac::Deprecated>);

static_assert(std::is_same_v<ft::version::V<1>, cv::V<1>>);
static_assert(std::is_same_v<ft::version::V<3>, cv::V<3>>);
static_assert(std::is_same_v<ft::version::V<99>, cv::V<99>>);

static_assert(ft::version::V<3>::number == 3);

static_assert(std::is_same_v<ft::vessel_trust::FromPytorch, cvt::FromPytorch>);
static_assert(std::is_same_v<ft::vessel_trust::Validated, cvt::Validated>);

static_assert(std::is_same_v<ft::secret_policy::AuditedLogging, csp::AuditedLogging>);
static_assert(std::is_same_v<ft::secret_policy::WireSerialize, csp::WireSerialize>);
static_assert(std::is_same_v<ft::secret_policy::HashForCompare, csp::HashForCompare>);
static_assert(std::is_same_v<ft::secret_policy::LengthOnly, csp::LengthOnly>);
static_assert(std::is_same_v<ft::secret_policy::UserDisplay, csp::UserDisplay>);
static_assert(std::is_same_v<ft::secret_policy::AuthorizedReplay, csp::AuthorizedReplay>);

static_assert(std::is_same_v<ft::hash_family::FamilyA, chf::FamilyA>);
static_assert(std::is_same_v<ft::hash_family::FamilyB, chf::FamilyB>);

using TaggedAlias = crucible::safety::Tagged<int, ft::source::FromUser>;
using TaggedDirect = crucible::safety::Tagged<int, cs::FromUser>;
static_assert(std::is_same_v<TaggedAlias, TaggedDirect>,
              "Tagged<T, fixy::tags::source::X> must alias Tagged<T, safety::source::X>");

static_assert(sizeof(TaggedAlias) == sizeof(int), "tagging through the alias must preserve the empty-base collapse");

int main() {
    crucible::safety::Tagged<int, ft::source::FromUser> t{42};
    (void)t.value();
    return 0;
}
