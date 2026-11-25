// drain gives each element of a list away.  A named list stays with the caller
// while the body runs, so the body could change it through a capture.  So the
// caller gives the list up.

#include <foundation/core/List.h>

int main() {
    ::foundation::core::FixedList<int, 2> numbers{};
    static_cast<void>(numbers.append(1).expect("the list has room"));
    int sum = 0;
    numbers.drain([&sum](int number) noexcept { sum += number; });
    return sum;
}
