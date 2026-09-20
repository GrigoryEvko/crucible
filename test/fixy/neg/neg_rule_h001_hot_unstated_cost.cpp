// H001: hot x an unstated or unbounded cost.
//
// A hot binding has to say what its compute envelope is.  The rule reads
// two ways of failing to: the explicit cost_unbounded grade, and the
// Complexity strict pole, which is the absence of any Complexity grade at
// all.  This fixture names the explicit one, because the absent one
// cannot be isolated — a pack with no Complexity grade and no Refinement
// grade trips H002 as well, and test/fixy/test_collision.cpp is where
// both halves of the premise are held apart.
//
// The refinement witness is in the pack for exactly that reason.  Without
// it the fixture would also trip H002, and its second regex would then be
// witnessing a rule the file does not claim — the defect #166 was.

#include <fixy/Fn.h>

namespace {

struct parser_bounds_proved final {};

}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot, ::fixy::atom::cost_unbounded,
                                ::fixy::atom::refined_with<parser_bounds_proved>>
        refused{};
    return 0;
}
