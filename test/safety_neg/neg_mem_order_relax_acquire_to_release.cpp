// relax<Release> on an Acquire value.
//
// relax moves a value down the order and never up or sideways.
// Release and Acquire are incomparable, so neither is below the other,
// and relax refuses the step.  The value relaxes to AcqRel, which
// orders both sides of the operation.

#include <crucible/safety/_MemOrder.h>

#include <utility>

using namespace crucible::safety;

int main() {
    MemOrder<MemOrderTag_v::Acquire, int> acquire_value{42};
    auto released = std::move(acquire_value).relax<MemOrderTag_v::Release>();
    return std::move(released).consume();
}
