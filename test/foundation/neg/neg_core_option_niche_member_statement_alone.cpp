// A class states that no door builds the empty value, and it gives no
// member of the niche protocol, so it names no empty value.  The statement
// names the member protocol, so the payload gate refuses the niche that is
// not well formed, and the Option does not fall to the plain form with no
// message.

#include <foundation/core/Choice.h>

#include <cstdint>

struct SlotNumber {
    std::uint32_t raw = 0;

    static constexpr bool is_niche_empty_never_built = true;
};

int main() { return sizeof(::foundation::core::Option<SlotNumber>) > 0 ? 0 : 1; }
