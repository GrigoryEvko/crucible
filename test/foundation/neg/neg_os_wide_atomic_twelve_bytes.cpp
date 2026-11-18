// The cell of 128 bits holds a value of 16 bytes.  A value of 12 bytes would
// leave 4 bytes of the word with no meaning, and a compare-and-swap would
// compare them too, so the cell refuses it.

#include <foundation/os/WideAtomic.h>

#include <cstdint>

namespace {

struct ThreeWords {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    std::uint32_t third = 0;
};

}  // namespace

int main() { return static_cast<int>(::foundation::os::WideAtomicByLock<ThreeWords>{}.load_acquire().first); }
