// A move assignment of a OneOf whose alternative writes its own end.  That end could
// read the sum between the old alternative and the new one, so the sum has no
// assignment.

#include <foundation/core/OneOf.h>

struct Handle {
    int value = 0;
    explicit Handle(int from) noexcept : value{from} {}
    Handle(Handle&&) noexcept = default;
    ~Handle() {}
};

int main() {
    ::foundation::core::OneOf<Handle, int> target{Handle{1}};
    ::foundation::core::OneOf<Handle, int> source{2};
    target = static_cast<::foundation::core::OneOf<Handle, int>&&>(source);
    return target.is<int>() ? 0 : 1;
}
