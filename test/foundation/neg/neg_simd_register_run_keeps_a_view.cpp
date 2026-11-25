// The body of the walk of the register runs of two Views captures by reference
// an Option of a View, which the caller reads after the walk.  The body can keep
// the View of a run there.  for_each_register_run refuses a body that can store
// a borrow outside the walk (RegisterRunBody of foundation/Simd.h).

#include <foundation/Simd.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>

#include <cstddef>

int main() {
    using Bytes = ::foundation::simd::vec<unsigned char, 16>;
    ::foundation::core::FixedArray<std::byte, 32> const first{};
    ::foundation::core::FixedArray<std::byte, 32> const second{};
    ::foundation::core::Option<::foundation::core::View<std::byte const>> kept{::foundation::core::none};
    auto rests = ::foundation::simd::for_each_register_run<Bytes>(
        ::foundation::core::View<std::byte const>{first.view()},
        ::foundation::core::View<std::byte const>{second.view()}, [&kept](auto left, auto right) noexcept {
            static_cast<void>(left);
            static_cast<void>(right);
            static_cast<void>(kept.is_some());
        });
    static_cast<void>(rests);
    return 0;
}
