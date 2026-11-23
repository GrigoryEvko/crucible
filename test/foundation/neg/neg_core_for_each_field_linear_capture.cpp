// The visitor of a walk of the fields captures a linear end by move.  The
// walk calls the visitor for each field and ends it, and a call is no
// consume, so the end would end with the visitor.  The walk refuses a visitor
// that holds a linear value, and the deleted twin gives the reason.
#include <foundation/Linear.h>
#include <foundation/core/Scalar.h>

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

struct Pair {
    int first = 1;
    int second = 2;
};
}  // namespace

int main() {
    Pair const pair{};
    End end{3};
    ::foundation::core::for_each_field(pair, [held = std::move(end)](auto const& field) noexcept {
        static_cast<void>(field);
        static_cast<void>(held.handle);
    });
    return 0;
}
