// The bytes of a plain Option hold its tag, and bytes can hold a tag that
// names no alternative, such as a tag of 2.  The cells of the tag carry the
// mark closed_tag, so the checked lifetime start refuses the Option.

#include <foundation/Lifetime.h>
#include <foundation/core/Choice.h>

#include <cstdint>

struct ForgedWords {
    std::uint64_t payload = 7;
    std::uint64_t tag = 2;
};

int main() {
    ForgedWords words{};
    static_cast<void>(::foundation::lifetime::start_as_array<::foundation::core::Option<std::uint64_t>>(&words, 1));
    return 0;
}
