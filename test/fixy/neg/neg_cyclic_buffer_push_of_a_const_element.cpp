// A push into a ring of const elements.  A push assigns into the slot it
// claimed, and a const element cannot be assigned, so push is refused.

#include <fixy/CyclicBuffer.h>

namespace test_cyclic_buffer_const_element {
struct Reading {
    int value = 0;
};
}  // namespace test_cyclic_buffer_const_element

int main() {
    using test_cyclic_buffer_const_element::Reading;
    ::fixy::CyclicBuffer<const Reading, 4> ring{};
    ring.push(Reading{3});
    return ring.recent(0).value;
}
