// A Fixed takes a float or a double.  An integer has an exact text of its
// own, and a count of decimals after it would claim a fraction that the
// value does not hold, so the deleted constructor refuses it at the call.

#include <foundation/core/Report.h>

#include <cstdint>

int main() {
    std::int64_t const count = 3;
    ::foundation::core::report(::foundation::core::Sink::Out, "the count is {}\n", ::foundation::core::Fixed<2>{count});
}
