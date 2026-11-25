// An element with no assignment whose end is code.  update would end the element
// in its slot and then start the copy there, and the end of the element could
// read the array, which still holds that slot.  So the door refuses the element.

#include <foundation/core/Array.h>

struct Guarded {
    int value = 0;
    Guarded() noexcept = default;
    Guarded(Guarded const&) noexcept = default;
    Guarded& operator=(Guarded const&) = delete("the fixture has no assignment");
    ~Guarded() { value = 0; }
};

int main() {
    ::foundation::core::FixedArray<Guarded, 2> array{};
    array.update(::foundation::core::index<0>(), [](Guarded& element) noexcept { element.value = 1; });
    return array.at(::foundation::core::index<0>()).value;
}
