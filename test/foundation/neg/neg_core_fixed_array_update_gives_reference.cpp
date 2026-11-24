// The body of update gets a copy that ends with the call, so the result of
// the body holds no reference to it.

#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>

int main() {
    ::foundation::core::FixedArray<int, 2> numbers{1, 2};
    auto const held = numbers.update(::foundation::core::index<0>(),
                                     [](int& element) noexcept { return ::foundation::core::to(element); });
    return *held;
}
