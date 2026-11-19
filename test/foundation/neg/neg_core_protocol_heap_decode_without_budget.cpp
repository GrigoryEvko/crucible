// decode of an image of run-time size takes a budget of the bytes of storage
// that its heap parts can allocate.  A call with no budget does not compile,
// so no caller lets a short input ask for an allocation of any size.

#include <foundation/core/Protocol.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct[[= ::foundation::core::derive_protocol{}]] HoldsWords {
    ::foundation::core::HeapArray<unsigned> words{};
};

int main() {
    ::foundation::core::FixedArray<std::byte, 8> const image{};
    return ::foundation::core::decode<HoldsWords>(::foundation::effects::testing::test().alloc,
                                                  ::foundation::core::View<std::byte const>{image.view()})
                   .is_ok()
             ? 0
             : 1;
}
