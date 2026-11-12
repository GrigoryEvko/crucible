// Each write of the cell of a sole writer takes the proof of the writer.
// A store with no proof would let a second thread write the cell.

#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>

#include <cstdint>
#include <utility>

namespace {
struct HeadRole {};
}  // namespace

int main() {
    auto writer = ::foundation::core::mint_sole_writer<HeadRole>();
    ::foundation::core::Atomic<std::uint64_t, decltype(writer)> head{0};
    head.store_release(1);
    ::foundation::linear::drop(std::move(writer));
    return 0;
}
