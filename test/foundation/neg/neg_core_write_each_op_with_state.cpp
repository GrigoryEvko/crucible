// The op of write_each captures a View of the run of the output.  Through the
// capture the op would read the run that the restrict loop writes, so the
// door takes only an op with no state.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    ::foundation::core::HeapArray<int> out =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 1);
    ::foundation::core::View<int const> const seen = out.view();
    out = ::foundation::core::with_mut(std::move(out), [seen](auto out_view) noexcept {
        static_cast<void>(::foundation::core::write_each(
            std::move(out_view), [seen](int const& value) noexcept { return seen.size() == 0 ? 0 : value; }, seen));
    });
    return 0;
}
