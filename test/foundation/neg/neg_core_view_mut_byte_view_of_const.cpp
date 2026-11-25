// A body takes the bytes of a const ViewMut.  A const ViewMut writes nothing,
// so it gives no writer of its bytes.

#include <foundation/core/Array.h>

int main() {
    auto [storage, storage_key] =
        ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<unsigned char, 4>);
    storage.with_mut(storage_key, [](auto run) noexcept {
        auto const& shared = run;
        auto bytes = ::foundation::core::byte_view(shared);
        static_cast<void>(bytes);
    });
    return 0;
}
