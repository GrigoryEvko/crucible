// The body of a heap scope gives a closure that captures a block by
// reference.  The type of a closure has no template argument, so the concept
// of the mint does not see the brand in it, and the block leaves the scope
// of its heap.  The rule brand-escape refuses each result of a scope mint that
// holds its own scope brand, also in a capture by reference.

#include <foundation/alloc/HeapState.h>
#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>
#include <foundation/heap/HeapScope.h>

#include <cstdint>

namespace {

struct ReturnsClosure final {
    template <class Brand>
    auto operator()(::foundation::heap::HeapScope<Brand>& scope) const noexcept {
        auto block = scope.template allocate<::foundation::alloc::SizeClass{1}>();
        auto& held = block;
        return [&held]() noexcept { return static_cast<std::uint64_t>(held.address()); };
    }
};

}  // namespace

int main() {
    auto const context = ::foundation::effects::testing::test();
    auto const leaked = ::foundation::heap::mint_heap_scope(context.alloc, ReturnsClosure{});
    return static_cast<int>(leaked() & 1U);
}
