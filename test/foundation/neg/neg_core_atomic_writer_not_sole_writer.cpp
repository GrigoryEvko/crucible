// The writer of an Atomic is void, for a cell that each thread can write,
// or a SoleWriter, for a cell of one writer.  Each other type names no
// writer, so the cell refuses it.

#include <foundation/core/Atomic.h>

#include <cstdint>

int main() {
    ::foundation::core::Atomic<std::uint64_t, int> head{0};
    return static_cast<int>(head.load_acquire());
}
