// An element with no assignment whose move is code.  update would end the
// element in its slot and then start the copy there with that move, and the move
// could read the array while the slot holds no element.  So the door refuses the
// element, although its end is trivial.

#include <foundation/core/Array.h>

struct Counted {
    int value = 0;
    Counted() noexcept = default;
    Counted(Counted const&) noexcept = default;
    Counted(Counted&& other) noexcept : value{other.value + 1} {}
    Counted& operator=(Counted const&) = delete("the fixture has no assignment");
};

int main() {
    ::foundation::core::FixedArray<Counted, 2> array{};
    array.update(::foundation::core::index<0>(), [](Counted& element) noexcept { element.value = 1; });
    return array.at(::foundation::core::index<0>()).value;
}
