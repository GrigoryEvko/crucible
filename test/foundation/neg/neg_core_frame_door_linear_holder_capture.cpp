// The body of the frame door of a keyed FixedArray captures by value a struct
// that holds a linear end.  The struct is linear by its member, so the body holds
// a linear value, and the door refuses it.
#include <foundation/Linear.h>
#include <foundation/core/Array.h>

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

struct Holder {
    End end;
    int count = 0;
};
}  // namespace

int main() {
    auto [counts, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 2>);
    Holder holder{End{3}, 1};
    counts.with_mut(key, [held = std::move(holder)](auto run) noexcept {
        static_cast<void>(run.size());
        static_cast<void>(held.count);
    });
    return counts.at(::foundation::core::index<0>());
}
