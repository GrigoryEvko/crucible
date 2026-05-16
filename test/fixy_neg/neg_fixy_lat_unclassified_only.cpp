// fixy_neg: gr::as_unclassified is the only engagement — 19 axes unengaged.
//
// FIXY-LAT-Security: a single Security-axis lattice tag engages
// only Security; the IsAccepted gate fires on the first
// remaining-unengaged axis.  For a pack consisting solely of
// `gr::as_unclassified`, that is Type (DimensionAxis index 0).
//
// Expected diagnostic: "FixyNotEngaged_Type".

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reject.h>

namespace gr = crucible::fixy::grant;
using D      = crucible::fixy::dim::DimensionAxis;

// Per-fixture-type carrier — HS14 discipline.
struct TypeFixyLatUnclassifiedOnly {};

static_assert(!crucible::fixy::IsAccepted<TypeFixyLatUnclassifiedOnly, gr::as_unclassified>,
    "A single Security lattice tag must reject — 19 other axes unengaged.");

static_assert(crucible::fixy::first_missing_axis_v<gr::as_unclassified> == D::Type,
    "first_missing_axis_v on a single Security tag must point at Type "
    "(DimensionAxis index 0).");

using TypeAxisTag = crucible::fixy::diag::tag_for_axis_t<D::Type>;
static_assert(sizeof(TypeAxisTag) > 0 && false,
    "FixyNotEngaged_Type: a single gr::as_unclassified engages only "
    "the Security axis.  The Type axis (and 18 others) remain "
    "unengaged.");

int main() { return 0; }
