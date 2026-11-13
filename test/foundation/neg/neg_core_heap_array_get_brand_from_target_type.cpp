// The address of HeapArray::get with a target type does not choose the brand of
// the Ref.  The brand of the return type is in a context that deduction
// does not read, so the brand parameter keeps its default, the fresh brand.
// A target type with a different brand then matches no function.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace {

struct Held final {};

}  // namespace

int main() {
    namespace core = ::foundation::core;
    auto values = core::mint_heap_array(::foundation::effects::Alloc{}, 4, 1);
    using Pick = core::Option<core::Ref<int, Held>> (core::HeapArray<int>::*)(std::size_t) & noexcept;
    auto result = (values.*static_cast<Pick>(&core::HeapArray<int>::get))(0);
    static_cast<void>(result);
    return 0;
}
