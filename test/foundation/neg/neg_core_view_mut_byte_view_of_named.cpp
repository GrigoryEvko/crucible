// A body takes the bytes of a named ViewMut.  The bytes would write the run
// while the name stays with the body, so the run would have two writers.
// The bytes of a ViewMut take it by move.

#include <foundation/core/Array.h>

int main() {
    auto [storage, storage_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<unsigned char, 4>);
    storage.with_mut(storage_key, [](auto run) noexcept {
        auto bytes = ::foundation::core::byte_view(run);
        static_cast<void>(bytes);
    });
    return 0;
}
