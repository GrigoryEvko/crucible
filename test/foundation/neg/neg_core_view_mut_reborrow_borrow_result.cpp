// The body of a reborrow gives the Ref of an element of its run.  The Ref has
// the brand of the frame and would outlive the body, so the result of a
// reborrow leaves the frame only with no borrow.

#include <foundation/core/Array.h>

#include <utility>

int main() {
    ::foundation::core::FixedArray<int, 4> array{1, 2, 3, 4};
    array.with_mut([](auto out) noexcept {
        auto back = ::foundation::core::reborrow(
            std::move(out), [](auto lent) noexcept { return std::move(lent.get(0)).expect("one element"); });
        static_cast<void>(back);
    });
    return 0;
}
