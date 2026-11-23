// A body copies a View of three elements into a ViewMut of four elements and
// discards the result.  Only two runs of one fixed extent take the copy that
// cannot fail.  Two other counts take the copy that gives a Result with
// LengthMismatch, and the body must read that Result.

#include <foundation/core/Array.h>

#include <utility>

int main() {
    ::foundation::core::FixedArray<int, 3> source{1, 2, 3};
    ::foundation::core::FixedArray<int, 4> target{};
    target.with_mut([&source](auto out) noexcept { ::foundation::core::copy(std::move(out), source.view()); });
    return 0;
}
