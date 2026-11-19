// A record with the two functions of the owner core and no marker is no
// owner state, so an owner on the core with it does not compile.  The marker
// keeps a record from becoming a state by accident.

#include <foundation/core/Owner.h>

#include <cstddef>

namespace {

struct RecordWithNoMark {
    std::size_t first = 0;

    [[nodiscard]] static constexpr RecordWithNoMark emptied(RecordWithNoMark const& /*state*/) noexcept {
        return RecordWithNoMark{};
    }
    static constexpr void release(RecordWithNoMark const& /*state*/) noexcept {}
};

struct OwnerOfRecord final : ::foundation::core::OwnerCore<RecordWithNoMark> {};

}  // namespace

int main() {
    OwnerOfRecord const owner{};
    return sizeof(owner) == sizeof(RecordWithNoMark) ? 0 : 1;
}
