// An alternative that owns something makes the OneOf move-only, so a copy,
// which would duplicate what the alternative owns, is refused.

#include <foundation/core/OneOf.h>

struct Owner {
    int handle = 0;
    explicit Owner(int value) noexcept : handle{value} {}
    Owner(Owner&& other) noexcept : handle{other.handle} { other.handle = 0; }
    Owner(Owner const&) = delete;
    Owner& operator=(Owner const&) = delete;
    Owner& operator=(Owner&&) = delete;
    ~Owner() {}
};

int main() {
    ::foundation::core::OneOf<Owner, int> const source{Owner{1}};
    ::foundation::core::OneOf<Owner, int> const copy{source};
    return copy.is<Owner>() ? 0 : 1;
}
