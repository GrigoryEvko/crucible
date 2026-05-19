// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-09 fixture: `from_source<Source>` requires
// `IsProvenanceSource<Source>` — substrate convention is that every
// `safety::source::*` tag is an EMPTY marker class.  A fundamental
// type (here `int`) violates both `std::is_class_v` AND
// `std::is_empty_v`; the requires-clause rejects.
//
// Pre-M-09 `from_source<int>` compiled silently; the resolver would
// project `int` into the Provenance slot of `safety::fn::source_t`
// and produce ill-typed downstream Tagged-source comparisons.  Post-
// M-09 the rejection fires HERE with the named concept in the
// diagnostic.
//
// Distinct rejection class from the other three fixtures because
// `IsProvenanceSource` is strictly tighter (adds `is_empty_v`) — a
// future regression that loosens the gate (e.g. allowing stateful
// source tags) would redden this fixture even though `int` would
// still be rejected by the class-only fixtures.
//
// Expected diagnostic: constraints not satisfied /
// IsProvenanceSource / is_class / is_empty / no matching function.

#include <crucible/fixy/Grant.h>

namespace gr = crucible::fixy::grant;

int main() {
    // Should FAIL: `int` is fundamental, not a class, not empty →
    // IsProvenanceSource<int> evaluates to false → the requires-
    // clause on `from_source<Source>` rejects.
    [[maybe_unused]] auto bad = gr::from_source<int>{};
    return 0;
}
