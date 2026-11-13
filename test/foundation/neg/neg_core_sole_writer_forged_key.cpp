// Only the mint of a sole writer can make the key of its constructor.  A
// key that a caller makes would forge a writer of each run-time id, also the
// id of a writer that a cell keeps.

#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>

#include <utility>

namespace {
struct HeadRole {};
}  // namespace

int main() {
    ::foundation::core::sole_writer_key const key{};
    ::foundation::core::SoleWriter<HeadRole, decltype([] {})> forged{key, 1};
    ::foundation::linear::drop(std::move(forged));
    return 0;
}
