// Runtime checks of the checked lifetime start of foundation/Lifetime.h.
//
// start_as_array gives the guarantee of std::start_lifetime_as_array with
// the asm statement of libstdc++.  A new object over bytes holds the bytes
// of the last store to them, also when the last store has a different type.
// Without the statement, GCC can carry the value of the first store past a
// store of another type, because type-based alias analysis says that the two
// stores do not overlap.  A Release build then reads the first store.

#include <foundation/Lifetime.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

namespace lifetime = ::foundation::lifetime;

struct Pair {
    std::uint32_t first;
    std::uint32_t second;
};

// An integer store, then a float store through a second pointer to the same
// bytes, then a read through a new integer over the bytes.  The function is
// not inlined, so GCC cannot see that the two pointers are equal.  Returns
// the bits that the read gives.
[[gnu::noinline]] std::uint32_t read_after_a_store_of_another_type(unsigned char* storage, void* same_bytes) {
    std::uint32_t* const counter = lifetime::start_as_array<std::uint32_t>(storage, 1).data();
    *counter = 5;
    float* const ratio = lifetime::start_as_array<float>(same_bytes, 1).data();
    *ratio = 1.0F;
    return lifetime::start_as_array<std::uint32_t>(storage, 1).front();
}

// The read sees the last store, which is the float.
[[nodiscard]] int read_sees_the_last_store() {
    alignas(8) unsigned char storage[8]{};
    const std::uint32_t bits = read_after_a_store_of_another_type(storage, storage);
    return bits == std::bit_cast<std::uint32_t>(1.0F) ? 0 : 1;
}

// Const storage gives const elements, and each element holds the bytes that
// the storage held before the start.
[[nodiscard]] int const_storage_reads_its_bytes() {
    alignas(Pair) unsigned char storage[2 * sizeof(Pair)]{};
    const Pair written[2] = {{1, 2}, {3, 4}};
    std::memcpy(storage, written, sizeof(written));
    const unsigned char* const view = storage;
    const auto pairs = lifetime::start_as_array<Pair>(view, 2);
    if (pairs.size() != 2) return 1;
    if (pairs[0].first != 1 || pairs[0].second != 2 || pairs[1].first != 3 || pairs[1].second != 4) return 2;
    return 0;
}

// A count of zero is an empty span that starts at the storage.
[[nodiscard]] int zero_count_is_empty_at_the_storage() {
    alignas(4) unsigned char storage[4]{};
    const auto none = lifetime::start_as_array<std::uint32_t>(storage, 0);
    if (!none.empty()) return 1;
    return static_cast<const void*>(none.data()) == static_cast<const void*>(storage) ? 0 : 2;
}

}  // namespace

int main() {
    if (const int failed = read_sees_the_last_store(); failed != 0) return 10 + failed;
    if (const int failed = const_storage_reads_its_bytes(); failed != 0) return 20 + failed;
    if (const int failed = zero_count_is_empty_at_the_storage(); failed != 0) return 30 + failed;
    return 0;
}
