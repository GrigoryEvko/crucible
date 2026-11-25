// An address changes from run to run, so an order of two addresses gives a
// different answer in a different run.  RunAddress has no order.

#include <foundation/core/Array.h>
#include <foundation/core/DataAddress.h>

int main() {
    auto [storage, storage_key] =
        ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<unsigned char, 8>);
    return storage.with_mut(storage_key, [](auto bytes) noexcept -> int {
        auto const low = ::foundation::core::mint_run_address(bytes, 0).expect("eight bytes");
        auto const high = ::foundation::core::mint_run_address(bytes, 4).expect("eight bytes");
        return low < high ? 0 : 1;
    });
}
