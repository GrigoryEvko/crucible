// relax moves down one trunk and never across to another.  The memory
// scopes form a partial order over two trunks that meet only at the
// shared bottom Thread and the shared top System, so a block scope on
// the accelerator trunk and an inner-shareable domain on the ARM trunk
// are incomparable: neither publishes where the other does.  Asking a
// Cta value to relax to Inner would assert the value is visible to
// observers the fence never reached, so the leq gate in the requires
// clause of both relax overloads evaluates to false for the pair.
//
// This is the case a chain band cannot produce.  Every other band in
// fixy/Bands.h pins a total order, where a rejected relax is always a
// move up; here the target sits neither above nor below the source.

#include <fixy/Bands.h>

int main() {
    fixy::scoped_fence::Cta<int> cta{42, {}};
    auto inner = fixy::relax<fixy::MemoryScope_v::Inner>(cta);
    return inner.peek();
}
