// An element with no assignment whose end is code.  push_new would end the old
// element in its slot and then start the new one there, and the end of the old
// element could read the ring, which still holds that slot.  So the door refuses
// the element.

#include <fixy/CyclicBuffer.h>

#include <utility>

struct Guarded {
    int value = 0;
    Guarded() noexcept = default;
    Guarded(Guarded const&) noexcept = default;
    Guarded& operator=(Guarded const&) = delete("the fixture has no assignment");
    ~Guarded() { value = 0; }
};

int main() {
    ::fixy::CyclicBuffer<Guarded, 2> ring{};
    Guarded guarded{};
    ring.push_new(std::move(guarded));
    return 0;
}
