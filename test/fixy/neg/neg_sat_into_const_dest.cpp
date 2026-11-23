// *_sat_into steps its counter.  A const counter takes the checked form of
// fixy/Saturated.h, which writes nothing back.

#include <fixy/Saturate.h>

int main() {
    const fixy::Saturated<unsigned> counter = fixy::Saturated<unsigned>::exact(1u);
    fixy::sat::add_sat_into(counter, 2u);
    return static_cast<int>(counter.value());
}
