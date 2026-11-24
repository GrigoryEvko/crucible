// A holder of a set that is not const takes its run as a View that writes.
// The set holds no unique borrow of the run, so no conversion gives a writing
// View of it.

#include <foundation/core/RunSet.h>

#include <cstdint>

int main() {
    ::foundation::core::RunSet<std::uint16_t, ::foundation::core::RunOf<int, 0>> set{};
    ::foundation::core::View<int> const writing = set.view<0>();
    return static_cast<int>(writing.size());
}
