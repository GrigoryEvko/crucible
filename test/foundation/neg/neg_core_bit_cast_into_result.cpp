// The bit copy door of the Scalar family refuses a Result as its
// destination.  The cells of the tag of a plain Result carry the mark
// closed_tag, so a Result is not BitValid.

#include <foundation/core/Choice.h>
#include <foundation/core/Scalar.h>

#include <cstdint>

enum class Refusal : std::uint8_t {
    full,
    closed
};

struct ForgedWords {
    std::uint64_t value = 7;
    std::uint64_t tag = 2;
};

using Word = foundation::core::Result<std::uint64_t, Refusal>;

int main() { return foundation::core::bit_cast<Word>(ForgedWords{}).is_ok() ? 1 : 0; }
