// An element with no assignment whose end is code.  update would end the element
// in its slot and then start the copy there, and the end of the element could
// read the ring, which still holds that slot.  So the door refuses the element.

#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

struct Guarded {
    int value = 0;
    Guarded() noexcept = default;
    Guarded(Guarded const&) noexcept = default;
    Guarded& operator=(Guarded const&) = delete("the fixture has no assignment");
    ~Guarded() { value = 0; }
};

int main() {
    ::fixy::CyclicBuffer<Guarded, 2> ring{};
    ring.update(ring.next_slot(), [](Guarded& element) noexcept { element.value = 1; });
    return 0;
}
