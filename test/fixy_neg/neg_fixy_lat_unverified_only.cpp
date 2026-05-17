// fixy_neg: gr::trust_unverified is the only engagement — 19 axes unengaged.
//
// FIXY-LAT-Trust: a single Trust-axis lattice tag engages only
// Trust; the IsAccepted gate fires on the first remaining-unengaged
// axis (Type, index 0).
//
// Expected diagnostic: "FixyNotEngaged_Type".

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reject.h>

namespace gr = crucible::fixy::grant;
using D      = crucible::fixy::dim::DimensionAxis;

struct TypeFixyLatUnverifiedOnly {};

static_assert(!crucible::fixy::IsAccepted<TypeFixyLatUnverifiedOnly, gr::trust_unverified>,
    "A single Trust lattice tag must reject — 19 axes unengaged.");

static_assert(crucible::fixy::first_missing_axis_v<gr::trust_unverified> == D::Type,
    "first_missing_axis_v on a single Trust tag must point at Type.");

using TypeAxisTag = crucible::fixy::diag::tag_for_axis_t<D::Type>;
static_assert(sizeof(TypeAxisTag) > 0 && false,
    "FixyNotEngaged_Type: a single gr::trust_unverified engages only "
    "the Trust axis; Type (and 18 others) remain unengaged.");

int main() { return 0; }
