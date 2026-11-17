// filter drops a refused payload, and a linear payload must not be dropped.
// So an Option of a linear payload has no filter.

#include <foundation/Linear.h>
#include <foundation/core/Choice.h>

struct[[= ::foundation::linear::linear{}]] Descriptor {
    int raw = 0;
    constexpr explicit Descriptor(int value) noexcept : raw{value} {}
    Descriptor(Descriptor&&) noexcept = default;
    Descriptor(Descriptor const&) = delete("a descriptor has one owner");
    constexpr Descriptor& operator=(Descriptor&& other) noexcept {
        raw = other.raw;
        return *this;
    }
};

int main() {
    using Held = ::foundation::core::Option<Descriptor>;
    Held kept =
        Held::some(Descriptor{3}).filter([](Descriptor const& descriptor) noexcept { return descriptor.raw > 2; });
    return ::foundation::linear::drop(static_cast<Held&&>(kept)), 0;
}
