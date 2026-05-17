// fixy_neg: gr::as_classified is the only engagement — 19 axes unengaged.
//
// FIXY-LAT-Security: a single Security-axis lattice tag engages
// only Security; the IsAccepted gate fires on the first
// remaining-unengaged axis (Type, index 0).
//
// Expected diagnostic: "FixyNotEngaged_Type".

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reject.h>

namespace gr = crucible::fixy::grant;
using D      = crucible::fixy::dim::DimensionAxis;

struct TypeFixyLatClassifiedOnly {};

static_assert(!crucible::fixy::IsAccepted<TypeFixyLatClassifiedOnly, gr::as_classified>,
    "A single Security lattice tag must reject — 19 axes unengaged.");

static_assert(crucible::fixy::first_missing_axis_v<gr::as_classified> == D::Type,
    "first_missing_axis_v on a single Security tag must point at Type.");

using TypeAxisTag = crucible::fixy::diag::tag_for_axis_t<D::Type>;
static_assert(sizeof(TypeAxisTag) > 0 && false,
    "FixyNotEngaged_Type: a single gr::as_classified engages only "
    "the Security axis; Type (and 18 others) remain unengaged.");

int main() { return 0; }
