// A Ref has no compare.  same_object() tells if two Refs reach one object.

#include <foundation/core/Ref.h>

int main() {
    int number = 1;
    ::foundation::core::Ref<int> const first = ::foundation::core::Ref<int>::to(number);
    ::foundation::core::Ref<int> const second = ::foundation::core::Ref<int>::to(number);
    return first == second ? 0 : 1;
}
