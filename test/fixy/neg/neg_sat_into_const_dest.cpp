// *_sat_into mutates its destination counter.  A const counter takes the
// checked form of fixy/Saturated.h, which writes nothing back.

#include <fixy/Saturate.h>

int main() {
    const unsigned counter = 1u;
    auto wrong = fixy::sat::add_sat_into(counter, 2u);
    return static_cast<int>(wrong.value());
}
