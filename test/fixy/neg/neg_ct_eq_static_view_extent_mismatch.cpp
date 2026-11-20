// Two static extents that differ cannot name one tag, so ct::eq refuses
// two such Views at compile time.  Without the refusal, both Views convert
// to the dynamic overload, and its precondition stops the mismatch only at
// run time.
#include <fixy/ConstantTime.h>
#include <fixy/Core.h>

#include <cstddef>

int main() {
    ::fixy::FixedArray<std::byte, 3> const short_tag{};
    ::fixy::FixedArray<std::byte, 5> const long_tag{};
    return ::fixy::ct::eq(short_tag.view(), long_tag.view()) ? 1 : 0;
}
