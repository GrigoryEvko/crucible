// An element with no assignment whose end is code.  update would end the element
// in its slot and then start the copy there, and the end of the element could
// read the array, which still holds that slot.  So the door of a FixedHeapArray
// refuses the element, as the door of a FixedArray does.

#include <foundation/core/FixedHeapArray.h>
#include <foundation/effects/Effect.h>

struct Guarded {
    int value = 0;
    Guarded() noexcept = default;
    Guarded(Guarded const&) noexcept = default;
    Guarded& operator=(Guarded const&) = delete("the fixture has no assignment");
    ~Guarded() { value = 0; }
};

int main() {
    ::foundation::core::FixedHeapArray<Guarded, 2> array =
        ::foundation::core::mint_fixed_heap_array<Guarded, 2>(::foundation::effects::testing::test().alloc, Guarded{});
    array.update(::foundation::core::index<0>(), [](Guarded& element) noexcept { element.value = 1; });
    return array.at(::foundation::core::index<0>()).value;
}
