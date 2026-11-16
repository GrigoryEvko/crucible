// A generic callable of transform() whose parameter is a value.
//
// Reflection substitutes the payload into the call operator template, as
// the deduction from an rvalue of the payload does, and it reads a
// parameter by value.  That parameter is a copy of the payload that nothing
// zeroizes, so the deleted overload of transform() refuses the call.

#include <fixy/Secret.h>

#include <cstdint>
#include <utility>

int main() {
    auto key = ::fixy::mint_secret<std::uint64_t>(std::uint64_t{7});
    [[maybe_unused]] auto derived = std::move(key).transform([](auto value) noexcept { return value ^ 1u; });
    return 0;
}
