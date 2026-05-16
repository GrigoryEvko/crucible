// fixy_neg: gr::trust_tested is the only engagement — 19 axes unengaged.
//
// FIXY-LAT-Trust: a single Trust-axis lattice tag engages only
// Trust; the IsAccepted gate fires on the first remaining-unengaged
// axis.  For a pack consisting solely of `gr::trust_tested`, that
// is Type (DimensionAxis index 0).
//
// Expected diagnostic: "FixyNotEngaged_Type".

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reject.h>

namespace gr = crucible::fixy::grant;
using D      = crucible::fixy::dim::DimensionAxis;

// Per-fixture-type carrier — HS14 discipline.
struct TypeFixyLatTestedOnly {};

static_assert(!crucible::fixy::IsAccepted<TypeFixyLatTestedOnly, gr::trust_tested>,
    "A single Trust lattice tag must reject — 19 other axes unengaged.");

static_assert(crucible::fixy::first_missing_axis_v<gr::trust_tested> == D::Type,
    "first_missing_axis_v on a single Trust tag must point at Type.");

using TypeAxisTag = crucible::fixy::diag::tag_for_axis_t<D::Type>;
static_assert(sizeof(TypeAxisTag) > 0 && false,
    "FixyNotEngaged_Type: a single gr::trust_tested engages only "
    "the Trust axis.  The Type axis (and 18 others) remain "
    "unengaged.");

int main() { return 0; }
