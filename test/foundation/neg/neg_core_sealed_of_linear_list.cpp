// The end of a Sealed ends its object with no consume, so a list of a linear
// element does not seal.

#include <foundation/Linear.h>
#include <foundation/core/Sealed.h>
#include <foundation/core/SegmentList.h>

namespace {
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] End {
    int handle = 0;
    constexpr explicit End(int value) noexcept : handle{value} {}
    constexpr End(End&&) noexcept = default;
    constexpr End& operator=(End&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    End(End const&) = delete("a linear end of the fixture does not copy");
    End& operator=(End const&) = delete("a linear end of the fixture does not copy");
};
}  // namespace

int main() {
    // The size is the one use of the type.
    static_cast<void>(sizeof(foundation::core::Sealed<foundation::core::SegmentList<End>>));
    return 0;
}
