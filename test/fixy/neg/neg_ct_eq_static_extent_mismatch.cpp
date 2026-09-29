// Two static extents that differ cannot name one tag, so ct::eq refuses
// the pair at compile time.  Without the refusal, both spans convert to
// the dynamic overload, and its precondition stops the mismatch only at
// run time.
#include <fixy/ConstantTime.h>

#include <cstddef>
#include <span>

int main() {
    const std::byte short_tag[3]{};
    const std::byte long_tag[5]{};
    return ::fixy::ct::eq(std::span<const std::byte, 3>{short_tag}, std::span<const std::byte, 5>{long_tag}) ? 1 : 0;
}
