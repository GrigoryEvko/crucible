// A rule family added to the relation after fixy/Refined.h.
//
// The family decides non_negative ⇒ non_zero, which zero refutes.  It is
// a class and not an edge, so a count of edges alone would not see it.
// The seal counts every member of admitted_implications, and each query
// reads the seal first.  The query below asks a pair that the header
// does not ask, so it stops the build.

#include <fixy/Refined.h>

namespace fixy::refined::admitted_implications {

struct late_family : rule_family<late_family> {
    static consteval bool holds_(predicate_t<::fixy::non_negative>*, predicate_t<::fixy::non_zero>*) noexcept {
        return true;
    }
};

}  // namespace fixy::refined::admitted_implications

static_assert(!fixy::implies_v<fixy::bounded_below<2>, fixy::non_zero>);

int main() { return 0; }
