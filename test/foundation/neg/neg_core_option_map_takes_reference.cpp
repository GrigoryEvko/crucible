// map moves the payload out of the Option, and the payload ends when map
// returns.  A function that takes the payload by reference names an object
// that ends, so map refuses it, as the rvalue match refuses such an arm.

#include <foundation/core/Choice.h>

int main() {
    ::foundation::core::Option<long> const mapped =
        ::foundation::core::Option<int>::some(4).map([](int const& value) noexcept { return long{value}; });
    return mapped.is_some() ? 0 : 1;
}
