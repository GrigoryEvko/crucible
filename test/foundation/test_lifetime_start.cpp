// Runtime checks of the checked lifetime start of foundation/Lifetime.h.
//
// start_as_array gives the guarantee of std::start_lifetime_as_array with
// the asm statement of libstdc++.  A new object over bytes holds the bytes
// of the last store to them, also when the last store has a different type.
// Without the statement, GCC can carry the value of the first store past a
// store of another type, because type-based alias analysis says that the two
// stores do not overlap.  A Release build then reads the first store.

#include <foundation/Lifetime.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>
#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace core = ::foundation::core;
namespace lifetime = ::foundation::lifetime;

struct Pair {
    std::uint32_t first;
    std::uint32_t second;
};

// The storage of each probe.  A FixedArray holds its elements at its own
// address, so the alignment of the array is the alignment of its bytes.
using StoreBytes = core::FixedArray<unsigned char, 8>;
using PairBytes = core::FixedArray<std::byte, 2 * sizeof(Pair)>;
using EmptyBytes = core::FixedArray<unsigned char, 4>;

// The probes start a lifetime over the raw address of a storage.  The start
// gives its objects as a std::span, and each probe reads that span.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the checked lifetime start reads raw bytes through pointers of two types")
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

// The bits of the read after the two stores, with the two pointers at the
// first byte of the storage.
[[nodiscard]] std::uint32_t read_through_two_types(StoreBytes& storage) {
    return read_after_a_store_of_another_type(core::foreign_first(storage.view()), core::foreign_first(storage.view()));
}

// Starts two pairs over the const storage.  Returns 0 when the two pairs hold
// the values 1 and 2, then 3 and 4.  Each other value names the check that
// fails.
[[nodiscard]] int read_pairs_of(PairBytes const& storage) {
    const auto pairs = lifetime::start_as_array<Pair>(core::foreign_first(storage.view()), 2);
    if (pairs.size() != 2) return 1;
    if (pairs[0].first != 1 || pairs[0].second != 2 || pairs[1].first != 3 || pairs[1].second != 4) return 2;
    return 0;
}

// Starts no object over the storage.  Returns 0 when the span is empty and
// starts at the storage.  Each other value names the check that fails.
[[nodiscard]] int read_none_of(EmptyBytes& storage) {
    const auto none = lifetime::start_as_array<std::uint32_t>(core::foreign_first(storage.view()), 0);
    if (!none.empty()) return 1;
    const bool starts_at_the_storage =
        static_cast<const void*>(none.data()) == static_cast<const void*>(core::foreign_first(storage.view()));
    return starts_at_the_storage ? 0 : 2;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The read sees the last store, which is the float.
[[nodiscard]] int read_sees_the_last_store() {
    alignas(8) StoreBytes storage{};
    return read_through_two_types(storage) == core::bit_cast<std::uint32_t>(1.0F) ? 0 : 1;
}

// Const storage gives const elements, and each element holds the bytes that
// the storage held before the start.
[[nodiscard]] int const_storage_reads_its_bytes() {
    alignas(Pair) PairBytes storage{};
    const core::FixedArray<Pair, 2> written{Pair{1, 2}, Pair{3, 4}};
    core::copy(storage.view(), core::bytes(written));
    return read_pairs_of(storage);
}

// A count of zero is an empty span that starts at the storage.
[[nodiscard]] int zero_count_is_empty_at_the_storage() {
    alignas(4) EmptyBytes storage{};
    return read_none_of(storage);
}

}  // namespace

int main() {
    if (const int failed = read_sees_the_last_store(); failed != 0) return 10 + failed;
    if (const int failed = const_storage_reads_its_bytes(); failed != 0) return 20 + failed;
    if (const int failed = zero_count_is_empty_at_the_storage(); failed != 0) return 30 + failed;
    return 0;
}
