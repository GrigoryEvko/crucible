// The bytes of a run of int.  Only a run of unsigned chars has bytes that
// each value of its element owns, so the constraint refuses each other
// element.

#include <foundation/core/Array.h>
#include <foundation/core/Region.h>

int main() {
    auto [storage, storage_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    return storage.with_mut(storage_key, [](auto run) noexcept -> int {
        auto bytes = ::foundation::core::byte_view(run.view());
        return static_cast<int>(bytes.size());
    });
}
