// The writer of an Atomic is void, for a cell that each thread can write, a
// Counter, a SoleWriter, for a cell of one writer, or a SoleWriterProof, for
// the cell of an owner.  Each other type names no writer, so the cell refuses
// it.

#include <foundation/core/Atomic.h>

#include <cstdint>

int main() {
    ::foundation::core::Atomic<std::uint64_t, int> head{0};
    return static_cast<int>(head.load_acquire());
}
