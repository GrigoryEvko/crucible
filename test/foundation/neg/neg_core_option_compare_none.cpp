// An Option compares only with an Option of its own payload type.  The
// implicit constructor from none makes no compare: the state has its own
// test, is_none().

#include <foundation/core/Choice.h>

int main() {
    auto const holder = ::foundation::core::Option<int>::some(3);
    return holder == ::foundation::core::none ? 1 : 0;
}
