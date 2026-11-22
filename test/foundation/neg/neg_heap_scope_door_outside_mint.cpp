// A unit calls the scope door of the tag of the owner of a heap, to open a
// scope of the heap of its thread on a brand that it names.  Two scopes of one
// brand would let a block of one scope go back through the other.  The door
// is private, and mint_heap_scope is its one friend, so the call is refused.

#include <foundation/heap/HeapScope.h>

struct ForgedBrand {};

int main() {
    static_cast<void>(
        foundation::heap::heap_tag::Owner::scope_door::open<ForgedBrand>(foundation::alloc::thread_heap()));
    return 0;
}
