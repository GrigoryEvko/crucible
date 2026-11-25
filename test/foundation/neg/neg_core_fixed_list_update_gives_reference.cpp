// The body of update gets a copy that ends with the call, so the result of
// the body holds no reference to it.

#include <foundation/core/List.h>
#include <foundation/core/Ref.h>

int main() {
    ::foundation::core::FixedList<int, 2> numbers{};
    static_cast<void>(numbers.append(1).expect("the list has room"));
    auto const held = numbers.update(::foundation::core::index<0>(),
                                     [](int& element) noexcept { return ::foundation::core::to(element); });
    return *held;
}
