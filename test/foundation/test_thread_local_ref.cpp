// Sentinel TU for foundation/ThreadLocalRef.h.  Handles that name one Tag
// and T reach one cell, a different Tag reaches a different cell, and each
// thread has a cell of its own.

#include <foundation/ThreadLocalRef.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/SipHash.h>
#include <foundation/core/StableIdentity.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/Hash.h>

#include <cstdint>

// A tag needs a stable identity, so the tags stand in a named namespace.
namespace thread_local_ref_test {

struct CounterTag {};
struct AccumulatorTag {};
struct OtherTag {};

// A family of tags, one for each cell of a thread and one more.
template <int Index>
struct ManyTag {};

struct ScalarsTag {};

enum class Shade : std::uint8_t {
    dark,
    light
};

// A value of a cell whose T{} is zero bytes: an enumeration, a bool, an
// integer and a float.  A value of a cell is trivially default constructible,
// so its members have no initializer, and T{} zero-initializes them.
struct Scalars {
    Shade shade;
    bool is_set;
    std::uint16_t count;
    float weight;
};

}  // namespace thread_local_ref_test

namespace {

namespace fnd = ::foundation;

using IntCounter = fnd::ThreadLocalRef<thread_local_ref_test::CounterTag, int>;
using IntAccumulator = fnd::ThreadLocalRef<thread_local_ref_test::AccumulatorTag, int>;
using DoubleOther = fnd::ThreadLocalRef<thread_local_ref_test::OtherTag, double>;

[[nodiscard]] int cell_round_trips() {
    const IntCounter counter{};
    if (counter.peek() != 0) return 1;
    counter.store(34);
    if (counter.peek() != 34) return 2;
    const IntCounter writer{};
    writer.store(51);
    if (counter.peek() != 51) return 3;
    counter.reset();
    if (counter.peek() != 0) return 4;
    return 0;
}

// Same T, different Tag: a write to one does not reach the other.  Same
// Tag: every handle, a copy among them, reaches one cell.
[[nodiscard]] int tags_select_cells() {
    const IntCounter counter{};
    const IntAccumulator accumulator{};
    counter.store(100);
    accumulator.store(200);
    if (counter.peek() != 100 || accumulator.peek() != 200) return 1;
    const IntCounter second{};
    second.store(17);
    if (counter.peek() != 17) return 2;
    const IntCounter copied = second;
    if (copied.peek() != 17) return 3;
    counter.reset();
    accumulator.reset();
    return 0;
}

// The compare goes through bit_cast because equality on a floating-point
// value is an error under the project warning set.
[[nodiscard]] int floating_cell_round_trips() {
    const DoubleOther other{};
    other.store(2.5);
    const bool same_bits = ::fixy::bit_cast<std::uint64_t>(other.peek()) == ::fixy::bit_cast<std::uint64_t>(2.5);
    other.reset();
    return same_bits ? 0 : 1;
}

using ScalarsCell = fnd::ThreadLocalRef<thread_local_ref_test::ScalarsTag, thread_local_ref_test::Scalars>;

// The first value of each cell that the worker reads.  The struct of scalars
// gives its bytes through bit_cast, so the compare reads no equality of a
// floating-point value.
struct FirstValues {
    int counter = -1;
    std::uint64_t scalars_bits = ~std::uint64_t{0};
};

// A second thread starts each cell at T{}: an int and a struct of scalars.
// Its write does not reach the cell of this thread.  The worker runs on the
// thread door, and the join of the door orders its writes before the reads.
[[nodiscard]] int each_thread_has_its_own_cell() {
    const IntCounter counter{};
    counter.store(5);
    FirstValues seen_by_worker{};
    {
        auto worker = [&seen_by_worker] noexcept {
            const IntCounter worker_counter{};
            seen_by_worker.counter = worker_counter.peek();
            seen_by_worker.scalars_bits = ::fixy::bit_cast<std::uint64_t>(ScalarsCell{}.peek());
            worker_counter.store(99);
        };
        const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
        const ::fixy::BgLoadCtx ctx{::foundation::effects::testing::bg()};
        ::fixy::thread::run_joined_raw_probe(ctx, test, "the worker writes the values of this thread on purpose",
                                             ::fixy::thread::ThreadOptions{}, std::move(worker))
            .expect("the thread door starts the worker");
    }
    const bool isolated = seen_by_worker.counter == 0 && counter.peek() == 5;
    counter.reset();
    if (!isolated) return 1;
    if (seen_by_worker.scalars_bits != ::fixy::bit_cast<std::uint64_t>(thread_local_ref_test::Scalars{})) return 2;
    return 0;
}

// Reads the cell of each tag of the family from Index to the end of the family.
template <int Index>
void read_tags_from() noexcept {
    static_cast<void>(fnd::ThreadLocalRef<thread_local_ref_test::ManyTag<Index>, int>{}.peek());
    if constexpr (Index + 1 <= static_cast<int>(fnd::thread_local_cell_count)) read_tags_from<Index + 1>();
}

// The process has thread_local_cell_count cells for each thread, so the key
// after the last cell ends the process with the text that names the count.
// The claims run in a child through expect_dies of the Check family.
void the_key_after_the_last_cell_ends_the_process(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test, [] noexcept { read_tags_from<0>(); },
        ::fixy::TextView{"finds no free cell: the process already has thread_local_cell_count "
                         "cells.  Raise thread_local_cell_count"});
}

// The oracle of the second word of a key: the SipHash-2-4 of a name under
// the key of the 16 characters "ThreadLocalRef 2", as a little-endian word.
[[nodiscard]] constexpr std::uint64_t second_word_of_name(::foundation::core::TextView name) noexcept {
    constexpr ::foundation::core::TextView key_text{"ThreadLocalRef 2"};
    ::foundation::core::SipKeyBytes const key =
        ::foundation::core::build_fixed_array<std::byte, ::foundation::core::sip_key_bytes>(
            [key_text](auto run) noexcept {
                auto letter = key_text.begin();
                for (std::byte& byte : run) {
                    byte = static_cast<std::byte>(*letter);
                    ++letter;
                }
            });
    std::uint64_t word = 0;
    int shift = 0;
    for (std::byte const byte : ::foundation::core::sip_hash(key, name)) {
        word |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(byte)) << shift;
        shift += 8;
    }
    return word;
}

// The two words of the key of a cell are the stable type id of the handle
// type and the SipHash of its stable name, two independent functions of
// one name.
static_assert(IntCounter::cell_ids().type_id == ::foundation::reflect::stable_type_id(^^IntCounter));
static_assert(IntCounter::cell_ids().name_hash
              == second_word_of_name(::foundation::core::stable_name_text<IntCounter>()));
static_assert(IntCounter::cell_ids().name_hash != IntAccumulator::cell_ids().name_hash);
static_assert(IntCounter::cell_ids().name_hash != IntCounter::cell_ids().type_id);

}  // namespace

int main() {
    // The death test forks, so it runs before a test starts a thread.
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    the_key_after_the_last_cell_ends_the_process(test);
    if (const int failed = cell_round_trips(); failed != 0) return 10 + failed;
    if (const int failed = tags_select_cells(); failed != 0) return 20 + failed;
    if (const int failed = floating_cell_round_trips(); failed != 0) return 30 + failed;
    if (const int failed = each_thread_has_its_own_cell(); failed != 0) return 40 + failed;
    return 0;
}
