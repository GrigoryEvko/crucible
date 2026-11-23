// A record with the marker of an owner state and no release does not give
// the operations of the owner core, so an owner on the core with it does not
// compile.  The core would have no way to end what the state owns.

#include <foundation/core/Owner.h>

#include <cstddef>

namespace {

struct MarkedRecordWithNoRelease {
    using owner_state_mark = ::foundation::core::OwnerStateMark;

    std::size_t first = 0;

    [[nodiscard]] static constexpr MarkedRecordWithNoRelease
    emptied(MarkedRecordWithNoRelease const& /*state*/) noexcept {
        return MarkedRecordWithNoRelease{};
    }
};

struct OwnerOfRecord final : ::foundation::core::OwnerCore<MarkedRecordWithNoRelease, OwnerOfRecord> {};

}  // namespace

int main() {
    OwnerOfRecord const owner{};
    return sizeof(owner) == sizeof(MarkedRecordWithNoRelease) ? 0 : 1;
}
