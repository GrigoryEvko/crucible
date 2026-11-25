// Runtime checks of the checked lifetime start of foundation/core/Array.h.
//
// start_as_array gives the guarantee of std::start_lifetime_as_array with
// the asm statement of libstdc++.  A new object over bytes holds the bytes
// of the last store to them, also when the last store has a different type.
// Without the statement, GCC can carry the value of the first store past a
// store of another type, because type-based alias analysis says that the two
// stores do not overlap.  A Release build then reads the first store.
//
// Each probe starts a lifetime over the bytes of a View, so no probe holds a
// raw address.  Two death tests give the start bytes that it refuses: bytes
// that are not aligned for the object, and a count of bytes that holds no
// whole count of objects.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Lifetime.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

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

// The bytes of one integer.
using WordBytes = core::View<unsigned char, sizeof(std::uint32_t)>;

// An integer store, then a float store through a second View of the same
// bytes, then a read through a new integer over the bytes.  The function is
// not inlined, so GCC cannot see that the two Views are equal.  Returns the
// bits that the read gives.
[[gnu::noinline]] std::uint32_t read_after_a_store_of_another_type(WordBytes storage, WordBytes same_bytes) {
    const core::View<std::uint32_t, 1> counter = lifetime::start_as_array<std::uint32_t>(storage);
    counter.at(core::index<0>()) = 5;
    const core::View<float, 1> ratio = lifetime::start_as_array<float>(same_bytes);
    ratio.at(core::index<0>()) = 1.0F;
    return lifetime::start_as_array<std::uint32_t>(storage).at(core::index<0>());
}

// The family of the arena of the probe.
struct[[= ::foundation::brand::arena_family_brand{}]] ProbeArena final {};

// The bits of the read after the two stores, with the two Views at the first
// byte of an arena run.  The probe needs two Views that write the same bytes.
// A frame gives one writer of its run, and the run of an arena is a plain
// View, so the bytes come from an arena.  The first run of an arena starts at
// the alignment of its block, and start_as_array ends the process if the
// bytes are not aligned for an integer.
[[nodiscard]] std::uint32_t read_through_two_types() {
    using Bytes = core::Quantity<core::unit::Byte, std::size_t>;
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    core::BumpArena<ProbeArena> arena = core::mint_bump_arena<ProbeArena>(Bytes{4096});
    auto storage = arena.array<unsigned char>(alloc, sizeof(std::uint32_t), 0);
    const WordBytes word = storage.view().template window<sizeof(std::uint32_t)>(0).expect("the run holds one integer");
    return read_after_a_store_of_another_type(word, word);
}

// Starts two pairs over the const storage.  Returns 0 when the two pairs hold
// the values 1 and 2, then 3 and 4.  Each other value names the check that
// fails.
[[nodiscard]] int read_pairs_of(PairBytes const& storage) {
    const core::View<Pair const> pairs = lifetime::start_as_array<Pair>(storage.view());
    if (pairs.size() != 2) return 1;
    const core::View<Pair const, 2> both = pairs.window<2>(0).expect("a View of two pairs has a window of two");
    const Pair& head = both.at(core::index<0>());
    const Pair& tail = both.at(core::index<1>());
    if (head.first != 1 || head.second != 2 || tail.first != 3 || tail.second != 4) return 2;
    return 0;
}

// Starts no object over the empty window at the start of the storage.
// Returns 0 when the View is empty and starts at the storage.  The integer
// over all the bytes of the storage starts at the storage, so the empty View
// starts there when it is at the position zero of that integer.  Each other
// value names the check that fails.
[[nodiscard]] int read_none_of(EmptyBytes& storage) {
    const core::View<unsigned char const> no_bytes =
        storage.view().window(0, 0).expect("the storage has an empty window");
    const core::View<std::uint32_t const> none = lifetime::start_as_array<std::uint32_t>(no_bytes);
    if (none.size() != 0) return 1;
    const core::View<std::uint32_t const, 1> whole = lifetime::start_as_array<std::uint32_t>(storage.view());
    const bool starts_at_the_storage =
        core::offset_of(whole, none)
            .match([](std::size_t position) noexcept { return position == 0; }, [] noexcept { return false; });
    return starts_at_the_storage ? 0 : 2;
}

// The read sees the last store, which is the float.
[[nodiscard]] int read_sees_the_last_store() {
    return read_through_two_types() == core::bit_cast<std::uint32_t>(1.0F) ? 0 : 1;
}

// Const storage gives const elements, and each element holds the bytes that
// the storage held before the start.
[[nodiscard]] int const_storage_reads_its_bytes() {
    alignas(Pair) PairBytes storage{};
    const core::FixedArray<Pair, 2> written{Pair{1, 2}, Pair{3, 4}};
    storage.copy_from(core::bytes(written));
    return read_pairs_of(storage);
}

// A count of zero is an empty View that starts at the storage.
[[nodiscard]] int zero_count_is_empty_at_the_storage() {
    alignas(4) EmptyBytes storage{};
    return read_none_of(storage);
}

// The start refuses bytes that are not aligned for the object, and bytes that
// hold no whole count of objects.  Each body runs in a child process.
void refuses_bytes_that_hold_no_object(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            alignas(8) StoreBytes storage{};
            const core::View<unsigned char const, sizeof(std::uint32_t)> shifted =
                storage.view().window<sizeof(std::uint32_t)>(1).expect("the storage holds it");
            static_cast<void>(lifetime::start_as_array<std::uint32_t>(shifted));
        },
        core::TextView{"foundation: fatal invariant failed: (__builtin_bit_cast(std::uintptr_t, storage) & "
                       "(alignof(Element) - 1)) == 0"});
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            alignas(8) StoreBytes storage{};
            const core::View<unsigned char const> three =
                storage.view().window(0, 3).expect("the storage holds three bytes");
            static_cast<void>(lifetime::start_as_array<std::uint32_t>(three));
        },
        core::TextView{"foundation: fatal invariant failed: bytes.size() % sizeof(T) == 0"});
}

}  // namespace

int main() {
    if (const int failed = read_sees_the_last_store(); failed != 0) return 10 + failed;
    if (const int failed = const_storage_reads_its_bytes(); failed != 0) return 20 + failed;
    if (const int failed = zero_count_is_empty_at_the_storage(); failed != 0) return 30 + failed;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    refuses_bytes_that_hold_no_object(test);
    return 0;
}
