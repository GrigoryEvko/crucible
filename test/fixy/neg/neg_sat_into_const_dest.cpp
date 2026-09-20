// *_sat_into mutates its destination counter.  A const counter must use
// *_sat_from instead, preserving the read-only/mutating split.
//
// Old spelling: test/safety_neg/neg_saturate_into_const_dest.cpp.

#include <fixy/Saturate.h>

int main() {
    const unsigned counter = 1u;
    auto wrong = fixy::sat::add_sat_into(counter, 2u);
    return static_cast<int>(wrong.value());
}
