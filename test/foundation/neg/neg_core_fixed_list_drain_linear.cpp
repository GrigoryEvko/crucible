// A body of drain that takes an element by reference drops it, so a list of a
// linear type has no drain.  take_last and remove give each element to its
// consumer.
#include <foundation/Linear.h>
#include <foundation/core/List.h>

#include <utility>

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
    ::foundation::core::FixedList<End, 2> ends{};
    std::move(ends).drain([](End&& end) noexcept { static_cast<void>(end.handle); });
    return 0;
}
