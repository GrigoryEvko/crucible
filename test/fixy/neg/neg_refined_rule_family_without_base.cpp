// A rule family says it is one by deriving rule_family<itself>, and the
// walk over admitted_implications finds it that way.
//
// The old shape looked for a marker variable declared beside the
// family, so a family written without its variable compiled, read as
// admitted, and decided nothing.  The base class cannot be left off the
// same way: a class in that namespace that does not derive it is
// refused where the namespace is walked.
//
// The class is planted before the header, because the walk runs at the
// foot of the header and reads the namespace as it stands there.

namespace fixy::refined::admitted_implications {

// Neither an edge nor a rule family.
struct not_a_rule_family {};

}  // namespace fixy::refined::admitted_implications

#include <fixy/Refined.h>

int main() { return 0; }
