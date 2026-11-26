// The body of for_each_chunk keeps a View of one part in an Option that it
// captures by reference.  The door gives the rest of the run back to the
// caller as its one writer, so the kept View would read a run that another
// name writes after the walk.  The door refuses a body that can store a borrow
// outside the walk (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include <utility>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 8>);
    array.with_mut(key, [](auto out) noexcept {
        ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
        auto rest = ::foundation::core::for_each_chunk<4>(std::move(out), [&kept](auto part) noexcept {
            static_cast<void>(part);
            static_cast<void>(kept.is_some());
        });
        static_cast<void>(rest);
    });
    return 0;
}
