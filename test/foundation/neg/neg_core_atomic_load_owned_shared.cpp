// A cell that each thread can write has no relaxed read.  A writer of a
// role proves nothing about a cell whose type names no writer, so the
// shared cell has no member load_owned.

#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>

#include <cstdint>
#include <utility>

namespace {
struct HeadRole {};
}  // namespace

int main() {
    auto writer = ::foundation::core::mint_sole_writer<HeadRole>();
    ::foundation::core::Atomic<std::uint64_t> shared{0};
    std::uint64_t const value = shared.load_owned(writer);
    ::foundation::linear::drop(std::move(writer));
    return static_cast<int>(value);
}
