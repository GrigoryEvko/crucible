// A class that is not the friend of the scope door takes the name of the mint
// in its own namespace, and opens a scope of the heap of its thread on a brand
// that it names.  The one friend of the door is
// foundation::heap::mint_heap_scope, so the door is private here.

#include <foundation/heap/HeapScope.h>

namespace lookalike {

struct ForgedBrand {};

struct HeapScopes {
    [[nodiscard]] static auto mint_heap_scope() noexcept {
        return foundation::heap::heap_tag::Owner::scope_door::open<ForgedBrand>(foundation::alloc::thread_heap());
    }
};

}  // namespace lookalike

int main() { return 0; }
