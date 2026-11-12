// A niche needs trivial copy and move constructors, because the Option
// keeps the payload in place of a flag.  A payload that owns something and
// declares a niche would fall to the owning form with no message, so the
// payload gate refuses it.

#include <foundation/core/Choice.h>

struct Handle {
    int descriptor = -1;
    constexpr Handle() noexcept = default;
    constexpr Handle(Handle&& other) noexcept : descriptor{other.descriptor} { other.descriptor = -1; }
    Handle(Handle const&) = delete;
    constexpr Handle& operator=(Handle const&) noexcept = default;
};

template <>
struct foundation::core::niche<Handle> {
    [[nodiscard]] static constexpr Handle empty() noexcept { return Handle{}; }
    [[nodiscard]] static constexpr bool is_empty(Handle const& handle) noexcept { return handle.descriptor < 0; }
};

int main() { return sizeof(::foundation::core::Option<Handle>) > 0 ? 0 : 1; }
