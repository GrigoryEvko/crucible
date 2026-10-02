// An Option of a payload that owns something is move-only.  A copy would
// duplicate what the payload owns.

#include <foundation/core/Choice.h>

struct Owner {
    int handle = 0;
    Owner() noexcept = default;
    Owner(Owner&& other) noexcept : handle{other.handle} { other.handle = 0; }
    ~Owner() {}
};

int main() {
    ::foundation::core::Option<Owner> original = ::foundation::core::Option<Owner>::some(Owner{});
    ::foundation::core::Option<Owner> duplicate = original;
    return duplicate.is_some() ? 0 : 1;
}
