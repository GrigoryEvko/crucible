// fixy_neg: gr::as_internal used in lieu of a Trust engagement.
//
// FIXY-LAT-Security: `as_internal` is a Security-axis lattice point.
// Using it as the (only) Trust grant leaves Trust unengaged.  The
// IsAccepted gate fires `FixyNotEngaged_Trust`.
//
// Expected diagnostic: "FixyNotEngaged_Trust".

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reject.h>

namespace gr = crucible::fixy::grant;
using D      = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

struct TypeFixyLatInternalWrongAxis {};

using BadPack = std::tuple<
    strict<D::Type>, strict<D::Refinement>, strict<D::Usage>,
    strict<D::Effect>,
    gr::as_internal,
    strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
    gr::as_internal,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>>;

template <typename Tuple> struct rejects_via;
template <typename... Ts>
struct rejects_via<std::tuple<Ts...>> {
    static constexpr bool value = !crucible::fixy::IsAcceptedGrants<Ts...>;
    // fixy-H-08: first_missing_axis_v is std::optional<D>.
    static constexpr auto first_missing = crucible::fixy::first_missing_axis_v<Ts...>;
};

using Probe = rejects_via<BadPack>;

static_assert(Probe::value,
    "Pack must reject — Trust axis unengaged by Security-axis tag.");
static_assert(Probe::first_missing == D::Trust,
    "first_missing_axis_v must point at Trust.");

using TrustTag = crucible::fixy::diag::tag_for_axis_t<D::Trust>;
static_assert(sizeof(TrustTag) > 0 && false,
    "FixyNotEngaged_Trust: gr::as_internal engages Security, NOT Trust.");

int main() { return 0; }
