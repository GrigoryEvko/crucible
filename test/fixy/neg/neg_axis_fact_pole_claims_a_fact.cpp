// The pole of a Fact axis claims nothing.
//
// A Fact axis carries a fact that the binding states about its value or
// its body, and a gate that must have the fact refuses a binding that
// does not state it.  A Provenance pole that names an internal source
// gives each binding that says nothing a source that nothing proved, and
// a gate that must have an internal source admits that binding.  The
// rule refuses such a pole.

#include <fixy/Axis.h>

static_assert(::fixy::PoleFitsClaim<::fixy::Claim::Fact, ::fixy::tags::source::FromInternal>,
              "a Fact pole that names an internal source is the weakest claim");

int main() { return 0; }
