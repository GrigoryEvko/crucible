// set ends the old value of an element with no consume, so a list of a linear
// type has no set.  exchange gives the old value back.
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

void replace(::foundation::core::FixedList<End, 2>& ends, End&& next) noexcept {
    ends.set(::foundation::core::index<0>(), std::move(next));
}
}  // namespace

int main() {
    ::foundation::core::FixedList<End, 2> ends{};
    replace(ends, End{3});
    std::move(ends).expect_empty("the fixture puts no end into the list");
    return 0;
}
