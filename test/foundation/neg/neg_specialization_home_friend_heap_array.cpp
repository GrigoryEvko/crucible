// A file outside Array.h writes an explicit specialization of the template
// mint_heap_array.  While HeapArray befriends mint_heap_array, the body of
// the specialization reaches the private constructor of HeapArray and builds
// an array over storage that the allocation door did not give.  The rule
// specialization-home refuses the specialization in each build, because a
// function template of the tree gets an explicit specialization only in its
// own file.  The body reaches no private member, so the fixture reads only
// the rule.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

template <>
[[noreturn]] foundation::core::HeapArray<int, alignof(int)>
foundation::core::mint_heap_array<int, alignof(int), foundation::effects::Alloc>(foundation::effects::Alloc,
                                                                                 std::size_t, int const&) noexcept {
    for (;;) {}
}

int main() { return 0; }
