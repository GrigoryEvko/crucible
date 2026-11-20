// The bytes of a plain OneOf hold its tag, and bytes can hold a tag that
// names no alternative, such as a tag of 200.  The OneOf carries the
// annotation no_start_over_bytes, and the cells of its tag carry the mark
// closed_tag, so the checked lifetime start refuses the OneOf.

#include <foundation/core/Array.h>
#include <foundation/core/OneOf.h>

#include <cstdint>

struct ForgedReading {
    std::uint32_t storage = 7;
    std::uint32_t tag_word = 200;
};

using Reading = ::foundation::core::OneOf<std::uint32_t, std::uint16_t>;

int main() {
    ForgedReading bytes{};
    static_cast<void>(::foundation::lifetime::start_as_array<Reading>(&bytes, 1));
    return 0;
}
