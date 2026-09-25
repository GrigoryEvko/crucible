// An Acquire value handed to a consumer that requires Release.
//
// An acquire load orders the operations after it.  A release store
// orders the operations before it.  Neither gives the guarantee of the
// other ([atomics.order]), so the order of MemOrderLattice has Release
// and Acquire side by side, below Relaxed and above AcqRel.  The chain
// the lattice used to be put Release below Acquire, so this call
// compiled and a publication that needed its earlier stores ordered was
// handed a value that orders only its later loads.

#include <crucible/safety/_MemOrder.h>

#include <utility>

using namespace crucible::safety;

template <typename W>
    requires(W::template satisfies<MemOrderTag_v::Release>)
static int release_fence_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    MemOrder<MemOrderTag_v::Acquire, int> acquire_value{42};
    return release_fence_consumer(std::move(acquire_value));
}
