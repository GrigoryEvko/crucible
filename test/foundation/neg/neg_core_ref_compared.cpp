// A Ref has no compare.  same_object() tells if two Refs reach one object.

#include <foundation/core/Ref.h>

int main() {
    int number = 1;
    auto const first = ::foundation::core::to(number);
    auto const second = first;
    return first == second ? 0 : 1;
}
