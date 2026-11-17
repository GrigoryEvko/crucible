// and_then moves the payload out of the Option, and the payload ends when
// and_then returns.  A function that takes the payload by reference names an
// object that ends, so and_then refuses it.

#include <foundation/core/Choice.h>

int main() {
    using Number = ::foundation::core::Option<int>;
    Number const chained = Number::some(4).and_then([](int const& value) noexcept { return Number::some(value); });
    return chained.is_some() ? 0 : 1;
}
