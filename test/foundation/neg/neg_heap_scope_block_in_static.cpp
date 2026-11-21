// A static local of the generic body of a heap scope keeps a block of one
// call.  One call site of mint_heap_scope gives each of its calls one scope
// brand, so a later call would release the block of a scope that ended, and
// the scope of a second thread would release a block of the heap of the
// first.  The rule brand-escape refuses each object of static or thread
// storage duration that holds a scope brand.

#include <foundation/alloc/HeapState.h>
#include <foundation/alloc/Layout.h>
#include <foundation/core/AllocTag.h>
#include <foundation/effects/Effect.h>
#include <foundation/heap/HeapScope.h>

#include <cstdint>

namespace {

struct KeepsBlock final {
    template <class Brand>
    int operator()(::foundation::heap::HeapScope<Brand>& scope) const noexcept {
        static auto const kept = scope.template allocate<::foundation::alloc::SizeClass{1}>();
        return static_cast<int>(static_cast<std::uint64_t>(kept.address()) & 1U);
    }
};

int scope_of(::foundation::core::Alloc const& capability) {
    return ::foundation::heap::mint_heap_scope(capability, KeepsBlock{});
}

}  // namespace

int main() {
    auto const context = ::foundation::effects::testing::test();
    int const first = scope_of(context.alloc);
    return first + scope_of(context.alloc);
}
