// fixy_neg: gr::trust_verified used in lieu of a Security engagement.
//
// FIXY-LAT-Trust: `trust_verified` is a Trust-axis lattice point.
// Using it twice (once correctly on Trust, once mistakenly in the
// Security slot) leaves Security unengaged.  The IsAccepted gate
// fires `FixyNotEngaged_Security`.
//
// Expected diagnostic: "FixyNotEngaged_Security".

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reject.h>

namespace gr = crucible::fixy::grant;
using D      = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

struct TypeFixyLatVerifiedWrongAxis {};

using BadPack = std::tuple<
    strict<D::Type>, strict<D::Refinement>, strict<D::Usage>,
    strict<D::Effect>,
    gr::trust_verified,
    strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
    gr::trust_verified,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>>;

template <typename Tuple> struct rejects_via;
template <typename... Ts>
struct rejects_via<std::tuple<Ts...>> {
    static constexpr bool value = !crucible::fixy::IsAcceptedGrants<Ts...>;
    static constexpr D first_missing = crucible::fixy::first_missing_axis_v<Ts...>;
};

using Probe = rejects_via<BadPack>;

static_assert(Probe::value,
    "Pack must reject — Security axis unengaged by Trust-axis tag.");
static_assert(Probe::first_missing == D::Security,
    "first_missing_axis_v must point at Security.");

using SecurityTag = crucible::fixy::diag::tag_for_axis_t<D::Security>;
static_assert(sizeof(SecurityTag) > 0 && false,
    "FixyNotEngaged_Security: gr::trust_verified engages Trust, "
    "NOT Security.");

int main() { return 0; }
