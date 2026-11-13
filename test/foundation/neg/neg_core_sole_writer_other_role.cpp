// The type of the cell of a sole writer names its writer.  The writer of
// the tail of a ring cannot write the head.

#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>

#include <cstdint>
#include <utility>

namespace {
struct HeadRole {};
struct TailRole {};
}  // namespace

int main() {
    auto head_writer = ::foundation::core::mint_sole_writer<HeadRole>();
    auto tail_writer = ::foundation::core::mint_sole_writer<TailRole>();
    ::foundation::core::Atomic<std::uint64_t, decltype(head_writer)> head{head_writer, 0};
    head.store_release(tail_writer, 1);
    ::foundation::linear::drop(std::move(head_writer));
    ::foundation::linear::drop(std::move(tail_writer));
    return 0;
}
