// No plain value converts into a Saturated.  The conversion would state
// that no clamp made the value, and the value can come from the plain
// saturating form, which drops the fact of a clamp.  exact() states the
// claim at the call.

#include <fixy/Saturated.h>

#include <cstdint>

int main() {
    const fixy::Saturated<std::uint32_t> count = std::uint32_t{5};
    return count.was_clamped() ? 1 : 0;
}
