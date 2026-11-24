// The offset of mint_run_address counts bytes, so a run of int would make
// the offset of an element and the offset of a byte the same number.
// mint_run_address refuses a run of elements that are not bytes.

#include <foundation/core/Array.h>
#include <foundation/core/DataAddress.h>

int main() {
    ::foundation::core::FixedArray<int, 8> storage{};
    return storage.with_mut([](auto numbers) noexcept -> int {
        static_cast<void>(::foundation::core::mint_run_address(numbers, 1));
        return 0;
    });
}
