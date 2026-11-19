// The budget of decode of an image of run-time size is a quantity of bytes.
// A raw integer does not convert to it, so a count of elements or an offset
// cannot stand for the budget.

#include <foundation/core/Protocol.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct[[= ::foundation::core::derive_protocol{}]] HoldsWords {
    ::foundation::core::HeapArray<unsigned> words{};
};

int main() {
    ::foundation::core::FixedArray<std::byte, 8> const image{};
    return ::foundation::core::decode<HoldsWords>(::foundation::effects::testing::test().alloc, std::size_t{4096},
                                                  ::foundation::core::View<std::byte const>{image.view()})
                   .is_ok()
             ? 0
             : 1;
}
