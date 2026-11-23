// The op of write_each captures a linear end by move.  The door calls the op
// for each element and ends it, and a call is no consume, so the end would
// end with the op.  KernelOp refuses an op that holds a linear value, and the
// deleted twin gives the reason.
#include <foundation/Linear.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace {
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] End {
    int handle = 0;
    constexpr explicit End(int value) noexcept : handle{value} {}
    constexpr End(End&&) noexcept = default;
    constexpr End& operator=(End&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    End(End const&) = delete("a linear end of the fixture does not copy");
    End& operator=(End const&) = delete("a linear end of the fixture does not copy");
};
}  // namespace

int main() {
    ::foundation::core::HeapArray<int> out =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 1);
    ::foundation::core::HeapArray<int> const input =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 2);
    ::foundation::core::View<int const> const read = input.view();
    out = ::foundation::core::with_mut(std::move(out), [read](auto out_view) noexcept {
        End end{5};
        static_cast<void>(::foundation::core::write_each(
            std::move(out_view), [held = std::move(end)](int const& value) noexcept { return value + held.handle; },
            read));
    });
    return 0;
}
