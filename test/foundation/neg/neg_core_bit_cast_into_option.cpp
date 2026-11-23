// The bit copy door of the Scalar family refuses an Option as its
// destination.  The cells of the tag of a plain Option carry the mark
// closed_tag, so an Option is not BitValid.

#include <foundation/core/Choice.h>
#include <foundation/core/Scalar.h>

#include <cstdint>

struct ForgedWords {
    std::uint64_t payload = 7;
    std::uint64_t tag = 2;
};

using Word = foundation::core::Option<std::uint64_t>;

int main() { return foundation::core::bit_cast<Word>(ForgedWords{}).is_some() ? 1 : 0; }
