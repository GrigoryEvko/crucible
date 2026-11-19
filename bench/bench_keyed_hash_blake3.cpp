// The door of bench_keyed_hash to the pinned official BLAKE3 (utils/pins.txt).
// The library blake3_reference of bench/CMakeLists.txt holds its C and its
// assembly, and the door calls its C interface.

#include "bench_keyed_hash_references.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the pinned official BLAKE3 is a C library, the reference of the keyed hash")

#include "blake3.h"

namespace bench_keyed_hash_references {

void blake3_keyed(std::byte const* key, std::byte const* first, std::size_t count, std::byte* out) noexcept {
    blake3_hasher hasher{};
    blake3_hasher_init_keyed(&hasher, static_cast<unsigned char const*>(static_cast<void const*>(key)));
    blake3_hasher_update(&hasher, first, count);
    blake3_hasher_finalize(&hasher, static_cast<unsigned char*>(static_cast<void*>(out)), BLAKE3_OUT_LEN);
}

}  // namespace bench_keyed_hash_references

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
