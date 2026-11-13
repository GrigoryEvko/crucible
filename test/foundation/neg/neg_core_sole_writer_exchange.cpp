// The cell of a sole writer has no read-modify-write operation.  Its writer
// reads with load_owned and stores with store_release, with no locked
// instruction.

#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>

#include <cstdint>
#include <utility>

namespace {
struct HeadRole {};
}  // namespace

int main() {
    auto writer = ::foundation::core::mint_sole_writer<HeadRole>();
    ::foundation::core::Atomic<std::uint64_t, decltype(writer)> head{writer, 0};
    std::uint64_t const before = head.exchange_acq_rel(1);
    ::foundation::linear::drop(std::move(writer));
    return static_cast<int>(before);
}
